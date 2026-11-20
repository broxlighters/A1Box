// =====================================================================
//  A1BOX 核心逻辑（依据 REQUIREMENTS.md v13 定稿）
//
//  这里只放「模式、排风、超温、按键、联动」的判断，不碰任何硬件。
//  ESPHome 的 YAML 负责把传感器、按键、HA 的输入喂进来，再把结果写到
//  PWM 和 HA 实体上。这样逻辑可以在电脑上单独测试（见 test/test_logic.cpp）。
//
//  时间一律用 millis() 的毫秒数（uint32_t），用差值比较，49 天回绕也没问题。
// =====================================================================
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

namespace a1box {

// 模式。数值就是 HA 下拉框里选项的顺序，不要改。
enum class Mode : uint8_t { OFF = 0, PETG = 1, PA = 2, PURGE = 3 };

// 固定参数（REQUIREMENTS 4.5、5.2）
constexpr float OVERTEMP_ON_C = 50.0f;       // 超过这个温度：超温保护
constexpr float OVERTEMP_OFF_C = 45.0f;      // 降到这个温度以下：解除超温
constexpr uint32_t SENSOR_STALE_MS = 30000;  // 传感器多久没新数据算失效
constexpr float STALE_FALLBACK_PCT = 60.0f;  // 传感器失效时 PETG 模式的排风占空比
constexpr float PETG_RAMP_SPAN_C = 5.0f;     // 上限以下多少度开始排风
constexpr float PETG_RAMP_HYST_C = 0.5f;     // 起停回差，防止在临界温度来回启停
constexpr uint32_t FAN_KICK_MS = 2000;       // 两线风扇从静止启动时先全速冲一下的时长
constexpr uint32_t SCREEN_ON_MS = 30000;     // 板载小屏：按键后亮多久（REQUIREMENTS 4.10）

// 是否 PA 类耗材（PA、PA-CF、PAHT-CF、PA6-GF、PPA-CF、Support for PA/PET 等）。
// 按分隔符拆成词，有一个词以 PA 或 PPA 开头就算。只看耗材类型（type），
// 不看耗材名称：名称里可能有 Sparkle 这类恰好含 PA 的词。
inline bool is_pa_material(const std::string &type) {
  std::string word;
  auto check = [&word]() {
    bool pa = word.rfind("PA", 0) == 0 || word.rfind("PPA", 0) == 0;
    word.clear();
    return pa;
  };
  for (char ch : type) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      word.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    } else if (check()) {
      return true;
    }
  }
  return check();
}

class Controller {
 public:
  // ---------------- 可调参数（HA 上的数字实体，每次同步时写进来） ----------------
  uint32_t purge_ms = 10u * 60u * 1000u;  // 打印后排风时长
  float fan_min_pct = 30.0f;              // 两线风扇能稳定转动的最低占空比，待实测

  void set_petg_limit(float limit_c) {
    petg_limit_c_ = limit_c;
    update_petg_running_();
  }

  // ---------------- 输入 ----------------

  // 新的温度读数。NaN 直接忽略，靠超时判断传感器失效。
  void on_temperature(float t, uint32_t now) {
    if (std::isnan(t)) return;
    temp_c_ = t;
    has_temp_ = true;
    last_temp_ms_ = now;
    stale_ = false;
    if (!overtemp_ && t > OVERTEMP_ON_C) {
      overtemp_ = true;  // 新一次超温：重新生效（之前被按键停过也一样）
      overtemp_suppressed_ = false;
    } else if (overtemp_ && t < OVERTEMP_OFF_C) {
      overtemp_ = false;
      overtemp_suppressed_ = false;
    }
    update_petg_running_();
  }

  // 按键 2（REQUIREMENTS 4.7）。实体按键优先级最高，超温排风也能停。
  void on_button2(uint32_t now) {
    if (overtemp_exhaust()) {
      overtemp_suppressed_ = true;  // 这次超温不再自动排风，降到 45°C 以下才重新布防
      set_mode_(Mode::OFF, true, now);
    } else if (mode_ == Mode::OFF) {
      set_mode_(Mode::PURGE, true, now);
    } else {
      set_mode_(Mode::OFF, true, now);
    }
  }

  // HA 上手动选模式。算手动操作，但停不掉超温排风（REQUIREMENTS 4.9）。
  void on_ha_mode(Mode m, uint32_t now) { set_mode_(m, true, now); }

  // 联动：开始打印。先按 PETG 处理，耗材类型确定后再调用 on_print_material。
  // 灯由 YAML 打开。
  void on_print_started(uint32_t now) {
    manual_ = false;  // 新的一次打印，清掉上一次的「手动优先」
    set_mode_(Mode::PETG, false, now);
  }

  // 联动：当前耗材类型。只在本次打印没被手动改过、并且还在 PETG 模式时生效。
  void on_print_material(const std::string &type, uint32_t now) {
    if (!manual_ && mode_ == Mode::PETG && is_pa_material(type)) set_mode_(Mode::PA, false, now);
  }

  // 联动：打印完成、失败或取消。打印结束是新事件，不受「手动优先」影响。
  // 取消打印时可能先后收到「取消」和「失败」两个事件，第二个不重新计时。
  void on_print_finished(uint32_t now) {
    if (mode_ == Mode::PURGE && purge_from_print_) return;
    set_mode_(Mode::PURGE, false, now);
    purge_from_print_ = true;
  }

  // 灯被改动（按键 1 或 HA）。只在打印结束触发的排风期间有意义：
  // 动过灯，排风结束后就不自动关灯。
  void on_light_changed() {
    if (mode_ == Mode::PURGE && purge_from_print_) light_touched_ = true;
  }

  // 风扇测试（只在「关闭」模式下接受，用来实测两线风扇的调速范围）。
  // 其他模式下直接归零，HA 上的数值会弹回 0。
  void set_fan_test_pct(float pct) { fan_test_pct_ = mode_ == Mode::OFF ? clamp_(pct, 0.0f, 100.0f) : 0.0f; }

  // 任一实体按键按下：板载小屏亮 30 秒（REQUIREMENTS 4.10）
  void on_any_button(uint32_t now) {
    screen_woken_ = true;
    screen_wake_ms_ = now;
  }

  // 周期调用（YAML 里每 0.5 秒一次）
  void tick(uint32_t now) {
    uint32_t since = has_temp_ ? last_temp_ms_ : 0;  // 上电后 30 秒还没读数也算失效
    stale_ = (now - since) > SENSOR_STALE_MS;

    if (mode_ == Mode::PURGE && (now - purge_start_ms_) >= purge_ms) {
      bool turn_light_off = purge_from_print_ && !light_touched_;
      set_mode_(Mode::OFF, false, now);
      if (turn_light_off) light_off_request_ = true;
    }

    // 按键亮屏到时后清掉标记，避免 millis() 回绕后误判成刚按过
    if (screen_woken_ && (now - screen_wake_ms_) >= SCREEN_ON_MS) screen_woken_ = false;
  }

  // ---------------- 输出 ----------------

  // 板载小屏该不该亮：只看按键，按键后亮 30 秒。任何时候都不常亮，报警也不例外。
  bool screen_on(uint32_t now) const { return screen_woken_ && (now - screen_wake_ms_) < SCREEN_ON_MS; }

  // 打印后排风还剩多少毫秒（不在排风时为 0）
  uint32_t purge_remaining_ms(uint32_t now) const {
    if (mode_ != Mode::PURGE) return 0;
    uint32_t elapsed = now - purge_start_ms_;
    return elapsed >= purge_ms ? 0 : purge_ms - elapsed;
  }

  Mode mode() const { return mode_; }
  bool overtemp() const { return overtemp_; }  // 超温报警状态
  bool overtemp_exhaust() const { return overtemp_ && !overtemp_suppressed_; }
  bool sensor_stale() const { return stale_; }
  bool manual_override() const { return manual_; }
  float fan_test_pct() const { return fan_test_pct_; }
  bool fan_test_active() const { return fan_test_pct_ > 0.0f && mode_ == Mode::OFF && !overtemp_exhaust(); }

  // 排风目标占空比 0–100（也是显示在 HA 上的「排风速度」）
  float fan_target_pct() const {
    if (overtemp_exhaust()) return 100.0f;
    switch (mode_) {
      case Mode::OFF:
        return fan_test_active() ? fan_test_pct_ : 0.0f;
      case Mode::PA:
        return 0.0f;
      case Mode::PURGE:
        return 100.0f;
      case Mode::PETG: {
        float min_pct = clamp_(fan_min_pct, 0.0f, 100.0f);
        if (stale_) return STALE_FALLBACK_PCT > min_pct ? STALE_FALLBACK_PCT : min_pct;
        if (!petg_running_) return 0.0f;
        float lo = petg_limit_c_ - PETG_RAMP_SPAN_C;
        float frac = clamp_((temp_c_ - lo) / PETG_RAMP_SPAN_C, 0.0f, 1.0f);
        return min_pct + (100.0f - min_pct) * frac;
      }
    }
    return 0.0f;
  }

  // 实际写到 PWM 的占空比 0–1。风扇从静止启动、目标又不到全速时，先全速冲 2 秒，
  // 防止两线风扇低速起不来。测试模式不冲，方便实测真实的起转下限。
  float fan_duty(uint32_t now) {
    float target = fan_target_pct();
    if (target <= 0.0f) {
      kicking_ = false;
    } else if (last_target_pct_ <= 0.0f && target < 100.0f && !fan_test_active()) {
      kicking_ = true;
      kick_start_ms_ = now;
    }
    last_target_pct_ = target;
    if (kicking_ && (now - kick_start_ms_) >= FAN_KICK_MS) kicking_ = false;
    return kicking_ ? 1.0f : target / 100.0f;
  }

  // 打印后排风正常走完、期间没人动过灯：YAML 取走这个请求后关灯。
  bool take_light_off_request() {
    bool r = light_off_request_;
    light_off_request_ = false;
    return r;
  }

 private:
  static float clamp_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

  void set_mode_(Mode m, bool manual, uint32_t now) {
    if (manual) manual_ = true;
    mode_ = m;
    fan_test_pct_ = 0.0f;  // 任何模式切换都结束风扇测试
    purge_from_print_ = false;
    light_touched_ = false;
    if (m == Mode::PURGE) purge_start_ms_ = now;
  }

  void update_petg_running_() {
    if (!has_temp_) return;
    float lo = petg_limit_c_ - PETG_RAMP_SPAN_C;
    if (!petg_running_ && temp_c_ > lo) {
      petg_running_ = true;
    } else if (petg_running_ && temp_c_ < lo - PETG_RAMP_HYST_C) {
      petg_running_ = false;
    }
  }

  Mode mode_{Mode::OFF};  // 上电：关闭
  bool manual_{false};    // 本次打印手动改过模式

  float petg_limit_c_{33.0f};
  bool petg_running_{false};

  float temp_c_{NAN};
  bool has_temp_{false};
  uint32_t last_temp_ms_{0};
  bool stale_{false};

  bool overtemp_{false};
  bool overtemp_suppressed_{false};

  uint32_t purge_start_ms_{0};
  bool purge_from_print_{false};
  bool light_touched_{false};
  bool light_off_request_{false};

  float fan_test_pct_{0.0f};
  float last_target_pct_{0.0f};
  bool kicking_{false};
  uint32_t kick_start_ms_{0};

  bool screen_woken_{false};
  uint32_t screen_wake_ms_{0};
};

// 整个固件共用的一个实例，YAML 的 lambda 里用 a1box::ctrl 访问
inline Controller ctrl;

}  // namespace a1box

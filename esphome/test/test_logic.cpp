// A1BOX 核心逻辑测试：在电脑上跑，不需要硬件。
//   g++ -std=c++17 -Wall -Wextra -I.. test_logic.cpp -o /tmp/a1box_test && /tmp/a1box_test
// 每个用例对应 REQUIREMENTS.md v13 定稿里的一条规则。
#include "a1box_logic.h"

#include <cstdio>
#include <cstdlib>

using a1box::Controller;
using a1box::Mode;

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    ++g_checks;                                                       \
    if (!(cond)) {                                                    \
      ++g_failed;                                                     \
      std::printf("  失败 %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                 \
  } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// 新控制器：已上电 1 秒，箱温 25°C
static Controller fresh(uint32_t now = 1000) {
  Controller c;
  c.set_petg_limit(33.0f);
  c.fan_min_pct = 30.0f;
  c.on_temperature(25.0f, now);
  c.tick(now);
  return c;
}

static void test_boot() {
  std::puts("上电：模式关闭、风扇停（5.2）");
  Controller c;
  c.tick(1000);
  CHECK(c.mode() == Mode::OFF);
  CHECK(c.fan_target_pct() == 0.0f);
  CHECK(!c.sensor_stale());
  c.tick(31000);  // 上电 31 秒还没读数
  CHECK(c.sensor_stale());
  c.on_temperature(25.0f, 31500);
  CHECK(!c.sensor_stale());
}

static void test_button2_by_mode() {
  std::puts("按键 2 按模式判断（4.7）");
  Controller c = fresh();
  c.on_button2(2000);  // 关闭 → 打印后排风
  CHECK(c.mode() == Mode::PURGE);
  CHECK(c.fan_target_pct() == 100.0f);
  c.on_button2(3000);  // 打印后排风 → 关闭
  CHECK(c.mode() == Mode::OFF);
  CHECK(c.fan_target_pct() == 0.0f);

  // PETG 模式、箱温低、风扇没转：按一下是「停」，回到关闭（不是启动排风）
  c.on_ha_mode(Mode::PETG, 4000);
  CHECK(c.fan_target_pct() == 0.0f);
  c.on_button2(5000);
  CHECK(c.mode() == Mode::OFF);

  // PA 模式同理
  c.on_ha_mode(Mode::PA, 6000);
  c.on_button2(7000);
  CHECK(c.mode() == Mode::OFF);
}

static void test_petg_ramp() {
  std::puts("PETG 按箱温调速：上限-5°C 以下不转，到上限全速（4.5）");
  Controller c = fresh();
  c.on_ha_mode(Mode::PETG, 2000);
  c.on_temperature(27.9f, 3000);
  CHECK(c.fan_target_pct() == 0.0f);
  c.on_temperature(28.1f, 4000);  // 刚过 28°C：以最低占空比起转
  CHECK(c.fan_target_pct() > 30.0f && c.fan_target_pct() < 32.0f);
  c.on_temperature(30.5f, 5000);  // 中间
  CHECK(near(c.fan_target_pct(), 65.0f));
  c.on_temperature(33.0f, 6000);
  CHECK(near(c.fan_target_pct(), 100.0f));
  c.on_temperature(36.0f, 7000);
  CHECK(near(c.fan_target_pct(), 100.0f));
  // 回差：降到 27.8 还在转，降到 27.4 才停
  c.on_temperature(27.8f, 8000);
  CHECK(near(c.fan_target_pct(), 30.0f));
  c.on_temperature(27.4f, 9000);
  CHECK(c.fan_target_pct() == 0.0f);
  // 改上限后立即按新上限算
  c.on_temperature(30.0f, 10000);
  c.set_petg_limit(38.0f);
  CHECK(c.fan_target_pct() == 0.0f);
}

static void test_overtemp() {
  std::puts("超温：50°C 全速排风，45°C 以下恢复（5.2）");
  Controller c = fresh();
  c.on_temperature(50.5f, 2000);
  CHECK(c.overtemp());
  CHECK(c.fan_target_pct() == 100.0f);
  c.on_temperature(46.0f, 3000);  // 45–50 之间保持
  CHECK(c.fan_target_pct() == 100.0f);
  c.on_temperature(44.9f, 4000);
  CHECK(!c.overtemp());
  CHECK(c.fan_target_pct() == 0.0f);  // 回到当时的模式（关闭）

  std::puts("超温时 HA 改模式停不掉排风（4.9）");
  c.on_temperature(51.0f, 5000);
  c.on_ha_mode(Mode::PA, 6000);
  CHECK(c.mode() == Mode::PA);
  CHECK(c.fan_target_pct() == 100.0f);
  c.on_ha_mode(Mode::OFF, 7000);
  CHECK(c.fan_target_pct() == 100.0f);

  std::puts("超温时按键 2 能停，降到 45°C 以下再超温才重新触发（4.7）");
  c.on_ha_mode(Mode::PETG, 8000);
  c.on_button2(9000);
  CHECK(c.mode() == Mode::OFF);
  CHECK(c.fan_target_pct() == 0.0f);
  CHECK(c.overtemp());              // 报警状态还在
  c.on_temperature(53.0f, 10000);   // 继续升温也不再自动排风
  CHECK(c.fan_target_pct() == 0.0f);
  c.on_button2(11000);              // 这时再按，按模式判断：关闭 → 排风
  CHECK(c.mode() == Mode::PURGE);
  c.on_button2(12000);
  CHECK(c.mode() == Mode::OFF);
  c.on_temperature(44.0f, 13000);   // 降到 45 以下：重新布防
  CHECK(!c.overtemp());
  c.on_temperature(50.1f, 14000);   // 新一次超温
  CHECK(c.fan_target_pct() == 100.0f);
}

static void test_sensor_stale() {
  std::puts("传感器 30 秒无数据：PETG 排风 60%（5.2）");
  Controller c = fresh(1000);
  c.on_ha_mode(Mode::PETG, 1000);
  c.tick(30000);
  CHECK(!c.sensor_stale());
  c.tick(31500);
  CHECK(c.sensor_stale());
  CHECK(near(c.fan_target_pct(), 60.0f));
  c.fan_min_pct = 70.0f;  // 最低占空比比 60 还高时，用最低占空比
  CHECK(near(c.fan_target_pct(), 70.0f));
  c.fan_min_pct = 30.0f;
  c.on_ha_mode(Mode::PA, 32000);  // 其他模式不受影响
  CHECK(c.fan_target_pct() == 0.0f);
  c.on_ha_mode(Mode::PETG, 33000);
  c.on_temperature(25.0f, 34000);  // 恢复
  c.tick(34500);
  CHECK(!c.sensor_stale());
  CHECK(c.fan_target_pct() == 0.0f);
}

static void test_print_linkage() {
  std::puts("开始打印：PETG；耗材是 PA 类再切 PA（4.6）");
  Controller c = fresh();
  c.on_print_started(2000);
  CHECK(c.mode() == Mode::PETG);
  c.on_print_material("PLA", 3000);
  CHECK(c.mode() == Mode::PETG);
  c.on_print_material("PA-CF", 4000);
  CHECK(c.mode() == Mode::PA);

  std::puts("手动改过模式后，本次打印不再自动改（4.6）");
  c.on_print_started(5000);
  c.on_button2(6000);  // 手动停
  CHECK(c.mode() == Mode::OFF);
  c.on_print_material("PAHT-CF", 7000);
  CHECK(c.mode() == Mode::OFF);
  c.on_ha_mode(Mode::PETG, 8000);
  c.on_print_material("PA", 9000);
  CHECK(c.mode() == Mode::PETG);

  std::puts("打印结束照常排风，不受手动优先影响（4.6）");
  c.on_print_finished(10000);
  CHECK(c.mode() == Mode::PURGE);
  CHECK(c.fan_target_pct() == 100.0f);

  std::puts("新的打印清掉手动标记");
  c.on_print_started(20000);
  CHECK(!c.manual_override());
  c.on_print_material("PA6-GF", 21000);
  CHECK(c.mode() == Mode::PA);
}

static void test_purge_and_light() {
  const uint32_t ten_min = 10u * 60u * 1000u;

  std::puts("打印后排风走完 10 分钟：回到关闭并关灯（4.6）");
  Controller c = fresh();
  c.on_print_finished(2000);
  c.tick(2000 + ten_min - 1);
  CHECK(c.mode() == Mode::PURGE);
  CHECK(!c.take_light_off_request());
  c.tick(2000 + ten_min);
  CHECK(c.mode() == Mode::OFF);
  CHECK(c.take_light_off_request());
  CHECK(!c.take_light_off_request());  // 只关一次

  std::puts("排风期间动过灯：结束后不关灯");
  c.on_print_finished(1000000);
  c.on_light_changed();
  c.tick(1000000 + ten_min);
  CHECK(c.mode() == Mode::OFF);
  CHECK(!c.take_light_off_request());

  std::puts("打印期间动过灯不算（打印结束是新事件）");
  c.on_print_started(2000000);
  c.on_light_changed();
  c.on_print_finished(3000000);
  c.tick(3000000 + ten_min);
  CHECK(c.take_light_off_request());

  std::puts("排风被按键 2 提前停：不关灯");
  c.on_print_finished(4000000);
  c.on_button2(4100000);
  CHECK(c.mode() == Mode::OFF);
  c.tick(4000000 + ten_min);
  CHECK(!c.take_light_off_request());

  std::puts("按键启动的排风：结束回到关闭，不关灯");
  c.on_button2(5000000);
  CHECK(c.mode() == Mode::PURGE);
  c.tick(5000000 + ten_min);
  CHECK(c.mode() == Mode::OFF);
  CHECK(!c.take_light_off_request());

  std::puts("排风时长可调");
  c.purge_ms = 60000;
  c.on_print_finished(6000000);
  c.tick(6000000 + 60000);
  CHECK(c.mode() == Mode::OFF);
  CHECK(c.take_light_off_request());

  std::puts("取消打印时连续收到两个结束事件：不重新计时，动过灯的记录也保留");
  c.purge_ms = ten_min;
  c.on_print_finished(6500000);
  c.on_light_changed();
  c.on_print_finished(6502000);
  c.tick(6500000 + ten_min);
  CHECK(c.mode() == Mode::OFF);
  CHECK(!c.take_light_off_request());

  std::puts("排风期间开始新的打印：联动接管");
  c.purge_ms = ten_min;
  c.on_print_finished(7000000);
  c.on_print_started(7100000);
  CHECK(c.mode() == Mode::PETG);
  c.tick(7000000 + ten_min);
  CHECK(c.mode() == Mode::PETG);
  CHECK(!c.take_light_off_request());
}

static void test_millis_wrap() {
  std::puts("millis() 回绕（约 49 天）不影响计时");
  Controller c;
  const uint32_t t0 = 0xFFFFFFFFu - 5000u;
  c.on_temperature(25.0f, t0);
  c.purge_ms = 60000;
  c.on_print_finished(t0);
  c.tick(t0 + 30000u);  // 已经回绕
  CHECK(c.mode() == Mode::PURGE);
  CHECK(!c.sensor_stale());
  c.on_temperature(25.0f, t0 + 30000u);
  c.tick(t0 + 60000u);
  CHECK(c.mode() == Mode::OFF);
}

static void test_fan_kick_and_test_mode() {
  std::puts("风扇从静止低速启动：先全速 2 秒");
  Controller c = fresh();
  c.on_ha_mode(Mode::PETG, 2000);
  CHECK(c.fan_duty(2000) == 0.0f);
  c.on_temperature(29.0f, 3000);
  CHECK(c.fan_duty(3000) == 1.0f);
  CHECK(c.fan_duty(4999) == 1.0f);
  CHECK(near(c.fan_duty(5000), c.fan_target_pct() / 100.0f));
  c.on_temperature(30.0f, 6000);  // 已经在转，调速不再冲
  CHECK(near(c.fan_duty(6000), c.fan_target_pct() / 100.0f));

  std::puts("风扇测试：只在关闭模式生效，不冲，切模式就结束");
  c.on_ha_mode(Mode::OFF, 7000);
  CHECK(c.fan_duty(7000) == 0.0f);
  c.set_fan_test_pct(25.0f);
  CHECK(c.fan_test_active());
  CHECK(near(c.fan_duty(7100), 0.25f));
  c.on_ha_mode(Mode::PA, 8000);
  CHECK(c.fan_test_pct() == 0.0f);
  CHECK(c.fan_duty(8000) == 0.0f);
  c.set_fan_test_pct(50.0f);  // 不在关闭模式：不接受
  CHECK(c.fan_test_pct() == 0.0f);
  c.on_ha_mode(Mode::OFF, 9000);
  c.set_fan_test_pct(40.0f);
  c.on_button2(10000);  // 按键 2 照常按模式：关闭 → 排风
  CHECK(c.mode() == Mode::PURGE);
  CHECK(c.fan_test_pct() == 0.0f);
}

static void test_is_pa_material() {
  std::puts("PA 类耗材识别");
  CHECK(a1box::is_pa_material("PA"));
  CHECK(a1box::is_pa_material("PA-CF"));
  CHECK(a1box::is_pa_material("PAHT-CF"));
  CHECK(a1box::is_pa_material("pa6-gf"));
  CHECK(a1box::is_pa_material("PPA-CF"));
  CHECK(a1box::is_pa_material("Support for PA/PET"));
  CHECK(!a1box::is_pa_material("PLA"));
  CHECK(!a1box::is_pa_material("PETG"));
  CHECK(!a1box::is_pa_material("PET-CF"));
  CHECK(!a1box::is_pa_material("PLA Sparkle"));
  CHECK(!a1box::is_pa_material("PVA"));
  CHECK(!a1box::is_pa_material(""));
  CHECK(!a1box::is_pa_material("unknown"));
}

static void test_screen() {
  std::puts("板载小屏：上电不亮（4.10）");
  Controller c = fresh(1000);
  CHECK(!c.screen_on(1000));
  CHECK(!c.screen_on(100000));

  std::puts("板载小屏：按键后亮 30 秒");
  c.on_any_button(200000);
  CHECK(c.screen_on(200000));
  CHECK(c.screen_on(200000 + 29999));
  CHECK(!c.screen_on(200000 + 30000));
  c.tick(200000 + 30000);
  CHECK(!c.screen_on(200000 + 30000));

  std::puts("板载小屏：30 秒内再按，重新计 30 秒");
  c.on_any_button(400000);
  c.on_any_button(420000);
  CHECK(c.screen_on(400000 + 40000));
  CHECK(!c.screen_on(420000 + 30000));

  std::puts("板载小屏：有报警也不常亮，只看按键");
  c.on_temperature(51.0f, 1000000);
  CHECK(c.overtemp());
  CHECK(!c.screen_on(1000000));
  c.tick(1100000);  // 传感器也失效了
  CHECK(c.sensor_stale());
  CHECK(!c.screen_on(1100000));
  c.on_any_button(1100000);
  CHECK(c.screen_on(1100000 + 10000));
  CHECK(!c.screen_on(1100000 + 30000));

  std::puts("打印后排风剩余时间");
  Controller d = fresh(1000);
  CHECK(d.purge_remaining_ms(1000) == 0);
  d.on_print_finished(2000);
  CHECK(d.purge_remaining_ms(2000) == 600000);
  CHECK(d.purge_remaining_ms(2000 + 450000) == 150000);
  CHECK(d.purge_remaining_ms(2000 + 700000) == 0);
}

int main() {
  test_boot();
  test_button2_by_mode();
  test_petg_ramp();
  test_overtemp();
  test_sensor_stale();
  test_print_linkage();
  test_purge_and_light();
  test_millis_wrap();
  test_fan_kick_and_test_mode();
  test_is_pa_material();
  test_screen();
  std::printf("\n%d 项检查，%d 项失败\n", g_checks, g_failed);
  return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

# A1BOX 固件

依据 `REQUIREMENTS.md` v13 定稿。ESPHome 2026.10.2，ESP-IDF 框架。

| 文件 | 内容 |
|---|---|
| `a1box.yaml` | 硬件接线、HA 实体、对外动作 |
| `a1box_logic.h` | 核心逻辑：模式、排风、超温、按键、联动。不碰硬件 |
| `test/test_logic.cpp` | 核心逻辑测试，在电脑上跑 |
| `a1box.sh` | 命令行：测试、编译、烧录、看日志 |
| `secrets.yaml` | WiFi 密码和加密密钥，**不要发给别人** |

## 命令

```bash
./a1box.sh test      # 跑逻辑测试，不需要硬件
./a1box.sh compile   # 只编译
./a1box.sh run       # 编译并烧录：第一次插 USB，以后自动走 WiFi
./a1box.sh logs      # 看主控日志
```

- ESPHome 装在 `~/.local/share/a1box-esphome/venv`，编译产物在 `esphome/.esphome/`。
- ESP-IDF 工具链走乐鑫的国内镜像，`a1box.sh` 里已经设好。

## 第一次烧录（USB）

1. 让当前用户能访问串口，执行一次，然后**注销重新登录**：
   ```bash
   sudo usermod -aG dialout $USER
   ```
2. **断开箱子的 24V 电源**，只用 USB 线连主控（`HARDWARE.md` 第 4 节第 3 条）。
3. `./a1box.sh run`，提示选端口时选 `/dev/ttyUSB0`。这块板子的 USB 口走的是 CH340 转串口。
   - 如果连不上：按住 BOOT 键，再按一下 RST，松开 BOOT，再试。
4. 烧好后主控会连 WiFi。以后改固件直接 `./a1box.sh run`，选 WiFi（OTA）就行，升级过程是加密的。

## 到货后的调试清单

前提：HA 已经装好，主控已接入 HA。下面这些调试项都要在 HA 里操作。

1. **I2C 设备**（待定 4）：`./a1box.sh logs`，主控重启后日志开头会列出 I2C 总线上找到的地址。
   - 应该有 `0x44`（SHT40）。
   - 如果还有 `0x3C`，那就是板载小屏，和 SHT40 不冲突。
2. **板载 LED**：红灯是电源指示灯，通电就常亮，固件控制不了。绿灯（GPIO7）是状态灯：
   - 上电后先常亮约 3 秒（灯检）；
   - 常亮：一切正常；
   - 快闪（每秒约 4 次）：有组件故障，比如 SHT40 没接或接线有问题；
   - 慢闪（约 1.5 秒一次）：警告，比如 WiFi 断了。HA 没连上不会让它闪。
3. **两线风扇的调速范围**（待定 3）：
   1. 模式切到「关闭」。
   2. 在 HA 里找到 A1BOX 设备的「Fan Test Duty」（在设备页的「配置」里），从 100 往下调，每次减 5。
   3. 记下风扇开始抖动或停转的数值，加 5～10，填到「Fan Min Duty」。
   4. 再从 0 直接调到这个值，看能不能从静止转起来。转不起来也没关系，固件在风扇从静止启动时会先全速冲 2 秒。
   5. 有明显的嗡嗡声或咔哒声，告诉我，改 PWM 频率（`fan_pwm_freq`，现在是 100Hz）。
4. **按键**：
   - 按键 1 开关灯。
   - 按键 2：在「关闭」模式下按一下，全速排风；再按一下停止。
5. **超温保护**：用吹风机对着 SHT40 吹，别靠太近。
   - 超过 50°C：风扇全速，手机收到报警。
   - 这时按按键 2，风扇能停。
   - 降到 45°C 以下后报警解除。

## HA 里的实体

| 实体 | 作用 |
|---|---|
| Light | 灯，开关和亮度 |
| Mode | 模式：关闭 / PETG / PA / 打印后排风 |
| PETG Max Temp | PETG 箱温上限，25–38°C，默认 33 |
| Purge Duration | 打印后排风时长，默认 10 分钟 |
| Temperature、Humidity | 箱温、湿度。变化够大才上报，否则 5 分钟一次，减少树莓派 SD 卡的写入 |
| Exhaust Speed | 排风占空比 |
| Overtemp、Sensor Fault | 超温、传感器失效，HA 据此推送 |
| Fan Min Duty、Fan Test Duty | 调试用，见上面第 3 条 |

HA 的联动自动化调用三个动作：`esphome.a1box_print_started`、`esphome.a1box_print_material`、`esphome.a1box_print_finished`。见 `../homeassistant/`。

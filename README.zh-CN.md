[English](README.md) | **简体中文**

# A1BOX

给开放式**拓竹 A1** 打印机配的封闭箱控制器：管照明、排风过滤和温湿度监测，并通过 Home Assistant 和打印机自动联动。所有安全逻辑都在主控本地运行，WiFi 或 Home Assistant 断开也照样生效。

箱体用的是 MakerWorld 上**森林木院**的模型「**A1封箱pro**」，原样打印。模型文件属于 MakerWorld 独家许可，不收录在这个仓库里。本项目只包括电子部分、固件和 Home Assistant 配置。

## 功能（第一版）

- **照明**：24V COB 灯带，PWM 调光。开始打印时自动开灯，打印后排风结束后自动关灯。
- **排风过滤**：24V 12025 两线风扇，配 H13 HEPA + 活性炭滤芯。PWM 调速，从静止启动时先全速冲一下。
- **温湿度**：SHT40，在 Home Assistant 里记录历史曲线。
- **四种模式**：

  | 模式 | 排风 |
  |---|---|
  | 关闭 | 关 |
  | PETG | 按箱温自动调速，把箱温压在上限以下（默认 33°C） |
  | PA | 关，靠热床余热保温 |
  | 打印后排风 | 全速 10 分钟 |

- **和打印机联动**（通过 [ha-bambulab](https://github.com/greghesp/ha-bambulab)，云模式，不开开发者模式）：
  - 开始打印：开灯，进入 PETG 模式；耗材是 PA 类就切到 PA 模式。
  - 打印完成、失败或取消：进入打印后排风。
  - 打印过程中手动改过模式，这次打印就不再自动改。
- **两个实体按键**：
  - 按键 1 开关灯。
  - 按键 2：「关闭」模式下按一下开始排风；其他模式下按一下全部停止。
  - 按键优先级高于一切自动程序。
- **板载小屏**（0.96 寸 128×64 OLED）：按任一按键后亮 30 秒，显示模式、温湿度、排风、灯和报警。任何时候都不常亮，防止烧屏。
- **手机**：用 Home Assistant App 控制。推送打印完成（附摄像头截图）、箱温过高、传感器失效、主控掉线。

### 安全保护

| 情况 | 处理 |
|---|---|
| 箱温超过 50°C | 全速排风并报警，降到 45°C 以下自动恢复 |
| 温湿度传感器 30 秒没有数据 | 报警，PETG 模式下排风固定 60% |
| 上电 | 全部关闭 |
| WiFi 或 HA 断开 | 不影响，以上保护都在主控本地执行 |

- 硬件上：24V 输入端有慢断保险丝和总电源开关。
- 第二阶段给 PA 加 PTC 加热器时，会加装不依赖固件的温控开关和温度保险丝。

## 硬件

- **主控**：天问 ESP32C3-Pro（ESP32-C3，带板载 OLED，USB 口走 CH340 转串口）。
- **供电**：外接 24V 适配器 → XT60 → 保险丝 → 总开关，再降压成 5V 给主控。不从打印机取电。
- **驱动**：灯带和风扇各用一个 MOSFET 模块，风扇两端并续流二极管。
- **Home Assistant**：树莓派 3B+，装 Home Assistant OS。

| GPIO | 用途 |
|---|---|
| 0 | 预留给第二阶段的加热器 |
| 1 | 灯带（PWM） |
| 3 | 排风扇（PWM） |
| 5 / 6 | I2C SDA / SCL：SHT40（0x44）、板载小屏（0x3C） |
| 7 | 绿色状态灯：常亮 = 正常，快闪 = 故障，慢闪 = 警告 |
| 9 | 按键 1（和板载 BOOT 键是同一个引脚） |
| 10 | 按键 2 |

完整的物料清单和接线见 [`HARDWARE.md`](HARDWARE.md)。

## 目录

| 路径 | 内容 |
|---|---|
| `REQUIREMENTS.md` | 需求说明（v13 定稿） |
| `HARDWARE.md` | 物料清单与接线 |
| `SHOPPING.md` | 采购清单 |
| `esphome/` | 固件：`a1box.yaml`（接线和 HA 实体）、`a1box_logic.h`（全部控制逻辑，不碰硬件）、`test/`（在电脑上跑的测试）、`a1box.sh`（命令行脚本） |
| `homeassistant/` | HA 配置包（打印机联动、推送）和仪表盘卡片 |
| `存档/` | 完整版 v11 的设计存档（带触摸屏和摄像头，没有做） |
| `硬件资料/` | 引脚图等参考图片 |

## 快速开始

需要：Linux、Python 3.12、支持 C++17 的编译器（跑测试用）、[ESPHome](https://esphome.io/) 2026.10.2。

```bash
# 1. 把 ESPHome 装到 a1box.sh 预期的位置
python3.12 -m venv ~/.local/share/a1box-esphome/venv
~/.local/share/a1box-esphome/venv/bin/pip install "esphome==2026.10.2"

# 2. 填 WiFi 和密钥（secrets.yaml 不会进 git）
cd esphome
cp secrets.example.yaml secrets.yaml

# 3. 在电脑上跑逻辑测试，不需要硬件
./a1box.sh test

# 4. 编译并烧录：第一次走 USB（/dev/ttyUSB0），以后走 WiFi
./a1box.sh run
```

- Linux 上访问串口，当前用户需要在 `dialout` 组里。
- `a1box.sh` 默认从乐鑫的国内镜像下载 ESP-IDF 工具链。能直连 GitHub 的话，可以删掉 `IDF_GITHUB_ASSETS` 那一行。
- 调试步骤见 [`esphome/README.md`](esphome/README.md)。
- HA 的安装和配置见 [`homeassistant/README.md`](homeassistant/README.md)。

## 进度

- **已在实物主控上验证**：
  - USB 烧录和 WiFi 加密升级（OTA）；
  - 加密 API；
  - HA 操作和打印联动动作；
  - 按键 1；
  - 板载小屏和状态灯。
- **等硬件到货**：SHT40、风扇调速范围、按键 2、灯带和风扇的实际输出、超温测试。
- **还没装**：Home Assistant。
- **第二阶段**：PA 用的 PTC 加热器，已设计，还没做。

## 第三方内容

- `esphome/fonts/LiberationSans-Regular.ttf`：SIL Open Font License 1.1。
- `esphome/fonts/DroidSansFallbackFull.ttf`：Apache License 2.0。
- 箱体模型「A1封箱pro」：作者森林木院，MakerWorld 独家许可。不收录，请到 MakerWorld 获取。
- [ha-bambulab](https://github.com/greghesp/ha-bambulab)：在 Home Assistant 里另行安装，不收录。

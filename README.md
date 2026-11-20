**English** | [简体中文](README.zh-CN.md)

# A1BOX

An enclosure controller for the open-frame **Bambu Lab A1** 3D printer. It handles the lights, filtered exhaust and temperature/humidity monitoring. It links automatically with the printer through Home Assistant, and all safety logic keeps running when WiFi or Home Assistant is down.

The enclosure itself is the MakerWorld model **"A1封箱pro" by 森林木院**, printed as-is. The model files are under the MakerWorld Exclusive License, so they are not part of this repository. This project covers only the electronics, firmware and Home Assistant setup.

> The detailed design documents (`REQUIREMENTS.md`, `HARDWARE.md`, `SHOPPING.md`) and the sub-folder READMEs are in Chinese.

## Features (v1)

- **Lighting**: 24V COB LED strip, PWM dimming. Turns on when a print starts and off after the post-print purge.
- **Filtered exhaust**: 24V 12025 two-wire fan with a HEPA H13 + activated carbon filter. PWM speed control with kick-start from standstill.
- **Temperature and humidity**: SHT40, with history in Home Assistant.
- **Four modes**:

  | Mode | Exhaust |
  |---|---|
  | Off | off |
  | PETG | speed follows box temperature, holding it under a limit (default 33°C) |
  | PA | off, the box keeps heat from the bed |
  | Post-print purge | full speed for 10 minutes |

- **Printer linkage** (via [ha-bambulab](https://github.com/greghesp/ha-bambulab), cloud mode, no developer mode):
  - When a print starts: lights on, PETG mode, then PA mode if the filament is a PA type.
  - When a print finishes, fails or is cancelled: purge.
  - A manual mode change overrides the automation for the rest of that print.
- **Two physical buttons**:
  - Button 1 toggles the light.
  - Button 2 starts a purge from Off, and stops everything in any other mode.
  - Buttons outrank all automation.
- **Onboard OLED** (0.96", 128×64): shows mode, temperature, humidity, fan, light and alarms for 30 seconds after any button press. It is never always-on, to avoid burn-in.
- **Phone**: control and notifications through the Home Assistant app (print finished with a camera snapshot, over-temperature, sensor failure, controller offline).

### Safety

| Condition | Action |
|---|---|
| Box above 50°C | Full exhaust and alert. Resumes below 45°C. |
| No sensor data for 30 s | Alert. PETG mode falls back to 60% exhaust. |
| Power-on | Everything off. |
| WiFi or Home Assistant lost | No effect. All of the above runs locally on the controller. |

- Hardware side: a slow-blow fuse on the 24V input, plus a manual master switch.
- Phase 2 (a PTC heater for PA) adds a hardware thermal cut-off and a thermal fuse, independent of the firmware.

## Hardware

- **Controller**: 天问 ESP32C3-Pro (ESP32-C3, onboard OLED, CH340 USB serial).
- **Power**: external 24V adapter → XT60 → fuse → switch. A buck converter supplies 5V to the controller. Nothing is drawn from the printer.
- **Drivers**: MOSFET modules for the light and the fan. A flyback diode sits across the fan.
- **Home Assistant**: Raspberry Pi 3B+ running Home Assistant OS.

| GPIO | Use |
|---|---|
| 0 | Reserved for the phase 2 heater |
| 1 | Light (PWM) |
| 3 | Exhaust fan (PWM) |
| 5 / 6 | I2C SDA / SCL: SHT40 (0x44) and onboard OLED (0x3C) |
| 7 | Green status LED: steady = OK, fast blink = fault, slow blink = warning |
| 9 | Button 1 (same pin as the onboard BOOT button) |
| 10 | Button 2 |

The full bill of materials and wiring are in [`HARDWARE.md`](HARDWARE.md).

## Repository layout

| Path | Contents |
|---|---|
| `REQUIREMENTS.md` | Requirements (v13, final) |
| `HARDWARE.md` | Bill of materials and wiring |
| `SHOPPING.md` | Shopping list |
| `esphome/` | Firmware: `a1box.yaml` (wiring and Home Assistant entities), `a1box_logic.h` (all control logic, hardware-free), `test/` (host-side tests), `a1box.sh` (helper script) |
| `homeassistant/` | Home Assistant package (printer linkage, notifications) and a dashboard card |
| `存档/` | Archived full design v11 (touchscreen and camera, not built) |
| `硬件资料/` | Reference pinout images |

## Quick start

Requirements: Linux, Python 3.12, a C++17 compiler for the tests, and [ESPHome](https://esphome.io/) 2026.10.2.

```bash
# 1. Install ESPHome where a1box.sh expects it
python3.12 -m venv ~/.local/share/a1box-esphome/venv
~/.local/share/a1box-esphome/venv/bin/pip install "esphome==2026.10.2"

# 2. Fill in WiFi and keys (secrets.yaml is git-ignored)
cd esphome
cp secrets.example.yaml secrets.yaml

# 3. Run the logic tests on your computer (no hardware needed)
./a1box.sh test

# 4. Build and flash: over USB (/dev/ttyUSB0) the first time, over WiFi after that
./a1box.sh run
```

- On Linux, your user needs to be in the `dialout` group to access the serial port.
- `a1box.sh` downloads ESP-IDF tools from Espressif's mirror in China. Remove the `IDF_GITHUB_ASSETS` line if GitHub is reachable for you.
- Commissioning steps are in [`esphome/README.md`](esphome/README.md).
- Home Assistant setup is in [`homeassistant/README.md`](homeassistant/README.md).

## Status

- **Verified on the real controller**:
  - USB and encrypted WiFi (OTA) flashing;
  - encrypted API;
  - Home Assistant controls and printer-linkage actions;
  - button 1;
  - the OLED and the status LED.
- **Waiting for parts**: SHT40, fan speed range, button 2, the real light and fan outputs, over-temperature test.
- **Not yet set up**: Home Assistant.
- **Phase 2**: PTC heater for PA. It is designed but not built.

## Third-party content

- `esphome/fonts/LiberationSans-Regular.ttf`: SIL Open Font License 1.1.
- `esphome/fonts/DroidSansFallbackFull.ttf`: Apache License 2.0.
- Enclosure model: "A1封箱pro" by 森林木院, MakerWorld Exclusive License. Not included; get it from MakerWorld.
- [ha-bambulab](https://github.com/greghesp/ha-bambulab): installed separately in Home Assistant. Not included.

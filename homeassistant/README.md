# A1BOX 的 Home Assistant 配置

| 文件 | 内容 |
|---|---|
| `a1box.yaml` | 配置包：打印机联动、推送、中文名称 |
| `a1box_card.yaml` | 手机上用的仪表盘卡片 |

事件名和实体名在 2026-11-16 对照 ha-bambulab **v2.4.0** 源码核对过。

**网络情况**（2026-11-16 在电脑上测的）：
- 能连上：github.com、GitHub API、ghcr.io（HAOS 拉取 HA 本体用的）、PyPI。
- 连不上：raw.githubusercontent.com、jsDelivr。

所以 ha-bambulab 不走 HACS，直接用本地 clone 的代码手动装，见第 3 步。

## 1. 装 Home Assistant OS

1. 在 GitHub 的 home-assistant/operating-system 发布页下载 `haos_rpi3-64-<版本>.img.xz`。
2. 用 Raspberry Pi Imager 选「使用自定义镜像」，写到 TF 卡。
3. 插卡，接网线，上电。第一次启动会从 ghcr.io 下载 HA 本体，要等 20 分钟以上。
4. 电脑浏览器打开 `http://homeassistant.local:8123`，按提示建账号。

## 2. 接入主控

主控烧好固件、连上 WiFi 后，HA 会自动发现「A1BOX」：

1. 点「配置」。
2. 填加密密钥：`esphome/secrets.yaml` 里 `api_key` 那一行的值。

## 3. 装 ha-bambulab（手动，不用 HACS）

1. 在电脑上把本地 clone 切到正式版：
   ```bash
   cd ha-bambulab && git checkout v2.4.0
   ```
2. 在 HA 的加载项商店装「Samba share」并启动。
3. 电脑文件管理器打开 `smb://homeassistant.local/config`。
4. 把 `ha-bambulab/custom_components/bambu_lab` 整个文件夹拷到 `config/custom_components/` 下面。`custom_components` 不存在就新建。
5. 重启 HA。
6. 设置 → 设备与服务 → 添加集成 → Bambu Lab：
   - 选 **Bambu Cloud**，登录拓竹账号。
   - 「local_mqtt」**不要勾**，保持云模式，打印机也**不要**开开发者模式（REQUIREMENTS 4.6）。
   - 「host」填打印机的局域网 IP。A1 的摄像头是 HA 直接连打印机取画面，要用到这个 IP。最好在路由器里给打印机固定 IP。
   - 「access_code」保持默认，它是从云端自动带出来的。
7. 确认实体前缀：装好后打印机设备名是 `A1_<序列号>`，实体 ID 都以 `a1_<序列号小写>_` 开头。
   - 在 `a1box.yaml` 和 `a1box_card.yaml` 里，把 `a1_printer` 全部替换成 `a1_<序列号小写>`。
   - 替换后，下面这些实体都应该存在：

| 实体 | 用途 |
|---|---|
| `sensor.a1_printer_current_stage` | 当前阶段，真正打印时是 `printing` |
| `sensor.a1_printer_current_layer` | 当前层数 |
| `sensor.a1_printer_active_tray` | 当前料盘，属性 `type` 是耗材类型，如 `PETG`、`PA-CF` |
| `sensor.a1_printer_task_name` | 任务名，推送里用 |
| `camera.a1_printer_camera` | A1 摄像头，推送截图和仪表盘用 |

联动靠的是 ha-bambulab 发出的 `bambu_lab_event` 事件：开始、完成、失败、取消各一个。这些不需要改名。

## 4. 手机推送

1. 手机装 Home Assistant App 并登录。安卓手机要有谷歌服务，否则在外面收不到推送。
2. 设置 → 设备与服务 → Mobile App，看手机的名字。通知服务名是 `notify.mobile_app_<手机名>`。
3. 把 `a1box.yaml` 里的 `notify.mobile_app_phone` 换成它，只有这一处要改。

## 5. 装配置包

1. 在 `config/configuration.yaml` 里加下面几行。如果已经有 `homeassistant:`，就把 `packages` 那行加到它下面。
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
2. 新建文件夹 `config/packages/`，把改好的 `a1box.yaml` 放进去。
3. 开发者工具 → YAML → 检查配置。没有报错就重启。

## 6. 仪表盘

HA 仪表盘 → 编辑 → 添加卡片 → 手动，把改好的 `a1box_card.yaml` 整段粘进去。

## 7. 试一次联动

打一个小件，检查以下几点：

- 开始打印：灯亮，模式变成 PETG。打 PA 的话，第一层开打后变成 PA。
- 打印中按一下按键 2：模式变成「关闭」，之后联动不再改模式。
- 打印结束（完成、失败、取消都算）：
  - 模式变成「打印后排风」，10 分钟后变回「关闭」，同时灯灭；
  - 正常完成时，手机收到「打印完成」，在家里 WiFi 下能看到截图。

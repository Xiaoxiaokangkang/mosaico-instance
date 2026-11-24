# 版本说明

> 本目录对应**仓库发布版本 v2.0.0**，其中固件版本为 **1.0.0**。与根目录 v1.0.0「星芽小屋」是并列的两条产品线，详见 [docs/version-map.md](../docs/version-map.md)。

本文件是 [README](README.md) 第一章的展开，给出**可直接复现的完整版本矩阵**，以及版本语义与变更记录。

## 1. 版本矩阵（2026-11-24 实机验证通过）

| 层级 | 组件 | 版本 / 提交 | 来源 |
|---|---|---|---|
| ① 固件 | Mosaico Instance | `v1.0.0` | 本仓库 `sdkconfig.defaults` 的 `CONFIG_APP_PROJECT_VER` |
| ② 平台 | ESP-IDF | `v6.2.0` / `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` | 本地 ESP-IDF 克隆 |
| ③ 工作区 | esp-mosaico-vibe | `d5092c4` | 上游仓库 |
| ③ 工作区 | submodule `esp-mosaico-utils`（含 ESP-Iris、mosaico-tools） | `f58f4f9` | 工作区 submodule |
| ③ 工作区 | submodule `esp-mosaico-bsp` | `5f88b91` | 工作区 submodule |
| ③ 组件 | `esp-mosaico-bsp` | `0.2.0`（local） | 工作区 |
| ③ 组件 | `esp_iris` | `0.2.0`（local） | 工作区 |
| ③ 组件 | `mosaico_module_interact` / `mosaico_module_mgr` | `0.1.0`（local） | 工作区 |
| ③ 组件 | `lvgl/lvgl` | `9.5.0` | 组件注册表 |
| ③ 组件 | `espressif/esp_lvgl_adapter` | `0.6.2` | 组件注册表 |
| ③ 组件 | `espressif/esp_lcd_co5300` | `2.2.0` | 组件注册表 |
| ③ 组件 | `espressif/esp_lcd_touch_cst9220` | `0.1.2` | 组件注册表 |
| ③ 组件 | `espressif/touch_sensor_lowlevel` / `touch_sensor_fsm` / `touch_button_sensor` | `1.0.2` / `0.9.1~1` / `0.3.0` | 组件注册表 |
| ③ 组件 | `espressif/button` | `4.2.3` | 组件注册表 |
| ③ 组件 | `espressif/led_strip` | `3.1.1` | 组件注册表 |
| ③ 组件 | `espressif/esp_tinyusb` / `espressif/tinyusb` | `2.4.0` / `0.21.0~6` | 组件注册表 |
| ③ 契约 | ESP-Iris 固件契约 | `esp-mosaico/v2`，ABI `2` | `sysmeta` 分区 |

完整的 37 项依赖（含 BSP 传递依赖）见仓库根目录 `dependencies.lock`。

### 1.1 硬件与布局版本

| 项目 | 值 |
|---|---|
| 芯片 | ESP32-S31 |
| 板型 / 右侧模块 | 主板 `board_id=esp-mosaico`；右侧模块 EEPROM `0x51`、板型 `0x16` |
| 保留式布局 | `mosaico-retained-test-2m-v2`：分区表 `0x8000`、otadata `0x9000`、`sysmeta` `0xc000`、`vibe_mode` `0x20000`、coredump `0x1e0000`、`main_app` `0x210000` |
| bootloader 上限 | 24 KiB（`0x2000–0x8000`），实测 `0x4be0` |

---

## 2. 版本语义

- **升固件（①）**：只改本仓库业务代码与 `CONFIG_APP_PROJECT_VER`，不动 ②③。
- **换 IDF（②）**：属于破坏性变更。理由有三：bootloader 体积变化会撞 24 KiB 上限；`esp_phy` / `esp_wifi` / `esp_coex` 是预编译库，必须与 IDF 版本严格配对；ESP-Iris 契约与 recovery ABI 会随平台版本变化。
- **换组件（③）**：以 `dependencies.lock` 为准。修改 `idf_component.yml` 后必须重新生成并提交锁文件；否则相同固件版本可能编出不同二进制。
- **固件契约必须三层同时满足**，否则 `iris system-update` 拒绝安装。当前要求的契约项：`ESP_IRIS_PRODUCT_CONTRACT=esp-mosaico/v2`、`BOARD_ID=esp-mosaico`、`LAYOUT_ID=mosaico-retained-test-2m-v2`、`RECOVERY_ABI=2`、`FIRMWARE_ROLE=1`、`OTA=False`、`OTA_DEFAULT_VIA_RECOVERY=True`、`SYSTEM_INVENTORY=True`、`ESPTOOLPY_FLASHMODE_QIO=True`、`IDF_TARGET=esp32s31`。
- **固件版本号规则**：`主版本.次版本.修订号`。主版本变更 = 状态机或持久化格式不兼容（快照 `PTS1` 结构变化）；次版本 = 新增玩法/交互；修订 = 修 bug 与参数微调。

---

## 3. 变更记录

### v1.0.0（2026-11-24，实机验证）

新增：
- `pet_core`：纯 C 状态机（IDLE / GAME / SETTLE / SLEEP）、三项属性（affection / energy / satiety）、输入事件隔离（点按 250 ms、长按 3000 ms、三连击 800 ms 窗口）、光控双阈值 + 3 s 确认 + 180 s 手动唤醒保护、快照序列化（`PTS1` + v1 + CRC16）
- `pet_ui`：LVGL 桌宠与三条属性条；泡泡游戏层（6 并发、650 ms 生成、3000 ms 存活）；泡泡运动（上升/漂移/边界反弹）与消失前 500 ms 缩小淡出；进场 READY→3→2→1→GO；结算卡
- `pet_app`：20 ms 主循环与事件队列、AI 键喂食、右侧模块热插拔（500 ms 探活）、光敏 100 ms 采样、NVS 节流写入（≥1.5 s）
- 修复：游戏/结算期间屏幕 PRESS/RELEASE 会污染按压状态（`pet_app.c` 状态检查 + `pet_core.c` 防御性返回）；`snprintf` 缓冲区过小导致的 `-Werror=format-truncation`
- 工程：改用 Mosaico 脚手架（`mosaico_idf_project.cmake` + `system_update.cmake`），补齐 ESP-Iris 依赖与契约配置，bootloader 体积压缩

已知限制：
- 无暂停功能、无连击/尺寸计分、无命中率结算、无游戏期围观表情
- `iris screenshot` 不可用（纯 LVGL 无 GSP 镜像）
- ESP32-S31 的 USB PID `0x303A:0020` 不在 native-flash 白名单，需按 README 5.2 用 esptool 写入

# Mosaico Instance

> **本目录的版本定位（先读这一段）**
> - 仓库发布版本：**v2.0.0**（与 v1.0.0 并列的第二条产品线，不是它的功能升级）
> - 固件版本：**v1.0.0**（该产品线首个固件，`CONFIG_APP_PROJECT_VER=1.0.0`）
> - 与根目录 v1.0.0「星芽小屋」**不是同一固件谱系**：硬件相同，玩法、状态机、依赖与许可均不同
> - 平台：ESP-IDF **6.2.0 @ 7b9cc1ac79f8**；许可：**Apache-2.0**（根目录 v1 为 MIT）

ESP-Mosaico 桌面宠物固件（ESP32-S31）：属性系统、有限状态机、泡泡小游戏、光线睡眠/唤醒、NVS 持久化，以及右侧多功能交互模块（电容触摸、光敏、PIR、WS2812）的接入。

本仓库是 [esp-mosaico-vibe](https://github.com/esp-mosaico/esp-mosaico-vibe) 工作区里的**子项目**，源码放在 `projects/desk_pet` 下编译。

---

## 一、版本说明（三层版本，请勿混淆）

本项目的“版本”分三层，各自独立演进，排查问题或复现构建时必须同时记录：

| 层级 | 当前值 | 记录位置 | 说明 |
|---|---|---|---|
| **① 固件版本** | `v1.0.0` | `sdkconfig.defaults` 的 `CONFIG_APP_PROJECT_VER`；运行时可在 `esp_iris` 的 `app_version` 读到 | 只代表本仓库的业务代码迭代，与 IDF 版本无关 |
| **② 平台版本（ESP-IDF）** | `v6.2.0`，commit `7b9cc1ac79f8` | 构建机上的 `/path/to/esp-idf/version.txt` 与 git HEAD | 升级 IDF 会改变 bootloader 大小、预编译 PHY/WiFi 库、固件契约校验结果，属于破坏性变更 |
| **③ 依赖版本（工作区 + 组件）** | 工作区 `esp-mosaico-vibe@d5092c4`、BSP `5f88b91`、utils `f58f4f9`、LVGL `9.5.0` 等 | `dependencies.lock`（已入库） | 完整清单见 [VERSIONS.md](VERSIONS.md) |

一句话规则：**固件升级只改 ①；换 IDF 才动 ②；②不动的前提下，③ 由 `dependencies.lock` 锁定。**

固件契约（写入 `sysmeta` 的声明）为 `ESP-Iris ABI 2`：`product_contract=esp-mosaico/v2`、`board_id=esp-mosaico`、`layout_id=mosaico-retained-test-2m-v2`、`recovery_abi=2`、`firmware_role=normal`、`ota=disabled`。这三层版本都必须满足该契约，否则 `system-update` 会拒绝安装。

---

## 二、硬件

| 项目 | 参数 |
|---|---|
| 主控 | ESP32-S31（`CONFIG_IDF_TARGET=esp32s31`） |
| 屏幕 | CO5300，QSPI，480×480，RGB565，TE 同步 |
| 触摸 | CST9220（I2C） |
| AI 实体键 | GPIO7，低电平有效（`iot_button` 消抖） |
| 右侧交互模块 | EEPROM `0x51`、板型 `0x16`；LDR=GPIO46、KEY_L=GPIO11、KEY_R=GPIO10、PIR=GPIO5、WS2812=GPIO38（6 灯） |

---

## 三、目录结构

```
mosaico-instance/
├── CMakeLists.txt              # Mosaico 工程脚手架 + system-update-bundle 目标
├── partitions.csv              # 保留式布局（app=0x210000）
├── sdkconfig.defaults          # 目标/显示/LVGL/ESP-Iris 契约配置
├── sdkconfig.override.defaults # 仅在最后追加，用于压 bootloader 体积
├── dependencies.lock           # 依赖版本锁（入库，用于复现）
├── main/                       # app_main：esp_iris 引导 + pet_app
├── components/
│   ├── pet_core/               # 纯 C 状态机与属性系统（平台无关，可宿主测试）
│   ├── pet_ui/                 # LVGL 绘制：桌宠、属性条、泡泡层、结算卡
│   └── pet_app/                # 粘合层：BSP/触摸/按键/右侧模块/NVS/事件队列
└── test/host/test_pet_core.c   # 主机侧单元测试（12 项）
```

---

## 四、构建

### 4.1 前置条件

1. 克隆工作区并固定提交：
   ```bash
   git clone https://github.com/esp-mosaico/esp-mosaico-vibe.git
   cd esp-mosaico-vibe && git checkout d5092c4
   git submodule update --init --recursive
   ```
2. 准备 ESP-IDF **v6.2.0 @ `7b9cc1ac79f8`** 并安装 `esp32s31` 工具链。
3. 把本仓库放到 `esp-mosaico-vibe/projects/desk_pet`。

### 4.2 编译

```bash
source /path/to/esp-idf/export.sh
# macOS 的 CMake 装在 .app 内，export.sh 不会把它的 bin 加入 PATH：
export PATH="$HOME/.espressif/tools/cmake/3.30.2/CMake.app/Contents/bin:$PATH"

cd esp-mosaico-vibe/projects/desk_pet
idf.py build
```

> bootloader 区固定为 `0x2000–0x8000`（24 KiB）。当前 IDF 快照默认配置超出 288 字节，因此 `sdkconfig.override.defaults` 关闭了 bootloader 日志与 WDT，实测 bootloader 为 `0x4be0`。该项必须在 recovery 默认配置**之后**追加，故在 `CMakeLists.txt` 里 `list(APPEND SDKCONFIG_DEFAULTS ...)`。

### 4.3 主机单元测试（不需要硬件）

```bash
cd projects/desk_pet
cc -std=c11 -Wall -Wextra \
   -I components/pet_core/include \
   components/pet_core/pet_core.c test/host/test_pet_core.c \
   -o /tmp/pet_test && /tmp/pet_test
```

覆盖：属性衰减与边界、长按单次触发、三连击进游戏、点按窗口、慢按不计点按、睡眠精力结算、唤醒保护期、黑暗不打断游戏、快照 CRC、精力上限等 12 项。

---

## 五、烧录（两条路径，务必区分）

### 5.1 标准路径（推荐）

```bash
cd esp-mosaico-vibe
python3 mosaico.py iris system-update --project projects/desk_pet
```

该命令会校验固件契约、生成 `.irisfw` 系统包并通过 ESP-Iris USB 通道写入。

### 5.2 例外路径：ESP32-S31 的 USB PID 不在白名单

实测本机 ESP32-S31 的 USB 描述符为 `303A:0020`（USB Highspeed），而 ESP-Iris 的 native-flash 白名单只接受 `303A:1001`，`iris flash` 会报：

```
native flash currently requires UART or USB Serial/JTAG
```

此时使用工具链自带 esptool，按 `build/flasher_args.json` 的偏移直接写入（偏移与固件契约完全一致）：

```bash
python -m esptool --chip esp32s31 -p /dev/cu.usbmodem101 \
  --before default-reset --after hard-reset write-flash \
  0x2000   build/bootloader/bootloader.bin \
  0x8000   build/partition_table/partition-table.bin \
  0x9000   build/ota_data_initial.bin \
  0x210000 build/desk_pet.bin
```

> 该路径会覆盖位于 `0x2000` 的保留式 bootloader。本项目编译出的 bootloader 含 `esp_mosaico_boot`（ABI2 选择器），与保留布局兼容；但若换用其他固件，请优先走 5.1。设备需要处于 ROM 下载模式（`303A:0020` 的 `/dev/cu.usbmodem1xx` 端口）才能被 esptool 打开；若端口不存在，按住 BOOT 再点 RESET。

---

## 六、验证状态（三种状态严格区分）

### 6.1 已在实机通过

- 构建通过；`system-update` 固件契约校验通过；`.irisfw` 生成成功
- esptool 写入 + 哈希校验通过；重启后 `app_version=1.0.0`、`idf_version=7b9cc1ac`、`firmware_sha256` 与构建产物一致
- 右侧交互模块识别：`slot=right eeprom=0x51 LDR=46 PIR=5 IR=47 KEY_L=11 KEY_R=10 WS2812=38 leds=6`
- 触摸双通道标定完成（CH4/CH5 基线 5184 / 4992），触摸低层 11 ms 采样运行中
- 光敏持续采样正常（示例 `raw=371 level=1825/2196`）

### 6.2 需要人工确认（固件已实现，但自动化无法覆盖）

- 界面渲染：桌宠本体、LOVE/ENERGY/FULL 三条属性条
- 长按 3 s → `LOVE +5`；AI 键 → `FULL +5`
- 三连击进游戏：READY → 3-2-1-GO 开场，泡泡上升/漂移、消失前 500 ms 缩小淡出
- 遮光 3 s 入睡、睡眠 Zzz、三连击唤醒且 180 s 内不自动再入睡
- 重启后属性恢复；断电期间不计消耗

### 6.3 尚未实现

- 游戏暂停/继续（含暂停遮罩与倒计时冻结）
- 计分梯度：连击奖励、泡泡尺寸权重、命中率结算
- 游戏期间桌宠围观表情
- 截图通道：`iris screenshot` 依赖 GSP 的 `iris_screen_mirror`（`esp_gsp_handle_t`），纯 LVGL 应用无法接入，返回设备错误 `0x106`

---

## 七、行为参数

| 参数 | 值 | 位置 |
|---|---|---|
| 点按上限 / 长按阈值 | 250 ms / 3000 ms | `pet_config_default` |
| 三连击窗口 | 800 ms | `pet_config_default` |
| 游戏时长 | 15 s | `pet_config_default.game_duration_ms` |
| 泡泡：生成间隔 / 存活 / 并发上限 | 650 ms / 3000 ms / 6 | `pet_ui.c` |
| 泡泡运动 | 上升 25–55 px/s，水平漂移 ±12 px/s | `pet_ui.c` |
| 开场 | READY 800 ms → 3/2/1 各 1 s → GO 500 ms | `pet_ui.c` |
| 光敏滤波 | 1/4 EMA（驱动内）+ 内部 EMA | `pet_core.c` |
| 睡眠双阈值 | dark=25 / bright=45，持续确认 3 s | `pet_config_default` |
| 手动唤醒保护 | 180 s | `pet_config_default` |
| 精力衰减 / 饱食衰减 / 睡眠回精力 | 60 s −1 / 300 s −1 / 30 s +1 | `pet_core.c` |

光敏极性默认 `light_bright_is_high = true`，实机若读数反向改为 `false`。

---

## 八、许可

Apache License 2.0，见 [LICENSE](LICENSE)。各源文件保留 SPDX 头。

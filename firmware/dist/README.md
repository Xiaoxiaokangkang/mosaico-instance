# 星芽小屋 · Mosaico 桌宠 v1.0.0 — 预编译固件

目标板：**ESP-Mosaico / ESP32-S31**，16 MB Flash，480×480 CO5300。

## 文件

| 文件 | 说明 |
|---|---|
| `star_pet.bin` | 应用固件（约 863 KiB） |
| `bootloader/bootloader.bin` | 二级 bootloader |
| `partition_table/partition-table.bin` | 分区表 |
| `flash_args` | esptool 写入地址与参数 |
| `SHA256.txt` | 校验和 |

## 烧录

在本目录执行：

```bash
esptool --chip esp32s31 -p /dev/cu.usbmodem101 -b 460800 \
  --before default-reset --after hard-reset write-flash @flash_args
```

如果开发板没有自动复位：

1. 按住 **BOOT**
2. 点一下 **RESET**
3. 继续按住 **BOOT** 约 1 秒
4. 松开 BOOT

然后使用手动下载模式参数（更稳定的波特率）：

```bash
esptool --chip esp32s31 -p /dev/cu.usbmodem101 -b 74880 \
  --before no-reset --after hard-reset write-flash @flash_args
```

## 说明

- 应用分区 4 MB，本固件约占 863 KiB，剩余约 79%。
- 首次上电会进入命名界面；命名后进入主界面。
- 光敏阈值在源码 `components/interaction_module/src/interaction_module.c` 的 `IM_LDR_DARK_RAW`，默认 30000。
- 这是预编译演示固件；完整功能、交互规则和源码构建见仓库根目录的 `README.md`。

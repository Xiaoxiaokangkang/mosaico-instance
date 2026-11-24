/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_iris.h"
#include "pet_app.h"

void app_main(void)
{
    /* 尽早上报复位原因，覆盖产品初始化阶段的崩溃。 */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    pet_app_start();
    /* 桌面宠物需要 ESP-Iris 提供 USB 设备安装、日志与截图通道。 */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_start());
}

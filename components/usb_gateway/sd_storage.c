#include "sd_storage.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

#include "gateway_paths.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

#define SD_ALLOCATION_UNIT_SIZE (32U * 1024U)
#define SD_POWER_GPIO           GPIO_NUM_45
#define SD_POWER_ON_LEVEL       0
#define SD_LDO_CHANNEL          4
#define SD_LDO_VOLTAGE_MV       3300
#define SD_LDO_SETTLE_MS        10U
#define SD_POWER_SETTLE_MS      50U

static const char *TAG = "sd_storage";

static sdmmc_card_t *s_card;
static esp_ldo_channel_handle_t s_ldo;
static bool s_mounted;
static bool s_power_initialized;

static esp_err_t sd_ldo_enable(void)
{
    if (s_ldo != NULL) {
        return ESP_OK;
    }
    const esp_ldo_channel_config_t config = {
        .chan_id = SD_LDO_CHANNEL,
        .voltage_mv = SD_LDO_VOLTAGE_MV,
        .flags = {
            .adjustable = 0,
            .owned_by_hw = 0,
        },
    };
    const esp_err_t error = esp_ldo_acquire_channel(&config, &s_ldo);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "LDO_VO4 enable failed: %s", esp_err_to_name(error));
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(SD_LDO_SETTLE_MS));
    ESP_LOGI(TAG, "LDO_VO4 enabled: channel=%d voltage=%d mV settle=%u ms",
        SD_LDO_CHANNEL, SD_LDO_VOLTAGE_MV, (unsigned)SD_LDO_SETTLE_MS);
    return ESP_OK;
}

static esp_err_t sd_power_initialize(void)
{
    if (s_power_initialized) {
        return ESP_OK;
    }
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << SD_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t error = gpio_config(&config);
    if (error == ESP_OK) {
        s_power_initialized = true;
    }
    return error;
}

static esp_err_t sd_power_enable(void)
{
    esp_err_t error = sd_ldo_enable();
    if (error != ESP_OK) {
        return error;
    }
    error = sd_power_initialize();
    if (error != ESP_OK) {
        return error;
    }
    error = gpio_set_level(SD_POWER_GPIO, SD_POWER_ON_LEVEL);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_SETTLE_MS));
    ESP_LOGI(TAG, "SD power enabled: GPIO=%d level=%d settle=%u ms",
        SD_POWER_GPIO, SD_POWER_ON_LEVEL, (unsigned)SD_POWER_SETTLE_MS);
    return ESP_OK;
}

static esp_err_t create_directory(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Cannot create directory %s: errno=%d", path, errno);
    return ESP_FAIL;
}

esp_err_t sd_storage_ensure_directories(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    static const char *const DIRECTORIES[] = {
        GATEWAY_CONFIG_DIR,
        GATEWAY_MOTION_DIR,
        GATEWAY_SD_ROOT "/captures",
        GATEWAY_SD_ROOT "/logs",
        GATEWAY_SD_ROOT "/firmware",
    };
    for (size_t index = 0U; index < sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0]); ++index) {
        const esp_err_t error = create_directory(DIRECTORIES[index]);
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

esp_err_t sd_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    esp_err_t error = sd_power_enable();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD supply initialization failed: %s", esp_err_to_name(error));
        return error;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = CONFIG_USB_GATEWAY_SDMMC_FREQ_KHZ;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = CONFIG_USB_GATEWAY_SDMMC_CLK_GPIO;
    slot.cmd = CONFIG_USB_GATEWAY_SDMMC_CMD_GPIO;
    slot.d0 = CONFIG_USB_GATEWAY_SDMMC_D0_GPIO;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = CONFIG_USB_GATEWAY_SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOCATION_UNIT_SIZE,
        .disk_status_check_enable = true,
    };
    ESP_LOGI(TAG, "Mount FAT SDMMC 1-bit: CLK=%d CMD=%d D0=%d max=%lu kHz",
        slot.clk, slot.cmd, slot.d0, (unsigned long)host.max_freq_khz);
    error = esp_vfs_fat_sdmmc_mount(GATEWAY_SD_ROOT, &host, &slot, &mount_config, &s_card);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(error));
        s_card = NULL;
        s_mounted = false;
        return error;
    }
    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);

    error = sd_storage_ensure_directories();
    if (error != ESP_OK) {
        esp_vfs_fat_sdcard_unmount(GATEWAY_SD_ROOT, s_card);
        s_card = NULL;
        s_mounted = false;
        return error;
    }
    uint64_t total = 0U;
    uint64_t free_bytes = 0U;
    (void)esp_vfs_fat_info(GATEWAY_SD_ROOT, &total, &free_bytes);
    ESP_LOGI(TAG, "SD mounted: total=%llu bytes free=%llu bytes",
        (unsigned long long)total, (unsigned long long)free_bytes);
    return ESP_OK;
}

void sd_storage_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(GATEWAY_SD_ROOT, s_card);
    s_card = NULL;
    s_mounted = false;
    ESP_LOGI(TAG, "SD unmounted; supply remains enabled");
}

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

uint64_t sd_storage_free_bytes(void)
{
    if (!s_mounted) {
        return 0U;
    }
    /* statvfs() is not implemented by the FAT VFS; use the FATFS query. */
    uint64_t total = 0U;
    uint64_t free_bytes = 0U;
    if (esp_vfs_fat_info(GATEWAY_SD_ROOT, &total, &free_bytes) != ESP_OK) {
        return 0U;
    }
    return free_bytes;
}

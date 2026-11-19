#include "sd_storage.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sdkconfig.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
static sdmmc_card_t*card;static bool mounted;
static void mk(const char*p){(void)mkdir(p,0775);}
esp_err_t sd_storage_ensure_directories(void){if(!mounted)return ESP_ERR_INVALID_STATE;mk("/sdcard/motion");mk("/sdcard/logs");mk("/sdcard/logs/current");mk("/sdcard/logs/archive");mk("/sdcard/tests");mk("/sdcard/tests/latest");mk("/sdcard/tests/archive");mk("/sdcard/system");return ESP_OK;}
esp_err_t sd_storage_mount(void){if(mounted)return ESP_OK;sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=CONFIG_USB_GATEWAY_SDMMC_FREQ_KHZ;sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.cd=CONFIG_USB_GATEWAY_SDMMC_CD_GPIO;slot.width=1;slot.clk=CONFIG_USB_GATEWAY_SDMMC_CLK_GPIO;slot.cmd=CONFIG_USB_GATEWAY_SDMMC_CMD_GPIO;slot.d0=CONFIG_USB_GATEWAY_SDMMC_D0_GPIO;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;const esp_vfs_fat_mount_config_t cfg={.format_if_mount_failed=false,.max_files=CONFIG_USB_GATEWAY_SD_MAX_FILES,.allocation_unit_size=32*1024,.disk_status_check_enable=true};esp_err_t e=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&cfg,&card);if(e==ESP_OK){mounted=true;e=sd_storage_ensure_directories();}return e;}
void sd_storage_unmount(void){if(mounted){esp_vfs_fat_sdcard_unmount("/sdcard",card);card=NULL;mounted=false;}}bool sd_storage_is_mounted(void){return mounted;}uint64_t sd_storage_free_bytes(void){struct statvfs s;if(!mounted||statvfs("/sdcard",&s)!=0)return 0;return(uint64_t)s.f_bavail*s.f_frsize;}

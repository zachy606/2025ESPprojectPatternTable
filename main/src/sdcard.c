#include"app_config.h"
#include "sdcard.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"

#include "driver/sdmmc_host.h"
// #define TAG "SDcard"

// // ==== SDSPI PIN 定義（依你實機腳位調整）====
// #define PIN_NUM_MISO  2
// #define PIN_NUM_MOSI  15
// #define PIN_NUM_CLK   14
// #define PIN_NUM_CS    13
// #define SPI_DMA_CHAN   1

// bool mount_sdcard(sdmmc_card_t **g_card) {
//     esp_err_t ret;

//     spi_bus_config_t bus_cfg = {
//         .mosi_io_num = PIN_NUM_MOSI,
//         .miso_io_num = PIN_NUM_MISO,
//         .sclk_io_num = PIN_NUM_CLK,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//         .max_transfer_sz = SD_MAX_TRANSFER_SIZE,
//     };
//     ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CHAN));

//     sdmmc_host_t host = SDSPI_HOST_DEFAULT();
//     host.max_freq_khz = SD_SPI_MAX_FREQ_KHZ ;  //not sure
//     sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
//     slot_config.gpio_cs = PIN_NUM_CS;
//     slot_config.host_id = SPI2_HOST;

//     esp_vfs_fat_sdmmc_mount_config_t mount_config = {
//         .format_if_mount_failed = false,
//         .max_files = SD_MAX_FILES,
//         .allocation_unit_size = SD_ALLOC_UNIT_SIZE
//     };

//     ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, g_card);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
//         spi_bus_free(SPI2_HOST);
//         return false;
//     }

//     sdmmc_card_print_info(stdout, *g_card);
//     return true;
// }



// void unmount_sdcard(sdmmc_card_t **g_card) {
//     if (*g_card) {
//         esp_vfs_fat_sdcard_unmount(MOUNT_POINT, *g_card);
//         *g_card = NULL;
//     }
//     spi_bus_free(SPI2_HOST);
// }




#define TAG "SDMMC"

// ==== SDMMC 4-bit PIN 定義（依實際接線修改）====
#define PIN_NUM_CLK    14
#define PIN_NUM_CMD    15
#define PIN_NUM_D0     2
#define PIN_NUM_D1     4
#define PIN_NUM_D2     12
#define PIN_NUM_D3     13

#define SDMMC_FREQ     SDMMC_FREQ_DEFAULT//SDMMC_FREQ_HIGHSPEED  // 40MHz，不穩就改 SDMMC_FREQ_DEFAULT (20MHz)
#define SDMMC_WIDTH    1                    // 資料寬度 4-bit

bool mount_sdcard(sdmmc_card_t **g_card)
{
    esp_err_t ret;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SDMMC_WIDTH;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; 


    // 指定 GPIO
    slot_config.clk = PIN_NUM_CLK;
    slot_config.cmd = PIN_NUM_CMD;
    slot_config.d0  = PIN_NUM_D0;
    slot_config.d1  = PIN_NUM_D1;
    slot_config.d2  = PIN_NUM_D2;
    slot_config.d3  = PIN_NUM_D3;

    slot_config.gpio_cd = -1; // 沒有卡檢測腳
    slot_config.gpio_wp = -1; // 沒有寫保護腳

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT_SIZE
    };

    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, g_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATFS mount fail: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_card_print_info(stdout, *g_card);
    return true;
}

void unmount_sdcard(sdmmc_card_t **g_card)
{
    if (*g_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, *g_card);
        *g_card = NULL;
    }
}

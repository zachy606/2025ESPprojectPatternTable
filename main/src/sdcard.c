#include "sdcard.h"
#define TAG "SDcard"

// ==== SDSPI PIN 定義（依你實機腳位調整）====
#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  15
#define PIN_NUM_CLK   14
#define PIN_NUM_CS    13
#define SPI_DMA_CHAN   1

bool mount_sdcard(sdmmc_card_t *g_card,const char *mount_point) {
    esp_err_t ret;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8*1024,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CHAN));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 26000;  //not sure
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &g_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return false;
    }

    sdmmc_card_print_info(stdout, g_card);
    return true;
}

void unmount_sdcard(sdmmc_card_t *g_card,const char *mount_point) {
    if (g_card) {
        esp_vfs_fat_sdcard_unmount(mount_point, g_card);
        g_card = NULL;
    }
    spi_bus_free(SPI2_HOST);
}
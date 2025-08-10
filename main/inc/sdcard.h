#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"

bool mount_sdcard(sdmmc_card_t **g_card);
void unmount_sdcard(sdmmc_card_t **g_card);
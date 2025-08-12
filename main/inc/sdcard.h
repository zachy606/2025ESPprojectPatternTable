#pragma once


#include "sdmmc_cmd.h"


bool mount_sdcard(sdmmc_card_t **g_card);
void unmount_sdcard(sdmmc_card_t **g_card);
#include "lightdance_reader.h"
#include "player.h"
#include "sdcard.h"
#include "state_changer.h"


#define TAG "PLAYER_MAIN"
#define MOUNT_POINT "/sdcard"
#define TIME_DATA "200time.txt"
#define FRAME_DATA "200data.txt"

static sdmmc_card_t *g_card = NULL;
static player P;
static PlayerState State = STATE_IDLE;

// command list start pause resume stop exit

void app_main(void) {
    // 1) 掛載 SD
    if (!mount_sdcard(&g_card,MOUNT_POINT)) {
        ESP_LOGE(TAG, "SD mount failed. Abort.");
        return;
    }
    
    // 2) 初始化 Reader + 讀檔
    player_reader_init(&P,MOUNT_POINT,TIME_DATA,FRAME_DATA,&g_card);


    
    command_loop(&P,&State,&g_card,MOUNT_POINT);
    ESP_LOGE(TAG, "code finish");
}

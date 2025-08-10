#include "lightdance_reader.h"
#include "player.h"
#include "sdcard.h"

#define TAG "PLAYER_MAIN"
#define MOUNT_POINT "/sdcard"
#define TIME_DATA "times.txt"
#define FRAME_DATA "8data.txt"

static sdmmc_card_t *g_card = NULL;
static player P;

void app_main(void) {
    // 1) 掛載 SD
    if (!mount_sdcard(g_card,MOUNT_POINT)) {
        ESP_LOGE(TAG, "SD mount failed. Abort.");
        return;
    }

    // 2) 初始化 Reader + 讀檔
    player_init(&P,MOUNT_POINT,TIME_DATA,FRAME_DATA,g_card);


    LightdanceReader_read_frame_at(&P.Reader,P.reader_index,"8data.txt",&P.fd_test[P.reader_index%2]);
    LightdanceReader_read_frame_go_through(&P.Reader,&P.fd_test[(P.reader_index+1)%2]);
    // print_framedata(&fd_test[(reader_index+1)%2]);
    //gptimer
    timer_init(&P);
    player_start(&P);
    while(P.reader_index < LightdanceReader_get_total_frames(&P.Reader) ){
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    player_stop(&P);



    unmount_sdcard(g_card,MOUNT_POINT);
    ESP_LOGI(TAG, "Main exits.");
}

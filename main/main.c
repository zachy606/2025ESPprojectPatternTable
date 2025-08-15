#include"app_config.h"
#include "pattern_table.h"
#include "player.h"
#include "sdcard.h"
#include "state_changer.h"
#include "testing.h"


#define TAG "PLAYER_MAIN"

static player P;
static PlayerState State = STATE_IDLE;

// command list  start  pause  resume  stop  exit
// please copy above commend and paste directly into cmd to use
// if commend is splitted, please paste it again

// commad update 
// now can input start time to start at any time(ms)
// ex : start 12500 (need waiting if time is far from 0)   start 1

// void app_main(void) {
//     // esp_log_level_set("*", ESP_LOG_WARN);
//     player_reader_init(&P);
//     command_loop(&P,&State);
//         // player_var_init(&P);
//     // ESP_LOGI(TAG,"start init");
//     // PatternTable_read_frame_at(&P->Reader,P.reader_index,&P->fd_test[&P->reader_index%2]);
//     // PatternTable_read_frame_go_through(&P->Reader,&P->fd_test[(P->reader_index+1)%2]);
    
// }
static char buf[8192];
void app_main(void)
{
    sdmmc_card_t *card;
    if(mount_sdcard(&card)){
        ESP_LOGI("SD","mount succesful");
    }
    char path[128];
    snprintf(path, sizeof(path), "%s%s", MOUNT_POINT, "/h800data.txt");

    FILE *fp = fopen(path, "r"); 
    if (!fp) {
        // printf("fail to open: %s\n", path);
        return;
    }
    int64_t time = perf_timer_start();
    // char buf[512];  


    while (fgets(buf, sizeof(buf), fp) != NULL) {
    
        // printf("%s", buf);
    }
    perf_timer_end(time,"timer", "test end");


    fclose(fp);
   
}
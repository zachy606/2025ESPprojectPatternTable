#include "lightdance_reader.h"
#include "player.h"
#include "sdcard.h"

#define TAG "PLAYER_MAIN"
#define MOUNT_POINT "/sdcard"
#define TIME_DATA "times.txt"
#define FRAME_DATA "8data.txt"

static sdmmc_card_t *g_card = NULL;
static player P;

typedef enum {
    STATE_IDLE = 0,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_STOPPED,
    STATE_EXITING
} PlayerState;

static PlayerState State = STATE_IDLE;

void cmd_start(player *p, PlayerState *state){

    if(*state == STATE_IDLE||*state == STATE_STOPPED){
        *state = STATE_RUNNING;
        LightdanceReader_read_frame_at(&p->Reader,p->reader_index,"8data.txt",&p->fd_test[p->reader_index%2]);
        LightdanceReader_read_frame_go_through(&p->Reader,&p->fd_test[(p->reader_index+1)%2]);
        // print_framedata(&fd_test[(reader_index+1)%2]);
        //gptimer
        timer_init(p);
        player_start(p);
    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to start");
    }

}


void cmd_pause(player *p, PlayerState *state){

    if(*state == STATE_RUNNING){

        *state = STATE_PAUSED;

    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to paused");
    }

}


void cmd_resume(player *p, PlayerState *state){
    
    if(*state == STATE_PAUSED){

        *state = STATE_RUNNING;

    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to rseume");
    }
}

void cmd_stop(player *p, PlayerState *state){


    if(*state == STATE_RUNNING){

        *state = STATE_STOPPED;


    }
    else if(*state == STATE_PAUSED){

        *state = STATE_STOPPED;
    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to stop");
    }
}
void cmd_exit(player *p, PlayerState *state){
    
    
    if(*state == STATE_STOPPED){

        *state = STATE_EXITING;


    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to exit");
    }
}

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

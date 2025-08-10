#include "lightdance_reader.h"
#include "player.h"
#include "sdcard.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define TAG "PLAYER_MAIN"
#define MOUNT_POINT "/sdcard"
#define TIME_DATA "200time.txt"
#define FRAME_DATA "200data.txt"

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

void cmd_start(player *p, PlayerState *state, int start_frame_index){

    if(*state == STATE_IDLE||*state == STATE_STOPPED){
        *state = STATE_RUNNING;
        // p->reader_index = start_frame_index;
        player_var_init(p);
        ESP_LOGI("cmd","start init");
        LightdanceReader_read_frame_at(&p->Reader,p->reader_index,"8data.txt",&p->fd_test[p->reader_index%2]);
        LightdanceReader_read_frame_go_through(&p->Reader,&p->fd_test[(p->reader_index+1)%2]);
        
        
        timer_init(p);
        player_start(p);
    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to start");
    }

}


void cmd_pause(player *p, PlayerState *state){

    if(*state == STATE_RUNNING){
        player_pause(p);
        *state = STATE_PAUSED;

    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to paused");
        ESP_LOGI("cmd","now state %d",*state );
    }

}


void cmd_resume(player *p, PlayerState *state){
    
    if(*state == STATE_PAUSED){
        player_resume(p);
        *state = STATE_RUNNING;

    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to resume");
        ESP_LOGI("cmd","now state %d",*state );
    }
}

void cmd_stop(player *p, PlayerState *state){

    if(*state == STATE_RUNNING){

        *state = STATE_STOPPED;
        player_pause(p);
        player_stop(p);

    }
    else if(*state == STATE_PAUSED){
        *state = STATE_STOPPED;
        player_stop(p);
    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to stop");
        ESP_LOGI("cmd","now state %d",*state );
    }
}
void cmd_exit(player *p, PlayerState *state){
    
    
    if(*state == STATE_STOPPED){

        *state = STATE_EXITING;
        fclose(p->Reader.data_fp);
        unmount_sdcard(g_card,MOUNT_POINT);
        ESP_LOGI(TAG, "Main exits.");

    }
    else{
        ESP_LOGI("cmd","wonrg state not allow to exit");
        ESP_LOGI("cmd","now state %d",*state );
    }
}


void command_loop(player *p, PlayerState *state) {
    char line[32];
    ESP_LOGI(TAG, "Enter command: start | pause | resume | stop | exit");

    while (*state != STATE_EXITING) {
        // 注意：idf.py monitor 下，stdin 可直接讀。若你要用 UART，請改成 UART API。
        if (fgets(line, sizeof(line), stdin) == NULL) {
            
            if(p->reader_index >= LightdanceReader_get_total_frames(&p->Reader) && *state != STATE_STOPPED){

                strcpy(line, "stop");  
                
            }else{
                 vTaskDelay(pdMS_TO_TICKS(10));
            
                 continue;
            }

        }
        

        char *cmd = strtok(line, " ");
        char *cmd_frame_index = strtok(NULL, " ");
        int start_frame_index = 0;

        if(p->reader_index >= LightdanceReader_get_total_frames(&p->Reader) ){

            strcpy(cmd, "stop");  
        }

        if (strcmp(cmd, "start") == 0) {
            if (cmd_frame_index != NULL) {
                start_frame_index = atoi(cmd_frame_index);
            }
            cmd_start(p,state,start_frame_index);

        } else if (strcmp(cmd, "pause") == 0) {
            cmd_pause(p,state);
        } else if (strcmp(cmd, "resume") == 0) {
            cmd_resume(p,state);
        } else if (strcmp(cmd, "stop") == 0) {
            cmd_stop(p,state);
        } else if (strcmp(cmd, "exit") == 0) {
            cmd_exit(p,state);
        } else if (cmd[0] != '\0') {
            ESP_LOGW(TAG, "Unknown cmd: %s", line);
        }

    }
}


void app_main(void) {
    // 1) 掛載 SD
    if (!mount_sdcard(g_card,MOUNT_POINT)) {
        ESP_LOGE(TAG, "SD mount failed. Abort.");
        return;
    }

    // 2) 初始化 Reader + 讀檔
    player_reader_init(&P,MOUNT_POINT,TIME_DATA,FRAME_DATA,g_card);


    
    command_loop(&P,&State);

}


/*
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
*/
#include "state_changer.h"
#include "app_config.h"
#include "sdcard.h"          // for un/mount helpers
#include "esp_log.h"

#include <stdio.h>           // fgets, printf
#include <string.h>          // strtok, strcmp, strcspn
#include <stdlib.h>          // atoi

#define TAG "cmd"


void cmd_start(player *p, PlayerState *state, int start_frame_index){

    if(*state == STATE_IDLE||*state == STATE_STOPPED){
        *state = STATE_RUNNING;
        // p->reader_index = start_frame_index;
        player_var_init(p);
        ESP_LOGI(TAG,"start init");
        PatternTable_read_frame_at(&p->Reader,p->reader_index,"8data.txt",&p->fd_test[p->reader_index%2]);
        PatternTable_read_frame_go_through(&p->Reader,&p->fd_test[(p->reader_index+1)%2]);
        
        
        timer_init(p);
        player_start(p);
    }
    else{
        ESP_LOGI(TAG,"wonrg state not allow to start");
    }

}


void cmd_pause(player *p, PlayerState *state){

    if(*state == STATE_RUNNING){
        player_pause(p);
        *state = STATE_PAUSED;

    }
    else{
        ESP_LOGI(TAG,"wonrg state not allow to paused");
        ESP_LOGI(TAG,"now state %d",*state );
    }

}


void cmd_resume(player *p, PlayerState *state){
    
    if(*state == STATE_PAUSED){
        player_resume(p);
        *state = STATE_RUNNING;

    }
    else{
        ESP_LOGI(TAG,"wonrg state not allow to resume");
        ESP_LOGI(TAG,"now state %d",*state );
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
        ESP_LOGI(TAG,"wonrg state not allow to stop");
        ESP_LOGI(TAG,"now state %d",*state );
    }
}
void cmd_exit(player *p, PlayerState *state, const char *mount_point){
    
    
    if(*state == STATE_STOPPED){

        *state = STATE_EXITING;
        fclose(p->Reader.data_fp);
        unmount_sdcard(&p->Reader.card,mount_point);
        ESP_LOGI(TAG, "Main exits.");

    }
    else{
        ESP_LOGI(TAG,"wonrg state not allow to exit");
        ESP_LOGI(TAG,"now state %d",*state );
    }
}


void command_loop(player *p, PlayerState *state,const char *mount_point) {
    char line[CMD_LINE_BUF];
    ESP_LOGI(TAG, "Enter command: start | pause | resume | stop | exit");

    while (*state != STATE_EXITING) {
        // 注意：idf.py monitor 下，stdin 可直接讀。若你要用 UART，請改成 UART API。
        if (fgets(line, sizeof(line), stdin) == NULL) {
            
            if(p->reader_index >= PatternTable_get_total_frames(&p->Reader) && *state != STATE_STOPPED){ // auto stop

                strcpy(line, "stop");  
                
            }else{
                 vTaskDelay(pdMS_TO_TICKS(CMD_IDLE_POLL_DELAY_MS));
            
                 continue;
            }

        }
        

        char *cmd = strtok(line, " ");
        char *cmd_frame_index = strtok(NULL, " ");
        int start_frame_index = 0;

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
            cmd_exit(p,state,mount_point);
        } else if (cmd[0] != '\0') {
            ESP_LOGW(TAG, "Unknown cmd: %s", line);
        }

    }
}

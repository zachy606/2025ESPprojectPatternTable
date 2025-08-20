#include"app_config.h"
#include "pattern_table.h"
#include "player.h"
#include "sdcard.h"
#include "state_changer.h"


#define TAG "PLAYER_MAIN"

static player P;
static PlayerState State = STATE_IDLE;

// command list start pause resume stop exit
//issue 1
//solved 1 2 4 5 6


void app_main(void) {

    player_reader_init(&P,MOUNT_POINT,TIME_DATA,FRAME_DATA);


    
    command_loop(&P,&State,MOUNT_POINT);
    ESP_LOGE(TAG, "code finish");
    
}

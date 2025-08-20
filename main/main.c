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
//solved 1 2 3 4 5 6 7


void app_main(void) {

    

    cmd_init(&P);
    command_loop(&P,&State,MOUNT_POINT);
    ESP_LOGE(TAG, "code finish");
    
}

#include"app_config.h"
#include "pattern_table.h"
#include "player.h"
#include "sdcard.h"
#include "state_changer.h"


#define TAG "PLAYER_MAIN"

static player P;
static PlayerState State = STATE_IDLE;

// command list start pause resume stop exit



void app_main(void) {

    player_reader_init(&P,MOUNT_POINT,TIME_DATA,FRAME_DATA);
    command_loop(&P,&State,MOUNT_POINT);
    
}

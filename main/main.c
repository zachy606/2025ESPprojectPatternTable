#include"app_config.h"
#include "pattern_table.h"
#include "player.h"
#include "sdcard.h"
#include "state_changer.h"


#define TAG "PLAYER_MAIN"

static player P;
static PlayerState State = STATE_IDLE;

// command list  start  pause  resume  stop  exit
// please copy above commend and paste directly into cmd to use
// if commend is splitted, please paste it again

// commad update 
// now can input start time to start at any time(ms)
// ex : start 12500 (need waiting if time is far from 0) 

void app_main(void) {

    player_reader_init(&P);
    command_loop(&P,&State);
    
}

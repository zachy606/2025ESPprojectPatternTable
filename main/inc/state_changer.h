#pragma once


#include "player.h"
#include "sdmmc_cmd.h"

typedef enum {
    STATE_IDLE = 0,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_STOPPED,
    STATE_EXITING
} PlayerState;

void cmd_start(player *p, PlayerState *state, uint32_t start_time);
void cmd_pause(player *p, PlayerState *state);
void cmd_resume(player *p, PlayerState *state);
void cmd_stop(player *p, PlayerState *state);
void cmd_exit(player *p, PlayerState *state);
void command_loop(player *p, PlayerState *state);
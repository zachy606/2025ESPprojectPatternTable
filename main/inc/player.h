#pragma once

#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gptimer.h"     // for gptimer_handle_t
#include "pattern_table.h"      // for PatternTable, FrameData
#include "sdmmc_cmd.h"          // for sdmmc_card_t


typedef struct {

    int cnt ;
    int reader_index;

    bool suspend_detect_playback ;
    bool suspend_detect_refill ;

    uint64_t tick_saved;
    uint64_t period_us;

    TaskHandle_t s_playback_task;
    TaskHandle_t s_refill_task ;
    TaskHandle_t s_timer_alarm_fps_task;
    PatternTable Reader;
    FrameData fd_test[2];
    gptimer_handle_t gptimer;

} player;

void player_reader_init(player *p);
void player_var_init(player *p);
void timer_init(player *p);


void timer_alarm_fps_task(void *arg);

void refill_task(void *arg) ;
void playback_task(void *arg);

void player_start(player *p);
void player_resume(player *p);
void player_pause(player *p);
void player_stop(player *p);
void gptimer_seek_to_ms(player *p, uint32_t t_ms);
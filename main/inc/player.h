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

    TaskHandle_t s_playback_task;
    TaskHandle_t s_refill_task ;

    PatternTable Reader;
    FrameData fd_test[2];
    gptimer_handle_t gptimer;

} player;

void player_reader_init(player *p, const char *mount_point,const char *time_data, const char *frame_data, sdmmc_card_t **card );
void player_var_init(player *p);
void timer_init(player *p);


bool IRAM_ATTR example_timer_on_alarm_cb_v1(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *self);

void refill_task(void *arg) ;
void playback_task(void *arg);

void player_start(player *p);
void player_resume(player *p);
void player_pause(player *p);
void player_stop(player *p);

#pragma once

#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lightdance_reader.h"
#include <inttypes.h>
#include "sdmmc_cmd.h"


typedef struct {

    int cnt ;
    int fps ;
    int reader_index;

    bool suspend_detect_playback ;
    bool suspend_detect_refill ;

    TaskHandle_t s_playback_task;
    TaskHandle_t s_refill_task ;

    LightdanceReader Reader;
    FrameData fd_test[2];
    gptimer_handle_t gptimer;

} player;

void player_init(player *self, const char *mount_point,const char *time_data, const char *frame_data, sdmmc_card_t *_card );

void timer_init(player *self);


bool IRAM_ATTR example_timer_on_alarm_cb_v1(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *self);

void refill_task(void *arg) ;
void playback_task(void *arg);

void player_start(player *self);

void player_stop(player *self);

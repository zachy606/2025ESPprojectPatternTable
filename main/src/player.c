#include "app_config.h"
#include "player.h"
#include "sdcard.h"
#include <stdio.h>                 // for fflush()



void player_reader_init(player *p){
    p-> cnt = 0;
    p->  reader_index = 0;
    p->  tick_saved = 0;
    


    p->  suspend_detect_playback = false;
    p->  suspend_detect_refill = false;

    p-> s_playback_task = NULL;
    p-> s_refill_task = NULL;

    p-> gptimer = NULL;

    // 1) 掛載 SD
    if (!mount_sdcard(&p->Reader.card)) {
        ESP_LOGE("SD", "SD mount failed. Abort.");
        return;
    }

    PatternTable_init(&p->Reader);


    if (!PatternTable_load_times(&p->Reader)) {
        ESP_LOGE("Player init", "Failed to load times.txt");
        unmount_sdcard(&p->Reader.card);
        return;
    }
    if (!PatternTable_index_frames(&p->Reader)) {
        ESP_LOGE("Player init", "Failed to index data.txt");
        unmount_sdcard(&p->Reader.card);
        return;
    }

    ESP_LOGE("Player init", "FPS %d",p->Reader.fps);
    if(p->Reader.fps==0) p->Reader.fps = DEFAULT_FPS;
    p->period_us = TIMER_RESOLUTION_HZ / p->Reader.fps ;
    ESP_LOGE("Player init", "FPS %d",p->Reader.fps);


    ESP_LOGE("Player init", "Total frames=%d, total_leds=%d, fps=%d",
             PatternTable_get_total_frames(&p->Reader),
             PatternTable_get_total_leds(&p->Reader),
             p->Reader.fps);
    fflush(stdout);


}


void player_var_init(player *p){
    
    p->  cnt = 0;
    p->  tick_saved = 0;
    p->  reader_index = 0;


    p->  suspend_detect_playback = false;
    p->  suspend_detect_refill = false;

    p-> s_playback_task = NULL;
    p-> s_refill_task = NULL;

    p-> gptimer = NULL;

}

void timer_alarm_fps_task(void *arg){

    player *p = (player *)arg;
    uint64_t tick_now = 0;

    while(1){
        
        ESP_ERROR_CHECK(gptimer_get_raw_count(p->gptimer, &tick_now));
        if( tick_now / p->period_us > p->cnt){
            p->cnt++;
        }

        if(p->suspend_detect_playback){
        p->suspend_detect_playback = false;
            xTaskResumeFromISR(p->s_playback_task );
        }
        
        if ((p->cnt+1) *(1000/p->Reader.fps) >= p->Reader.frame_times[p->reader_index] ){
            if(p->suspend_detect_refill){
                
                xTaskResumeFromISR(p->s_refill_task );
                p->suspend_detect_refill = false;
            }
            // ESP_LOGE("alarm","refill");
            
        }
        vTaskDelay(1); 
    }

}


void refill_task(void *arg) {

    player *p = (player *)arg;    

    p->suspend_detect_refill = true;
    vTaskSuspend(NULL);
    while(1){

        // print_framedata(&fd_test ,&g_reader);
        ESP_LOGE("refill","change frame");
        ESP_LOGI("IRAM", "%" PRIu32 "",p->Reader.frame_times[p->reader_index]);
        p->reader_index++;
        if (p->reader_index+1 < PatternTable_get_total_frames(&p->Reader)){
            ESP_LOGE("refill","refill");
            
            PatternTable_read_frame_go_through(&p->Reader,&p->fd_test[(p->reader_index-1)%2]);
  
        }
        ESP_LOGE("refill","READ finish");
        p->suspend_detect_refill = true;
        vTaskSuspend(NULL);
    }

}


void playback_task(void *arg) {

    player *p = (player *)arg;
    
    while(1){
        // ESP_LOGE("playback","refresh");
        // print_framedata(&fd_test[reader_index%2]);
        p->suspend_detect_playback = true;
        vTaskSuspend(NULL);
    }

}






void timer_init(player *p){
    
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // 选择默认的时钟源
        .direction = GPTIMER_COUNT_UP,      // 计数方向为向上计数
        .resolution_hz = TIMER_RESOLUTION_HZ,   // 分辨率为 1 MHz，即 1 次滴答为 1 微秒
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &p->gptimer));

    ESP_LOGI("TIMER", "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(p->gptimer));

    ESP_LOGI("TIMER", "Start timer, auto-reload at alarm event");
}


void player_start(player *p){

    xTaskCreate(timer_alarm_fps_task, "timer_alarm_fps_task", 4096, p, 4, &p->s_timer_alarm_fps_task );
    ESP_ERROR_CHECK(gptimer_start(p->gptimer));
    ESP_LOGI("PLAYER", "Start");
    xTaskCreate(playback_task, "playback_task", 4096, p, 5, &p->s_playback_task );
    xTaskCreate(refill_task, "refill_task", 4096, p, 5, &p->s_refill_task );
}

void player_pause(player *p){

    vTaskSuspend(p->s_timer_alarm_fps_task);
    ESP_ERROR_CHECK(gptimer_stop(p->gptimer));
    ESP_ERROR_CHECK(gptimer_disable(p->gptimer));
    
    ESP_LOGI("PLAYER", "pause");
}

void player_resume(player *p){
    ESP_ERROR_CHECK(gptimer_enable(p->gptimer));
    ESP_ERROR_CHECK(gptimer_start(p->gptimer));
    vTaskResume(p->s_timer_alarm_fps_task);
    ESP_LOGI("PLAYER", "resume");
}


void player_stop(player *p){


    vTaskDelete(p->s_timer_alarm_fps_task);
    ESP_LOGI("PLAYER", "stop");
    ESP_ERROR_CHECK(gptimer_del_timer(p->gptimer));
    ESP_LOGI("TIMER", "delete");
    ESP_LOGI("FILE", "delete");
    vTaskDelete(p->s_playback_task);
    vTaskDelete(p->s_refill_task);
    ESP_LOGI("TASK", "delete");
}
#include "app_config.h"
#include "player.h"
#include "sdcard.h"
#include <stdio.h>                 // for fflush()



void player_reader_init(player *p, const char *mount_point,const char *time_data, const char *frame_data ){
    p-> cnt = 0;
    p->  reader_index = 0;


    p->  suspend_detect_playback = false;
    p->  suspend_detect_refill = false;

    p-> s_playback_task = NULL;
    p-> s_refill_task = NULL;

    p-> gptimer = NULL;

    // 1) 掛載 SD
    if (!mount_sdcard(&p->Reader.card,mount_point)) {
        ESP_LOGE("SD", "SD mount failed. Abort.");
        return;
    }

    PatternTable_init(&p->Reader, mount_point);


        if (!PatternTable_load_times(&p->Reader)) {
        ESP_LOGE("Player init", "Failed to load times.txt");
        unmount_sdcard(&p->Reader.card,mount_point);
        return;
    }
    if (!PatternTable_index_frames(&p->Reader)) {
        ESP_LOGE("Player init", "Failed to index data.txt");
        unmount_sdcard(&p->Reader.card,mount_point);
        return;
    }

    ESP_LOGE("Player init", "FPS %d",p->Reader.fps);
    if(p->Reader.fps==0) p->Reader.fps = DEFAULT_FPS;

    ESP_LOGE("Player init", "FPS %d",p->Reader.fps);


    ESP_LOGE("Player init", "Total frames=%d, total_leds=%d, fps=%d",
             PatternTable_get_total_frames(&p->Reader),
             PatternTable_get_total_leds(&p->Reader),
             p->Reader.fps);
    fflush(stdout);


}


void player_var_init(player *p){
    
    p->  cnt = 0;

    p->  reader_index = 0;


    p->  suspend_detect_playback = false;
    p->  suspend_detect_refill = false;

    p-> s_playback_task = NULL;
    p-> s_refill_task = NULL;

    p-> gptimer = NULL;

}


bool IRAM_ATTR example_timer_on_alarm_cb_v1(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *self)
{
    player *p = (player *)self;    
    p->cnt++;
    // print_framedata(&fd_test[reader_index%2] ,&g_reader);
    if(p->suspend_detect_refresh){
        p->suspend_detect_refresh = false;
        xTaskResumeFromISR(p->s_refresh_task );
        
        return true;
    }
    
    // if ((p->cnt+1) *(1000/p->Reader.fps) >= p->Reader.frame_times[p->reader_index] ){
    //     if(p->suspend_detect_refill){
            
    //         xTaskResumeFromISR(p->s_refill_task );
    //         p->suspend_detect_refill = false;
    //     }
        
    //     return false;
    // }
    ESP_LOGD("timer","failed to alarm");
    return false;
}


void refresh_task(void *arg){
    player *p = (player *)arg;    
    p->suspend_detect_refresh = true;
    vTaskSuspend(NULL);
    while(1){
        
        /*
        led light
        */


        if ((p->cnt+1) *(1000/p->Reader.fps) >= p->Reader.frame_times[p->reader_index] ){
            // print_framedata(&fd_test ,&g_reader);
            ESP_LOGE("refill","change frame");
            ESP_LOGI("IRAM", "%" PRIu32 "",p->Reader.frame_times[p->reader_index]);
            p->reader_index++;
            if (p->reader_index+1 < PatternTable_get_total_frames(&p->Reader)){
                ESP_LOGE("refill","refill");
                
                PatternTable_read_frame_go_through(&p->Reader,&p->fd_test[(p->reader_index-1)%2]);
    
            }
            ESP_LOGE("refill","READ finish");
            p->suspend_detect_refresh = true;
            vTaskSuspend(NULL);
        }
        
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
        
        vTaskSuspend(NULL);
    }
    p->suspend_detect_refill = true;
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

    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb_v1,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(p->gptimer, &cbs,p));
    ESP_LOGI("TIMER", "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(p->gptimer));

    
    gptimer_alarm_config_t alarm_config2 = {
        .reload_count = 0,
        .alarm_count = TIMER_RESOLUTION_HZ/p->Reader.fps, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(p->gptimer, &alarm_config2));
    ESP_LOGI("TIMER", "Start timer, auto-reload at alarm event");
}


void player_start(player *p){
    ESP_ERROR_CHECK(gptimer_start(p->gptimer));
    ESP_LOGI("PLAYER", "Start");
    // xTaskCreate(playback_task, "playback_task", 4096, p, 5, &p->s_playback_task );
    // xTaskCreate(refill_task, "refill_task", 4096, p, 5, &p->s_refill_task );
    xTaskCreate(refresh_task, "refresh_task", 4096, p, 5, &p->s_refresh_task );
}

void player_pause(player *p){
    ESP_ERROR_CHECK(gptimer_stop(p->gptimer));
    ESP_ERROR_CHECK(gptimer_disable(p->gptimer));
  
    ESP_LOGI("PLAYER", "pause");
}

void player_resume(player *p){
    ESP_ERROR_CHECK(gptimer_enable(p->gptimer));
    ESP_ERROR_CHECK(gptimer_start(p->gptimer));
    ESP_LOGI("PLAYER", "resume");
}


void player_stop(player *p){

    ESP_LOGI("PLAYER", "stop");
    ESP_ERROR_CHECK(gptimer_del_timer(p->gptimer));
    ESP_LOGI("TIMER", "delete");
    ESP_LOGI("FILE", "delete");
    vTaskDelete(p->s_playback_task);
    vTaskDelete(p->s_refill_task);
    ESP_LOGI("TASK", "delete");
}

void gptimer_seek_to_ms(player *p, uint32_t t_ms)
{
    if (!p->gptimer){
        ESP_LOGE("TIMER", "timer not init");
        return ;
    } 

    // 將 ms 轉為 ticks：ticks = t_ms * (Hz / 1000)
    // 用 64-bit 避免乘法溢位
    uint64_t ticks = ((uint64_t)t_ms * TIMER_RESOLUTION_HZ) / 1000ULL;

    // 最穩流程：先停 -> 設定 -> 再啟動
    // （若已停止，stop 仍會回傳 OK）
  

    ESP_ERROR_CHECK(gptimer_set_raw_count(p->gptimer, ticks));
    ESP_LOGI("TIMER","timer set ok");
}
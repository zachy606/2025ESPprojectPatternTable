#include "player.h"


void player_reader_init(player *p, const char *mount_point,const char *time_data, const char *frame_data, sdmmc_card_t **card ){
    p-> cnt = 0;
    p->  fps = 40;
    p->  reader_index = 0;


    p->  suspend_detect_playback = false;
    p->  suspend_detect_refill = false;

    p-> s_playback_task = NULL;
    p-> s_refill_task = NULL;

    p-> gptimer = NULL;
    LightdanceReader_init(&p->Reader, mount_point);

    if (!LightdanceReader_load_times(&p->Reader, time_data)) {
        ESP_LOGE("Player init", "Failed to load times.txt");
        unmount_sdcard(card,mount_point);
        return;
    }
    if (!LightdanceReader_index_frames(&p->Reader, frame_data)) {
        ESP_LOGE("Player init", "Failed to index data.txt");
        unmount_sdcard(card,mount_point);
        return;
    }

    ESP_LOGE("Player init", "Total frames=%d, total_leds=%d, fps=%d",
             LightdanceReader_get_total_frames(&p->Reader),
             LightdanceReader_get_total_leds(&p->Reader),
             p->Reader.fps);
    fflush(stdout);


}


void player_var_init(player *p){
    p->  cnt = 0;
    p->  fps = 40;
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
    if(p->suspend_detect_playback){
        p->suspend_detect_playback = false;
        xTaskResumeFromISR(p->s_playback_task );
    }
    
    if ((p->cnt+1) *(1000/p->fps) >= p->Reader.frame_times[p->reader_index] ){
        if(p->suspend_detect_refill){
            
            xTaskResumeFromISR(p->s_refill_task );
            p->suspend_detect_refill = false;
        }
        
        return false;
    }
    
    return true;
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
        if (p->reader_index+1 < LightdanceReader_get_total_frames(&p->Reader)){
            ESP_LOGE("refill","refill");
            
            LightdanceReader_read_frame_go_through(&p->Reader,&p->fd_test[(p->reader_index-1)%2]);
  
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
        .resolution_hz = 1 * 1000 * 1000,   // 分辨率为 1 MHz，即 1 次滴答为 1 微秒
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &p->gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb_v1,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(p->gptimer, &cbs,p));
    ESP_LOGI("TIMER", "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(p->gptimer));

    ESP_LOGI("TIMER", "Start timer, auto-reload at alarm event");
    gptimer_alarm_config_t alarm_config2 = {
        .reload_count = 0,
        .alarm_count = 1000000/p->fps, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(p->gptimer, &alarm_config2));
}


void player_start(player *p){
    ESP_ERROR_CHECK(gptimer_start(p->gptimer));
    ESP_LOGI("PLAYER", "Start");
    xTaskCreate(playback_task, "playback_task", 4096, p, 5, &p->s_playback_task );
    xTaskCreate(refill_task, "refill_task", 4096, p, 5, &p->s_refill_task );
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
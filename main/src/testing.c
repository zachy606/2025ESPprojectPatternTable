#include "testing.h"
#include "esp_timer.h"
#include "esp_log.h"

int64_t perf_timer_start(void) {
    return esp_timer_get_time(); // 單位：微秒
}

void perf_timer_end(int64_t start_time, const char *tag, const char *msg) {
    int64_t end_time = esp_timer_get_time();
    int64_t elapsed = end_time - start_time;
    ESP_LOGI(tag, "%s took %lld us", msg, (long long)elapsed);
}

bool perf_timer_cnt(int64_t start_time,int64_t end_time, const char *tag, const char *msg){
    int64_t tmp_time = esp_timer_get_time();
    if( tmp_time - start_time >= end_time){
        ESP_LOGD(tag, "%s count %lld us", msg,end_time);
        return true;
    }else{
        return false;
    }
}   
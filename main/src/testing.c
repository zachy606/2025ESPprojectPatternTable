#include "testing.h"
#include "esp_timer.h"
#include "esp_log.h"

int64_t perf_timer_start(void) {
    return esp_timer_get_time(); // 單位：微秒
}

void perf_timer_end(int64_t start_time, const char *tag, const char *msg) {
    int64_t end_time = esp_timer_get_time();
    int64_t elapsed = end_time - start_time;
    ESP_LOGW(tag, "%s took %lld us", msg, (long long)elapsed);
}
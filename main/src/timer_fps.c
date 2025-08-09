#include "timer_fps.h"
#include "esp_check.h"


static bool IRAM_ATTR _isr_cb(gptimer_handle_t t, const gptimer_alarm_event_data_t *e, void *ctx) {
    // (void)t; (void)e;
    GpTimerSem *obj = (GpTimerSem *)ctx;
    if (!obj || !obj->sem) return false;
    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(obj->sem, &hpw);
    return hpw == pdTRUE;
}

esp_err_t gptimer_sem_init(GpTimerSem *obj, uint32_t resolution_hz, uint32_t fps, SemaphoreHandle_t sem) {
    if (!obj || !sem || fps == 0) return ESP_ERR_INVALID_ARG;
    obj->resolution_hz = resolution_hz ? resolution_hz : 1000000;
    obj->fps = fps;
    obj->sem = sem;

    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = obj->resolution_hz,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&cfg, &obj->handle), "GPTSEM", "new_timer");
    gptimer_event_callbacks_t cbs = {.on_alarm = _isr_cb};
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(obj->handle, &cbs, obj), "GPTSEM", "reg_cb");
    return gptimer_sem_set_fps(obj, obj->fps);
}

esp_err_t gptimer_sem_set_fps(GpTimerSem *obj, uint32_t fps) {
    if (!obj || fps == 0) return ESP_ERR_INVALID_ARG;
    obj->fps = fps;
    gptimer_alarm_config_t alarm = {
        .reload_count = 0,
        .alarm_count  = obj->resolution_hz / obj->fps, // 例：1e6/60 ≈ 16666 tick
        .flags.auto_reload_on_alarm = true,
    };
    return gptimer_set_alarm_action(obj->handle, &alarm);
}

esp_err_t gptimer_sem_start(GpTimerSem *obj) {
    if (!obj) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(gptimer_enable(obj->handle), "GPTSEM", "enable");
    return gptimer_start(obj->handle);
}

esp_err_t gptimer_sem_pause(GpTimerSem *obj) {
    if (!obj) return ESP_ERR_INVALID_ARG;
    return gptimer_stop(obj->handle); // 不 disable
}

esp_err_t gptimer_sem_resume(GpTimerSem *obj) {
    if (!obj) return ESP_ERR_INVALID_ARG;
    return gptimer_start(obj->handle); // 從暫停點繼續
}

esp_err_t gptimer_sem_stop(GpTimerSem *obj) {
    if (!obj) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(gptimer_stop(obj->handle), "GPTSEM", "stop");
    return gptimer_disable(obj->handle);
}
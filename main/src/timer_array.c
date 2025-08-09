#include "timer_array.h"
#include "esp_check.h"

#define TAG "timer_array"


static inline uint64_t _ms_to_ticks(const GpTimerSeqSem *o, uint32_t ms) {
    return ((uint64_t)o->resolution_hz * (uint64_t)ms) / 1000ULL;
}

static bool IRAM_ATTR _isr_cb(gptimer_handle_t t,
                              const gptimer_alarm_event_data_t *e,
                              void *ctx)
{
    // (void)t;
    GpTimerSeqSem *obj = (GpTimerSeqSem *)ctx;
    if (!obj || !obj->sem) return false;

    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(obj->sem, &hpw);

    // 設下一個間隔
    obj->index++;
    if (obj->index < obj->count) {
        uint64_t base = e->alarm_value; // 若版本無此欄位，改用 e->count_value
        uint64_t next_alarm_abs_ticks = _ms_to_ticks(obj, obj->intervals_ms[obj->index]);

        gptimer_alarm_config_t next = {
            .reload_count = 0,
            .alarm_count  = next_alarm_abs_ticks,
            .flags.auto_reload_on_alarm = false,
        };
        gptimer_set_alarm_action(obj->handle, &next);
    }
    return hpw == pdTRUE;
}

esp_err_t gptimer_seq_sem_init(GpTimerSeqSem *obj,
                               uint32_t resolution_hz,
                               const uint32_t *intervals_ms, uint32_t count,
                               SemaphoreHandle_t sem)
{
    if (!obj || !sem || !intervals_ms || count == 0) return ESP_ERR_INVALID_ARG;
    obj->resolution_hz = resolution_hz ? resolution_hz : 1000000;
    obj->intervals_ms  = intervals_ms;
    obj->count         = count;
    obj->index         = 0;
    obj->sem           = sem;

    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = obj->resolution_hz,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&cfg, &obj->handle), "GPTSEQ", "new_timer failed");

    gptimer_event_callbacks_t cbs = { .on_alarm = _isr_cb };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(obj->handle, &cbs, obj), "GPTSEQ", "reg_cb failed");

    return ESP_OK;
}

esp_err_t gptimer_seq_sem_set_intervals(GpTimerSeqSem *obj,
                                        const uint32_t *intervals_ms, uint32_t count)
{
    if (!obj || !intervals_ms || count == 0) return ESP_ERR_INVALID_ARG;
    obj->intervals_ms = intervals_ms;
    obj->count = count;
    obj->index = 0;
    return ESP_OK;
}

esp_err_t gptimer_seq_sem_start(GpTimerSeqSem *obj)
{
    ESP_LOGE(TAG, "index %"PRIu32", count %"PRIu32"",obj->index , obj->count );
    if (!obj || obj->count == 0 || obj->index >= obj->count) return ESP_ERR_INVALID_ARG;
    ESP_LOGE(TAG, "start1");
    uint64_t now = 0;
    gptimer_get_raw_count(obj->handle, &now);
    uint64_t delta = _ms_to_ticks(obj, obj->intervals_ms[obj->index]);

    gptimer_alarm_config_t first = {
        .reload_count = 0,
        .alarm_count  = now + delta,
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(obj->handle, &first);
    gptimer_enable(obj->handle);
    return gptimer_start(obj->handle);
}

esp_err_t gptimer_seq_sem_pause(GpTimerSeqSem *obj)
{
    if (!obj) return ESP_ERR_INVALID_ARG;
    return gptimer_stop(obj->handle);
}

esp_err_t gptimer_seq_sem_resume(GpTimerSeqSem *obj)
{
    if (!obj) return ESP_ERR_INVALID_ARG;
    return gptimer_start(obj->handle);
}

esp_err_t gptimer_seq_sem_stop(GpTimerSeqSem *obj)
{
    if (!obj) return ESP_ERR_INVALID_ARG;
    gptimer_stop(obj->handle);
    gptimer_seq_sem_reset(obj);
    return gptimer_disable(obj->handle);
}
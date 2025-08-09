#pragma once
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    gptimer_handle_t handle;
    uint32_t resolution_hz;          
    const uint32_t *intervals_ms;    // 每段間隔(ms)
    uint32_t count;                    // 陣列長度
    uint32_t index;                    // 下一段 index
    SemaphoreHandle_t sem;           // 觸發就 give
} GpTimerSeqSem;

esp_err_t gptimer_seq_sem_init(GpTimerSeqSem *obj,
                               uint32_t resolution_hz,
                               const uint32_t *intervals_ms, uint32_t count,
                               SemaphoreHandle_t sem);

esp_err_t gptimer_seq_sem_set_intervals(GpTimerSeqSem *obj,
                                        const uint32_t *intervals_ms, uint32_t count);

esp_err_t gptimer_seq_sem_start(GpTimerSeqSem *obj);
esp_err_t gptimer_seq_sem_pause(GpTimerSeqSem *obj);
esp_err_t gptimer_seq_sem_resume(GpTimerSeqSem *obj);
esp_err_t gptimer_seq_sem_stop(GpTimerSeqSem *obj);

static inline void gptimer_seq_sem_reset(GpTimerSeqSem *obj) { if (obj) obj->index = 0; }
static inline esp_err_t gptimer_seq_sem_trigger_now(GpTimerSeqSem *obj) {
    if (!obj || !obj->sem) return ESP_ERR_INVALID_ARG;
    xSemaphoreGive(obj->sem);
    return ESP_OK;
}
static inline bool gptimer_seq_sem_finished(const GpTimerSeqSem *obj) {
    return (!obj || obj->index >= obj->count);
}

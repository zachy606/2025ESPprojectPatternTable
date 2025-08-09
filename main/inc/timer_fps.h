
// ===== timer_driver.h =====
#pragma once
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <stdint.h>

//fps
typedef struct {
    gptimer_handle_t handle;
    uint32_t resolution_hz;   // 例如 1_000_000 → 1 tick = 1us
    uint32_t fps;             // 例：60
    SemaphoreHandle_t sem;    // 觸發就 give
} GpTimerSem;

/* 初始化（Semaphore 版） */
esp_err_t gptimer_sem_init(GpTimerSem *obj, uint32_t resolution_hz, uint32_t fps, SemaphoreHandle_t sem);
/* 改 FPS（動態） */
esp_err_t gptimer_sem_set_fps(GpTimerSem *obj, uint32_t fps);
/* 啟停 */
esp_err_t gptimer_sem_start(GpTimerSem *obj);
esp_err_t gptimer_sem_resume(GpTimerSem *obj);
esp_err_t gptimer_sem_pause(GpTimerSem *obj);
esp_err_t gptimer_sem_stop(GpTimerSem *obj);
/* 立刻給一次（不等下一個 alarm） */
static inline esp_err_t gptimer_sem_trigger_now(GpTimerSem *obj) {
    if (!obj || !obj->sem) return ESP_ERR_INVALID_ARG;
    xSemaphoreGive(obj->sem);
    return ESP_OK;
}

// 
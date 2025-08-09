#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"

#include "lightdance_reader.h"
#include "frame_buffer_player.h"
#include "timer_array.h"





#define TAG "PLAYER_MAIN"
#define MOUNT_POINT "/sdcard"

// ==== SDSPI PIN 定義（依你實機腳位調整）====
#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  15
#define PIN_NUM_CLK   14
#define PIN_NUM_CS    13
#define SPI_DMA_CHAN   1

typedef enum {
    STATE_IDLE = 0,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_STOPPED,
    STATE_EXITING
} PlayerState;




// ==== 全域物件 ====
static LightdanceReader g_reader;
static FrameBufferPlayer g_player;
static GpTimerSeqSem g_timer;

// 播放狀態
static PlayerState g_state = STATE_IDLE;

// 用於 on_alarm 回呼 -> 通知播放任務
static SemaphoreHandle_t g_play_sem;

// ======== 你的 LED 輸出（替換成實機驅動 WS2812 的函式）========
// static void output_frame_to_leds(const FrameData *fd) {
//     // TODO: 這裡塞你的 WS2812 更新程式
//     // 例如：send to RMT / SPI / I2S
//     ESP_LOGI(TAG, "Output frame (fade=%d), LED0=(%d,%d,%d,%d)",
//              fd->fade,
//              fd->colors[0][0], fd->colors[0][1],
//              fd->colors[0][2], fd->colors[0][3]);
// }

// // ArrayTimer 的 on_alarm 回呼：一到時間就喚醒播放任務
// static void on_timestamp_alarm(void *arg) {
//     BaseType_t hpw = pdFALSE;
//     xSemaphoreGiveFromISR(g_play_sem, &hpw);
//     if (hpw) portYIELD_FROM_ISR();
// }

// ======== 播放任務：被喚醒即播放下一幀，然後補幀 ========
static void playback_task(void *arg) {
    while (g_state != STATE_EXITING) {
        if (xSemaphoreTake(g_play_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (g_state == STATE_RUNNING) {
                FrameData fd;
                if (FrameBufferPlayer_play(&g_player, &fd)) {
                    // output_frame_to_leds(&fd);
                    // 播完補一幀，維持雙緩衝
                    FrameBufferPlayer_fill(&g_player);
                } else {
                    // 播放完
                    ESP_LOGI(TAG, "All frames played.");
                    g_state = STATE_STOPPED;
                }
            }
        }
    }
    vTaskDelete(NULL);
}

// ======== 控制函式 ========
static void cmd_start(void) {
    ESP_LOGW(TAG, "start");
    if (!(g_state == STATE_IDLE || g_state == STATE_STOPPED)) {
        ESP_LOGW(TAG, "Cannot start unless in STOPPED/IDLE state. Current=%d", g_state);
        return;
    }

    ESP_LOGW(TAG, "%d",LightdanceReader_get_total_frames(&g_reader));
    FrameBufferPlayer_init(&g_player, &g_reader);
    FrameBufferPlayer_fill(&g_player);
    FrameBufferPlayer_fill(&g_player);

    ESP_LOGW(TAG, "%d",LightdanceReader_get_total_frames(&g_reader));

    gptimer_seq_sem_init(&g_timer,0,
        LightdanceReader_get_time_array(&g_reader),
        LightdanceReader_get_total_frames(&g_reader),
        g_play_sem);

    g_state = STATE_RUNNING;
    
    ESP_ERROR_CHECK(gptimer_seq_sem_start(&g_timer));

    ESP_LOGI(TAG, "START: begin playback.");
}

static void cmd_pause(void) {
    if (g_state != STATE_RUNNING) {
        ESP_LOGW(TAG, "Cannot pause unless running. Current=%d", g_state);
        return;
    }
    
    ESP_ERROR_CHECK(gptimer_seq_sem_pause(&g_timer));
    g_state = STATE_PAUSED;
    ESP_LOGI(TAG, "PAUSE.");
}

static void cmd_resume(void) {
    if (g_state != STATE_PAUSED) {
        ESP_LOGW(TAG, "Cannot resume unless paused. Current=%d", g_state);
        return;
    }
    g_state = STATE_RUNNING;
    ESP_ERROR_CHECK(gptimer_seq_sem_resume(&g_timer));
    ESP_LOGI(TAG, "RESUME.");
}

static void cmd_stop(void) {
    if (!(g_state == STATE_RUNNING || g_state == STATE_PAUSED)) {
        ESP_LOGW(TAG, "Cannot stop unless running or paused. Current=%d", g_state);
        return;
    }
    ESP_ERROR_CHECK(gptimer_seq_sem_stop(&g_timer));
    g_state = STATE_STOPPED;
    ESP_LOGI(TAG, "STOP: playback stopped.");
}

static void cmd_exit(void) {
    if (!(g_state == STATE_STOPPED || g_state == STATE_IDLE)) {
        ESP_LOGW(TAG, "Cannot exit unless stopped. Current=%d", g_state);
        return;
    }
    g_state = STATE_EXITING;

    ESP_LOGI(TAG, "EXIT requested.");
}

// ======== SDSPI 掛載 ========

static sdmmc_card_t *g_card = NULL;

static bool mount_sdcard(void) {
    esp_err_t ret;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CHAN));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &g_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return false;
    }

    sdmmc_card_print_info(stdout, g_card);
    return true;
}

static void unmount_sdcard(void) {
    if (g_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, g_card);
        g_card = NULL;
    }
    spi_bus_free(SPI2_HOST);
}
// ======== 指令讀取（簡化：從 stdin）========
// 實機上可改用 UART 驅動 or esp_consolestatic 
static void command_loop(void) {
    char line[32];
    ESP_LOGI(TAG, "Enter command: start | pause | resume | stop | exit");

    while (g_state != STATE_EXITING) {
        // 注意：idf.py monitor 下，stdin 可直接讀。若你要用 UART，請改成 UART API。
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 去除換行
        line[strcspn(line, "\r\n")] = 0;

        if (strcmp(line, "start") == 0) {
            cmd_start();
        } else if (strcmp(line, "pause") == 0) {
            cmd_pause();
        } else if (strcmp(line, "resume") == 0) {
            cmd_resume();
        } else if (strcmp(line, "stop") == 0) {
            cmd_stop();
        } else if (strcmp(line, "exit") == 0) {
            cmd_exit();
        } else if (line[0] != '\0') {
            ESP_LOGW(TAG, "Unknown cmd: %s", line);
        }
    }
}

// ================== app_main ==================
void app_main(void) {
    // 1) 掛載 SD
    if (!mount_sdcard()) {
        ESP_LOGE(TAG, "SD mount failed. Abort.");
        return;
    }

    // 2) 初始化 Reader + 讀檔
    LightdanceReader_init(&g_reader, MOUNT_POINT);

    if (!LightdanceReader_load_times(&g_reader, "times.txt")) {
        ESP_LOGE(TAG, "Failed to load times.txt");
        unmount_sdcard();
        return;
    }
    if (!LightdanceReader_index_frames(&g_reader, "data.txt")) {
        ESP_LOGE(TAG, "Failed to index data.txt");
        unmount_sdcard();
        return;
    }

    ESP_LOGE(TAG, "Total frames=%d, total_leds=%d, fps=%d",
             LightdanceReader_get_total_frames(&g_reader),
             LightdanceReader_get_total_leds(&g_reader),
             g_reader.fps);
    fflush(stdout);

    // // 3) 建立播放同步 semaphore 與播放任務
    g_play_sem = xSemaphoreCreateBinary();
    xTaskCreate(playback_task, "playback_task", 4096, NULL, 5, NULL);

    // // 4) 進入命令迴圈
    command_loop();

    // 5) 收尾
    vSemaphoreDelete(g_play_sem);
    unmount_sdcard();
    ESP_LOGI(TAG, "Main exits.");
}
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "driver/gptimer.h"
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

static LightdanceReader g_reader;

int cnt = 0;

static bool IRAM_ATTR example_timer_on_alarm_cb_v1(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    cnt++;
    ESP_LOGI("IRAMATTR", "cnt %d",cnt);
    return false;
}


// static void playback_task(void *arg) {
//     while (g_state != STATE_EXITING) {
//         if (xSemaphoreTake(g_play_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
//             if (g_state == STATE_RUNNING) {
//                 FrameData fd;
//                 if (FrameBufferPlayer_play(&g_player, &fd)) {
//                     // output_frame_to_leds(&fd);
//                     // 播完補一幀，維持雙緩衝
//                     FrameBufferPlayer_fill(&g_player);
//                 } else {
//                     // 播放完
//                     ESP_LOGI(TAG, "All frames played.");
//                     g_state = STATE_STOPPED;
//                 }
//             }
//         }
//     }
//     vTaskDelete(NULL);
// }




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


    //gptimer
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // 选择默认的时钟源
        .direction = GPTIMER_COUNT_UP,      // 计数方向为向上计数
        .resolution_hz = 1 * 1000 * 1000,   // 分辨率为 1 MHz，即 1 次滴答为 1 微秒
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb_v1,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs,NULL));
    ESP_LOGI("TIMER", "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, auto-reload at alarm event");
    gptimer_alarm_config_t alarm_config2 = {
        .reload_count = 0,
        .alarm_count = 1000000, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config2));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    ESP_LOGI(TAG, "Start");
    
    vTaskDelay(pdMS_TO_TICKS(4000));
    
    

    ESP_ERROR_CHECK(gptimer_stop(gptimer));
    ESP_LOGI("TIMER", "stop");
    ESP_ERROR_CHECK(gptimer_disable(gptimer));
    ESP_LOGI("TIMER", "disable");
    ESP_ERROR_CHECK(gptimer_del_timer(gptimer));
    ESP_LOGI("TIMER", "delete");


    unmount_sdcard();
    ESP_LOGI(TAG, "Main exits.");
}
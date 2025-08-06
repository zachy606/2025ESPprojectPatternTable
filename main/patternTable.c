#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>

#define TAG1 "sdspi"
#define TAG2 "FrameReader"
#define TAG3 "Player"
#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO  19
#define PIN_NUM_MOSI  23
#define PIN_NUM_CLK   18
#define PIN_NUM_CS    5
#define SPI_DMA_CHAN  1

typedef struct FileHeader{
    uint32_t num_frames;
    uint16_t num_leds;
    uint16_t reserved;
} FileHeader;

typedef struct LEDUpdate {
    uint16_t led_index;
    uint8_t r, g, b;
} LEDUpdate;

typedef struct Frame {
    uint32_t timestamp_ms;
    uint16_t num_leds_changed;
    struct LEDUpdate updates[];
} Frame;

typedef struct FrameNode{
    struct FrameNode *next;
    Frame *frame;
} FrameNode;

typedef struct FrameQueue  {
    FrameNode *head;
    FrameNode *tail;
    size_t size; 
} FrameQueue;

void init_frame_queue(FrameQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

bool frame_queue_push(FrameQueue *q, Frame *frame) {
    FrameNode *node = malloc(sizeof(FrameNode));
    if (!node) return false;
    node->frame = frame;
    node->next = NULL;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->size++;
    return true;
}

Frame *frame_queue_pop(FrameQueue *q) {
    if (!q->head) return NULL;
    FrameNode *node = q->head;
    Frame *frame = node->frame;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    free(node);
    q->size--;
    return frame;
}

FrameQueue light_table;
bool play_start_flag = false;
FileHeader file_header;
SemaphoreHandle_t LED_play;
TaskHandle_t refillTaskHandle = NULL;
TaskHandle_t playbackTaskHandle = NULL;

sdmmc_card_t* card = NULL;
const char mount_point[] = MOUNT_POINT;
sdmmc_host_t host;

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_RESUMED
} PlayerState;

volatile PlayerState current_state = PLAYER_STOPPED;

bool read_frame(FILE *f, Frame **out_frame) {
    uint32_t timestamp_ms;
    uint16_t num_leds_changed;
    if (fread(&timestamp_ms, sizeof(uint32_t), 1, f) != 1 ||
        fread(&num_leds_changed, sizeof(uint16_t), 1, f) != 1)
        return false;

    size_t frame_size = sizeof(Frame) + num_leds_changed * sizeof(LEDUpdate);
    Frame *frame = malloc(frame_size);
    if (!frame) return false;

    frame->timestamp_ms = timestamp_ms;
    frame->num_leds_changed = num_leds_changed;
    if (fread(frame->updates, sizeof(LEDUpdate), num_leds_changed, f) != num_leds_changed) {
        free(frame);
        return false;
    }

    *out_frame = frame;
    return true;
}

void initialize() {
    LED_play = xSemaphoreCreateBinary();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG1, "Initializing SD card");
    host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN));

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card));
    sdmmc_card_print_info(stdout, card);
}

void end_release() {
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    spi_bus_free(host.slot);
}

bool IRAM_ATTR led_timer_isr(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(LED_play, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken == pdTRUE;
}

void initialize_led_timer() {
    timer_config_t config = {
        .divider = 80,
        .counter_dir = TIMER_COUNT_UP,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
        .intr_type = TIMER_INTR_LEVEL
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 20000);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, led_timer_isr, NULL, 0);
}

void start_led_timer(FrameQueue *queue) {
    xSemaphoreGive(LED_play);
    timer_start(TIMER_GROUP_0, TIMER_0);
}

void stop_led_timer() {
    timer_pause(TIMER_GROUP_0, TIMER_0);
    timer_disable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_remove(TIMER_GROUP_0, TIMER_0);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
}

void refill_task(void *arg) {
    FrameQueue *queue = (FrameQueue *)arg;
    FILE *f = fopen("/sdcard/table.bin", "rb");
    if (!f || fread(&file_header, sizeof(FileHeader), 1, f) != 1) {
        vTaskDelete(NULL);
    }
    while (1) {
        if (queue->size < 2) {
            Frame *frame = NULL;
            if (read_frame(f, &frame)) frame_queue_push(queue, frame);
            else break;
        }
        if (queue->size == 2) play_start_flag = true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    fclose(f);
    vTaskDelete(NULL);
}

void playback_task(void *arg) {
    FrameQueue *queue = (FrameQueue *)arg;
    while (1) {
        if (xSemaphoreTake(LED_play, portMAX_DELAY) == pdTRUE) {
            Frame *frame = frame_queue_pop(queue);
            if (frame) {
                free(frame);
            } else {
                stop_led_timer();
                player_stop();
                vTaskDelete(NULL);
            }
        }
    }
}

void player_play() {
    if (current_state == PLAYER_STOPPED) {
        current_state = PLAYER_PLAYING;
        init_frame_queue(&light_table);
        xTaskCreate(refill_task, "Refill", 4096, &light_table, 4, &refillTaskHandle);
        xTaskCreate(playback_task, "Playback", 4096, &light_table, 5, &playbackTaskHandle);
        while (!play_start_flag) vTaskDelay(1);
        start_led_timer(&light_table);
    }
}

void player_pause() {
    if (current_state == PLAYER_PLAYING || current_state == PLAYER_RESUMED) {
        vTaskSuspend(playbackTaskHandle);
        current_state = PLAYER_PAUSED;
    }
}

void player_resume() {
    if (current_state == PLAYER_PAUSED) {
        vTaskResume(playbackTaskHandle);
        current_state = PLAYER_RESUMED;
    }
}

void player_stop() {
    if (current_state != PLAYER_STOPPED) {
        current_state = PLAYER_STOPPED;
        stop_led_timer();
        if (refillTaskHandle) {
            vTaskDelete(refillTaskHandle);
            refillTaskHandle = NULL;
        }
        if (playbackTaskHandle) {
            vTaskDelete(playbackTaskHandle);
            playbackTaskHandle = NULL;
        }
        while (light_table.size > 0) {
            Frame *f = frame_queue_pop(&light_table);
            free(f);
        }
    }
}

void app_main(void) {
    initialize();
    initialize_led_timer();
    LED_play = xSemaphoreCreateBinary();
    init_frame_queue(&light_table);

    char cmd[16];

    printf("Enter command: play, pause, resume, stop, exit\n");

    while (true) {
        printf("> ");
        fflush(stdout);  // 確保提示符輸出

        if (scanf("%15s", cmd) != 1) continue;

        if (strcmp(cmd, "play") == 0) {
            player_play();
        } else if (strcmp(cmd, "pause") == 0) {
            player_pause();
        } else if (strcmp(cmd, "resume") == 0) {
            player_resume();
        } else if (strcmp(cmd, "stop") == 0) {
            player_stop();
        } else if (strcmp(cmd, "exit") == 0) {
            player_stop();
            break;
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }

    end_release();
    printf("Exited.\n");
}

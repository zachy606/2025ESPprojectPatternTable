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
#include <string.h>



#define TAG1 "sdspi"
#define TAG2 "FrameReader"
#define TAG3 "Player"
#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO  19
#define PIN_NUM_MOSI  23
#define PIN_NUM_CLK   18
#define PIN_NUM_CS    5
#define SPI_DMA_CHAN  1

#define MAX_STRIPE_NUM 150
#define MAX_FRAME_NUM 2000


uint16_t frame_stamps[MAX_FRAME_NUM];


typedef struct FileHeader{
    uint16_t num_stripes;
    uint16_t length_stripes[ MAX_STRIPE_NUM ];
    uint16_t fps;
} FileHeader;

typedef struct LEDUpdate {
    uint8_t r, g, b ,a;
} LEDUpdate;

typedef struct Frame {
    uint8_t fade;
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
bool play_ending_flag = false;


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
void player_stop(void);

bool read_file_header_txt(FILE *f, FileHeader *header) {
    if (!f || !header) return false;

    // 1. 讀取 num_stripes
    if (fscanf(f, "%hu", &header->num_stripes) != 1) return false;
    if (header->num_stripes > MAX_STRIPE_NUM) return false;

    // 2. 讀取 length_stripes[]
    for (int i = 0; i < header->num_stripes; ++i) {
        if (fscanf(f, "%hu", &header->length_stripes[i]) != 1) return false;
    }

    // 3. 讀取 fps
    if (fscanf(f, "%hu", &header->fps) != 1) return false;

    return true;
}



bool read_frame(FILE *f, Frame **out_frame) {
    ESP_LOGI(TAG2, "read_frame() called - simulated fail for test");
    /*
    uint32_t timestamp_ms;
    
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
    return true;*/
    return false;
}

bool read_frame_txt(FILE *f, Frame **out_frame) {
    if (!f || !out_frame) return false;

    // 計算總共需要讀多少顆 LED
    uint32_t total_leds = 0;
    // for (int i = 0; i < file_header.num_stripes; ++i) {
    //     total_leds += file_header.length_stripes[i];
    // }
    total_leds = file_header.num_stripes;
    
    // 讀取 fade 值（單行）
    int fade_int = 0;
    if (fscanf(f, "%d", &fade_int) != 1) return false;
    uint8_t fade = (fade_int != 0);  // 轉換為 0 或 1

    // 分配記憶體：Frame + updates[]
    size_t frame_size = sizeof(Frame) + total_leds * sizeof(LEDUpdate);
    Frame *frame = malloc(frame_size);
    if (!frame) return false;

    frame->fade = fade;

    // 開始讀取每個 LED 的 RGBA
    for (uint32_t i = 0; i < total_leds; ++i) {
        int r, g, b, a;
        if (fscanf(f, "%d %d %d %d", &r, &g, &b, &a) != 4) {
            free(frame);
            return false;
        }
        frame->updates[i].r = (uint8_t)r;
        frame->updates[i].g = (uint8_t)g;
        frame->updates[i].b = (uint8_t)b;
        frame->updates[i].a = (uint8_t)a;
    }

    *out_frame = frame;
    return true;
}

void initialize() {


    ESP_LOGI(TAG1, "Skipped SD card init for testing...");
    /*
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
    sdmmc_card_print_info(stdout, card);*/
}

void end_release() {
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    spi_bus_free(host.slot);
}

bool IRAM_ATTR led_timer_isr(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(LED_play, &xHigherPriorityTaskWoken);
    ESP_EARLY_LOGI(TAG3, "led_timer_isr: Semaphore given from ISR");
    return xHigherPriorityTaskWoken == pdTRUE;
}

void initialize_led_timer() {

    // uint32_t ticks_per_frame = 1000000 / file_header.fps;
    uint32_t ticks_per_frame = 1000000 / 10;

    timer_config_t config = {
        .divider = 80,
        .counter_dir = TIMER_COUNT_UP,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
        .intr_type = TIMER_INTR_LEVEL
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, ticks_per_frame);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, led_timer_isr, NULL, 0);
}

void start_led_timer() {
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
    ESP_LOGI(TAG2, "Refill task started");
    FrameQueue *queue = &light_table;
    FILE *f  = (FILE *)arg;

    while (1) {
        if (queue->size < 2) {
            Frame *frame = NULL;
            // if (read_frame(f, &frame)) frame_queue_push(queue, frame);
            if (read_frame(NULL, &frame)){
                frame_queue_push(queue, frame);
                ESP_LOGI(TAG2, "Pushed new frame");
            } 
            else {
                ESP_LOGI(TAG2, "No more frames. Ending refill task.");
                break;
            } 
        }
        if (queue->size == 2){

            play_start_flag = true;
            ESP_LOGI(TAG2, "play_start_flag set to true");
        } 
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    // fclose(f);
    ESP_LOGI(TAG2, "Refill task exiting");
    while(1)vTaskDelay(1);
}

void playback_task(void *arg) {
    ESP_LOGI(TAG3, "Playback task started");    
    FrameQueue *queue = (FrameQueue *)arg;

    while (1) {
        if (xSemaphoreTake(LED_play, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG3, "LED_play semaphore taken");

            Frame *frame = frame_queue_pop(queue);
            if (frame) {
                ESP_LOGI(TAG3, "Popped frame - simulated display");
                free(frame);
            } else {
                ESP_LOGI(TAG3, "No more frames - stopping playback");
                // stop_led_timer();
                // player_stop();
                
                play_ending_flag = true;
                // vTaskDelete(NULL);
            }
        }
    }
}


void player_play(FILE *f) {
    if (current_state == PLAYER_STOPPED) {
        ESP_LOGI(TAG3, "player_play: Starting playback");
        current_state = PLAYER_PLAYING;
        
        xTaskCreate(refill_task, "Refill", 4096,f , 4, &refillTaskHandle);
        xTaskCreate(playback_task, "Playback", 4096, &light_table, 5, &playbackTaskHandle);

        // while (!play_start_flag) vTaskDelay(1);
        start_led_timer();
    } else {
        ESP_LOGW(TAG3, "player_play: Already playing or paused");
    }
}


void player_pause() {
    if (current_state == PLAYER_PLAYING || current_state == PLAYER_RESUMED) {
        ESP_LOGI(TAG3, "player_pause: Pausing playback");
        vTaskSuspend(playbackTaskHandle);
        vTaskSuspend(refillTaskHandle);
        current_state = PLAYER_PAUSED;
    }
}

void player_resume() {
    if (current_state == PLAYER_PAUSED) {
        ESP_LOGI(TAG3, "player_resume: Resuming playback");
        vTaskResume(playbackTaskHandle);
        vTaskResume(refillTaskHandle);
        current_state = PLAYER_RESUMED;
    }
}
void player_stop() {
    if (current_state != PLAYER_STOPPED) {
        ESP_LOGI(TAG3, "player_stop: Stopping playback and cleaning up");
        current_state = PLAYER_STOPPED;
        stop_led_timer();

        if (refillTaskHandle != NULL) {
            vTaskDelete(refillTaskHandle);
            refillTaskHandle = NULL;
            ESP_LOGI(TAG3, "Deleted refillTask");
        }
        if (playbackTaskHandle != NULL) {
            vTaskDelete(playbackTaskHandle);
            playbackTaskHandle = NULL;
            ESP_LOGI(TAG3, "Deleted playbackTask");
        }

        while (light_table.size > 0) {
            Frame *f = frame_queue_pop(&light_table);
            free(f);
        }

        ESP_LOGI(TAG3, "player_stop: Cleanup complete");
    }
}

void app_main(void) {
    initialize();
    
    LED_play = xSemaphoreCreateBinary();
    init_frame_queue(&light_table);
    FILE *f = NULL;

    // FILE *f = fopen("table. txt", "rb");
    // if (read_file_header_txt(f, &file_header)) {
    //     printf("Stripes: %d, FPS: %d\n", file_header.num_stripes, file_header.fps);
    //     for (int i = 0; i < file_header.num_stripes; ++i) {
    //         printf("Length[%d] = %d\n", i, file_header.length_stripes[i]);
    //     }
    // }
    



    initialize_led_timer();

    
    char cmd[16];
    int cnt = 0;
    ESP_LOGI(TAG3, "Enter command: play, pause, resume, stop, exit\n");
    ESP_LOGI(TAG3, "System ready. Type command to begin: play, pause, resume, stop, exit");
    while (true) {
        ESP_LOGI(TAG3, "> ");
        
        if(cnt==0)strcpy(cmd, "play");
        else if(cnt==1)strcpy(cmd, "pause");
        else if(cnt==2)strcpy(cmd, "resume");
        else if(cnt==3)strcpy(cmd, "pause");
        else if(cnt==4)strcpy(cmd, "stop");

        if(play_ending_flag){
            player_stop();
        }
        else if (strcmp(cmd, "play") == 0) {
            player_play(f);
        } else if (strcmp(cmd, "pause") == 0) {
            player_pause();
        } else if (strcmp(cmd, "resume") == 0) {
            player_resume();
        } else if (strcmp(cmd, "stop") == 0) {
            player_stop();
        } else if (strcmp(cmd, "exit") == 0) {
            // player_stop();
            break;
        } else {
            ESP_LOGI(TAG3, "Unknown command: %s\n", cmd);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG3, "now: %s\n", cmd);
        cnt++; 
    }

    end_release();
    ESP_LOGI(TAG3, "Exited.\n");
}

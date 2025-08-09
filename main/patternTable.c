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
#include <errno.h>  
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/errno.h>



#define TAG "SD_LIST"



#define TAG1 "sdspi"
#define TAG2 "FrameReader"
#define TAG3 "Player"
#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  15
#define PIN_NUM_CLK   14
#define PIN_NUM_CS    13



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

typedef struct FrameQueue

{
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
int frame_num = 0;


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
    ESP_LOGI(TAG1, "header stage 1");
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

    ESP_LOGE(TAG2, "read_frame_txt: started 1");

    uint32_t total_leds = 0;
    for (int i = 0; i < file_header.num_stripes; ++i) {
        total_leds += file_header.length_stripes[i];
    }

    // 1. 讀取 fade 值（單獨一行）
    int fade_int;
    if (fscanf(f, "%d", &fade_int) != 1) {
        ESP_LOGE(TAG2, "read_frame_txt: Failed to read fade value");
        return false;
    }
    uint8_t fade = (fade_int != 0);
    ESP_LOGE(TAG2, "read_frame_txt: fade=%d", fade);

    // 2. 分配記憶體
    size_t frame_size = sizeof(Frame) + total_leds * sizeof(LEDUpdate);
    Frame *frame = malloc(frame_size);
    if (!frame) {
        ESP_LOGE(TAG2, "read_frame_txt: Failed to allocate memory");
        return false;
    }
    frame->fade = fade;

    // 3. 逐行讀取 RGBA
    for (uint32_t i = 0; i < total_leds; ++i) {
        int r, g, b, a;
        if (fscanf(f, "%d %d %d %d", &r, &g, &b, &a) != 4) {
            ESP_LOGE(TAG2, "read_frame_txt: Invalid RGBA line at LED %"PRIu32"", i);
            free(frame);
            return false;
        }
        frame->updates[i].r = (uint8_t)r;
        frame->updates[i].g = (uint8_t)g;
        frame->updates[i].b = (uint8_t)b;
        frame->updates[i].a = (uint8_t)a;
    }

    *out_frame = frame;
    ESP_LOGI(TAG2, "Frame read complete (fade=%d, leds=%"PRIu32")", fade, total_leds);
    return true;
}

bool load_frame_stamps(const char *filename) {
    
    
    FILE *f = fopen(MOUNT_POINT"/times.txt", "r");
    if (!f) {
        ESP_LOGE(TAG2, "Failed to open %s", filename);
        return false;
    }
    char line[32];
    while (fgets(line, sizeof(line), f)) {
        frame_stamps[frame_num++] = atoi(line);
    }
    fclose(f);
    // ESP_LOGI(TAG, "Loaded %d frame times", total_frames);
    ESP_LOGI(TAG2, "Loaded %d frame stamps from %s", frame_num, filename);
    return true;
}


void initialize() {
    ESP_LOGI(TAG1, "Initializing system...");

    LED_play = xSemaphoreCreateBinary();

    // SD卡掛載設定
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };


    // 指定 SDSPI Host
    host = (sdmmc_host_t) SDSPI_HOST_DEFAULT();
    
    // SPI bus 設定
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_LOGI(TAG1, "Initializing SPI bus (CLK=%d, MOSI=%d, MISO=%d)",
             PIN_NUM_CLK, PIN_NUM_MOSI, PIN_NUM_MISO);

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG1, "Failed to initialize bus.");
        return;
    }

    // 指定 SD 卡的 SPI slave 裝置
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG1, "Mounting SD card at %s (CS=%d)", mount_point, PIN_NUM_CS);

    
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG1, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG1, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
            check_sd_card_pins(&config, pin_count);
#endif
        }
        return;
    }


    ESP_LOGI(TAG1, "SD card mounted successfully!");
    sdmmc_card_print_info(stdout, card);
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
    vTaskDelay(pdMS_TO_TICKS(100));
    while (1) {
        if (queue->size < 2) {
            Frame *frame = NULL;
            if (read_frame_txt(f, &frame)) {
            // if (read_frame(NULL, &frame)){
                frame_queue_push(queue, frame);
                ESP_LOGI(TAG2, "Pushed new frame");
            } 
            else {
                ESP_LOGI(TAG2, "No more frames. Ending refill task.");
                break;
            } 
        }
        if (queue->size == 2 && !play_start_flag){

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
        xTaskCreate(playback_task, "Playback", 4096, &light_table, 3, &playbackTaskHandle);

        while (!play_start_flag) vTaskDelay(1);
        ESP_LOGI(TAG3, "player_play: Starting timer");
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

void list_dir(const char *path)
{
    ESP_LOGI(TAG, "Listing files in %s:", path);

    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", strerror(errno));
        return;
    }

    printf("%s:\n", path);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf(
            "    %s: %s\n",
            (entry->d_type == DT_DIR)
                ? "directory"
                : "file     ",
            entry->d_name
        );
        //  printf("FILE : %s\n", entry->d_name);
    }

    closedir(dir);
}

void app_main(void) {
    initialize();
    list_dir(mount_point);
    LED_play = xSemaphoreCreateBinary();
    init_frame_queue(&light_table);
    // FILE *f = NULL;

    FILE *f = fopen(MOUNT_POINT"/data.txt", "r");
    if(!f){
        ESP_LOGI("MAIN","Stripes header failed");
    }
    if (read_file_header_txt(f, &file_header)) {
        ESP_LOGI("MAIN","Stripes: %d, FPS: %d\n", file_header.num_stripes, file_header.fps);
        for (int i = 0; i < file_header.num_stripes; ++i) {
            ESP_LOGI("MAIN","Length[%d] = %d\n", i, file_header.length_stripes[i]);
        }
    }else{
        ESP_LOGI("MAIN","Stripes header failed");
    }
    char timestamps_file[64] = "/sdcardtimes.txt";
    if(load_frame_stamps(timestamps_file)){
        for (int i = 0; i < frame_num; i++) {
            ESP_LOGI("MAIN","Length[%d] = %d\n", i, frame_stamps[i]);
        }
    }



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

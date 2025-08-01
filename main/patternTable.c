#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <processthreadsapi.h>


#define TAG1 "sdspi"
#define TAG2 "FrameReader"
#define TAG3 "Playback"
#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO  19
#define PIN_NUM_MOSI  23
#define PIN_NUM_CLK   18
#define PIN_NUM_CS    5

#define SPI_DMA_CHAN  1

typedef struct FileHeader{
    uint32_t num_frames;     // Total number of frames
    uint16_t num_leds;       // Total number of LEDs in the strip
    uint16_t reserved;       // Reserved (set to 0)
} FileHeader;

typedef struct LEDUpdate {
    uint16_t led_index;
    uint8_t r, g, b;
} LEDUpdate;

typedef struct Frame {
    uint32_t timestamp_ms;       // Time to display this frame (ms)
    uint16_t num_leds_changed;   // How many LEDs changed
    struct LEDUpdate updates[];
} Frame ;

typedef struct FrameNode{
    struct FrameNode *next;
    Frame *frame;
} FrameNode;

typedef struct FrameQueue  {
    FrameNode *head;
    FrameNode *tail;
    size_t size; 
} FrameQueue ;

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

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->size++;
    return true;
}

Frame *frame_queue_pop(FrameQueue *q) {
    if (q->head == NULL) return NULL;

    FrameNode *node = q->head;
    Frame *frame = node->frame;

    q->head = node->next;
    if (q->head == NULL) q->tail = NULL;

    free(node);
    q->size--; 
    return frame;
}

size_t frame_queue_size(FrameQueue *q) {
    return q->size;
}

FrameQueue light_table;
bool play_start_flag = false;
FileHeader file_header;
SemaphoreHandle_t LED_play;


sdmmc_card_t* card = NULL;                       // SD 卡描述結構
const char mount_point[] = MOUNT_POINT;   // 掛載點，如 "/sdcard"
// 使用 SDSPI host (預設為 SPI2_HOST)
sdmmc_host_t host;

void initialize(){
    
    LED_play = xSemaphoreCreateBinary();//LED_timer

    esp_err_t ret;

    // 掛載 FAT 檔案系統的設定
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,       // 掛載失敗時是否格式化 SD 卡
        .max_files = 5,                       // 同時允許開啟的最大檔案數
        .allocation_unit_size = 16 * 1024     // 分配單位大小 (效能優化)
    };

    // sdmmc_card_t* card;                       // SD 卡描述結構
    // const char mount_point[] = MOUNT_POINT;   // 掛載點，如 "/sdcard"

    ESP_LOGI(TAG1, "Initializing SD card");


    ESP_LOGI(TAG1, "Using SPI peripheral");

    // 使用 SDSPI host (預設為 SPI2_HOST)
    host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();

    // 配置 SPI 線路對應的 GPIO 腳位
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8192 ,
    };

    // 初始化 SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG1, "Failed to initialize bus.");
        return;
    }

    // 初始化 slot 結構
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // 掛載檔案系統
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    // 判斷掛載是否成功
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG1, "Failed to mount filesystem. "
                        "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG1, "Failed to initialize the card (%s). "
                        "Make sure SD card lines have pull-up resistors in place.",
                        esp_err_to_name(ret));
        }
        return;
    }

    // 印出卡片資訊
    sdmmc_card_print_info(stdout, card);
}

bool read_frame(FILE *f, Frame **out_frame){//3ms

    uint32_t timestamp_ms;
    uint16_t num_leds_changed;

    // 1. 先讀取前6 bytes（timestamp + num_leds_changed）
    if (fread(&timestamp_ms, sizeof(uint32_t), 1, f) != 1)
        return false;
    if (fread(&num_leds_changed, sizeof(uint16_t), 1, f) != 1)
        return false;

    // 2. 計算整個 Frame 所需記憶體大小
    size_t frame_size = sizeof(Frame) + num_leds_changed * sizeof(LEDUpdate);

    // 3. 分配 Frame 記憶體
    Frame *frame = malloc(frame_size);
    if (!frame){
        ESP_LOGE(TAG2, "Failed to allocate memory for Frame (%zu bytes)", frame_size);
        return false;
    } 

    frame->timestamp_ms = timestamp_ms;
    frame->num_leds_changed = num_leds_changed;
    if (fread(frame->updates, sizeof(LEDUpdate), num_leds_changed, f) != num_leds_changed) {

        ESP_LOGE(TAG2, "Failed to read updates[] (expected %u entries, size %zu). "
                "ftell=%ld, feof=%d, ferror=%d",
                num_leds_changed,
                sizeof(LEDUpdate),
                ftell(f), feof(f), ferror(f));
        free(frame);
        return false;
    }


    *out_frame = frame;
    return true;

}

void end_realease(){
    //結束釋放資源

    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG1, "Card unmounted");

    // 卸載 SPI bus 資源
    spi_bus_free(host.slot);
}

bool IRAM_ATTR led_timer_isr(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(LED_play, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken == pdTRUE;
}

void initialize_led_timer(){

    timer_config_t config = {
        .divider = 80,                     // 1 us per tick (80MHz APB / 80 = 1MHz)
        .counter_dir = TIMER_COUNT_UP,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
        .intr_type = TIMER_INTR_LEVEL
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    // 20,000 us = 20ms
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 20000);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, led_timer_isr, NULL, 0);
}
void start_led_timer(FrameQueue *queue) {
    
    xSemaphoreGive(LED_play); //first frame
    ESP_LOGW(TAG2, "first frame start, 0ms");
    timer_start(TIMER_GROUP_0, TIMER_0);
}

void stop_led_timer(void){
    // 1. 暫停 timer
    timer_pause(TIMER_GROUP_0, TIMER_0);

    // 2. 禁用中斷
    timer_disable_intr(TIMER_GROUP_0, TIMER_0);

    // 3. 移除 ISR callback（避免殘留）
    timer_isr_callback_remove(TIMER_GROUP_0, TIMER_0);

    // 4. 重設 counter（選擇性）
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
}


void refill_task(void *arg) {

    FrameQueue *queue = (FrameQueue *)arg;
    FILE *f = fopen("/sdcard/table.bin", "rb");
    if (!f) {
        ESP_LOGE(TAG2, "Failed to open animation file");
        vTaskDelete(NULL);
    }

    //read file header
    if (fread(&file_header, sizeof(FileHeader), 1, f) != 1) {
        ESP_LOGE(TAG2, "Failed to read header!");
        vTaskDelete(NULL);
    }


    while (1) {
        if (queue->size < 2) {
            Frame *frame = NULL;
            if (read_frame(f, &frame)) {
                frame_queue_push(queue, frame);
            } else {
                ESP_LOGW(TAG2, "No more frames (EOF or error)");
               
                break;
            }
        }

        if(queue->size == 2){
            play_start_flag = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));  // 微休息，必要?
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
                //for LED
                free(frame);
            } else {
                ESP_LOGW(TAG3, "No frame available!");
                stop_led_timer();
                vTaskDelete(NULL);
                
            }
        }
    }
}


void app_main(void){
    
    initialize();
    initialize_led_timer();

    xTaskCreate(playback_task, "Playback", 4096, &light_table, 5, NULL);
    xTaskCreate(refill_task, "Refill", 4096, &light_table, 4, NULL);
    while(!play_start_flag){
        vTaskDelay(1);
    }
    start_led_timer(&light_table);
    
    
    end_realease();



}

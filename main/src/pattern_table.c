#include"app_config.h"
#include "pattern_table.h"
#include "sdcard.h"
#include "esp_log.h"
#include <stdio.h>      
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "testing.h"


#define TAG "LD_READER"



void print_framedata(const FrameData *frame_data){
    ESP_LOGI("FD","fade %d",(int)frame_data->fade);
    // for(int i=0;i<self->total_leds;i++){
    for(int i=0;i<3;i++){
        ESP_LOGI("FD","R %d, G %d, B %d, A %d",frame_data->colors[i][0],frame_data->colors[i][1],frame_data->colors[i][2],frame_data->colors[i][3]);
    }
}



void PatternTable_init(PatternTable *self, const char *mount_point) {
    self->mount_point = mount_point;
    self->data_fp = NULL;
    self->time_fp = NULL;
    self->total_parts = 0;
    self->fps = 0;
    self->total_frames = 0;
    self->total_leds = 0;
    self->index = 0;
}

bool PatternTable_load_times(PatternTable *self) {
    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s", self->mount_point, TIME_DATA);
    self->time_fp = fopen(path, "r");
    if (!self->time_fp) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return false;
    }

    self->total_frames = 0;
    while (fscanf(self->time_fp, "%lu", &self->frame_times[self->total_frames]) == 1) {
        self->total_frames++;
        if (self->total_frames >= MAX_FRAMES) break;
    }

    fclose(self->time_fp);
    ESP_LOGI(TAG, "Loaded %d frame times", self->total_frames);
    return true;
}

static inline uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    c = (char)tolower((unsigned char)c);
    return (uint8_t)(10 + (c - 'a'));
}

static inline uint8_t hexpair_to_byte(char hi, char lo) {
    return (uint8_t)((hex_nibble(hi) << 4) | hex_nibble(lo));
}

// 讀掉本行行尾（支援 \n 或 \r\n）
static void eat_eol(FILE *fp) {
    int c = fgetc(fp);
    if (c == '\r') {
        int c2 = fgetc(fp);
        if (c2 != '\n' && c2 != EOF) ungetc(c2, fp);
    } else if (c != '\n' && c != EOF) {
        ungetc(c, fp);
    }
}

// 以小塊緩衝「丟棄」need 個字元（用於建索引時跳過 dense hex 行）
static bool discard_exact_chars(FILE *fp, size_t need) {
    char buf[LED_DISCARD_BUF_LEN];
    while (need > 0) {
        size_t chunk = need < sizeof(buf) ? need : sizeof(buf);
        size_t got = fread(buf, 1, chunk, fp);
        if (got != chunk) return false;
        need -= got;
    }
    eat_eol(fp);
    return true;
}


/**
 * 將一整行「緊密的 16 進位字串」邊讀邊轉成 bytes，直接寫入 out[]。
 * - 期待字元數 = need_hex
 * - 讀完會吃掉行尾（\n 或 \r\n）
 * - 回傳 false 表示資料不足
 *
 * 注意：本函式假設資料行「不含空白或逗號」，只由 0-9A-Fa-f 組成。
 * 若你的檔案可能含空白，請告訴我我再給你「過濾空白」的串流版本。
 */
static bool stream_dense_hex_to_bytes(FILE *fp, size_t need_hex, uint8_t *out) {
    // 每兩個 hex → 1 byte
    const size_t need_bytes = need_hex / 2;
    const size_t BUF_SZ = LED_RGBS_BUF_LEN; // 檔案讀取緩衝
    char buf[BUF_SZ];
    size_t produced = 0;  // 已產生的 bytes
    int have_hi = 0;      // 是否已有高半位
    char hi = 0;

    while (produced < need_bytes) {
        size_t remain_hex = (need_bytes - produced) * 2 + (have_hi ? 1 : 0);
        size_t to_read = remain_hex < BUF_SZ ? remain_hex : BUF_SZ;

        size_t got = fread(buf, 1, to_read, fp);
        if (got != to_read) return false; // dense 格式：期望精確讀滿

        for (size_t i = 0; i < got; ++i) {
            char c = buf[i];
            // dense 格式：不應該出現換行；若真的出現就是資料不足
            if (c == '\r' || c == '\n') return false;

            if (!have_hi) {
                hi = c;
                have_hi = 1;
            } else {
                out[produced++] = hexpair_to_byte(hi, c);
                have_hi = 0;
            }
        }
    }

    // 讀完本行，吃掉行尾
    eat_eol(fp);
    return true;
}


/* ===================== 你的函式：建索引 ===================== */
bool PatternTable_index_frames(PatternTable *self) {
    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, FRAME_DATA);
    self->data_fp = fopen(path, "r");
    if (!self->data_fp) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return false;
    }

    // 1) parts
    if (fscanf(self->data_fp, "%d", &self->total_parts) != 1) return false;

    // 2) part lengths
    self->total_leds = 0;
    for (int i = 0; i < self->total_parts; ++i) {
        if (fscanf(self->data_fp, "%d", &self->part_lengths[i]) != 1) return false;
        self->total_leds += self->part_lengths[i];
    }

    // 3) fps
    if (fscanf(self->data_fp, "%d", &self->fps) != 1) return false;
    ESP_LOGI(TAG, "FPS %d", self->fps);
    // 清掉 fps 行殘餘
    int c;
    while ((c = fgetc(self->data_fp)) != '\n' && c != EOF) {}

    // 4) frames 索引
    const size_t need_hex = (size_t)self->total_leds * 8; // RRGGBBAA per LED
    self->total_frames = 0;

    for (;;) {
        long offset = ftell(self->data_fp);
        if (offset < 0) break;

        int fade_dummy;
        if (fscanf(self->data_fp, "%d", &fade_dummy) != 1) break;

        if (self->total_frames >= MAX_FRAMES) break;
        self->frame_offsets[self->total_frames] = (int)offset;
        ESP_LOGI("Ftell", "frame %d starts @ %ld", self->total_frames, offset);
        self->total_frames++;

        // 吃掉 fade 行尾
        while ((c = fgetc(self->data_fp)) != '\n' && c != EOF) {}

        // 快速丟棄 hex 行（不用存）
        if (!discard_exact_chars(self->data_fp, need_hex)) {
            self->total_frames--;
            break;
        }
    }

    ESP_LOGI(TAG, "Indexed %d frames", self->total_frames);
    self->index = 0;
    return true;
}

/* ===================== 隨機讀取某一 frame ===================== */
void PatternTable_read_frame_at(PatternTable *self, const int index, FrameData *framedata) {

    

    if (!self->data_fp || index < 0 || index >= self->total_frames) return;

    fseek(self->data_fp, self->frame_offsets[index], SEEK_SET);
    int64_t read_timer = perf_timer_start();
    int fade;
    if (fscanf(self->data_fp, "%d", &fade) != 1) return;
    framedata->fade = (fade != 0);

    // 吃掉 fade 行尾
    int c;
    while ((c = fgetc(self->data_fp)) != '\n' && c != EOF) {}

    // 直接邊讀邊轉成 bytes（寫入 framedata->colors）
    const size_t need_hex = (size_t)self->total_leds * 8;
    uint8_t *out = (uint8_t *)framedata->colors; // 連續 4 bytes/LED (RGBA)
    if (!stream_dense_hex_to_bytes(self->data_fp, need_hex, out)) return;

    perf_timer_end(read_timer, "frame input", "end");

    self->index = index + 1; // 讓 go_through() 能接著讀下一個
}

/* ===================== 依序往下讀（無 seek） ===================== */
void PatternTable_read_frame_go_through(PatternTable *self, FrameData *framedata) {
    if (!self->data_fp || self->index >= self->total_frames) return;

    int fade;
    if (fscanf(self->data_fp, "%d", &fade) != 1) return;
    framedata->fade = (fade != 0);

    // 吃掉 fade 行尾
    int c;
    while ((c = fgetc(self->data_fp)) != '\n' && c != EOF) {}

    const size_t need_hex = (size_t)self->total_leds * 8;
    uint8_t *out = (uint8_t *)framedata->colors;
    if (!stream_dense_hex_to_bytes(self->data_fp, need_hex, out)) return;

    self->index++;
}

void find_led_index_ms(PatternTable *self, uint32_t t_ms)
{
    if (self->total_frames == 0) return ;

    size_t lo = 0, hi = self->total_frames; // 右開區間 [lo, hi)
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        if (self->frame_times[mid] <= t_ms) {
            lo = mid + 1;      // 還可以往右找
        } else {
            hi = mid;          // 縮到左半邊
        }
    }
    // 迴圈結束時 lo 是第一個 > t_ms 的位置，因此 lo-1 是最後一個 <= t_ms
    self->index = lo-1;
    ESP_LOGI("find led","start from frame %d",self->index);
}




const uint32_t *PatternTable_get_time_array(const PatternTable *self) {
    return self->frame_times;
}

int PatternTable_get_total_frames(const PatternTable *self) {
    return self->total_frames;
}

int PatternTable_get_total_leds(const PatternTable *self) {
    return self->total_leds;
}

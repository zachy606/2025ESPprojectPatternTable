#include"app_config.h"
#include "pattern_table.h"
#include "sdcard.h"
#include "esp_log.h"
#include <stdio.h>      
#include <inttypes.h>
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

bool PatternTable_load_times(PatternTable *self, const char *time_file) {
    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s", self->mount_point, time_file);
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

bool PatternTable_index_frames(PatternTable *self, const char *data_file) {
    char path[PATH_BUF_LEN];
    snprintf(path, sizeof(path), "%s/%s", self->mount_point, data_file);
    self->data_fp = fopen(path, "r");
    if (!self->data_fp) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return false;
    }

    // 1. total_parts
    if (fscanf(self->data_fp, "%d", &self->total_parts) != 1) return false;

    // 2. part lengths
    for (int i = 0; i < self->total_parts; ++i) {
        if (fscanf(self->data_fp, "%d", &self->part_lengths[i]) != 1) return false;
        self->total_leds += self->part_lengths[i];
    }

    // 3. fps
    if (fscanf(self->data_fp, "%d", &self->fps) != 1) return false;
    ESP_LOGE(TAG, "FPS %d", self->fps);
    // 4. index each frame
    self->total_frames = 0;
    while (!feof(self->data_fp) && self->total_frames < MAX_FRAMES) {
        int offset = ftell(self->data_fp);
        self->frame_offsets[self->total_frames] = offset;
        self->total_frames++;
        ESP_LOGI("Ftell","ftell direction %d", offset);
        int fade_dummy;
        if (fscanf(self->data_fp, "%d", &fade_dummy) != 1) break;

        for (int i = 0; i < self->total_leds; i++) {
            int r, g, b, a;
            if (fscanf(self->data_fp, "%d %d %d %d", &r, &g, &b, &a) != 4) break;
        }
    }

    ESP_LOGI(TAG, "Indexed %d frames", self->total_frames);
    // fclose(self->data_fp);
    return true;
}

void PatternTable_read_frame_at(PatternTable *self,const int index ,const char *data_file,FrameData *framedata) {

    // char path[128];
    // snprintf(path, sizeof(path), "%s/%s", self->mount_point, data_file);
    // self->data_fp = fopen(path, "r");


    ESP_LOGI(TAG, "Read %d frames", index);
    if (!self->data_fp || index < 0 || index >= self->total_frames) return ;
    ESP_LOGI(TAG, "Read %d frames", index);

    fseek(self->data_fp, self->frame_offsets[index], SEEK_SET);
    
    ESP_LOGI(TAG, "Read %d led", self->total_leds);
    
    int fade;
    if (fscanf(self->data_fp, "%d", &fade) != 1) return ;
    framedata->fade = (fade != 0);
    ESP_LOGI(TAG, "Read %d frames", index);
    for (int i = 0; i < self->total_leds; ++i) {
        int r, g, b, a;
        if (fscanf(self->data_fp, "%d %d %d %d", &r, &g, &b, &a) != 4) break;
        framedata->colors[i][0] = r;
        framedata->colors[i][1] = g;
        framedata->colors[i][2] = b;
        framedata->colors[i][3] = a;
    }
    self->index = index;
    return ;
}

void PatternTable_read_frame_go_through(PatternTable *self,FrameData *framedata) {




    // ESP_LOGI(TAG, "Read %d frames", index);
    if (!self->data_fp  || self->index >= self->total_frames) return ;
    // ESP_LOGI(TAG, "Read %d frames", index);

    // fseek(self->data_fp, self->frame_offsets[index], SEEK_SET);
    
    ESP_LOGI(TAG, "Read %d led", self->total_leds);
    
    int fade;
    if (fscanf(self->data_fp, "%d", &fade) != 1) return ;
    framedata->fade = (fade != 0);
    // ESP_LOGI(TAG, "Read %d frames", index);
    for (int i = 0; i < self->total_leds; ++i) {
        int r, g, b, a;
        if (fscanf(self->data_fp, "%d %d %d %d", &r, &g, &b, &a) != 4) break;
        framedata->colors[i][0] = r;
        framedata->colors[i][1] = g;
        framedata->colors[i][2] = b;
        framedata->colors[i][3] = a;
    }
    self->index++;
    return;
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

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define MAX_PARTS 32
#define MAX_COLOR_VALUES 4096
#define MAX_FRAMES 2048

typedef struct {
    bool fade;
    uint8_t colors[MAX_COLOR_VALUES][4]; // RGBA
} FrameData;

typedef struct {
    FILE *data_fp;
    FILE *time_fp;

    int total_parts;
    int part_lengths[MAX_PARTS];
    int total_leds;
    int fps;
    int total_frames;
    int index;
    uint32_t frame_times[MAX_FRAMES];       // in milliseconds
    int frame_offsets[MAX_FRAMES];       // byte offset of each frame

    const char *mount_point;
} LightdanceReader;


void print_framedata(const FrameData *frame_data,const LightdanceReader *self);

void LightdanceReader_init(LightdanceReader *self, const char *mount_point);
bool LightdanceReader_load_times(LightdanceReader *self, const char *time_file);
bool LightdanceReader_index_frames(LightdanceReader *self, const char *data_file);
void LightdanceReader_read_frame_at(LightdanceReader *self, int index,const char *data_file,FrameData *framedata);
void LightdanceReader_read_frame_go_through(LightdanceReader *self,FrameData *framedata);
uint32_t *LightdanceReader_get_time_array(LightdanceReader *self);
int LightdanceReader_get_total_frames(const LightdanceReader *self);
int LightdanceReader_get_total_leds(const LightdanceReader *self);
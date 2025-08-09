#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

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

    uint32_t frame_times[MAX_FRAMES];       // in milliseconds
    size_t frame_offsets[MAX_FRAMES];       // byte offset of each frame

    const char *mount_point;
} LightdanceReader;

void LightdanceReader_init(LightdanceReader *self, const char *mount_point);
bool LightdanceReader_load_times(LightdanceReader *self, const char *time_file);
bool LightdanceReader_index_frames(LightdanceReader *self, const char *data_file);
FrameData LightdanceReader_read_frame_at(LightdanceReader *self, int index);
uint32_t *LightdanceReader_get_time_array(LightdanceReader *self);
int LightdanceReader_get_total_frames(const LightdanceReader *self);
int LightdanceReader_get_total_leds(const LightdanceReader *self);
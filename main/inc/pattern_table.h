#pragma once

#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "sdmmc_cmd.h"

#define MAX_PARTS 40
// MAX_PARTS : MAX total num of led stripes + OFs

#define MAX_COLOR_VALUES 4096 
// MAX_PARTS : total num of leds 

#define MAX_FRAMES 2048
// MAX_PARTS : MAX total num of Frames

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
    int index; //current Frame in read
    uint32_t frame_times[MAX_FRAMES];       // in milliseconds
    int frame_offsets[MAX_FRAMES];       // byte offset of each frame

    const char *mount_point;
    sdmmc_card_t *card;

    
} PatternTable;


void print_framedata(const FrameData *frame_data);

void PatternTable_init(PatternTable *self, const char *mount_point);
bool PatternTable_load_times(PatternTable *self, const char *time_file);
bool PatternTable_index_frames(PatternTable *self, const char *data_file);
void PatternTable_read_frame_at( PatternTable *self, const int index,const char *data_file,FrameData *framedata);
void PatternTable_read_frame_go_through( PatternTable *self,FrameData *framedata);
const uint32_t *PatternTable_get_time_array(const PatternTable *self);
int PatternTable_get_total_frames(const PatternTable *self);
int PatternTable_get_total_leds(const PatternTable *self);
#pragma once

#include "lightdance_reader.h"

typedef struct {
    FrameData buffer[2];
    int read_index;
    int play_index;
    int buffer_ptr;
    int buffer_count;
    int total_frames;

    const LightdanceReader *reader;
} FrameBufferPlayer;

void FrameBufferPlayer_init(FrameBufferPlayer *fbp,const LightdanceReader *reader);
bool FrameBufferPlayer_fill(FrameBufferPlayer *fbp);  // 補 frame 到 buffer
bool FrameBufferPlayer_ready(FrameBufferPlayer *fbp); // buffer 是否有 frame 可播
bool FrameBufferPlayer_play(FrameBufferPlayer *fbp, FrameData *out_frame);
bool FrameBufferPlayer_is_done(FrameBufferPlayer *fbp);
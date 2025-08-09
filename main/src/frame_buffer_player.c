#include "frame_buffer_player.h"

void FrameBufferPlayer_init(FrameBufferPlayer *fbp, const LightdanceReader *reader) {
    fbp->reader = reader;
    fbp->read_index = 0;
    fbp->play_index = 0;
    fbp->buffer_ptr = 0;
    fbp->buffer_count = 0;
    fbp->total_frames = LightdanceReader_get_total_frames(reader);
}

bool FrameBufferPlayer_fill(FrameBufferPlayer *fbp) {
    if (fbp->buffer_count >= 2) return false; // buffer 滿了

    if (fbp->read_index >= fbp->total_frames) return false; // 已讀完

    FrameData frame = LightdanceReader_read_frame_at(fbp->reader, fbp->read_index);
    fbp->buffer[fbp->buffer_ptr] = frame;

    fbp->read_index++;
    fbp->buffer_ptr = (fbp->buffer_ptr + 1) % 2;
    fbp->buffer_count++;

    return true;
}

bool FrameBufferPlayer_ready(FrameBufferPlayer *fbp) {
    return fbp->buffer_count > 0;
}

bool FrameBufferPlayer_play(FrameBufferPlayer *fbp, FrameData *out_frame) {
    if (fbp->buffer_count == 0) return false;

    int play_buf_index = fbp->play_index % 2;
    *out_frame = fbp->buffer[play_buf_index];

    fbp->play_index++;
    fbp->buffer_count--;

    return true;
}

bool FrameBufferPlayer_is_done(FrameBufferPlayer *fbp) {
    return fbp->play_index >= fbp->total_frames;
}
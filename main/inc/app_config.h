#pragma once

// --- timing / player in player.c---
#define DEFAULT_FPS               39
#define TIMER_RESOLUTION_HZ       1000000ULL   // 1 tick = 1 us

// --- command loop in state_changer.c---
#define CMD_LINE_BUF              64 // string buffer for cmd
#define CMD_IDLE_POLL_DELAY_MS    10 // time wait for cmd each loop

// --- files / paths in pattern_table.c---
#define PATH_BUF_LEN              128  // string buffer for file name
#define LED_RGBS_BUF_LEN          1024 // buffer size for reading rgba, multiple of 512 better 
#define LED_DISCARD_BUF_LEN       512  // buffer size for discarding rgba when finding offsets, multiple of 512 better 

// --- SD (可選，看你專案需求) in sdcard.c---
#define SD_SPI_MAX_FREQ_KHZ       26000
#define SD_MAX_FILES              5
#define SD_ALLOC_UNIT_SIZE        (16 * 1024)
#define SD_MAX_TRANSFER_SIZE      (8 * 1024)

// --- MAIN in main.c---
#define MOUNT_POINT               "/sdcard"
#define TIME_DATA                 "800time.txt"
#define FRAME_DATA                "h800data.txt"


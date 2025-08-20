#pragma once
#include <stdint.h>
#include <stdbool.h>

// 啟動計時，傳回啟動時間（μs）
int64_t perf_timer_start(void);

// 結束計時並輸出，傳入 start_time、TAG 及訊息
void perf_timer_end(int64_t start_time, const char *tag, const char *msg);

bool perf_timer_cnt(int64_t start_time,int64_t end_time, const char *tag, const char *msg);
// ==============================================
// File: main/sntp_client.h
// ==============================================
#pragma once
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct tm* time_local;
    struct tm* time_utc;
} time_data_t;

void start_sntp_task(QueueHandle_t out_queue_print, QueueHandle_t out_queue_rtc);

#ifdef __cplusplus
}
#endif

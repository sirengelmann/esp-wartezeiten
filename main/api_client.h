// ==============================================
// File: main/api_client.h
// ==============================================
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void start_fetch_waitingtimes_api_task(QueueHandle_t out_queue,
                                       const char* park_id,
                                       const char* target_ride_name);

void start_fetch_openingtimes_api_task(QueueHandle_t out_queue_park,
                                       const char* park_id);

#ifdef __cplusplus
}
#endif

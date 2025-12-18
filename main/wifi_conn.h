// ==============================================
// File: main/wifi_conn.h
// ==============================================
#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_conn_init(void);
void wifi_conn_start(void);
bool wifi_conn_wait_ip(TickType_t timeout_ticks);
void wifi_conn_set_credentials(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

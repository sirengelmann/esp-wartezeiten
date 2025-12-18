// ==============================================
// File: main/sd_config.h
// ==============================================
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *ssid;
    char *password;
} sd_wifi_config_t;

/**
 * Attempt to load Wi-Fi credentials from SD card.
 * Returns true on success and fills cfg (caller must free cfg->ssid/password).
 * Returns false on any failure (no card, no file, parse error, etc.).
 */
bool sd_config_load_wifi(sd_wifi_config_t *cfg);

#ifdef __cplusplus
}
#endif

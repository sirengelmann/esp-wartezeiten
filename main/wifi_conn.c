// ==============================================
// File: main/wifi_conn.c
// ==============================================
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "wifi_conn.h"

static const char* TAG_WIFI = "wifi_conn";
static EventGroupHandle_t s_evt;
static const int GOT_IP_BIT = BIT0;
static bool s_initialized = false;
static wifi_config_t s_cfg = { 0 };
static bool s_cfg_overridden = false;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG_WIFI, "Wi-Fi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_evt, GOT_IP_BIT);
    }
}

void wifi_conn_init(void)
{
    if (s_initialized) return;
    s_evt = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();   // <— wichtig für DHCP/IP Events

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    if (!s_cfg_overridden) {
        #ifdef CONFIG_WIFI_SSID
            strlcpy((char*)s_cfg.sta.ssid, CONFIG_WIFI_SSID, sizeof(s_cfg.sta.ssid));
        #endif
        #ifdef CONFIG_WIFI_PASSWORD
            strlcpy((char*)s_cfg.sta.password, CONFIG_WIFI_PASSWORD, sizeof(s_cfg.sta.password));
        #endif
    }
    if (s_cfg.sta.password[0] != '\0') {
        s_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        s_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    s_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    s_initialized = true;
}

void wifi_conn_start(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &s_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_conn_wait_ip(TickType_t timeout_ticks)
{
    EventBits_t bits = xEventGroupWaitBits(s_evt, GOT_IP_BIT, pdFALSE, pdFALSE, timeout_ticks);
    return (bits & GOT_IP_BIT) != 0;
}

void wifi_conn_set_credentials(const char *ssid, const char *password)
{
    if (!ssid) return;
    memset(&s_cfg, 0, sizeof(s_cfg));
    strlcpy((char*)s_cfg.sta.ssid, ssid, sizeof(s_cfg.sta.ssid));
    if (password) {
        strlcpy((char*)s_cfg.sta.password, password, sizeof(s_cfg.sta.password));
    }
    if (s_cfg.sta.password[0] != '\0') {
        s_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        s_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    s_cfg_overridden = true;
}

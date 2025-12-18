// ==============================================
// File: main/sntp_client.c
// ==============================================
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
//#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "wifi_conn.h"      // enthält: bool wifi_conn_wait_ip(TickType_t timeout_ticks)
//#include "esp_sntp.h"
#include <time.h>
#include "esp_log.h"
#include "sntp_client.h"

#define SNTP_FALLBACK_URL "de.pool.ntp.org"

static const char* TAG_SNTP = "sntp_client";

// -----------------------------------------------------------------------------
// Tasks
// -----------------------------------------------------------------------------

// Task: sntp_task (Aktuelle Zeit von einem SNTP Server beziehen)
static void sntp_task(void* arg)
{
    void** pack = (void**)arg;
    QueueHandle_t out_q_print = (QueueHandle_t)pack[0];
    QueueHandle_t out_q_rtc = (QueueHandle_t)pack[1];
    free(pack);

    if (!wifi_conn_wait_ip(pdMS_TO_TICKS(20000))) {
        ESP_LOGE(TAG_SNTP, "Timeout: keine Wi-Fi Verbindung (waitingtimes)");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_SNTP, "Fordere SNTP-Server vom DHCP-Client an (Option 42), setze Fallback auf pool.ntp.org");

    // Konfiguration: DHCP erlauben, ABER Fallback-Server mitgeben
    // Hinweis: MULTIPLE nimmt Anzahl + Stringliste; hier 1 Server als Fallback
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(1, { SNTP_FALLBACK_URL });
    cfg.start = false;               // später explizit starten
    cfg.server_from_dhcp = true;     // akzeptiere Option 42, falls vorhanden
    cfg.renew_servers_after_new_IP = true;

    esp_netif_sntp_init(&cfg);
    esp_netif_sntp_start();
    ESP_LOGI(TAG_SNTP, "Starte Zeitabfrage.");

    // Warte bis synchronisiert (Timeout evtl. etwas großzügiger wählen)
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        ESP_LOGE(TAG_SNTP, "Failed to update system time within timeout");
        // Aufräumen
        esp_netif_sntp_deinit();
        vTaskDelete(NULL);
        return;
    }

   
    time_t now;

    struct tm* tm_local_ptr = (struct tm*) calloc(1,sizeof(struct tm));
    struct tm* tm_utc_ptr = (struct tm*) calloc(1,sizeof(struct tm));

    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    time(&now);

    localtime_r(&now, tm_local_ptr);
    gmtime_r(&now, tm_utc_ptr);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", tm_local_ptr);
    ESP_LOGI(TAG_SNTP, "Zeit gesetzt auf: %s", buf);

    time_data_t* time_struct = calloc(1, sizeof(*time_struct));

    time_struct->time_local = tm_local_ptr;
    time_struct->time_utc = tm_utc_ptr;

    if (out_q_print) {
        if (xQueueSend(out_q_print, &time_struct, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG_SNTP, "time-Queue voll, Wert nicht gesendet.");
            //free(park->open_from); free(park->closed_from); free(park);
        }
    } else {
        //free(park->open_from); free(park->closed_from); free(park);
    }

    if (out_q_rtc) {
        if (xQueueSend(out_q_rtc, &time_struct, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG_SNTP, "time-Queue voll, Wert nicht gesendet.");
            //free(park->open_from); free(park->closed_from); free(park);
        }
    } else {
        //free(park->open_from); free(park->closed_from); free(park);
    }

    esp_netif_sntp_deinit();
    vTaskDelete(NULL);
}


// -----------------------------------------------------------------------------
// Public Start-Funktionen
// -----------------------------------------------------------------------------
void start_sntp_task(QueueHandle_t out_queue_print, QueueHandle_t out_queue_rtc)
{
    void** pack = calloc(2, sizeof(void*));
    pack[0] = (void*)out_queue_print;
    pack[1] = (void*)out_queue_rtc;

    xTaskCreate(sntp_task, "sntp_task", 8192, pack, 5, NULL);
}

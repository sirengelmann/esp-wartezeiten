// ==============================================
// File: main/main.c
// ==============================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

#include "coaster_types.h"
#include "wifi_conn.h"
#include "api_client.h"
#include "sntp_client.h"
#include "print_task.h"
#include "rtc_task.h"
#include "sd_config.h"



#define PARK_ID          "30816cc0-aedb-4bfc-a180-b269a3a2f31d"
#define TARGET_RIDE_NAME "Voltron Nevera"
#define RTC_ALERT_GPIO   GPIO_NUM_7
#define WAKE_INTERVAL_HOURS   0
#define WAKE_INTERVAL_MINUTES 1
#define REFRESH_HOUR          4
#define REFRESH_MINUTE        0

/* Queues */
static QueueHandle_t q_coaster;
static QueueHandle_t q_park;
static QueueHandle_t q_time_print;
static QueueHandle_t q_time_rtc;
static SemaphoreHandle_t s_display_done;
static QueueHandle_t q_status_summary;
static const char *TAG_MAIN = "app_main";

static bool is_refresh_time(const struct tm *tm_local)
{
    if (!tm_local) return false;
    return (tm_local->tm_hour == REFRESH_HOUR) && (tm_local->tm_min == REFRESH_MINUTE);
}

static void enqueue_time_for_print(time_data_t *time_data)
{
    if (!time_data) return;
    if (q_time_print) {
        xQueueOverwrite(q_time_print, &time_data);
    }
}





void app_main(void)
{
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    bool woke_from_rtc_alert = (wake_cause == ESP_SLEEP_WAKEUP_EXT0);

    /* --- Delay to make flashing over USB possible (device has to be awake) --- */
    if (!woke_from_rtc_alert) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    q_coaster       = xQueueCreate(1, sizeof(coaster_data_t*));
    q_park          = xQueueCreate(1, sizeof(park_data_t*));
    q_time_print    = xQueueCreate(1, sizeof(time_data_t*));
    q_time_rtc      = xQueueCreate(1, sizeof(time_data_t*));
    s_display_done  = xSemaphoreCreateBinary();
    q_status_summary = xQueueCreate(1, sizeof(status_summary_t));
    configASSERT(q_coaster && q_park && q_time_print && q_time_rtc && s_display_done && q_status_summary);

    sd_wifi_config_t sd_cfg = {0};
    if (sd_config_load_wifi(&sd_cfg)) {
        wifi_conn_set_credentials(sd_cfg.ssid, sd_cfg.password);
    }

    wifi_conn_init();
    wifi_conn_start();

    ESP_ERROR_CHECK(rtc_setup_wakeup_source_gpio(RTC_ALERT_GPIO));

    bool run_sntp_this_wake = false;
    time_data_t *rtc_time = NULL;

    if (woke_from_rtc_alert) {
        esp_err_t rtc_err = rtc_read_current_time(&rtc_time);
        if (rtc_err == ESP_OK && rtc_time) {
            if (is_refresh_time(rtc_time->time_local)) {
                run_sntp_this_wake = true;
                free(rtc_time->time_local);
                free(rtc_time->time_utc);
                free(rtc_time);
                rtc_time = NULL;
            } else {
                enqueue_time_for_print(rtc_time);
            }
        } else {
            ESP_LOGW(TAG_MAIN, "rtc_read_current_time failed: %s", esp_err_to_name(rtc_err));
        }
    } else {
        
        run_sntp_this_wake = true;
    }

    if (run_sntp_this_wake) {
        start_sntp_task(q_time_print, q_time_rtc);
        start_rtc_task(q_time_rtc);
    }

    start_fetch_waitingtimes_api_task(q_coaster, PARK_ID, TARGET_RIDE_NAME);
    start_fetch_openingtimes_api_task(q_park,    PARK_ID);
    start_print_task(q_coaster, q_park, q_time_print, TARGET_RIDE_NAME, s_display_done, q_status_summary);

    if (s_display_done) {
        if (xSemaphoreTake(s_display_done, pdMS_TO_TICKS(30000)) != pdTRUE) {
            ESP_LOGW(TAG_MAIN, "display task completion timed out");
        }
    }

    status_summary_t summary = {0};
    bool have_summary = false;
    if (q_status_summary) {
        have_summary = (xQueueReceive(q_status_summary, &summary, pdMS_TO_TICKS(1000)) == pdTRUE);
    }

    esp_err_t sched_err = ESP_OK;
    if (have_summary) {
        bool coaster_open = (summary.coaster_status == COASTER_OPENED) || (summary.coaster_status == COASTER_VIRTUALQUEUE);
        bool park_closed = difftime(summary.t_current, summary.t_closed_from) >= 0;
        bool before_open = difftime(summary.t_current, summary.t_open_from) < 0;

        if (park_closed && !coaster_open) {
            // Sleep until next refresh time (next occurrence of REFRESH_HOUR:REFRESH_MINUTE).
            sched_err = rtc_schedule_alarm_time_of_day(REFRESH_HOUR, REFRESH_MINUTE);
            ESP_LOGI(TAG_MAIN, "Park closed and coaster not open; scheduling refresh wake at %02d:%02d", REFRESH_HOUR, REFRESH_MINUTE);
        } else if (before_open) {
            struct tm *tm_open = localtime(&summary.t_open_from);
            if (tm_open) {
                sched_err = rtc_schedule_alarm_time_of_day((uint8_t)tm_open->tm_hour, (uint8_t)tm_open->tm_min);
                ESP_LOGI(TAG_MAIN, "Before park opening; scheduling wake at %02d:%02d", tm_open->tm_hour, tm_open->tm_min);
            } else {
                sched_err = rtc_schedule_next_alarm(WAKE_INTERVAL_HOURS, WAKE_INTERVAL_MINUTES);
            }
        } else {
            sched_err = rtc_schedule_next_alarm(WAKE_INTERVAL_HOURS, WAKE_INTERVAL_MINUTES);
        }
    } else {
        sched_err = rtc_schedule_next_alarm(WAKE_INTERVAL_HOURS, WAKE_INTERVAL_MINUTES);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(sched_err);
    free(sd_cfg.ssid);
    free(sd_cfg.password);
    esp_deep_sleep_start();
}

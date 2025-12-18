// ==============================================
// File: main/rtc_task.h
// ==============================================
#pragma once
#include <time.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/queue.h"
#include "sntp_client.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void start_rtc_task(QueueHandle_t in_queue_time);

esp_err_t rtc_setup_alarm_mode(i2c_master_dev_handle_t rtc_dev_handle);
esp_err_t rtc_set_alarm_time_of_day(i2c_master_dev_handle_t rtc_dev_handle, const struct tm *tm_alarm);
esp_err_t rtc_setup_wakeup_source_gpio(gpio_num_t gpio_num);
esp_err_t rtc_read_current_time(time_data_t **out_time);
esp_err_t rtc_schedule_next_alarm(uint8_t add_hours, uint8_t add_minutes);
esp_err_t rtc_schedule_alarm_time_of_day(uint8_t hour, uint8_t minute);



#ifdef __cplusplus
}
#endif

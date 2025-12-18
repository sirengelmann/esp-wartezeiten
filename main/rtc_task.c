// ==============================================
// File: main/rtc_task.c
// ==============================================
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"

#include "pcf85263a_regs.h"


#include "sntp_client.h"

static const char* TAG_RTC = "rtc_task";



uint8_t bcd2bin(uint8_t in)
{
    return 10 * ((in>>4)&0x0F) + (in&0x0F);
}

void buffer_to_struct_tm(uint8_t *buffer, struct tm *tmstruct)
{
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!tmstruct || !buffer) return;
    tmstruct->tm_sec = (int) bcd2bin(buffer[1] & 0x7F);
    tmstruct->tm_min = (int) bcd2bin(buffer[2] & 0x7F);
    tmstruct->tm_hour = (int) bcd2bin(buffer[3] & 0x3F);
    tmstruct->tm_mday = (int) bcd2bin(buffer[4] & 0x3F);
    /* Month uses 5 bits; mask 0x1F so the upper BCD nibble is preserved (else months 10-12 break). */
    tmstruct->tm_mon = (int) bcd2bin(buffer[6] & 0x1F) - 1;
    tmstruct->tm_year = (int) 100 + bcd2bin(buffer[7]);
    tmstruct->tm_wday = (int) bcd2bin(buffer[5] & 0x07);
    int cumsum_result = 0;
    for(uint8_t mon = 0; mon < (uint8_t)tmstruct->tm_mon; mon++) cumsum_result += daysInMonth[mon];
    if ((uint8_t)tmstruct->tm_mon > 1 && !(((uint8_t)tmstruct->tm_year) & 0x03)) cumsum_result += 1;
    tmstruct->tm_yday = cumsum_result;
}


uint8_t bin2bcd(uint8_t in)
{
    return (uint8_t)(((in / 10) << 4) | (in % 10));
}

static esp_err_t rtc_create_handles(i2c_master_bus_handle_t *bus_handle_out, i2c_master_dev_handle_t *dev_handle_out)
{
    if (!bus_handle_out || !dev_handle_out) return ESP_ERR_INVALID_ARG;

    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = (i2c_port_num_t)-1, // auto
        .sda_io_num = (gpio_num_t)5,
        .scl_io_num = (gpio_num_t)4,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = (uint8_t)7,
        .intr_priority = (int)0, // auto
        .trans_queue_depth = (size_t)0,
    };
    i2c_bus_cfg.flags.enable_internal_pullup = 0;
    i2c_bus_cfg.flags.allow_pd = 0;

    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t err = i2c_new_master_bus((const i2c_master_bus_config_t*)&i2c_bus_cfg, &bus_handle);
    if (err != ESP_OK) return err;

    i2c_device_config_t rtc_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)PCF85263A_I2C_ADDR,
        .scl_speed_hz = (uint32_t)100000,
        .scl_wait_us = (uint32_t)0,
    };
    rtc_dev_cfg.flags.disable_ack_check = 0;

    i2c_master_dev_handle_t rtc_dev_handle = NULL;
    err = i2c_master_bus_add_device(bus_handle, (const i2c_device_config_t*)&rtc_dev_cfg, &rtc_dev_handle);
    if (err != ESP_OK) {
        i2c_del_master_bus(bus_handle);
        return err;
    }

    *bus_handle_out = bus_handle;
    *dev_handle_out = rtc_dev_handle;
    return ESP_OK;
}

static void rtc_destroy_handles(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t dev_handle)
{
    if (dev_handle) {
        i2c_master_bus_rm_device(dev_handle);
    }
    if (bus_handle) {
        i2c_del_master_bus(bus_handle);
    }
}

void struct_tm_to_buffer(const struct tm *tmstruct, uint8_t *buffer)
{
    if (!tmstruct || !buffer) return;

    buffer[0] = 0x00;/* 100th seconds – not provided by struct tm */
    buffer[1] = bin2bcd((uint8_t)(tmstruct->tm_sec));
    buffer[2] = bin2bcd((uint8_t)(tmstruct->tm_min));
    buffer[3] = bin2bcd((uint8_t)(tmstruct->tm_hour));
    buffer[4] = bin2bcd((uint8_t)(tmstruct->tm_mday));
    buffer[5] = bin2bcd((uint8_t)(tmstruct->tm_wday));
    buffer[6] = bin2bcd((uint8_t)(tmstruct->tm_mon + 1));
    buffer[7] = bin2bcd((uint8_t)(tmstruct->tm_year - 100));
}

static esp_err_t rtc_read_reg(i2c_master_dev_handle_t rtc_dev_handle, uint8_t reg, uint8_t *val)
{
    if (!rtc_dev_handle || !val) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(rtc_dev_handle, &reg, 1, val, 1, 10 /*ms*/);
}

static esp_err_t rtc_write_reg(i2c_master_dev_handle_t rtc_dev_handle, uint8_t reg, uint8_t val)
{
    if (!rtc_dev_handle) return ESP_ERR_INVALID_ARG;
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = val;
    return i2c_master_transmit(rtc_dev_handle, buf, sizeof(buf), 10 /*ms*/);
}

static esp_err_t rtc_write_regs(i2c_master_dev_handle_t rtc_dev_handle, uint8_t start_reg, const uint8_t *data, size_t len)
{
    if (!rtc_dev_handle || !data || len == 0) return ESP_ERR_INVALID_ARG;
    uint8_t *buf = (uint8_t *)calloc(1, len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    buf[0] = start_reg;
    memcpy(&buf[1], data, len);
    esp_err_t err = i2c_master_transmit(rtc_dev_handle, buf, len + 1, 10 /*ms*/);
    free(buf);
    return err;
}

esp_err_t rtc_setup_alarm_mode(i2c_master_dev_handle_t rtc_dev_handle)
{
    if (!rtc_dev_handle) return ESP_ERR_INVALID_ARG;

    esp_err_t err;

    uint8_t pin_io = 0;
    err = rtc_read_reg(rtc_dev_handle, PCF85263A_REG_PIN_IO, &pin_io);
    if (err != ESP_OK) return err;
    pin_io &= (uint8_t)~0x03;
    pin_io |= (uint8_t)0x02; /* INTAPM[1:0] = 10 -> INTA output */
    err = rtc_write_reg(rtc_dev_handle, PCF85263A_REG_PIN_IO, pin_io);
    if (err != ESP_OK) return err;

    uint8_t inta_en = 0;
    err = rtc_read_reg(rtc_dev_handle, PCF85263A_REG_INTA_ENABLE, &inta_en);
    if (err != ESP_OK) return err;
    inta_en |= (uint8_t)(1 << 7); /* ILPA = 1 -> level mode (follows flags) */
    inta_en |= (uint8_t)(1 << 3); /* A2IEA = 1 -> alarm2 interrupt enable on INTA */
    err = rtc_write_reg(rtc_dev_handle, PCF85263A_REG_INTA_ENABLE, inta_en);
    if (err != ESP_OK) return err;

    err = rtc_write_reg(rtc_dev_handle, PCF85263A_REG_FLAGS, (uint8_t)~(1 << 6)); /* clear A2F */
    if (err != ESP_OK) return err;

    uint8_t alarm_en = 0;
    err = rtc_read_reg(rtc_dev_handle, PCF85263A_REG_ALARM_ENABLES, &alarm_en);
    if (err != ESP_OK) return err;
    alarm_en &= (uint8_t)~(1 << 7); /* WDAY_A2E = 0 -> ignore weekday for time-of-day alarm */
    alarm_en |= (uint8_t)(1 << 6);  /* HR_A2E = 1 */
    alarm_en |= (uint8_t)(1 << 5);  /* MIN_A2E = 1 */
    err = rtc_write_reg(rtc_dev_handle, PCF85263A_REG_ALARM_ENABLES, alarm_en);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t rtc_set_alarm_time_of_day(i2c_master_dev_handle_t rtc_dev_handle, const struct tm *tm_alarm)
{
    if (!rtc_dev_handle || !tm_alarm) return ESP_ERR_INVALID_ARG;

    uint8_t alarm2_buf[2];
    alarm2_buf[0] = (uint8_t)(bin2bcd((uint8_t)tm_alarm->tm_min) & 0x7F);
    alarm2_buf[1] = (uint8_t)(bin2bcd((uint8_t)tm_alarm->tm_hour) & 0x3F);
    esp_err_t err = rtc_write_regs(rtc_dev_handle, PCF85263A_REG_MINUTE_ALARM2, alarm2_buf, sizeof(alarm2_buf));
    if (err != ESP_OK) return err;

    err = rtc_write_reg(rtc_dev_handle, PCF85263A_REG_FLAGS, (uint8_t)~(1 << 6)); /* clear A2F */
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t rtc_setup_wakeup_source_gpio(gpio_num_t gpio_num)
{
    if (!rtc_gpio_is_valid_gpio(gpio_num)) return ESP_ERR_INVALID_ARG;

    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << gpio_num);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    err = rtc_gpio_pullup_dis(gpio_num);
    if (err != ESP_OK) return err;
    err = rtc_gpio_pulldown_dis(gpio_num);
    if (err != ESP_OK) return err;

    return esp_sleep_enable_ext0_wakeup(gpio_num, 0 /*active LOW*/);
}

esp_err_t rtc_read_current_time(time_data_t **out_time)
{
    if (!out_time) return ESP_ERR_INVALID_ARG;
    *out_time = NULL;

    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_dev_handle_t rtc_dev_handle = NULL;
    esp_err_t err = rtc_create_handles(&bus_handle, &rtc_dev_handle);
    if (err != ESP_OK) {
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return err;
    }

    uint8_t read_buf[8] = {0};
    uint8_t startaddr = PCF85263A_REG_100TH_SECONDS;
    err = i2c_master_transmit_receive(rtc_dev_handle, &startaddr, 1, read_buf, sizeof(read_buf), 10 /*ms*/);
    if (err != ESP_OK) {
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return err;
    }

    struct tm *tm_local = (struct tm *)calloc(1, sizeof(struct tm));
    if (!tm_local) {
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return ESP_ERR_NO_MEM;
    }
    buffer_to_struct_tm(read_buf, tm_local);

    time_t now = mktime(tm_local);
    struct tm *tm_utc = (struct tm *)calloc(1, sizeof(struct tm));
    if (!tm_utc) {
        free(tm_local);
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return ESP_ERR_NO_MEM;
    }
    gmtime_r(&now, tm_utc);

    time_data_t *time_data = (time_data_t *)calloc(1, sizeof(time_data_t));
    if (!time_data) {
        free(tm_local);
        free(tm_utc);
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return ESP_ERR_NO_MEM;
    }
    time_data->time_local = tm_local;
    time_data->time_utc = tm_utc;
    *out_time = time_data;

    rtc_destroy_handles(bus_handle, rtc_dev_handle);
    return ESP_OK;
}

esp_err_t rtc_schedule_next_alarm(uint8_t add_hours, uint8_t add_minutes)
{
    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_dev_handle_t rtc_dev_handle = NULL;
    esp_err_t err = rtc_create_handles(&bus_handle, &rtc_dev_handle);
    if (err != ESP_OK) {
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return err;
    }

    uint8_t read_buf[8] = {0};
    uint8_t startaddr = PCF85263A_REG_100TH_SECONDS;
    err = i2c_master_transmit_receive(rtc_dev_handle, &startaddr, 1, read_buf, sizeof(read_buf), 10 /*ms*/);
    if (err != ESP_OK) {
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return err;
    }

    struct tm tm_now = {0};
    buffer_to_struct_tm(read_buf, &tm_now);
    time_t now = mktime(&tm_now);
    now += (time_t)add_hours * 3600 + (time_t)add_minutes * 60;

    struct tm tm_next = {0};
    localtime_r(&now, &tm_next);

    err = rtc_setup_alarm_mode(rtc_dev_handle);
    if (err == ESP_OK) {
        err = rtc_set_alarm_time_of_day(rtc_dev_handle, &tm_next);
    }

    rtc_destroy_handles(bus_handle, rtc_dev_handle);
    return err;
}

esp_err_t rtc_schedule_alarm_time_of_day(uint8_t hour, uint8_t minute)
{
    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_dev_handle_t rtc_dev_handle = NULL;
    esp_err_t err = rtc_create_handles(&bus_handle, &rtc_dev_handle);
    if (err != ESP_OK) {
        rtc_destroy_handles(bus_handle, rtc_dev_handle);
        return err;
    }

    struct tm tm_alarm = {0};
    tm_alarm.tm_hour = hour;
    tm_alarm.tm_min = minute;

    err = rtc_setup_alarm_mode(rtc_dev_handle);
    if (err == ESP_OK) {
        err = rtc_set_alarm_time_of_day(rtc_dev_handle, &tm_alarm);
    }

    rtc_destroy_handles(bus_handle, rtc_dev_handle);
    return err;
}



// -----------------------------------------------------------------------------
// Tasks
// -----------------------------------------------------------------------------

// Task: rtc_task ()
static void rtc_task(void* arg)
{
    void** pack = (void**)arg;
    QueueHandle_t in_q_time = (QueueHandle_t)pack[0];
    free(pack);
    
    vTaskDelay( 3000 / portTICK_PERIOD_MS);
    
    // Use shared helper to create I2C handles
    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_dev_handle_t rtc_dev_handle = NULL;
    ESP_ERROR_CHECK(rtc_create_handles(&bus_handle, &rtc_dev_handle));

    ESP_LOGI(TAG_RTC, "rtc chip zu i2c bus hinzugefuegt");

    uint8_t read_buf[8] = {0};
    uint8_t startaddr = PCF85263A_REG_100TH_SECONDS;


    struct tm *tm_struct = (struct tm *) calloc(1, sizeof(struct tm));


    ESP_ERROR_CHECK(i2c_master_transmit_receive(rtc_dev_handle, &startaddr, 1, read_buf, sizeof(read_buf), 10 /*ms*/));
    //ESP_LOGI(TAG_RTC, "Sekunde aktuell: %d%d s, Hundertstel Sekunden aktuell: %d%d ms", (read_buf[1]>>4)&0x07, read_buf[1]&0x0F, (read_buf[0]>>4)&0x0F, (read_buf[0]&0x0F));
    buffer_to_struct_tm(read_buf, tm_struct);
    char printfbuf[80];
    strftime(printfbuf, 80, "Heutiges Datum: %d.%m.%Y, Uhrzeit: %H:%M:%S", tm_struct);
    ESP_LOGI(TAG_RTC, "%s", printfbuf);


    time_data_t* time_data = NULL;
    if (xQueueReceive(in_q_time, &time_data, portMAX_DELAY) == pdTRUE) {
        struct tm* tm_local_ptr = time_data->time_local;
        uint8_t write_buf[9];
        write_buf[0] = PCF85263A_REG_100TH_SECONDS;
        struct_tm_to_buffer(tm_local_ptr, &write_buf[1]);
        ESP_ERROR_CHECK(i2c_master_transmit(rtc_dev_handle, write_buf, sizeof(write_buf), 10 /*ms*/));
        ESP_LOGI(TAG_RTC, "rtc zeit via sntp gesetzt");
    } else {
        printf("Fehler: nichts aus time Queue empfangen.\n");
        vTaskDelete(NULL);
        return;
    }
    // Anything below here (e.g., looping) not needed currently.



    rtc_destroy_handles(bus_handle, rtc_dev_handle);
    vTaskDelete(NULL);
}

// -----------------------------------------------------------------------------
// Public Start-Funktionen
// -----------------------------------------------------------------------------
void start_rtc_task(QueueHandle_t in_queue_time)
{
    void** pack = calloc(1, sizeof(void*));
    pack[0] = (void*)in_queue_time;
    xTaskCreate(rtc_task, "rtc_task", 8192, pack, 5, NULL);
}

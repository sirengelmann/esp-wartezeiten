#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h" 
#include "driver/gpio.h"

#include "coaster_types.h"
#include "print_task.h"
#include "sntp_client.h"

#include "lvgl.h"

#include "EPD_1in54_V2.h"
#include "DEV_Config.h"

typedef struct {
    QueueHandle_t q_coaster;
    QueueHandle_t q_park;
    QueueHandle_t q_time;
    const char *target_ride_name;
    SemaphoreHandle_t done_sem;
    QueueHandle_t q_status_summary;
} print_task_ctx_t;

static print_task_ctx_t s_ctx;



/* ------- LVGL Display ------- */
#define HOR_RES 200
#define VER_RES 200
/* v9-Flush: kopiert 1bpp-Block direkt in 1bpp-Framebuffer */
/* 1) draw_mem: +8 Bytes für Palette */

#define WORK_ROWS 10
#define STRIDE_BYTES  ((HOR_RES + 7) >> 3)

static uint8_t       draw_mem[STRIDE_BYTES * WORK_ROWS + 8];
static lv_draw_buf_t draw_buf;

/* 1-Bit-Framebuffer: 200*200/8 = 5000 Bytes (MSB = linkstes Pixel) */
static uint8_t framebuffer_1bpp[(HOR_RES * VER_RES) >> 3];


#if LV_COLOR_DEPTH != 1
#error "Bitte LV_COLOR_DEPTH in lv_conf.h auf 1 setzen (monochrom, 1 bpp)."
#endif


LV_FONT_DECLARE(roboto_96);//lv_font_montserrat_48);

LV_IMAGE_DECLARE(EP_logo_bw);
LV_IMAGE_DECLARE(voltron_nevera_logo_160);
LV_IMAGE_DECLARE(cloud_96);
LV_IMAGE_DECLARE(lock_96);
LV_IMAGE_DECLARE(snowflake_96);
LV_IMAGE_DECLARE(ticket_96);
LV_IMAGE_DECLARE(wrench_96);



/* 2) Flush: Palette überspringen + memcpy pro Zeile */
static void my_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    const int32_t x1 = area->x1;
    const int32_t y1 = area->y1;
    const int32_t w  = area->x2 - area->x1 + 1;
    const int32_t h  = area->y2 - area->y1 + 1;

    const uint32_t src_stride = STRIDE_BYTES;               // so konfiguriert in lv_draw_buf_init
    const uint32_t dst_stride = STRIDE_BYTES;

    px_map += 8;  // *** Palette überspringen ***

    // LVGL garantiert Byte-Ausrichtung (x1 % 8 == 0, w % 8 == 0) -> rein byteweise kopieren
    for (int32_t yy = 0; yy < h; yy++) {
        const uint8_t *src_row = px_map + (size_t)yy * src_stride + (x1 >> 3);
        uint8_t *dst_row = framebuffer_1bpp + (size_t)(y1 + yy) * dst_stride + (x1 >> 3);
        memcpy(dst_row, src_row, (size_t)(w >> 3));
    }

    lv_display_flush_ready(disp);
}



static void print_task(void *arg)
{
    print_task_ctx_t *ctx = (print_task_ctx_t *)arg;
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }

    time_t t_current;
    time_t t_open_from;
    time_t t_closed_from;

    const char *ride_name = ctx->target_ride_name ? ctx->target_ride_name : "Unbekannt";

    /* Ensure local time calculations use the expected TZ (same as SNTP task). */
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();



    ESP_ERROR_CHECK(gpio_set_direction(1, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(1, 1));

    vTaskDelay( 100 / portTICK_PERIOD_MS);


    if (DEV_Module_Init() == 0) {
        EPD_1IN54_V2_Init();
        EPD_1IN54_V2_Clear();
    } else {
        printf("Fehler: Konnte Epaper nicht initialisieren.\n");
        vTaskDelete(NULL);
        return;
    }

    coaster_data_t* coaster_data = NULL;
    if (xQueueReceive(ctx->q_coaster, &coaster_data, portMAX_DELAY) == pdTRUE) {
        const char *coaster_name = coaster_data->name ? coaster_data->name : ride_name;
        printf("Wartezeit %s: %d min\n", coaster_name, coaster_data->waitingtime);
        printf("Status Coaster: %s\n", coaster_status_to_string(coaster_data->status));
        free(coaster_data->name);
    } else {
        printf("Fehler: nichts aus Coaster Data Queue empfangen.\n");
        vTaskDelete(NULL);
        return;
    }
    park_data_t* park_data = NULL;
    if (xQueueReceive(ctx->q_park, &park_data, portMAX_DELAY) == pdTRUE) {
        printf("Park heute geöffnet: %s\n", park_data->opened_today ? "Ja" : "Nein");
        printf("Park öffnet: %s\n", park_data->open_from);
        printf("Park schließt: %s\n", park_data->closed_from);
    } else {
        printf("Fehler: nichts aus Park Data Queue empfangen.\n");
        vTaskDelete(NULL);
        return;
    }
    time_data_t* time_data = NULL;
    if (xQueueReceive(ctx->q_time, &time_data, portMAX_DELAY) == pdTRUE) {

        struct tm* tm_local_ptr = time_data->time_local;

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", tm_local_ptr);
        printf("Aktuelle Zeit: %s\n", buf);

        t_current = mktime(tm_local_ptr);
    } else {
        printf("Fehler: nichts aus time Queue empfangen.\n");
        vTaskDelete(NULL);
        return;
    }

    struct tm tm_park_open_from = {0};
    strptime(park_data->open_from, "%Y-%m-%dT%H:%M:%S", &tm_park_open_from);

    struct tm tm_park_closed_from = {0};
    strptime(park_data->closed_from, "%Y-%m-%dT%H:%M:%S", &tm_park_closed_from);

    t_open_from = mktime(&tm_park_open_from);
    t_closed_from = mktime(&tm_park_closed_from);

    double park_open_since_secs = difftime(t_current, t_open_from); // If positive, then park is open
    printf("Park geöffnet seit %f Sekunden\n", park_open_since_secs);

    double park_closed_since_secs = difftime(t_current, t_closed_from); // If positive, then park was open and now is closed
    printf("Park geschlossen seit %f Sekunden\n", park_closed_since_secs);

    if (park_open_since_secs < 0 && park_closed_since_secs < 0)
        printf("Status Park: Noch nicht geöffnet\n");
    else if (park_open_since_secs > 0 && park_closed_since_secs < 0)
        printf("Status Park: Geöffnet\n");
    else if (park_open_since_secs > 0 && park_closed_since_secs > 0)
        printf("Status Park: Wieder geschlossen\n");
    else
        printf("Status Park: Wieder geschlossen, bevor er geöffnet war\n");




    /* --- LVGL setup (v9, 1 bpp) --- */
    




    lv_init();



    memset(framebuffer_1bpp, 0x00, sizeof(framebuffer_1bpp)); // schwarz


    lv_display_t *disp = lv_display_create(HOR_RES, VER_RES);

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);  // 1bpp


    lv_display_set_flush_cb(disp, my_flush);

    /* v9: lv_draw_buf_init(draw_buf, w, h, cf, stride_bytes, data, data_size)
       Farbformat = native (1bpp), Stride = Bytes/Zeile */
    lv_draw_buf_init(&draw_buf,
                     HOR_RES,                            /* w */
                     WORK_ROWS,                          /* h (Pufferhöhe) */
                     LV_COLOR_FORMAT_NATIVE,             /* 1 bpp durch LV_COLOR_DEPTH==1 */
                     (HOR_RES + 7) >> 3,                 /* stride (Bytes/Zeile) */
                     draw_mem,                           /* data */
                     sizeof(draw_mem));                  /* data_size */

    lv_display_set_draw_buffers(disp, &draw_buf, NULL);


    switch (coaster_data->status) {
	case COASTER_OPENED: {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        char buf[10];
        sprintf(buf, "%d", coaster_data->waitingtime);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_font(label, &roboto_96, 0);
        lv_obj_center(label);
    } break;
    case COASTER_VIRTUALQUEUE: {
        lv_obj_t *img_lock = lv_image_create(lv_screen_active());
        lv_image_set_src(img_lock, &ticket_96); // v9-API!
        lv_obj_align(img_lock, LV_ALIGN_CENTER, 0, 0);
    } break;
    case COASTER_MAINTENANCE: {
        lv_obj_t *img_lock = lv_image_create(lv_screen_active());
        lv_image_set_src(img_lock, &wrench_96); // v9-API!
        lv_obj_align(img_lock, LV_ALIGN_CENTER, 0, 0);
    } break;
    case COASTER_CLOSED_ICE: {
        lv_obj_t *img_lock = lv_image_create(lv_screen_active());
        lv_image_set_src(img_lock, &snowflake_96); // v9-API!
        lv_obj_align(img_lock, LV_ALIGN_CENTER, 0, 0);
    } break;
    case COASTER_CLOSED_WEATHER: {
        lv_obj_t *img_lock = lv_image_create(lv_screen_active());
        lv_image_set_src(img_lock, &cloud_96); // v9-API!
        lv_obj_align(img_lock, LV_ALIGN_CENTER, 0, 0);
    } break;
    case COASTER_CLOSED: {
        lv_obj_t *img_lock = lv_image_create(lv_screen_active());
        lv_image_set_src(img_lock, &lock_96); // v9-API!
        lv_obj_align(img_lock, LV_ALIGN_CENTER, 0, 0);
    } break;
	default: {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "-");
        lv_obj_set_style_text_font(label, &roboto_96, 0);
        lv_obj_center(label);
    } break;
}


    // EP-Logo unten mittig
    lv_obj_t *img_logo_ep = lv_image_create(lv_screen_active());
    lv_image_set_src(img_logo_ep, &EP_logo_bw);                 // v9-API!
    lv_obj_align(img_logo_ep, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Voltron-Logo oben mittig
    lv_obj_t *img_logo_voltron = lv_image_create(lv_screen_active());
    lv_image_set_src(img_logo_voltron, &voltron_nevera_logo_160); // v9-API!
    lv_obj_align(img_logo_voltron, LV_ALIGN_TOP_MID, 0, 0);


    /* exakt EIN Frame rendern */
    lv_obj_invalidate(lv_screen_active());
    lv_timer_handler();

    /* --- E-Paper-Ausgabe: nimm direkt den 1bpp-Framebuffer --- */
    EPD_1IN54_V2_Display(framebuffer_1bpp);

    if (ctx->q_status_summary) {
        status_summary_t summary = {
            .coaster_status = coaster_data->status,
            .park_opened_today = park_data->opened_today,
            .t_current = t_current,
            .t_open_from = t_open_from,
            .t_closed_from = t_closed_from,
        };
        (void)xQueueOverwrite(ctx->q_status_summary, &summary);
    }

    if (ctx->done_sem) {
        xSemaphoreGive(ctx->done_sem);
    }

    vTaskDelete(NULL);
}

void start_print_task(QueueHandle_t q_coaster,
                      QueueHandle_t q_park,
                      QueueHandle_t q_time,
                      const char *target_ride_name,
                      SemaphoreHandle_t done_sem,
                      QueueHandle_t q_status_summary)
{
    s_ctx.q_coaster = q_coaster;
    s_ctx.q_park = q_park;
    s_ctx.q_time = q_time;
    s_ctx.target_ride_name = target_ride_name;
    s_ctx.done_sem = done_sem;
    s_ctx.q_status_summary = q_status_summary;

    xTaskCreate(print_task, "print_task", 4096, &s_ctx, 5, NULL);
}

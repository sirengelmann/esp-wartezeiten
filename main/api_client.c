// ==============================================
// File: main/api_client.c
// ==============================================
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "coaster_types.h"  // enthält: coaster_data_t, park_data_t, coaster_status_from_string(...)
#include "wifi_conn.h"      // enthält: bool wifi_conn_wait_ip(TickType_t timeout_ticks)

static const char* TAG_API = "api_client";

#define API_OPENINGTIMES_URL   "https://api.wartezeiten.app/v1/openingtimes"
#define API_WAITINGTIMES_URL   "https://api.wartezeiten.app/v1/waitingtimes"
#define LANGUAGE               "de"

// -----------------------------------------------------------------------------
// HTTP Body-Buffer + Event-Handler (chunked-fähig)
// -----------------------------------------------------------------------------
typedef struct {
    char *buf;
    int   cap;
    int   len;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *hb = (http_buf_t*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && hb && evt->data_len > 0) {
        if (hb->len + evt->data_len + 1 > hb->cap) {
            int newcap = hb->cap ? hb->cap * 2 : 2048;
            while (newcap < hb->len + evt->data_len + 1) newcap *= 2;
            char *nb = realloc(hb->buf, newcap);
            if (!nb) return ESP_ERR_NO_MEM;
            hb->buf = nb; hb->cap = newcap;
        }
        memcpy(hb->buf + hb->len, evt->data, evt->data_len);
        hb->len += evt->data_len;
        hb->buf[hb->len] = '\0';
    }
    return ESP_OK;
}

// Kleiner Helfer: GET ausführen und Body in http_buf_t sammeln
static bool http_get_to_buf(const char* url,
                            const char* park_id_header,   // NULL wenn keiner
                            http_buf_t* hb,
                            int timeout_ms,
                            int* out_status)
{
    if (!hb) return false;
    *out_status = -1;
    *hb = (http_buf_t){0};

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = hb,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_http_client_set_header(client, "accept", "application/json");
    esp_http_client_set_header(client, "language", LANGUAGE);
    if (park_id_header) esp_http_client_set_header(client, "park", park_id_header);
    // Falls nötig: Identität statt gzip
    // esp_http_client_set_header(client, "Accept-Encoding", "identity");

    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || *out_status < 200 || *out_status >= 300 || hb->len <= 0) {
        ESP_LOGE(TAG_API, "HTTP fehlgeschlagen: %s, status=%d, len=%d",
                 esp_err_to_name(err), *out_status, hb->len);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// JSON-Helfer
// -----------------------------------------------------------------------------
static cJSON* json_find_item_by_name(cJSON* cJSON_root, const char *name)
{
    cJSON* cJSON_arr = NULL;
    if (cJSON_IsArray(cJSON_root)) cJSON_arr = cJSON_root; else if (cJSON_IsObject(cJSON_root)) {
        cJSON_arr = cJSON_GetObjectItem(cJSON_root, "data");
        if (!cJSON_IsArray(cJSON_arr)) cJSON_arr = cJSON_GetObjectItem(cJSON_root, "items");
        if (!cJSON_IsArray(cJSON_arr)) cJSON_arr = cJSON_GetObjectItem(cJSON_root, "results");
        if (!cJSON_IsArray(cJSON_arr)) {
            cJSON* cJSON_name = cJSON_GetObjectItem(cJSON_root, "name");
            if (cJSON_IsString(cJSON_name) && strcasecmp(cJSON_name->valuestring, name) == 0) return cJSON_root;
        }
    }
    if (!cJSON_IsArray(cJSON_arr)) return NULL;

    cJSON* cJSON_item = NULL;
    cJSON_ArrayForEach(cJSON_item, cJSON_arr) {
        if (!cJSON_IsObject(cJSON_item)) continue;
        cJSON* cJSON_name = cJSON_GetObjectItem(cJSON_item, "name");
        if (cJSON_IsString(cJSON_name) && strcasecmp(cJSON_name->valuestring, name) == 0) return cJSON_item;
    }
    return NULL;
}

// Deine Art, Strings in Strukturen zu schreiben (mit realloc etc.) bleibt exakt so:
static bool json_populate_coaster_data(cJSON* cJSON_coaster, coaster_data_t* coaster_data)
{
    if (!cJSON_coaster || !coaster_data) return false;

    cJSON* cJSON_waitingtime = cJSON_GetObjectItem(cJSON_coaster, "waitingtime");
    cJSON* cJSON_status      = cJSON_GetObjectItem(cJSON_coaster, "status");
    cJSON* cJSON_name        = cJSON_GetObjectItem(cJSON_coaster, "name");

    if (cJSON_IsNumber(cJSON_waitingtime))
        coaster_data->waitingtime = cJSON_waitingtime->valueint;

    coaster_data->status = cJSON_IsString(cJSON_status)
                         ? coaster_status_from_string(cJSON_status->valuestring)
                         : COASTER_UNKNOWN;

    if (cJSON_IsString(cJSON_name))
    {
        if (!coaster_data->name)
        {
            coaster_data->name = (char*) malloc( strlen(cJSON_name->valuestring) + 1 );
            if (!coaster_data->name)
                return false;
        }
        else if (strlen(coaster_data->name) != strlen(cJSON_name->valuestring))
        {
            coaster_data->name = (char*) realloc( (void*) coaster_data->name, strlen(cJSON_name->valuestring) + 1 );
            if (!coaster_data->name)
                return false;
        }
        memcpy ( (void*) coaster_data->name, (const void *) cJSON_name->valuestring, strlen(cJSON_name->valuestring) + 1 );
    }
    return true;
}

static bool json_populate_park_data(cJSON* cJSON_park, park_data_t* park_data)
{
    if (!cJSON_park || !park_data) return false;

    cJSON* cJSON_opened_today = cJSON_GetObjectItem(cJSON_park, "opened_today");
    cJSON* cJSON_open_from    = cJSON_GetObjectItem(cJSON_park, "open_from");
    cJSON* cJSON_closed_from  = cJSON_GetObjectItem(cJSON_park, "closed_from");

    park_data->opened_today = (cJSON_IsTrue(cJSON_opened_today)) ? true : false;

    if (cJSON_IsString(cJSON_open_from))
    {
        if (!park_data->open_from)
        {
            park_data->open_from = (char*) malloc( strlen(cJSON_open_from->valuestring) + 1 );
            if (!park_data->open_from)
                return false;
        }
        else if (strlen(park_data->open_from) != strlen(cJSON_open_from->valuestring))
        {
            park_data->open_from = (char*) realloc( (void*) park_data->open_from, strlen(cJSON_open_from->valuestring) + 1 );
            if (!park_data->open_from)
                return false;
        }
        memcpy ( (void*) park_data->open_from, (const void *) cJSON_open_from->valuestring, strlen(cJSON_open_from->valuestring) + 1 );
    }

    if (cJSON_IsString(cJSON_closed_from))
    {
        if (!park_data->closed_from)
        {
            park_data->closed_from = (char*) malloc( strlen(cJSON_closed_from->valuestring) + 1 );
            if (!park_data->closed_from)
                return false;
        }
        else if (strlen(park_data->closed_from) != strlen(cJSON_closed_from->valuestring))
        {
            park_data->closed_from = (char*) realloc( (void*) park_data->closed_from, strlen(cJSON_closed_from->valuestring) + 1 );
            if (!park_data->closed_from)
                return false;
        }
        memcpy ( (void*) park_data->closed_from, (const void *) cJSON_closed_from->valuestring, strlen(cJSON_closed_from->valuestring) + 1 );
    }

    return true;
}

// -----------------------------------------------------------------------------
// Tasks
// -----------------------------------------------------------------------------

// Task: Waitingtimes (Attraktion suchen, coaster_data_t* zur Queue schicken)
static void waitingtimes_task(void* arg)
{
    void** pack = (void**)arg;
    QueueHandle_t out_q = (QueueHandle_t)pack[0];
    const char*   park_id = (const char*)pack[1];
    const char*   target  = (const char*)pack[2];
    free(pack);

    if (!wifi_conn_wait_ip(pdMS_TO_TICKS(20000))) {
        ESP_LOGE(TAG_API, "Timeout: keine Wi-Fi Verbindung (waitingtimes)");
        vTaskDelete(NULL);
        return;
    }

    http_buf_t http_buf = {0};
    int status = 0;
    ESP_LOGI(TAG_API, "Starte API-Request: %s", API_WAITINGTIMES_URL);

    if (!http_get_to_buf(API_WAITINGTIMES_URL, park_id, &http_buf, 10000, &status)) {
        free(http_buf.buf);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG_API, "Waitingtimes: HTTP Status=%d, empfangen=%d Bytes", status, http_buf.len);

    coaster_data_t* coaster_data = calloc(1, sizeof(*coaster_data));
    if (!coaster_data) { free(http_buf.buf); vTaskDelete(NULL); return; }

    cJSON* cJSON_root = cJSON_Parse(http_buf.buf);
    if (cJSON_root) {
        cJSON *coaster = json_find_item_by_name(cJSON_root, target);
        if (coaster) {
            (void)json_populate_coaster_data(coaster, coaster_data);
        } else {
            ESP_LOGW(TAG_API, "Eintrag '%s' nicht gefunden.", target);
        }
        cJSON_Delete(cJSON_root);
    } else {
        ESP_LOGE(TAG_API, "Waitingtimes JSON parse error");
    }

    if (out_q) {
        if (xQueueSend(out_q, &coaster_data, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG_API, "Coaster-Queue voll, Wert nicht gesendet.");
            free(coaster_data->name); free(coaster_data);
        }
    } else {
        free(coaster_data->name); free(coaster_data);
    }

    free(http_buf.buf);
    vTaskDelete(NULL);
}

// Task: Openingtimes (Parkdaten holen, park_data_t* zur Queue schicken)
static void openingtimes_task(void* arg)
{
    void** pack = (void**)arg;
    QueueHandle_t out_q = (QueueHandle_t)pack[0];
    const char*   park_id = (const char*)pack[1];
    free(pack);

    if (!wifi_conn_wait_ip(pdMS_TO_TICKS(20000))) {
        ESP_LOGE(TAG_API, "Timeout: keine Wi-Fi Verbindung (openingtimes)");
        vTaskDelete(NULL);
        return;
    }

    http_buf_t hb = {0};
    int status = 0;
    ESP_LOGI(TAG_API, "Starte API-Request: %s", API_OPENINGTIMES_URL);

    if (!http_get_to_buf(API_OPENINGTIMES_URL, park_id, &hb, 10000, &status)) {
        free(hb.buf);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG_API, "Openingtimes: HTTP Status=%d, empfangen=%d Bytes", status, hb.len);

    park_data_t* park = calloc(1, sizeof(*park));
    if (!park) { free(hb.buf); vTaskDelete(NULL); return; }

    cJSON* root = cJSON_Parse(hb.buf);
    if (root) {
        cJSON* obj = NULL;

        if (cJSON_IsObject(root)) {
            obj = root;
        } else if (cJSON_IsArray(root)) {
            // <-- NEU: erstes Element aus dem Array nehmen
            obj = cJSON_GetArrayItem(root, 0);
        } else {
            // Fallback: in bekannten Containern nach Array suchen und erstes Element nehmen
            cJSON* arr = cJSON_GetObjectItem(root, "data");
            if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItem(root, "items");
            if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItem(root, "results");
            if (cJSON_IsArray(arr)) obj = cJSON_GetArrayItem(arr, 0);
        }

        if (cJSON_IsObject(obj)) {
            if (!json_populate_park_data(obj, park)) {
                ESP_LOGW(TAG_API, "json_populate_park_data fehlgeschlagen");
            }
        } else {
            ESP_LOGW(TAG_API, "Openingtimes: kein Objekt gefunden");
            // Optional einmalig zur Diagnose:
            // ESP_LOGI(TAG_API, "Payload: %.*s", hb.len, hb.buf);
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG_API, "Openingtimes JSON parse error");
    }


    if (out_q) {
        if (xQueueSend(out_q, &park, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG_API, "Park-Queue voll, Wert nicht gesendet.");
            free(park->open_from); free(park->closed_from); free(park);
        }
    } else {
        free(park->open_from); free(park->closed_from); free(park);
    }

    free(hb.buf);
    vTaskDelete(NULL);
}

// -----------------------------------------------------------------------------
// Public Start-Funktionen
// -----------------------------------------------------------------------------
void start_fetch_waitingtimes_api_task(QueueHandle_t out_queue_coaster,
                          const char* park_id,
                          const char* target_ride_name)
{
    void** pack = calloc(3, sizeof(void*));
    pack[0] = (void*)out_queue_coaster;
    pack[1] = (void*)park_id;
    pack[2] = (void*)target_ride_name;
    xTaskCreate(waitingtimes_task, "waitingtimes_task", 8192, pack, 5, NULL);
}

void start_fetch_openingtimes_api_task(QueueHandle_t out_queue_park,
                                       const char* park_id)
{
    void** pack = calloc(2, sizeof(void*));
    pack[0] = (void*)out_queue_park;
    pack[1] = (void*)park_id;
    xTaskCreate(openingtimes_task, "openingtimes_task", 8192, pack, 5, NULL);
}

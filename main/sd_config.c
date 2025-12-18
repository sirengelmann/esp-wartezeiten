// ==============================================
// File: main/sd_config.c
// ==============================================
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "sd_config.h"
#include <strings.h>

static const char *TAG_SD = "sd_config";

/* GPIO mapping */
#define SD_PWR_EN_GPIO  GPIO_NUM_2
#define SD_CD_GPIO      GPIO_NUM_47  /* Card detect (SW) active LOW */
#define SD_PIN_CLK      GPIO_NUM_39
#define SD_PIN_CMD      GPIO_NUM_40
#define SD_PIN_D0       GPIO_NUM_38
#define SD_PIN_D1       GPIO_NUM_48
#define SD_PIN_D2       GPIO_NUM_42
#define SD_PIN_D3       GPIO_NUM_41

/* Mount point */
#define SD_MOUNT_POINT "/sdcard"
#define SD_CONFIG_FILE SD_MOUNT_POINT"/config.txt"

static void sd_power_on(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SD_PWR_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(SD_PWR_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10)); // let power rail settle
}

static void sd_power_off(void)
{
    gpio_set_level(SD_PWR_EN_GPIO, 0);
}

static bool sd_card_present(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SD_CD_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    int level = gpio_get_level(SD_CD_GPIO);
    // Active LOW: 0 means card inserted.
    return level == 0;
}

static bool parse_line(char *line, char **out_ssid, char **out_pass)
{
    if (!line) return false;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
        line[len - 1] = '\0';
        len--;
    }
    if (len == 0 || line[0] == '#') return false;

    char *eq = strchr(line, '=');
    if (!eq) return false;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1; // keep leading spaces in value (SSID/PASS may contain them)

    // Trim trailing spaces/tabs from key only
    for (int i = (int)strlen(key) - 1; i >= 0; --i) {
        if (key[i] == ' ' || key[i] == '\t') key[i] = '\0'; else break;
    }

    if (strcasecmp(key, "WIFI_SSID") == 0) {
        free(*out_ssid);
        *out_ssid = strdup(val);
        return true;
    } else if (strcasecmp(key, "WIFI_PASS") == 0 || strcasecmp(key, "WIFI_PASSWORD") == 0) {
        free(*out_pass);
        *out_pass = strdup(val);
        return true;
    }
    return false;
}

bool sd_config_load_wifi(sd_wifi_config_t *cfg)
{
    if (!cfg) return false;
    cfg->ssid = NULL;
    cfg->password = NULL;
    bool mounted = false;
    sdmmc_card_t *card = NULL;

    sd_power_on();

    if (!sd_card_present()) {
        ESP_LOGW(TAG_SD, "SD card not detected (card detect low)");
        sd_power_off();
        return false;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0  = SD_PIN_D0;
    slot_config.d1  = SD_PIN_D1;
    slot_config.d2  = SD_PIN_D2;
    slot_config.d3  = SD_PIN_D3;
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.gpio_cd = SDMMC_SLOT_NO_CD;
    slot_config.gpio_wp = SDMMC_SLOT_NO_WP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_SD, "Failed to mount SD card: %s", esp_err_to_name(ret));
        sd_power_off();
        return false;
    }
    mounted = true;

    FILE *f = fopen(SD_CONFIG_FILE, "r");
    if (!f) {
        ESP_LOGW(TAG_SD, "Config file not found: %s", SD_CONFIG_FILE);
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        sd_power_off();
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        (void)parse_line(line, &cfg->ssid, &cfg->password);
    }
    fclose(f);

    if (mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
    }
    sd_power_off();

    if (cfg->ssid && cfg->ssid[0] != '\0') {
        ESP_LOGI(TAG_SD, "Loaded Wi-Fi SSID from SD");
        return true;
    }

    free(cfg->ssid); cfg->ssid = NULL;
    free(cfg->password); cfg->password = NULL;
    return false;
}

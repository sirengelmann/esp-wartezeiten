idf_component_register(SRCS "sd_config.c" "rtc_task.c" "icon_wrench_96.c" "icon_lock_96.c" "icon_ticket_96.c" "icon_snowflake_96.c" "icon_cloud_96.c" "logo_voltron.c" "logo_ep.c" "roboto_96.c" "print_task.c" "sntp_client.c" "main.c" "api_client.c" "wifi_conn.c" "DEV_Config.c" "EPD_1in54_V2.c"
                       REQUIRES esp_psram esp_system esp_event esp_netif esp_wifi nvs_flash esp_timer spi_flash esp_http_client json esp-tls driver lvgl__lvgl fatfs sdmmc
                       INCLUDE_DIRS "")

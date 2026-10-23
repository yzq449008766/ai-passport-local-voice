idf_component_register(
    SRCS "app_main.c"
         "voice_app.c"
         "voice_audio.c"
         "voice_http.c"
         "voice_ui.c"
         "wifi_sta.c"
         "fonts/lv_font_custom_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_http_client esp_netif esp_timer esp_wifi nvs_flash mbedtls
)

// wifi_sta.c —— STA 连接本地路由器（参考 demo_wifi.c 的初始化顺序 + 事件驱动连接/重连）
#include "wifi_sta.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "wifi_sta";

#define WIFI_CONNECTED_BIT BIT0

static bool s_started;
static bool s_connected;
static EventGroupHandle_t s_evt;
static char s_status[48];

static void set_status(const char *s)
{
    strncpy(s_status, s, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        set_status("Connecting...");
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_evt) xEventGroupClearBits(s_evt, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        s_connected = true;
        if (s_evt) xEventGroupSetBits(s_evt, WIFI_CONNECTED_BIT);
        char ipbuf[16];
        esp_ip4addr_ntoa(&ev->ip_info.ip, ipbuf, sizeof(ipbuf));
        snprintf(s_status, sizeof(s_status), "IP %s", ipbuf);
        ESP_LOGI(TAG, "Connected, IP: %s", ipbuf);
    }
}

esp_err_t wifi_sta_start(void)
{
    if (s_started) return ESP_OK;

    if (!s_evt) s_evt = xEventGroupCreate();
    if (!s_evt) return ESP_ERR_NO_MEM;

    // NVS（esp_wifi 需要）
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_FAIL;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              on_wifi_event, NULL, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              on_ip_event, NULL, NULL);
    if (err != ESP_OK) return err;

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, CONFIG_AIPASSPORT_WIFI_SSID,
            sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, CONFIG_AIPASSPORT_WIFI_PASSWORD,
            sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    s_started = true;
    set_status("Wi-Fi starting...");
    return ESP_OK;
}

bool wifi_sta_is_connected(void)
{
    return s_connected;
}

const char *wifi_sta_status_text(void)
{
    return s_status[0] ? s_status : "Wi-Fi idle";
}

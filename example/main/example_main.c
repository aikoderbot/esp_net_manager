#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

// This is the component we are demonstrating
#include "net_manager.h"

static const char *TAG = "NET_EXAMPLE";

/**
 * @brief The central callback for all network events from net_manager.
 */
void net_event_callback_handler(const net_manager_event_t *event)
{
    // This callback function remains the same as the previous version.
    // It is already designed to handle events from any source (STA, AP, ETH) independently.
    // (No changes needed here)
    if (!event)
        return;
    switch (event->source)
    {
    case NET_EVENT_SOURCE_STA:
        ESP_LOGI(TAG, "Wi-Fi Station Event:");
        switch (event->status)
        {
        case NET_STATUS_CONNECTING:
            ESP_LOGI(TAG, "  -> Connecting...");
            break;
        case NET_STATUS_CONNECTED:
            ESP_LOGI(TAG, "  -> Connected! IP: " IPSTR, IP2STR(&((esp_netif_ip_info_t *)event->data)->ip));
            break;
        case NET_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "  -> Disconnected.");
            break;
        default:
            break;
        }
        break;
    case NET_EVENT_SOURCE_AP:
        ESP_LOGI(TAG, "Wi-Fi Access Point Event:");
        switch (event->status)
        {
        case NET_STATUS_STARTED:
            // ESP_LOGI(TAG, "  -> AP Started. SSID: %s, IP: " IPSTR, CONFIG_EXAMPLE_AP_SSID, IP2STR(&((esp_netif_ip_info_t *)event->data)->ip));
            break;
        case NET_STATUS_CLIENT_CONNECTED:
        {
            wifi_event_ap_staconnected_t *client_info = (wifi_event_ap_staconnected_t *)event->data;
            // ESP_LOGI(TAG, "  -> Client " MACSTR " connected, AID=%d", MAC2STR(client_info->mac), client_info->aid);
            break;
        }
        case NET_STATUS_CLIENT_DISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *client_info = (wifi_event_ap_stadisconnected_t *)event->data;
            // ESP_LOGI(TAG, "  -> Client " MACSTR " disconnected, AID=%d", MAC2STR(client_info->mac), client_info->aid);
            break;
        }
        default:
            break;
        }
        break;
    case NET_EVENT_SOURCE_ETHERNET:
        ESP_LOGI(TAG, "Ethernet Event:");
        switch (event->status)
        {
        case NET_STATUS_STARTED:
            ESP_LOGI(TAG, "  -> Started.");
            break;
        case NET_STATUS_CONNECTING:
            ESP_LOGI(TAG, "  -> Link Up, waiting for IP...");
            break;
        case NET_STATUS_CONNECTED:
            ESP_LOGI(TAG, "  -> Connected! IP: " IPSTR, IP2STR(&((esp_netif_ip_info_t *)event->data)->ip));
            break;
        case NET_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "  -> Link Down.");
            break;
        default:
            break;
        }
        break;
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Initialize the Network Manager
    ESP_LOGI(TAG, "Initializing Net Manager...");
    ESP_ERROR_CHECK(net_manager_init(net_event_callback_handler));

    // 2. Dynamically prepare the configuration based on Kconfig settings
    ESP_LOGI(TAG, "Preparing network configuration...");
    net_manager_config_t config;
    memset(&config, 0, sizeof(config));

    // --- Configure Wi-Fi STA if enabled ---
#if CONFIG_EXAMPLE_WIFI_STA_ENABLED
    ESP_LOGI(TAG, "Wi-Fi STA is enabled in config.");
    config.wifi_sta_enabled = true;
    strncpy(config.wifi_sta_config.ssid, CONFIG_EXAMPLE_WIFI_SSID, sizeof(config.wifi_sta_config.ssid) - 1);
    strncpy(config.wifi_sta_config.password, CONFIG_EXAMPLE_WIFI_PASSWORD, sizeof(config.wifi_sta_config.password) - 1);

#if CONFIG_EXAMPLE_WIFI_STA_USE_STATIC_IP
    ESP_LOGI(TAG, "Using Static IP for Wi-Fi STA.");
    config.wifi_sta_config.use_static_ip = true;
    esp_netif_str_to_ip4(CONFIG_EXAMPLE_WIFI_STA_STATIC_IP_ADDR, &config.wifi_sta_config.ip_info.ip);
    esp_netif_str_to_ip4(CONFIG_EXAMPLE_WIFI_STA_STATIC_NETMASK, &config.wifi_sta_config.ip_info.netmask);
    esp_netif_str_to_ip4(CONFIG_EXAMPLE_WIFI_STA_STATIC_GATEWAY, &config.wifi_sta_config.ip_info.gw);
    if (strlen(CONFIG_EXAMPLE_WIFI_STA_STATIC_DNS_MAIN) > 0)
    {
        esp_netif_str_to_ip4(CONFIG_EXAMPLE_WIFI_STA_STATIC_DNS_MAIN, &config.wifi_sta_config.dns1);
    }
    if (strlen(CONFIG_EXAMPLE_WIFI_STA_STATIC_DNS_BACKUP) > 0)
    {
        esp_netif_str_to_ip4(CONFIG_EXAMPLE_WIFI_STA_STATIC_DNS_BACKUP, &config.wifi_sta_config.dns2);
    }
#endif

#endif

    // --- Configure Wi-Fi AP if enabled ---
#if CONFIG_EXAMPLE_WIFI_AP_ENABLED
    ESP_LOGI(TAG, "Wi-Fi AP is enabled in config.");
    config.wifi_ap_enabled = true;
    strncpy(config.wifi_ap_config.ssid, CONFIG_EXAMPLE_AP_SSID, sizeof(config.wifi_ap_config.ssid) - 1);
    strncpy(config.wifi_ap_config.password, CONFIG_EXAMPLE_AP_PASSWORD, sizeof(config.wifi_ap_config.password) - 1);
    config.wifi_ap_config.channel = 1;         // Default channel
    config.wifi_ap_config.max_connections = 4; // Default max connections
#endif

    // --- Configure Ethernet if enabled ---
#if CONFIG_EXAMPLE_ETHERNET_ENABLED
    ESP_LOGI(TAG, "Ethernet is enabled in config.");
    config.ethernet_enabled = true;

#if CONFIG_EXAMPLE_ETHERNET_USE_STATIC_IP
    ESP_LOGI(TAG, "Using Static IP for Ethernet.");
    config.ethernet_config.use_static_ip = true;
    esp_netif_str_to_ip4(CONFIG_EXAMPLE_ETHERNET_STATIC_IP_ADDR, &config.ethernet_config.ip_info.ip);
    esp_netif_str_to_ip4(CONFIG_EXAMPLE_ETHERNET_STATIC_NETMASK, &config.ethernet_config.ip_info.netmask);
    esp_netif_str_to_ip4(CONFIG_EXAMPLE_ETHERNET_STATIC_GATEWAY, &config.ethernet_config.ip_info.gw);
    if (strlen(CONFIG_EXAMPLE_ETHERNET_STATIC_DNS_MAIN) > 0)
    {
        esp_netif_str_to_ip4(CONFIG_EXAMPLE_ETHERNET_STATIC_DNS_MAIN, &config.ethernet_config.dns1);
    }
    if (strlen(CONFIG_EXAMPLE_ETHERNET_STATIC_DNS_BACKUP) > 0)
    {
        esp_netif_str_to_ip4(CONFIG_EXAMPLE_ETHERNET_STATIC_DNS_BACKUP, &config.ethernet_config.dns2);
    }
#endif
#endif

    // 3. Start the Network Manager
    ESP_LOGI(TAG, "Starting Net Manager with the configured interfaces...");
    ESP_ERROR_CHECK(net_manager_start(&config));

    // --- Main application logic ---
    ESP_LOGI(TAG, "Net Manager started. Application is running.");
    int uptime_seconds = 0;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
        uptime_seconds += 10;
        ESP_LOGI(TAG, "Uptime: %d seconds. STA: %s, ETH: %s",
                 uptime_seconds,
                 net_manager_is_sta_connected() ? "Connected" : "Not Connected",
                 net_manager_is_eth_connected() ? "Connected" : "Not Connected");
    }
}
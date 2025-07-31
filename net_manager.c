/**
 * @file net_manager.c
 *
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ethernet_init.h"

#include "net_manager.h"

/* --- Macros and Definitions --- */
static const char *TAG = "NET_MANAGER";
#define NVS_NAMESPACE "net_manager"
#define NVS_CONFIG_KEY "net_config"

/* --- Internal State Variables --- */

// Component lifecycle
static bool s_is_initialized = false;
static SemaphoreHandle_t s_component_mutex = NULL;

// Interface handles
static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_eth = NULL;

static esp_eth_handle_t *s_eth_handles = NULL;
static uint8_t s_eth_handles_num = 0;

// Status tracking
static net_manager_status_t s_status;
static net_event_callback_t s_user_callback = NULL;
static int s_sta_retry_count = 0;

/* --- Thread Safety Macros --- */
#define LOCK() \
    do         \
    {          \
    } while (xSemaphoreTake(s_component_mutex, portMAX_DELAY) != pdPASS)
#define UNLOCK() xSemaphoreGive(s_component_mutex)

/* --- Forward Declarations of Static Functions --- */
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static esp_err_t start_sta(const net_config_wifi_sta_t *sta_config);
static esp_err_t start_ap(const net_config_wifi_ap_t *ap_config);
static esp_err_t start_eth(const net_config_ethernet_t *eth_config); // ETH config is from Kconfig
static void stop_all_interfaces(void);
static void get_default_config_from_kconfig(net_manager_config_t *config);
// static void apply_static_ip_config(esp_netif_t *netif, const esp_netif_ip_info_t *ip_info, const esp_ip4_addr_t *dns1, const esp_ip4_addr_t *dns2);

/**
 * @brief Unified event handler for Wi-Fi, IP, and Ethernet events.
 */
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    LOCK();

    net_manager_event_t event_to_dispatch = {0};

    if (event_base == WIFI_EVENT)
    {
        /******************/
        /*  Wi-Fi Events  */
        /******************/
        switch (event_id)
        {
        // --- Station Events ---
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA Start: connecting...");
            s_status.sta_status = NET_STATUS_CONNECTING;
            esp_wifi_connect();
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_STA, .status = NET_STATUS_CONNECTING};
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            int max_retries = CONFIG_NET_MANAGER_STA_RECONNECT_ATTEMPTS;
            s_status.sta_status = NET_STATUS_DISCONNECTED;
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_STA, .status = NET_STATUS_DISCONNECTED};
            if (s_user_callback)
                s_user_callback(&event_to_dispatch); // Notify disconnect immediately

            if (max_retries < 0 || s_sta_retry_count < max_retries)
            {
                s_sta_retry_count++;
                ESP_LOGI(TAG, "STA Disconnected. Retrying to connect... (attempt %d)", s_sta_retry_count);
                vTaskDelay(pdMS_TO_TICKS(1000 << s_sta_retry_count)); // Exponential backoff
                esp_wifi_connect();
                s_status.sta_status = NET_STATUS_CONNECTING;
                event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_STA, .status = NET_STATUS_CONNECTING};
            }
            else
            {
                ESP_LOGE(TAG, "STA Disconnected. Failed to connect after %d attempts.", s_sta_retry_count);
            }
            break;
        }

        // --- Access Point Events ---
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP Started.");
            s_status.ap_status = NET_STATUS_STARTED;
            esp_netif_get_ip_info(s_netif_ap, &s_status.ap_ip_info);
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_AP, .status = NET_STATUS_STARTED, .data = &s_status.ap_ip_info};
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP Stopped.");
            s_status.ap_status = NET_STATUS_STOPPED;
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_AP, .status = NET_STATUS_STOPPED};
            break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            s_status.ap_connected_clients++;
            // ESP_LOGI(TAG, "AP Client Connected: "MACSTR", AID=%d. Total clients: %d", MAC2STR(event->mac), event->aid, s_status.ap_connected_clients);
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_AP, .status = NET_STATUS_CLIENT_CONNECTED, .data = event};
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            s_status.ap_connected_clients--;
            // ESP_LOGI(TAG, "AP Client Disconnected: "MACSTR", AID=%d. Total clients: %d", MAC2STR(event->mac), event->aid, s_status.ap_connected_clients);
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_AP, .status = NET_STATUS_CLIENT_DISCONNECTED, .data = event};
            break;
        }

        default:
            UNLOCK();
            return; // Don't dispatch unhandled events
        }
    }
    else if (event_base == IP_EVENT)
    {
        /****************/
        /*  IP Events   */
        /****************/
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (event->esp_netif == s_netif_sta)
        {
            ESP_LOGI(TAG, "STA Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_sta_retry_count = 0;
            s_status.sta_status = NET_STATUS_CONNECTED;
            memcpy(&s_status.sta_ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_STA, .status = NET_STATUS_CONNECTED, .data = &event->ip_info};
        }
        else if (event->esp_netif == s_netif_eth)
        {
            ESP_LOGI(TAG, "ETH Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_status.eth_status = NET_STATUS_CONNECTED;
            memcpy(&s_status.eth_ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_ETHERNET, .status = NET_STATUS_CONNECTED, .data = &event->ip_info};
        }
        else
        {
            UNLOCK();
            return;
        }
    }
    else if (event_base == ETH_EVENT)
    {
        /**********************/
        /*  Ethernet Events   */
        /**********************/
        switch (event_id)
        {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "ETH Link Up");
            s_status.eth_status = NET_STATUS_CONNECTING; // Link up, waiting for IP
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_ETHERNET, .status = NET_STATUS_CONNECTING};
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "ETH Link Down");
            s_status.eth_status = NET_STATUS_DISCONNECTED;
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_ETHERNET, .status = NET_STATUS_DISCONNECTED};
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "ETH Started");
            s_status.eth_status = NET_STATUS_STARTED;
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_ETHERNET, .status = NET_STATUS_STARTED};
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "ETH Stopped");
            s_status.eth_status = NET_STATUS_STOPPED;
            event_to_dispatch = (net_manager_event_t){.source = NET_EVENT_SOURCE_ETHERNET, .status = NET_STATUS_STOPPED};
            break;
        default:
            UNLOCK();
            return;
        }
    }
    else
    {
        UNLOCK();
        return;
    }

    if (s_user_callback)
    {
        s_user_callback(&event_to_dispatch);
    }
    UNLOCK();
}

/**
 * @brief Initializes and configures Wi-Fi STA interface.
 */
static esp_err_t start_sta(const net_config_wifi_sta_t *sta_config)
{
    s_netif_sta = esp_netif_create_default_wifi_sta();
    assert(s_netif_sta);

    // Apply static IP if configured
    if (sta_config->use_static_ip)
    {
        ESP_LOGI(TAG, "Using static IP for Wi-Fi STA");
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_netif_sta));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_netif_sta, &sta_config->ip_info));

        if (sta_config->dns1.addr != 0)
        {
            esp_netif_dns_info_t dns_info_1 = {.ip.u_addr.ip4 = sta_config->dns1};
            ESP_ERROR_CHECK(esp_netif_set_dns_info(s_netif_sta, ESP_NETIF_DNS_MAIN, &dns_info_1));
        }
        if (sta_config->dns2.addr != 0)
        {
            esp_netif_dns_info_t dns_info_2 = {.ip.u_addr.ip4 = sta_config->dns2};
            ESP_ERROR_CHECK(esp_netif_set_dns_info(s_netif_sta, ESP_NETIF_DNS_BACKUP, &dns_info_2));
        }
    }
    else
    {
        ESP_LOGI(TAG, "Using DHCP for Wi-Fi STA");
    }

    wifi_config_t wifi_cfg = {
        .sta = {
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)wifi_cfg.sta.ssid, sta_config->ssid, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, sta_config->password, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
    ESP_LOGI(TAG, "Wi-Fi STA configured for SSID: %s", sta_config->ssid);
    return ESP_OK;
}

/**
 * @brief Initializes and configures Wi-Fi AP interface.
 */
static esp_err_t start_ap(const net_config_wifi_ap_t *ap_config)
{
    s_netif_ap = esp_netif_create_default_wifi_ap();
    assert(s_netif_ap);

    wifi_config_t wifi_cfg = {
        .ap = {
            .channel = ap_config->channel,
            .max_connection = ap_config->max_connections,
            .authmode = (strlen(ap_config->password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_cfg.ap.ssid, ap_config->ssid, sizeof(wifi_cfg.ap.ssid));
    strncpy((char *)wifi_cfg.ap.password, ap_config->password, sizeof(wifi_cfg.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_cfg));
    ESP_LOGI(TAG, "Wi-Fi AP configured with SSID: %s", ap_config->ssid);
    return ESP_OK;
}

/**
 * @brief Initializes and configures Ethernet interface using the official ethernet_init component. (CORRECTED & SIMPLIFIED)
 */
// net_manager.c

static esp_err_t start_eth(const net_config_ethernet_t *eth_config)
{
    // 1. Initialize all Ethernet drivers using the component.
    ESP_ERROR_CHECK(ethernet_init_all(&s_eth_handles, &s_eth_handles_num));
    if (s_eth_handles_num == 0)
    {
        ESP_LOGE(TAG, "ethernet_init_all() did not initialize any Ethernet interfaces.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%d Ethernet interface(s) initialized. Using the first one.", s_eth_handles_num);

    // 2. Create the esp-netif instance.
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_netif_eth = esp_netif_new(&cfg);
    assert(s_netif_eth);

    // 3. Apply static IP configuration IF requested. This must be done
    //    before the driver is started.
    if (eth_config->use_static_ip)
    {
        ESP_LOGI(TAG, "Using static IP for Ethernet");
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_netif_eth));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_netif_eth, &eth_config->ip_info));

        if (eth_config->dns1.addr != 0)
        {
            esp_netif_dns_info_t dns_info_1 = {.ip.u_addr.ip4 = eth_config->dns1};
            ESP_ERROR_CHECK(esp_netif_set_dns_info(s_netif_eth, ESP_NETIF_DNS_MAIN, &dns_info_1));
        }
        if (eth_config->dns2.addr != 0)
        {
            esp_netif_dns_info_t dns_info_2 = {.ip.u_addr.ip4 = eth_config->dns2};
            ESP_ERROR_CHECK(esp_netif_set_dns_info(s_netif_eth, ESP_NETIF_DNS_BACKUP, &dns_info_2));
        }
    }
    else
    {
        ESP_LOGI(TAG, "Using DHCP for Ethernet");
    }

    // 4. Attach the Ethernet driver to the TCP/IP stack.
    // Use esp_eth_new_netif_glue() as shown in the official example.
    ESP_ERROR_CHECK(esp_netif_attach(s_netif_eth, esp_eth_new_netif_glue(s_eth_handles[0])));

    // 5. Start the Ethernet driver.
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handles[0]));

    ESP_LOGI(TAG, "Ethernet started.");
    return ESP_OK;
}

/**
 * @brief Stops and destroys all active network interfaces.
 */
static void stop_all_interfaces(void)
{
    bool wifi_active = (s_netif_sta || s_netif_ap);

    if (s_netif_eth)
    {
        ESP_LOGI(TAG, "Stopping Ethernet...");
        // Destroying the netif created by ethernet_init will also
        // handle stopping and de-initing the driver.
        esp_netif_destroy(s_netif_eth);
        s_netif_eth = NULL;
    }

    if (s_eth_handles)
    {
        ethernet_deinit_all(s_eth_handles);
        s_eth_handles = NULL;
        s_eth_handles_num = 0;
    }

    if (wifi_active)
    {
        ESP_LOGI(TAG, "Stopping Wi-Fi...");
        esp_wifi_stop();
        // 在销毁 netif 之前反初始化 Wi-Fi
        esp_wifi_deinit();
        if (s_netif_sta)
        {
            esp_netif_destroy(s_netif_sta);
            s_netif_sta = NULL;
        }
        if (s_netif_ap)
        {
            esp_netif_destroy(s_netif_ap);
            s_netif_ap = NULL;
        }
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.sta_status = NET_STATUS_STOPPED;
    s_status.ap_status = NET_STATUS_STOPPED;
    s_status.eth_status = NET_STATUS_STOPPED;

    s_sta_retry_count = 0;
    ESP_LOGI(TAG, "All network interfaces stopped and cleaned up.");
}

/**
 * @brief Loads default configuration from Kconfig.
 */
static void get_default_config_from_kconfig(net_manager_config_t *config)
{
    memset(config, 0, sizeof(net_manager_config_t));
#ifdef CONFIG_NET_MANAGER_WIFI_STA_ENABLED_DEFAULT
    config->wifi_sta_enabled = true;
    strncpy(config->wifi_sta_config.ssid, CONFIG_NET_MANAGER_WIFI_STA_SSID_DEFAULT, sizeof(config->wifi_sta_config.ssid) - 1);
    strncpy(config->wifi_sta_config.password, CONFIG_NET_MANAGER_WIFI_STA_PASSWORD_DEFAULT, sizeof(config->wifi_sta_config.password) - 1);
#endif
#ifdef CONFIG_NET_MANAGER_WIFI_AP_ENABLED_DEFAULT
    config->wifi_ap_enabled = true;
    strncpy(config->wifi_ap_config.ssid, CONFIG_NET_MANAGER_WIFI_AP_SSID_DEFAULT, sizeof(config->wifi_ap_config.ssid) - 1);
    strncpy(config->wifi_ap_config.password, CONFIG_NET_MANAGER_WIFI_AP_PASSWORD_DEFAULT, sizeof(config->wifi_ap_config.password) - 1);
    config->wifi_ap_config.channel = CONFIG_NET_MANAGER_WIFI_AP_CHANNEL_DEFAULT;
    config->wifi_ap_config.max_connections = CONFIG_NET_MANAGER_WIFI_AP_MAX_CONN_DEFAULT;
#endif
#ifdef CONFIG_NET_MANAGER_ETHERNET_ENABLED_DEFAULT
    config->ethernet_enabled = true;
#endif
}

#if 0
/**
 * @brief Helper to apply static IP configuration to a netif.
 */
static void apply_static_ip_config(esp_netif_t *netif, const esp_netif_ip_info_t *ip_info, const esp_ip4_addr_t *dns1, const esp_ip4_addr_t *dns2)
{
    // Stop the DHCP client first
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));

    // Set static IP, netmask, gateway
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, ip_info));

    // Set DNS servers
    if (dns1 && dns1->addr != 0) {
        esp_netif_dns_info_t dns_info_1 = {.ip.u_addr.ip4 = *dns1};
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info_1));
    }
    if (dns2 && dns2->addr != 0) {
        esp_netif_dns_info_t dns_info_2 = {.ip.u_addr.ip4 = *dns2};
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info_2));
    }
    ESP_LOGI(TAG, "Applied static IP settings for netif %p", netif);
}
#endif

/*
 * =====================================================================================
 * |                                                                                   |
 * |                                Public API Functions                               |
 * |                                                                                   |
 * =====================================================================================
 */

esp_err_t net_manager_init(net_event_callback_t cb)
{
    if (s_is_initialized)
    {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    s_component_mutex = xSemaphoreCreateMutex();
    if (!s_component_mutex)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    LOCK();
    memset(&s_status, 0, sizeof(net_manager_status_t));
    s_user_callback = cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));

    s_is_initialized = true;
    UNLOCK();

    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

esp_err_t net_manager_deinit(void)
{
    if (!s_is_initialized)
        return ESP_OK;

    LOCK();
    stop_all_interfaces();
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    s_is_initialized = false;
    UNLOCK();

    vSemaphoreDelete(s_component_mutex);
    s_component_mutex = NULL;
    ESP_LOGI(TAG, "De-initialized successfully");
    return ESP_OK;
}

esp_err_t net_manager_start(const net_manager_config_t *config)
{
    assert(s_is_initialized);
    LOCK();

    stop_all_interfaces(); // Ensure a clean state before starting

    net_manager_config_t cfg;
    if (config)
    {
        memcpy(&cfg, config, sizeof(net_manager_config_t));
    }
    else
    {
        if (net_manager_load_config_from_nvs(&cfg) != ESP_OK)
        {
            ESP_LOGI(TAG, "No config in NVS, using Kconfig defaults.");
            get_default_config_from_kconfig(&cfg);
        }
    }

    bool is_wifi_needed = cfg.wifi_sta_enabled || cfg.wifi_ap_enabled;
    if (is_wifi_needed)
    {
        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

        wifi_mode_t mode = WIFI_MODE_NULL;
        if (cfg.wifi_sta_enabled && cfg.wifi_ap_enabled)
            mode = WIFI_MODE_APSTA;
        else if (cfg.wifi_sta_enabled)
            mode = WIFI_MODE_STA;
        else if (cfg.wifi_ap_enabled)
            mode = WIFI_MODE_AP;

        if (mode != WIFI_MODE_NULL)
        {
            ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
        }
    }

    if (cfg.wifi_sta_enabled)
        start_sta(&cfg.wifi_sta_config);
    if (cfg.wifi_ap_enabled)
        start_ap(&cfg.wifi_ap_config);
    if (cfg.ethernet_enabled)
        start_eth(&cfg.ethernet_config);

    if (is_wifi_needed)
    {
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    UNLOCK();
    return ESP_OK;
}

esp_err_t net_manager_stop(void)
{
    assert(s_is_initialized);
    LOCK();
    stop_all_interfaces();
    UNLOCK();
    return ESP_OK;
}

esp_err_t net_manager_get_status(net_manager_status_t *status)
{
    assert(s_is_initialized && status);
    LOCK();
    memcpy(status, &s_status, sizeof(net_manager_status_t));
    UNLOCK();
    return ESP_OK;
}

bool net_manager_is_sta_connected(void)
{
    assert(s_is_initialized);
    LOCK();
    bool connected = (s_status.sta_status == NET_STATUS_CONNECTED);
    UNLOCK();
    return connected;
}

bool net_manager_is_eth_connected(void)
{
    assert(s_is_initialized);
    LOCK();
    bool connected = (s_status.eth_status == NET_STATUS_CONNECTED);
    UNLOCK();
    return connected;
}

esp_err_t net_manager_get_ap_clients_list(wifi_sta_list_t *clients)
{
    assert(s_is_initialized && clients);
    if (!s_netif_ap)
        return ESP_ERR_WIFI_NOT_STARTED;
    return esp_wifi_ap_get_sta_list(clients);
}

esp_err_t net_manager_save_config_to_nvs(const net_manager_config_t *config)
{
    assert(s_is_initialized && config);
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(nvs_handle, NVS_CONFIG_KEY, config, sizeof(net_manager_config_t));
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Configuration saved to NVS %s", (err == ESP_OK) ? "successfully" : "failed");
    return err;
}

esp_err_t net_manager_load_config_from_nvs(net_manager_config_t *config)
{
    assert(s_is_initialized && config);
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t required_size = sizeof(net_manager_config_t);
    err = nvs_get_blob(nvs_handle, NVS_CONFIG_KEY, config, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK && required_size != sizeof(net_manager_config_t))
    {
        ESP_LOGW(TAG, "NVS config size mismatch. Expected %d, got %d.", sizeof(net_manager_config_t), required_size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Configuration loaded from NVS %s", (err == ESP_OK) ? "successfully" : "failed (or not found)");
    return err;
}

esp_err_t net_manager_get_ip_info(net_event_source_t source, esp_netif_ip_info_t *ip_info)
{
    assert(s_is_initialized && ip_info);
    esp_netif_t *netif = NULL;
    if (source == NET_EVENT_SOURCE_STA)
        netif = s_netif_sta;
    else if (source == NET_EVENT_SOURCE_ETHERNET)
        netif = s_netif_eth;

    if (!netif)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_netif_get_ip_info(netif, ip_info);
}

esp_err_t net_manager_get_dns_info(net_event_source_t source, esp_netif_dns_type_t type, esp_netif_dns_info_t *dns_info)
{
    assert(s_is_initialized && dns_info);
    esp_netif_t *netif = NULL;
    if (source == NET_EVENT_SOURCE_STA)
        netif = s_netif_sta;
    else if (source == NET_EVENT_SOURCE_ETHERNET)
        netif = s_netif_eth;

    if (!netif)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_netif_get_dns_info(netif, type, dns_info);
}
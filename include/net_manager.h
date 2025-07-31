#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public Structures and Enums */

/**
 * @brief Network interface status enumeration
 */
typedef enum {
    NET_STATUS_UNINITIALIZED,
    NET_STATUS_STOPPED,
    NET_STATUS_STARTED,
    NET_STATUS_CONNECTING,
    NET_STATUS_CONNECTED,
    NET_STATUS_DISCONNECTED,
    NET_STATUS_WAITING_FOR_RECONNECT,
    NET_STATUS_CLIENT_CONNECTED,    // AP Mode: a client connected
    NET_STATUS_CLIENT_DISCONNECTED, // AP Mode: a client disconnected
} net_status_t;

/**
 * @brief Configuration for Wi-Fi Station (STA) interface
 */
typedef struct {
    char ssid[32];
    char password[64];

    // --- Static IP Configuration ---
    bool use_static_ip;
    esp_netif_ip_info_t ip_info; // Holds IP, netmask, gateway
    esp_ip4_addr_t dns1;         // Primary DNS server
    esp_ip4_addr_t dns2;         // Secondary DNS server
} net_config_wifi_sta_t;

/**
 * @brief Configuration for Wi-Fi Access Point (AP) interface
 */
typedef struct {
    char ssid[32];
    char password[64];
    uint8_t channel;
    uint8_t max_connections;
} net_config_wifi_ap_t;

/**
 * @brief Configuration for Ethernet (ETH) interface
 * @note Most ETH settings are handled via Kconfig for specific hardware.
 */
typedef struct {
    // --- Static IP Configuration ---
    bool use_static_ip;
    esp_netif_ip_info_t ip_info; // Holds IP, netmask, gateway
    esp_ip4_addr_t dns1;         // Primary DNS server
    esp_ip4_addr_t dns2;         // Secondary DNS server
} net_config_ethernet_t;

/**
 * @brief Master configuration structure for the network manager
 */
typedef struct {
    bool wifi_sta_enabled;
    bool wifi_ap_enabled;
    bool ethernet_enabled;

    net_config_wifi_sta_t wifi_sta_config;
    net_config_wifi_ap_t  wifi_ap_config;
    net_config_ethernet_t ethernet_config;
} net_manager_config_t;

/**
 * @brief Source of a network event
 */
typedef enum {
    NET_EVENT_SOURCE_STA,
    NET_EVENT_SOURCE_AP,
    NET_EVENT_SOURCE_ETHERNET,
} net_event_source_t;

/**
 * @brief Event structure passed to the user callback
 */
typedef struct {
    net_event_source_t source;
    net_status_t status;
    void* data; // Context-specific data (e.g., esp_netif_ip_info_t on connect)
} net_manager_event_t;

/**
 * @brief Network manager status structure
 */
typedef struct {
    net_status_t sta_status;
    net_status_t ap_status;
    net_status_t eth_status;

    esp_netif_ip_info_t sta_ip_info;
    esp_netif_ip_info_t ap_ip_info;
    esp_netif_ip_info_t eth_ip_info;

    uint8_t ap_connected_clients;
} net_manager_status_t;


/**
 * @brief User callback function pointer for network events
 * @param event Pointer to the event structure
 */
typedef void (*net_event_callback_t)(const net_manager_event_t *event);

/* Public API Functions */

/**
 * @brief Initializes the network manager.
 *        This function must be called once before any other net_manager function.
 *
 * @param cb User event callback function. Can be NULL.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t net_manager_init(net_event_callback_t cb);

/**
 * @brief De-initializes the network manager and releases all resources.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_deinit(void);

/**
 * @brief Starts network interface(s) based on the provided configuration.
 *
 * @param config Pointer to the network configuration. If NULL, default configuration
 *               from Kconfig or NVS will be used.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_start(const net_manager_config_t *config);

/**
 * @brief Stops all active network interfaces.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_stop(void);

/**
 * @brief Gets the current status of all network interfaces.
 *
 * @param status Pointer to a net_manager_status_t struct to be filled with status info.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_get_status(net_manager_status_t *status);

/**
 * @brief Convenience function to check if the Wi-Fi station is fully connected (has an IP).
 *
 * @return true if connected, false otherwise.
 */
bool net_manager_is_sta_connected(void);

/**
 * @brief Convenience function to check if the Ethernet is fully connected (has an IP).
 *
 * @return true if connected, false otherwise.
 */
bool net_manager_is_eth_connected(void);

/**
 * @brief Gets the list of clients connected to the AP.
 *
 * @param[out] clients Pointer to a wifi_sta_list_t struct to store the client list.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_get_ap_clients_list(wifi_sta_list_t *clients);

/**
 * @brief Saves a network configuration to NVS (Non-Volatile Storage).
 *
 * @param config Pointer to the configuration to save.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_save_config_to_nvs(const net_manager_config_t *config);

/**
 * @brief Loads a network configuration from NVS.
 *
 * @param config Pointer to a structure to be filled with the loaded configuration.
 * @return esp_err_t ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no config is saved.
 */
esp_err_t net_manager_load_config_from_nvs(net_manager_config_t *config);

/**
 * @brief Gets the IP information (IP, mask, gw) for a specific network interface.
 *
 * @param source The network interface (STA or ETH) to query.
 * @param[out] ip_info Pointer to a struct to be filled with the IP information.
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if interface is not active.
 */
esp_err_t net_manager_get_ip_info(net_event_source_t source, esp_netif_ip_info_t *ip_info);

/**
 * @brief Gets the DNS server information for a specific network interface.
 *
 * @param source The network interface (STA or ETH) to query.
 * @param type The type of DNS server to get (Primary, Secondary).
 * @param[out] dns_info Pointer to a struct to be filled with the DNS information.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t net_manager_get_dns_info(net_event_source_t source, esp_netif_dns_type_t type, esp_netif_dns_info_t *dns_info);


#ifdef __cplusplus
}
#endif

#endif // NET_MANAGER_H

# Net Manager Component for ESP-IDF

[中文版](README_CN.md)

## Overview

**Net Manager** is a robust, production-grade network management component designed for the Espressif IoT Development Framework (ESP-IDF). It aims to simplify the complex network connection logic in IoT projects by providing a unified, clean, and event-driven API to manage various network interfaces.

Whether your project requires **Wi-Fi Station (STA)**, **Wi-Fi Access Point (AP)**, **Ethernet**, or any combination thereof, `net_manager` handles it with ease. It is built with robustness, portability, and ease-of-use in mind, making it an ideal choice for mass-produced products.

## Key Features

- **Unified Multi-Interface Management**:
  - Supports Wi-Fi STA, Wi-Fi AP, and wired Ethernet.
  - Full support for **APSTA mode**, allowing the device to act as an AP while simultaneously connecting to another router.
  - Seamless **Wi-Fi and Ethernet coexistence** with automatic failover (Ethernet-first priority) handled by the underlying TCP/IP stack.

- **Clean and Simple API**:
  - Get started with just a few core functions: `net_manager_init()`, `net_manager_start()`, and `net_manager_stop()`.
  - A single, comprehensive configuration struct (`net_manager_config_t`) allows for flexible enabling and setup of all network interfaces.

- **Event-Driven Architecture**:
  - Fully based on the ESP-IDF system event loop (`event_loop`).
  - Receive asynchronous notifications for all network state changes (e.g., connecting, connected, disconnected, client joined/left) via a single callback function, ensuring a non-blocking and power-efficient main application flow.

- **Powerful Connection Handling**:
  - **Smart Auto-Reconnect**: When a Wi-Fi STA or Ethernet connection is lost, the component automatically attempts to reconnect using an "exponential backoff" algorithm to avoid overwhelming the network.
  - **Static IP Support**: Provides independent configuration for static IP, netmask, gateway, and DNS servers for both Wi-Fi STA and Ethernet interfaces.

- **Flexible Configuration**:
  - **Runtime Configuration**: Pass configuration dynamically via the API.
  - **Persistent Configuration**: Save and load network credentials and settings to/from NVS (Non-Volatile Storage), enabling automatic connection on reboot.
  - **Compile-Time Defaults**: Set default parameters via `menuconfig` for rapid development and firmware version management.

## How to Use

### 1. Add the Component to Your Project

Copy the `net_manager` folder into the `components` directory of your ESP-IDF project. Alternatively, if this component is hosted on a Git repository, add it as a dependency in your project's `idf_component.yml`.

### 2. Include the Header File

In your application code, include the main header:
```c
#include "net_manager.h"
```

### 3. Implement a Network Event Callback

This function will handle all network status updates from `net_manager`.

```c
void net_event_callback_handler(const net_manager_event_t *event)
{
    if (!event) return;

    switch (event->source) {
        case NET_EVENT_SOURCE_STA:
            ESP_LOGI(TAG, "Wi-Fi Station Event: Status %d", event->status);
            if (event->status == NET_STATUS_CONNECTED) {
                esp_netif_ip_info_t* ip_info = (esp_netif_ip_info_t*)event->data;
                ESP_LOGI(TAG, "STA Connected! IP: " IPSTR, IP2STR(&ip_info->ip));
            }
            break;
        case NET_EVENT_SOURCE_AP:
            ESP_LOGI(TAG, "Wi-Fi AP Event: Status %d", event->status);
            break;
        case NET_EVENT_SOURCE_ETHERNET:
            ESP_LOGI(TAG, "Ethernet Event: Status %d", event->status);
            break;
    }
}
```

### 4. Initialize and Start the Net Manager

In your `app_main` function, follow these steps:

```c
void app_main(void)
{
    // 1. Initialize NVS (required for persistent configuration)
    esp_err_t ret = nvs_flash_init();
    // ... (handle NVS errors)

    // 2. Initialize the network manager and register the callback
    net_manager_init(net_event_callback_handler);

    // 3. Prepare the network configuration
    net_manager_config_t config;
    memset(&config, 0, sizeof(config));

    // Example: Enable Wi-Fi STA and AP concurrently (APSTA mode)
    config.wifi_sta_enabled = true;
    strncpy(config.wifi_sta_config.ssid, "YourRouterSSID", sizeof(config.wifi_sta_config.ssid));
    strncpy(config.wifi_sta_config.password, "YourRouterPassword", sizeof(config.wifi_sta_config.password));

    config.wifi_ap_enabled = true;
    strncpy(config.wifi_ap_config.ssid, "MyESP32-AP", sizeof(config.wifi_ap_config.ssid));
    strncpy(config.wifi_ap_config.password, "12345678", sizeof(config.wifi_ap_config.password));

    // 4. Start the network manager
    net_manager_start(&config);
    
    // From here, your main application can perform other tasks.
    // Network state changes will be handled asynchronously by the callback.
}
```

### 5. See the Example Project

For a comprehensive demonstration of all features, including **static IP configuration**, **Ethernet-only mode**, and how to **switch between different modes**, please refer to the included example project:
`components/net_manager/example`

The example contains a flexible `menuconfig` menu that allows you to freely combine and test all functionalities.

## API Reference

### Main Functions

- `esp_err_t net_manager_init(net_event_callback_t cb)`
  - Initializes the component. Must be called first.
- `esp_err_t net_manager_start(const net_manager_config_t *config)`
  - Starts one or more network interfaces based on the provided configuration. If `config` is NULL, it attempts to load from NVS or use Kconfig defaults.
- `esp_err_t net_manager_stop(void)`
  - Stops all network activity and releases resources.
- `esp_err_t net_manager_deinit(void)`
  - De-initializes the component.

### Status Query Functions

- `bool net_manager_is_sta_connected(void)`
- `bool net_manager_is_eth_connected(void)`
- `esp_err_t net_manager_get_status(net_manager_status_t *status)`

### Configuration Access Functions

- `esp_err_t net_manager_save_config_to_nvs(const net_manager_config_t *config)`
- `esp_err_t net_manager_load_config_from_nvs(net_manager_config_t *config)`

## Contributing

Contributions in the form of Issues or Pull Requests are welcome.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

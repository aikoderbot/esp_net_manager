# Net Manager (网络管理器) 组件

## 简介

`net_manager` 是一个为乐鑫 ESP-IDF 设计的、功能强大且高度可靠的网络管理组件。它旨在简化物联网项目中复杂的网络连接逻辑，为开发者提供一个统一、简洁、事件驱动的API，以管理多种网络接口。

无论您的项目需要 **Wi-Fi Station (STA)**、**Wi-Fi Access Point (AP)**、**有线以太网 (Ethernet)**，还是它们的任意组合，`net_manager` 都能轻松应对。它是为量产级产品而设计的，注重代码的鲁棒性、可移植性和易用性。

## 核心特性

- **多接口统一管理**:
  - 支持 Wi-Fi STA, Wi-Fi AP, Ethernet。
  - 支持 **APSTA** 模式，可作为热点，同时连接到另一个路由器。
  - 支持 **Wi-Fi 与以太网共存**，并利用ESP-IDF的路由优先级实现自动故障转移（有线优先）。

- **简洁的API**:
  - `net_manager_init()`, `net_manager_start()`, `net_manager_stop()` 几个核心函数即可完成所有操作。
  - 通过一个统一的配置结构体 `net_manager_config_t` 灵活启用和配置所有网络接口。

- **事件驱动模型**:
  - 完全基于ESP-IDF的事件循环 (`event_loop`)。
  - 通过注册回调函数，异步接收网络状态通知（如连接中、已连接、已断开、客户端加入/退出等），不阻塞主流程，高效节能。

- **强大的连接管理**:
  - **智能自动重连**: Wi-Fi STA 或以太网断开后，组件会自动尝试重连，并采用“指数退避”算法，避免在网络不稳定时频繁冲击路由器。
  - **静态IP支持**: 为 Wi-Fi STA 和以太网接口提供独立的静态IP、子网掩码、网关和DNS服务器配置。

- **配置灵活**:
  - **运行时配置**: 通过API动态传入配置。
  - **持久化配置**: 支持将网络凭证等配置保存到NVS（非易失性存储），设备重启后可自动加载并连接。
  - **编译时默认配置**: 可通过`menuconfig`设置默认参数，方便快速开发和固件版本管理。

## 如何使用

### 1. 将组件添加到您的项目中

将 `net_manager` 文件夹复制到您项目根目录的 `components` 文件夹下。

### 2. 在您的代码中包含头文件

```c
#include "net_manager.h"
```

### 3. 实现一个网络事件回调函数

这个函数将处理所有来自 `net_manager` 的网络状态更新。

```c
void net_event_callback_handler(const net_manager_event_t *event)
{
    if (!event) return;

    switch (event->source) {
        case NET_EVENT_SOURCE_STA:
            ESP_LOGI(TAG, "Wi-Fi Station Event: %d", event->status);
            if (event->status == NET_STATUS_CONNECTED) {
                esp_netif_ip_info_t* ip_info = (esp_netif_ip_info_t*)event->data;
                ESP_LOGI(TAG, "STA Connected! IP: " IPSTR, IP2STR(&ip_info->ip));
            }
            break;
        case NET_EVENT_SOURCE_AP:
            ESP_LOGI(TAG, "Wi-Fi AP Event: %d", event->status);
            break;
        case NET_EVENT_SOURCE_ETHERNET:
            ESP_LOGI(TAG, "Ethernet Event: %d", event->status);
            break;
    }
}
```

### 4. 初始化并启动网络管理器

在您的 `app_main` 函数中，按以下步骤操作：

```c
void app_main(void)
{
    // 1. 初始化NVS (如果需要使用持久化配置)
    esp_err_t ret = nvs_flash_init();
    // ... (处理NVS错误)

    // 2. 初始化网络管理器，并注册回调函数
    net_manager_init(net_event_callback_handler);

    // 3. 准备网络配置
    net_manager_config_t config;
    memset(&config, 0, sizeof(config));

    // 示例：启动 Wi-Fi STA 和 AP 共存模式
    config.wifi_sta_enabled = true;
    strcpy(config.wifi_sta_config.ssid, "YourRouterSSID");
    strcpy(config.wifi_sta_config.password, "YourRouterPassword");

    config.wifi_ap_enabled = true;
    strcpy(config.wifi_ap_config.ssid, "MyESP32-AP");
    strcpy(config.wifi_ap_config.password, "12345678");

    // 4. 启动网络管理器
    net_manager_start(&config);
    
    // 从这里开始，您的主程序可以执行其他任务
    // 网络连接和状态变化将通过上面的回调函数异步通知
}
```

### 5. 查看示例代码

为了获得更全面的用法展示，包括如何配置**静态IP**、如何**只启动以太网**、以及如何**在不同模式间切换**，请参考本组件自带的示例项目：
`components/net_manager/example`

该示例项目包含一个非常灵活的 `menuconfig` 菜单，您可以自由组合和测试所有功能。

## API 参考

### 主要函数

- `esp_err_t net_manager_init(net_event_callback_t cb)`
  - 初始化组件。必须第一个调用。
- `esp_err_t net_manager_start(const net_manager_config_t *config)`
  - 根据传入的配置启动一个或多个网络接口。如果 `config` 为NULL，则尝试从NVS加载或使用Kconfig默认值。
- `esp_err_t net_manager_stop(void)`
  - 停止所有网络活动并释放资源。
- `esp_err_t net_manager_deinit(void)`
  - 反初始化组件。

### 状态查询函数

- `bool net_manager_is_sta_connected(void)`
- `bool net_manager_is_eth_connected(void)`
- `esp_err_t net_manager_get_status(net_manager_status_t *status)`

### 配置存取函数

- `esp_err_t net_manager_save_config_to_nvs(const net_manager_config_t *config)`
- `esp_err_t net_manager_load_config_from_nvs(net_manager_config_t *config)`

## 贡献

欢迎通过提交 Issues 或 Pull Requests 来为该项目做出贡献。

## 许可证

本项目采用 MIT 许可证。详情请见 `LICENSE` 文件。

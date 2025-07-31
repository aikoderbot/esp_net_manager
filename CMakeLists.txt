idf_component_register(SRCS "net_manager.c" 
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_eth nvs_flash)
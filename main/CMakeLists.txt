idf_component_register(SRCS "cam.cpp"
PRIV_REQUIRES spi_flash esp_http_server esp_wifi esp_event nvs_flash esp_adc spiffs esp_netif                 
INCLUDE_DIRS ".")

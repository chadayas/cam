idf_component_register(SRCS "cam.cpp"
PRIV_REQUIRES spi_flash esp_http_server esp_wifi esp_event nvs_flash esp_adc spiffs esp_netif esp_driver_gpio esp_timer                 
INCLUDE_DIRS ".")

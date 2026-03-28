idf_component_register(SRCS "cam.cpp" "mdns.cpp"
PRIV_REQUIRES spi_flash esp_http_server esp_wifi esp_event nvs_flash spiffs esp_netif esp_driver_gpio esp_timer servo driver                  
INCLUDE_DIRS ".")

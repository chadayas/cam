#pragma once

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "camera_pins.h"


#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
/*
Same classes used in breathalyzer project, initing wifi and 
http server in esp-idf is essentially boilerplate code that can 
be copied.
*/
#define CONFIG_XCLK_FREQ 20000000
#define PART_BOUNDARY "123456789000000000000987654321"

namespace Stream{
	const char* CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
	const char* BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
	const char* PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

};

class WifiService{
	public:
		WifiService();
	public:
		esp_err_t init();
		esp_err_t connect();
	public:
		const int WIFI_RETRY_ATTEMPT = 10;
		int wifi_retry_count = 0;

		esp_netif_t *tutorial_netif = NULL;
		esp_event_handler_instance_t ip_event_handler;
		esp_event_handler_instance_t wifi_event_handler;

		EventGroupHandle_t wifi_event_group = NULL;
	private:
		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		const char* TAG = "[--WIFI--]";
};


class Httpserver{
	public:
		Httpserver();
	public:
		httpd_handle_t init();
		esp_err_t register_route(const httpd_uri_t *uri_cfg);	
	private:
		httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
		httpd_handle_t svr = NULL;
		const char* TAG = "[--HTTP--]";
};


esp_err_t init_camera();


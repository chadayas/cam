#include "cam.h"


static void wifi_event_cb(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
	WifiService *svc = (WifiService *)arg;
	
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (svc->wifi_retry_count < svc->WIFI_RETRY_ATTEMPT) {
            ESP_LOGW(TAG, "Disconnected, retrying (%d/%d)", svc->wifi_retry_count + 1, svc->WIFI_RETRY_ATTEMPT);
            esp_wifi_connect();
            svc->wifi_retry_count++;
        } else {
            ESP_LOGE(TAG, "Failed to connect after %d attempts", svc->WIFI_RETRY_ATTEMPT);
            xEventGroupSetBits(svc->wifi_event_group, WIFI_FAIL_BIT);
        }
    }
}

static void ip_event_cb(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
	WifiService *svc = (WifiService *)arg;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        svc->wifi_retry_count = 0;
        xEventGroupSetBits(svc->wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t WifiService::init(){
     auto ret = nvs_flash_init();
     if (ret != ESP_OK){
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
     wifi_event_group = xEventGroupCreate();

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCP/IP network stack");
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop");
        return ret;
    }

    ret = esp_wifi_set_default_wifi_sta_handlers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default handlers");
        return ret;
    }

    tutorial_netif = esp_netif_create_default_wifi_sta();
    if (tutorial_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
        return ESP_FAIL;
    }
    
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
			    	&wifi_event_cb, this, &wifi_event_handler));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
			    	&ip_event_cb, this, &ip_event_handler));
    return ret;
}	


esp_err_t WifiService::connect(){
    wifi_config_t wifi_config{};

    strncpy((char*)wifi_config.sta.ssid, CONFIG_WIFI_NAME, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, CONFIG_WIFI_PW, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // default is WIFI_PS_MIN_MODEM
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); // default is WIFI_STORAGE_FLASH

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Connecting to Wi-Fi network: %s",wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi network: %s", wifi_config.sta.ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi network: %s", wifi_config.sta.ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Unexpected Wi-Fi error");
    return ESP_FAIL;
}

esp_err_t WifiService::deinit(){
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "Wi-Fi stack not initialized");
        return ret;
    }

    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(tutorial_netif));
    esp_netif_destroy(tutorial_netif);

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler));

    return ESP_OK;
}

esp_err_t WifiService::disconnect(){
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
    }

    return esp_wifi_disconnect();
}

WifiService::WifiService(){
	init();
	connect();
}

WifiService::~WifiService(){
	deinit();
	disconnect();
}




httpd_handle_t Httpserver::init(){
	if (httpd_start(&svr, &cfg) == ESP_OK){
		ESP_LOGI(TAG, "HTTP server started");

//		httpd_uri_t root_s{};
//		root_s.uri = "/";
//		root_s.method = HTTP_GET;
//		root_s.handler = root;

//		register_route(&root_s);

		return svr;
	} else{
		ESP_LOGE(TAG, "Unable to start server");
		return NULL;
	}
}

void Httpserver::deinit(){
	if(svr != NULL)
		httpd_stop(svr);
}

esp_err_t Httpserver::register_route(const httpd_uri_t *uri_cfg){
    return httpd_register_uri_handler(svr, uri_cfg);
}

Httpserver::Httpserver(){
	init();
}

Httpserver::~Httpserver(){
	deinit();
}

esp_err_t init_camera(){
 
	camera_config_t camera_config = {
		.pin_pwdn  = PWDN_GPIO_NUM,
		.pin_reset = RESET_GPIO_NUM,
		.pin_xclk = XCLK_GPIO_NUM,
		.pin_sccb_sda = SIOD_GPIO_NUM,
		.pin_sccb_scl = SIOC_GPIO_NUM,

		.pin_d7 = Y9_GPIO_NUM,
		.pin_d6 = Y8_GPIO_NUM,
		.pin_d5 = Y7_GPIO_NUM,
		.pin_d4 = Y6_GPIO_NUM,
		.pin_d3 = Y5_GPIO_NUM,
		.pin_d2 = Y4_GPIO_NUM,
		.pin_d1 = Y3_GPIO_NUM,
		.pin_d0 = Y2_GPIO_NUM,
		.pin_vsync = VSYNC_GPIO_NUM,
		.pin_href = HREF_GPIO_NUM,
		.pin_pclk = PCLK_GPIO_NUM,

		.xclk_freq_hz = CONFIG_XCLK_FREQ,
		.ledc_timer = LEDC_TIMER_0,
		.ledc_channel = LEDC_CHANNEL_0,

		.pixel_format = PIXFORMAT_JPEG,
		.frame_size = FRAMESIZE_UXGA,

		.jpeg_quality = 12,
		.fb_count = 1,
		.grab_mode = CAMERA_GRAB_WHEN_EMPTY
	};
	esp_err_t err = esp_camera_init(&camera_config);
    	if (err != ESP_OK)
    		return err;
   
    return ESP_OK;
}


extern "C" void app_main(void){
	WifiService wifi;
}

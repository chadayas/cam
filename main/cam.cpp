#include "cam.h"
#include<fstream>
#include<string>

static void wifi_event_cb(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
	const char* TAG = "[--WIFI CALLBACK--]";	
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
	
	const char* TAG = "[--IP CALLBACK--]";	
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


WifiService::WifiService(){
	init();
	
	connect();
}

esp_err_t auth_handler(httpd_req_t *req){
	std::ifstream file("/data/login.html");
	auto tag = "[--HTML(GET)--]";	
	ESP_LOGI(tag, "HTML sent to client");
	if(!file.is_open()){
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open Html");
		return ESP_FAIL;
	}

	std::string html((std::istreambuf_iterator<char>(file)),
			(std::istreambuf_iterator<char>()));
	httpd_resp_set_type(req,"text/html");
	if (html.c_str() == NULL){
		ESP_LOGE(tag, "HTML file is a NULL");
		return ESP_FAIL;
	} else
		return httpd_resp_set_type(req, html.c_str());
}

esp_err_t check_creds_handler(httpd_req_t *req){
	auto tag = "[--HTML(POST)--]";	
	char buf[] = {0};
	auto buf_len = sizeof(buf);
	int bytes = httpd_req_recv(req, buf, buf_len);	
	if ( bytes > 0){
		ESP_LOGI(tag, "%d Bytes received",bytes);
		size_t n_len = req->content_len;	
		ESP_LOGI(tag, "context_len size is %d ",n_len);
	} else{
		ESP_LOGE(tag, "No bytes received");
		return ESP_FAIL;	
	}	
	return ESP_OK;
}


esp_err_t stream_handler(httpd_req_t *req){
	const char* TAG = "[--STREAM HANDLER--]";	
	camera_fb_t * fb = NULL;
	esp_err_t res = ESP_OK;
	char  part_buf[64];
	static int64_t last_frame = 0;
	if(!last_frame) {
		last_frame = esp_timer_get_time();
	}

	res = httpd_resp_set_type(req, Stream::CONTENT_TYPE);
	if(res != ESP_OK){
		return res;
	}

	while(true){
		fb = esp_camera_fb_get();
		
		if (!fb) {
			ESP_LOGE(TAG, "Camera capture failed");
			res = ESP_FAIL;
			break;
		}

		res = httpd_resp_send_chunk(req, Stream::BOUNDARY, strlen(Stream::BOUNDARY));
		if(res == ESP_OK){
			size_t hlen = snprintf(part_buf, 64, Stream::PART, fb->len);
			res = httpd_resp_send_chunk(req, part_buf, hlen);
		}
		
		if(res == ESP_OK){
			res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
		}

		size_t frame_len = fb->len;
		esp_camera_fb_return(fb);
		if(res != ESP_OK){
			break;
		}

		int64_t fr_end = esp_timer_get_time();
		int64_t frame_time = fr_end - last_frame;
		last_frame = fr_end;
		frame_time /= 1000;
		ESP_LOGI(TAG, "MJPG: %uKB %ums (%.1ffps)",
				(uint32_t)(frame_len/1024),
				(uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
	}

	last_frame = 0;
	return res;

}


httpd_handle_t Httpserver::init(){
	if (httpd_start(&svr, &cfg) == ESP_OK){
		ESP_LOGI(TAG, "HTTP server started");

		httpd_uri_t stream_s{};
		stream_s.uri = "/stream";
		stream_s.method = HTTP_GET;
		stream_s.handler = stream_handler;
		
		httpd_uri_t auth_s{};
		auth_s.uri = "/auth";
		auth_s.method = HTTP_GET;
		auth_s.handler = auth_handler;
		
		/*httpd_uri_t cred_s{};
		stream_s.uri = "/auth";
		stream_s.method = HTTP_POST;
		stream_s.handler = auth_handler;*/



		register_route(&auth_s);
		register_route(&stream_s);
		//register_route(&cred_s);

		return svr;
	} else{
		ESP_LOGE(TAG, "Unable to start server");
		return NULL;
	}
}


esp_err_t Httpserver::register_route(const httpd_uri_t *uri_cfg){
   	auto tag = "[--REGISTER ROUTE--]";	
	if (uri_cfg == NULL){
		ESP_LOGE(tag, "URI config is null");
		return ESP_FAIL;
	}else{ 
		ESP_LOGI(tag, "URI config is fine");	
		return httpd_register_uri_handler(svr, uri_cfg);
	}

}
Httpserver::Httpserver(){
	init();
}


esp_err_t init_camera(){
 

	camera_config_t camera_config{};

	camera_config.pin_pwdn = PWDN_GPIO_NUM;
	camera_config.pin_reset = RESET_GPIO_NUM;
	camera_config.pin_xclk = XCLK_GPIO_NUM;
	camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
	camera_config.pin_sccb_scl = SIOC_GPIO_NUM;

	camera_config.pin_d7 = Y9_GPIO_NUM;
	camera_config.pin_d6 = Y8_GPIO_NUM;
	camera_config.pin_d5 = Y7_GPIO_NUM;
	camera_config.pin_d4 = Y6_GPIO_NUM;
	camera_config.pin_d3 = Y5_GPIO_NUM;
	camera_config.pin_d2 = Y4_GPIO_NUM;
	camera_config.pin_d1 = Y3_GPIO_NUM;
	camera_config.pin_d0 = Y2_GPIO_NUM;
	camera_config.pin_vsync = VSYNC_GPIO_NUM;
	camera_config.pin_href = HREF_GPIO_NUM;
	camera_config.pin_pclk = PCLK_GPIO_NUM;

	camera_config.xclk_freq_hz = CONFIG_XCLK_FREQ;
	camera_config.ledc_timer = LEDC_TIMER_0;
	camera_config.ledc_channel = LEDC_CHANNEL_0;

	camera_config.pixel_format = PIXFORMAT_JPEG;
	camera_config.frame_size = FRAMESIZE_XGA;

	camera_config.jpeg_quality = 15;
	camera_config.fb_count = 2;
	camera_config.fb_location = CAMERA_FB_IN_PSRAM;
	camera_config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
	
	esp_err_t err = esp_camera_init(&camera_config);
    	if (err != ESP_OK)
    		return err;
   
    return ESP_OK;
}


void scan_task(void *param) {
	const char* TAG = "[--SCAN--]";
	wifi_scan_config_t scan_config{};
	wifi_ap_record_t ap_records[10];
	uint16_t ap_count = 10;

	esp_wifi_scan_start(&scan_config, true);
	esp_wifi_scan_get_ap_records(&ap_count, ap_records);

	for (int i = 0; i < ap_count; i++) {
		ESP_LOGI(TAG, "SSID: %s, RSSI: %d dBm", ap_records[i].ssid, ap_records[i].rssi);
	}
	vTaskDelete(NULL);
}

extern "C" void app_main(void){
	static WifiService wifi;
	xTaskCreate(scan_task, "scan_task", 4096, NULL, 5, NULL);
	init_camera();	
		
	static Httpserver http;

}

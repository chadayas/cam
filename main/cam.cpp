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

extern "C" void app_main(void){
	WifiService wifi;
}

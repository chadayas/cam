#include "esp_log.h"
#include "mdns.h"

#define MTAG "[--MDNS--]"
#define NAME_CAM "esp-cam-server"
#define NAME_STREAM "esp-stream"

void start_mdns(){

   esp_err_t catch_err = mdns_init();
   if (catch_err != ESP_OK){
      ESP_LOGE(MTAG, "MDNS failed to intialize");
   }else
      ESP_LOGI(MTAG,"MDNS init okay");
   
   mdns_hostname_set("esp-server");

}


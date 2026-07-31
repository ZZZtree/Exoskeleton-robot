idf_component_register(SRCS "src/ota_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client app_update nvs_flash)

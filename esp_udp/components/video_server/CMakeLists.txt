idf_component_register(SRCS "src/video_server.c"
                              "src/http_handler.c"
                              "src/mjpeg_stream.c"
                              "src/camera_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_timer esp_video example_video_common
                                 h264_udp ota_manager cjson mdns nvs_flash)

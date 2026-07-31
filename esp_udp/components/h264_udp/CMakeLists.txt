idf_component_register(SRCS "src/h264_udp_stream.c"
                              "src/h264_encoder.c"
                              "src/h264_protocol.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer esp_video nvs_flash lwip)

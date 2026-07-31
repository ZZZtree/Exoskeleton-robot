set(srcs "main.c")

set(html_files "../frontend/gzipped/index.html.gz"
               "../frontend/gzipped/loading.jpg.gz"
               "../frontend/gzipped/favicon.ico.gz"
               "../frontend/gzipped/assets/index.js.gz"
               "../frontend/gzipped/assets/index.css.gz")

idf_component_register(SRCS "${srcs}"
                       PRIV_INCLUDE_DIRS .
                       EMBED_TXTFILES ${html_files}
                       REQUIRES esp_http_client app_update esp_http_server nvs_flash esp_timer
                                 video_server h264_udp ota_manager)

# ESP32 Video Server - Architecture

## Project Hierarchy (Large-Factory Standard)

```
esp_udp_v2/
├── CMakeLists.txt                  # Top-level ESP-IDF project
├── README.md                       # Project overview
├── sdkconfig.defaults              # Default Kconfig options
├── .gitignore
│
├── main/                           # Application entry point
│   ├── CMakeLists.txt
│   ├── idf_component.yml           # Dependencies
│   ├── Kconfig.projbuild           # Menuconfig options
│   └── main.c                      # app_main() - thin entry point
│
├── components/                     # Reusable ESP-IDF components
│   ├── video_server/               # HTTP video streaming server
│   │   ├── include/
│   │   │   └── video_server.h      # Public API
│   │   └── src/
│   │       ├── video_server.c      # Orchestrator
│   │       ├── http_handler.c      # HTTP endpoints (static + REST)
│   │       ├── mjpeg_stream.c      # MJPEG multipart streaming
│   │       ├── camera_manager.c    # V4L2 camera init/capture
│   │       └── camera_manager.h    # Camera context definition
│   │
│   ├── h264_udp/                   # H.264 UDP streaming
│   │   ├── include/
│   │   │   └── h264_udp_stream.h   # Public API
│   │   ├── src/
│   │   │   ├── h264_udp_stream.c   # Stream orchestrator
│   │   │   ├── h264_encoder.c      # M2M H.264 encoder
│   │   │   ├── h264_encoder.h
│   │   │   ├── h264_protocol.c     # UDP packet protocol
│   │   │   └── h264_protocol.h
│   │   └── test/
│   │       └── h264_loopback_test.c # Standalone loopback test
│   │
│   ├── ota_manager/                # OTA firmware updates
│   │   ├── include/
│   │   │   └── ota_manager.h       # Public API
│   │   └── src/
│   │       └── ota_manager.c
│   │
│   ├── esp_video/                  # ESP video driver (ESP-IDF component)
│   └── example_video_common/       # Video common helpers
│
├── frontend/                       # Web UI
│   ├── gzipped/                    # Compressed static assets
│   │   ├── index.html.gz
│   │   ├── loading.jpg.gz
│   │   ├── favicon.ico.gz
│   │   └── assets/
│   │       ├── index.js.gz
│   │       └── index.css.gz
│   ├── mock/                       # Mock development server
│   ├── public/                     # Raw static files
│   └── src/                        # Frontend source code
│
├── scripts/                        # Python tools
│   ├── h264_video_client.py        # PC H.264 video client
│   └── latency_measurement.py      # Latency measurement tool
│
├── tests/                          # Test infrastructure (placeholder)
├── docs/                           # Documentation
│   ├── architecture.md             # This file
│   └── plans/                      # Design documents
├── config/                         # Board-specific configurations
│   ├── sdkconfig.ci
│   ├── sdkconfig.defaults.esp32c3
│   ├── sdkconfig.defaults.esp32p4
│   └── ...
└── pic/                            # Project images
```

## Layer Architecture

```
┌─────────────────────────────────────────────────┐
│                  main.c (Entry)                  │
├─────────────────────────────────────────────────┤
│  video_server     h264_udp       ota_manager    │  ← Business Logic
│  (HTTP/MJPEG)    (UDP/H.264)    (Firmware OTA)  │
├─────────────────────────────────────────────────┤
│  camera_manager   h264_encoder  h264_protocol   │  ← Domain Logic
├─────────────────────────────────────────────────┤
│  esp_video        example_video_common          │  ← Hardware Abstraction
├─────────────────────────────────────────────────┤
│  ESP-IDF (FreeRTOS, LWIP, HTTP Server, V4L2)    │  ← Platform
└─────────────────────────────────────────────────┘
```

## Key Design Decisions

1. **Component-based architecture**: Each functional module is an independent ESP-IDF component with its own CMakeLists.txt and idf_component.yml
2. **Public/Private separation**: `include/` for public API, `src/` for implementation
3. **Thin entry point**: `main.c` is <100 lines, delegates all logic to components
4. **Single-responsibility**: Each source file handles one concern (camera, encoder, protocol, HTTP)
5. **Config isolation**: Board-specific Kconfigs in `config/`, scripts in `scripts/`, docs in `docs/`

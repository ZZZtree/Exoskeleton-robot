# Tests

This directory contains unit tests, integration tests, and test fixtures.

## Test Categories

- **Unit tests**: Individual component tests (camera_manager, h264_encoder, etc.)
- **Integration tests**: Multi-component integration tests
- **System tests**: Full end-to-end tests (requires hardware)

## Running Tests

```bash
# Unit tests via ESP-IDF
cd ../../
idf.py build
idf.py -p <PORT> flash monitor
```

## Test Structure

```
tests/
├── unit/
│   ├── test_camera_manager.c
│   ├── test_h264_encoder.c
│   └── test_h264_protocol.c
├── integration/
│   └── test_video_server.c
└── system/
    └── test_end_to_end.py
```

# ai_brain — 外骨骼机器人 AI 大脑（LLM 推理引擎）

> 本模块承载外骨骼机器人的"AI 大脑"：本地 LLM 推理服务 + 多端接入网关。
> 在 NVIDIA Jetson AGX Orin 64GB 上开发验证（llama.cpp 源码编译路线），
> 此目录为**可移植到其他设备**的完整工作集。

## 技术定位

```
手机/电脑/上位机 ──TCP──▶  home_gateway.py（网关，多用户路由）
                                │
ESP32 设备控制总线 ──UDP──▶      │
                                ▼
                         llama-server (:8080)
                         Qwen 本地模型推理
                                │
                           流式回答 (SSE)
```

## 目录结构

```
ai_brain/
├── server/            # 部署运行核心
│   ├── start_llama_server.sh   # 推理服务启动（16 slot / 64K 池 / KV swap）
│   ├── turbo_mode.sh           # Jetson 性能模式切换（MAXN/省电）
│   ├── home_gateway.py         # 多端 TCP/HTTP 统一网关（会话路由）
│   ├── embed_store.py          # 家庭知识库（共享前缀缓存思想）
│   ├── chat.py / qwen35.py     # 对话客户端示例
│   ├── coldstart_measure.py    # 冷启动测量
│   └── llama-kv-blocks.h       # KV Cache 块级分配器（PagedAttention 概念验证）
├── benchmark/         # 基准/压力测试（移植后验证性能）
├── tools/             # 监控诊断（内存/推理负载/恢复验证）
├── patches/           # llama.cpp 源码级修改（含说明）
├── docs/              # 技术报告与测试结论
├── REPRODUCE.md       # 📋 移植复现清单（版本锁定/外部依赖/修改点）
└── README.md
```

> ⚠️ **移植前先读 `REPRODUCE.md`**——它诚实列出"仓库已含/需外部获取"，
> 并锁定 llama.cpp 版本（0.3.0，2026-08-28 master 快照）与关键修改点。

## 移植到新设备（部署步骤）

### 1. 硬件要求
- ARM64 或 x86_64 Linux，建议 16GB+ 统一内存（CPU/GPU 共享）
- 模型：Qwen3.5-9B Q4_K_M（5.8GB）或 Qwen3.5-2B Q8_0（2.0GB）
  - 下载：`bartowski/Qwen_Qwen3.5-2B-GGUF`（HuggingFace）

### 2. 构建 llama.cpp（含 Jetson 统一内存优化）
```bash
# 源码：llama.cpp master + 以下 patches/
# patches/llama-kv-blocks.h → 放入 src/（KV 块级分配器，可选）
# 本机构建记录见 docs/TECHNICAL_REPORT.md 第三章（含 CMake 参数）
cmake -B build -DGGML_CUDA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

### 3. 启动服务
```bash
# 修改 server/start_llama_server.sh 中的 MODEL 路径后：
bash server/start_llama_server.sh          # llama-server @ :8080
python3 server/home_gateway.py 8000        # 网关 @ :8000
# 验证
curl -N -X POST http://localhost:8000/chat -d '{"user":"alice","message":"你好"}'
```

### 4. 关键配置说明（Jetson 调优成果）
| 参数 | 值 | 作用 |
|---|---|---|
| `-c 65536` | 64K 池 | 16 slot 共享（kv-unified） |
| `--parallel 16 --kv-unified` | 16 并发共享池 | 多用户内存复用 |
| `-fa on` | FlashAttention | 长上下文带宽优化 |
| `-ctk q8_0 -ctv q8_0` | KV 量化 | KV 内存减半 |
| `--cache-prompt --cache-reuse 256` | 前缀缓存 | 共享 system prompt 命中 |
| `--slot-save-path` | KV Swap | 空闲会话换出到磁盘 |

> ⚠️ 脚本内含 Jetson 路径（`/home/rock/...`、`nvpmodel`），移植到非 Jetson 设备时删除 `turbo_mode.sh` 依赖项、按需修改路径。

## 验证清单（移植后必跑）
```bash
cd benchmark/
python3 benchmark_chat.py          # 单会话基准
python3 benchmark_multi_user.py    # 多用户并发基准
python3 ../tools/mem_watch.py      # 内存监控
```
详细测试结果见 `docs/TECHNICAL_REPORT.md`。

## 与本仓库其他模块的关系
| 模块 | 关系 |
|---|---|
| `esp_udp/` | 外骨骼设备控制总线（电机/传感），UDP 上报 |
| `imu_qt_viewer/` | 上位机 IMU 可视化 |
| `ai_brain/` | **大脑**：理解设备上报 + 生成指令 + 语音对话 |

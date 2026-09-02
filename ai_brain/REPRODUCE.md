# 移植复现清单（REPRODUCE）

> 目标：在**新设备**上从零复现本 AI 大脑。本文件诚实列出
> **仓库已包含**与**需要外部获取**的每一项，避免移植踩坑。

## 版本锁定（关键！）

| 组件 | 版本 | 说明 |
|---|---|---|
| llama.cpp | **0.3.0**（2026-08-28 master 快照） | 非 git 源码包（codeload tarball），**无 commit 锁定**；移植时若用新版 master，API 可能变化导致编译/运行失败 |
| 模型 | Qwen3.5-2B Q8_0（2.0GB）/ Qwen3.5-9B Q4_K_M（5.8GB） | 见下方下载 |
| CUDA | 12.6 + cuDNN 9.x（Jetson JetPack 6.2） | 新设备按平台调整 |

## ✅ 仓库已包含（ai_brain/）

- 全部应用脚本：网关、服务启动、对话、知识库（`server/`）
- 全部基准/压力测试（`benchmark/`）
- 监控/验证工具（`tools/`）
- 源码修改说明：Jetson 统一内存 patch（代码+位置）、KV 块级分配器（`patches/`）
- 完整技术报告与部署记录（`docs/`）
- 移植部署指南（`README.md`）

## ❌ 需要外部获取（不在 git）

| 项 | 原因 | 获取方式 |
|---|---|---|
| llama.cpp 源码 | 太大（进 git 会使仓库膨胀） | `curl -L https://codeload.github.com/ggml-org/llama.cpp/tar.gz/refs/heads/master`（建议下载 2026-08-28 前后版本） |
| 模型 GGUF | 2-5.8GB，git 不适合大文件 | 见 README.md / docs |
| 测试数据 | `/tmp` 下过程性基准结果（txt/json） | 仓库 docs 已含汇总结论 |

## 🔧 关键修改点（移植时必查）

### 1. llama.cpp 统一内存 patch（Jetson/APU 类设备）
- 位置：`ggml/src/ggml-cuda/ggml-cuda.cu` → `ggml_cuda_device_malloc()`
- 修改：integrated GPU 自动走 `cudaMallocManaged`（完整代码见 `patches/README.md`）
- 若新设备是**独立显卡**（non-integrated）：不需要此 patch

### 2. 硬编码路径（脚本含 `/home/rock/...`）
| 文件 | 需改 |
|---|---|
| `server/start_llama_server.sh` | `MODEL=`、`BIN=`、`--slot-save-path=` |
| `server/home_gateway.py` | LLAMA_API、embed 路径 |
| `server/chat.py` / `embed_store.py` | 路径常量 |
| `server/turbo_mode.sh` | **仅 Jetson**（nvpmodel），其他平台删除 |

### 3. llama-server 版本依赖的参数
若新 llama.cpp 版本不再支持 `--kv-unified`/`--reasoning`/`--slot-save-path` 等参数，
需调整 `start_llama_server.sh`（参数表见 `docs/TECHNICAL_REPORT.md` §3.3）。

## 🧪 复现验证步骤（照此执行即可确认移植成功）

```bash
# 1. 构建（新设备）
cmake -B build -DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=<你的GPU arch> -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# 2. 起服务
bash server/start_llama_server.sh     # :8080
python3 server/home_gateway.py 8000   # :8000

# 3. 冒烟
curl -N -X POST http://localhost:8000/chat -d '{"user":"alice","message":"你好"}'

# 4. 基准验证（结果应与 Jetson 同量级）
cd benchmark/ && python3 benchmark_chat.py && python3 benchmark_multi_user.py

# 5. 内存/长会话验证
python3 tools/mem_watch.py            # 观察内存峰值
python3 benchmark/test_200.py         # 200 轮长对话
```

## 已知差异（移植到不同硬件时的预期变化）

| 平台 | 预期变化 |
|---|---|
| 独立 GPU（x86+RTX 等） | 无需统一内存 patch；显存即上限，需按显存调小 `-c`/slot |
| 另一台 Jetson | 全量复用（含 patch、sm_87 编译） |
| CPU-only | 去掉 GGML_CUDA，性能降一个量级，2B 模型可用 |

---

> 最后更新：2026-09（Jetson AGX Orin 64GB / JetPack 6.2 实测）

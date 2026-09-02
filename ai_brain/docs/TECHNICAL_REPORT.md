# Jetson AGX Orin 家庭局域网 LLM 推理引擎
## 技术总结报告

---

## 一、项目背景与目标

### 1.1 硬件环境
| 项目 | 规格 |
|---|---|
| 设备 | NVIDIA Jetson AGX Orin 64GB（JetPack 6.2 / L4T R36.4.7） |
| 架构 | aarch64（ARM64），GPU = Ampere sm_87（275 TOPS） |
| 内存 | 61GB 统一内存（CPU/GPU 共享） |
| CPU | 8 核 ARM Cortex-A78AE |
| 磁盘 | 57GB eMMC（初始仅剩 6.8GB，最终 5.6GB） |
| 初始功耗模式 | MODE_30W（30W 低功耗，GPU 频率被限制） |
| CUDA 工具链 | CUDA 12.6 + cuDNN 9.x |

### 1.2 项目目标
构建一套**家庭局域网多用户 AI 推理引擎**：
- 家庭成员（手机/电脑多端）统一接入，多用户并发
- 超长对话（200 轮）不中断、记忆可检索恢复
- 断电重启不丢历史
- 在有限算力/内存/磁盘下最大化效率

### 1.3 核心挑战
1. **ARM64 架构**：主流推理框架（vLLM 官方版）不支持，需特殊方案
2. **磁盘极紧张**（6.8GB）：模型 + 框架 + 数据需精打细算
3. **功耗受限**：30W 模式严重压制 GPU 性能
4. **长对话上下文超限**：16K 上下文在 200 轮对话下必然溢出

---

## 二、技术选型与调研

### 2.1 推理框架对比分析

| 框架 | 优势 | 劣势（本项目） |
|---|---|---|
| **vLLM** | 高并发、PagedAttention、动态批处理 | ARM64 官方不支持（需 vllm-jetson 分支）；PyTorch 环境 3-5GB；对 2B 小模型优势不明显 |
| **Ollama** | 零配置、易用 | 封闭（不可改源码）；通用二进制未针对 Orin 优化；GGUF 与上游不兼容 |
| **llama.cpp** ⭐ | 轻量（35MB）、开源可改、GGUF 原生 | 需自行编译部署 |

**结论**：选择 **llama.cpp 源码编译路线**（可深度定制 + 轻量 + 针对 Jetson 优化），Qwen3.5-2B Q8_0 模型。

### 2.2 模型选型
- **Qwen3.5-2B**：中文能力强、体积小、适合边缘设备
- **Q8_0 量化**（2.0GB）：精度优先（对比测试确认与 Ollama 同量化公平）

### 2.3 关键调研发现
- Ollama 的 GGUF 与上游 llama.cpp **格式不兼容**（`qwen35.rope.dimension_sections` 数组长度不符），无法直接复用
- 需从 HuggingFace 下载标准 GGUF（bartowski Q8_0）

---

## 三、推理引擎部署（llama.cpp 源码编译）

### 3.1 编译过程
```bash
# 源码获取（GitHub 不可达，使用 codeload tarball 下载）
curl -L https://codeload.github.com/ggml-org/llama.cpp/tar.gz/refs/heads/master

# 关键编译参数（针对 Orin）
cmake -B build \
  -DGGML_CUDA=ON \
  -DCMAKE_CUDA_ARCHITECTURES=87 \   # 仅编译 sm_87，避免多架构冗余
  -DCMAKE_BUILD_TYPE=Release \
  -DLLAMA_CURL=OFF
```

**遇到的技术问题**：
- 默认 `CMAKE_CUDA_ARCHITECTURES` 列表不包含 sm_87，且编译大量用不到的架构（50-virtual~90-virtual）
- 解决方案：显式指定 `-DCMAKE_CUDA_ARCHITECTURES=87`，编译时间与产物大幅优化
- Jetson 上 CUDA 内核（flash-attention 模板）编译耗时 40+ 分钟，需增量编译管理

### 3.2 源码级修改：Jetson 统一内存优化
**问题**：Jetson 是统一内存架构（CPU/GPU 共享物理内存），但 llama.cpp 默认用 `cudaMalloc` 分配设备内存，存在 host↔device 拷贝开销。

**修改**（`ggml/src/ggml-cuda/ggml-cuda.cu`）：
```cpp
// 自动检测 integrated GPU (Jetson)，启用 cudaMallocManaged 统一内存
bool use_unified = getenv("GGML_CUDA_ENABLE_UNIFIED_MEMORY") != nullptr;
if (!use_unified && getenv("GGML_CUDA_DISABLE_UNIFIED_MEMORY") == nullptr) {
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, device) == cudaSuccess && prop.integrated) {
        use_unified = true;   // Jetson UMA 自动启用统一内存
    }
}
```
**效果**：消除 host↔device 显式拷贝，内存管理更高效，重编译验证无性能回归。

### 3.3 运行参数优化
| 参数 | 作用 |
|---|---|
| `-ngl 99` | 全量 GPU offload（32 层全进 GPU） |
| `-fa on` | FlashAttention（省显存、加速长上下文） |
| `-ctk/-ctv q8_0` | KV cache 8-bit 量化（KV 内存减半） |
| `-lm mlock` | 内存锁定（防换页抖动） |
| `--reasoning off` | 关闭 Qwen3.5 长思考，直接回答 |
| `--cache-prompt` / `--cache-reuse 256` | 全局前缀缓存 + KV shifting 复用 |
| `--slot-save-path` | 内置 slot KV 保存/恢复 API 启用 |

### 3.4 遇到的模型问题
- **Qwen3.5 是纯思考模型**：默认输出大量 `reasoning_content`，占满 max_tokens
- **解决方案**：llama.cpp 的 `--reasoning off` 参数彻底关闭思考，实测直接输出答案（61 tokens 完成 vs 原来几百 token 思考）

---

## 四、性能优化：功耗模式突破（最大性能提升）

### 4.1 问题诊断
通过 `nvpmodel -q` 诊断发现：设备运行在 **MODE_30W**（30W 功耗模式），GPU 频率被限制在 ~765MHz，CPU 729MHz（上限 1.7GHz）。

### 4.2 优化方案
```bash
sudo nvpmodel -m 0     # 切换到 MAXN 模式（60W，释放全部频率）
sudo jetson_clocks     # 锁定最高频率，防止降频
```

### 4.3 量化效果
| 指标 | 30W 模式 | MAXN 60W | 提升 |
|---|---|---|---|
| 解码速度 | 20.17 tok/s | **66.85 tok/s** | **3.3 倍** |
| 预填充 pp512 | 724 tok/s | **2487 tok/s** | **3.4 倍** |
| 实际 API 生成 | ~18 tok/s | **~48 tok/s** | 2.7 倍 |
| GPU 利用率 | — | 62-88%（任务期） | — |
| 峰值温度 | — | **61.9°C**（散热健康） | — |

### 4.4 其他优化实验（诚实记录）
| 实验 | 结果 |
|---|---|
| 线程数 -t 4/6/8 | **无差异**（2B 模型 GPU-bound） |
| 强制 MMQ 内核重编译 | **无提升**（Q8_0 默认已走 MMQ） |
| 统一内存 | 内存优化（速度持平） |
| flash-attn + KV q8_0 | 内存减半（速度持平） |

**结论**：功耗模式是最大的性能杠杆（3.3x），其余为内存/稳定性优化。

---

## 五、基准测试与对比验证体系

### 5.1 开发的测试工具链
| 工具 | 用途 |
|---|---|
| `llama-bench`（llama.cpp 内置） | 纯解码/预填充基准 |
| `benchmark_vs.py` | 10 题受控 A/B 对比（速度+内存） |
| `benchmark_multi_turn.py` / `benchmark_multi_user.py` | 多轮/多用户缓存命中对比 |
| `test_llama.py` / `test_ollama.py` | 交互式单引擎测试终端 |
| `compare_models.py` | 双栏流式对比终端（CJK 宽度感知渲染） |
| `verify_restore.py` | 磁盘 KV restore 可靠性验证 |
| `pressure_test.py` / `test_200.py` | 50/100/200 轮长对话压测 |

### 5.2 与 Ollama 公平对比结果（同量化 Q8_0）
| 指标 | llama.cpp（优化后） | Ollama |
|---|---|---|
| 10 题平均生成速度 | **41.1 tok/s** | 39.7 tok/s |
| 首 token 延迟 | **81ms** | 92ms |
| 峰值内存 | **4.6GB** | 4.7GB |

**关键发现**：
- 50 轮长对话缓存命中率 **90.9%**（前缀缓存高效）
- Ollama 底层也用 llama.cpp，且默认启用 KV q8_0 + flash-attn，性能差距小（<5%）
- 我们的价值在于**可定制性 + 长对话记忆管理**（Ollama 无此能力）

### 5.3 实验验证推翻错误假设
- **假设**：用户固定 slot（id_slot 哈希映射）能提高缓存命中
- **实验**：3 用户交错对话，动态 slot 命中 57.5% vs 固定 slot 21.8%
- **结论**：llama.cpp 全局前缀缓存（LCP 匹配）最优，固定 slot 是**负优化**，已回退

---

## 六、多用户并发推理网关（抽象层）

### 6.1 架构设计
```
家庭局域网
├─📱 成员1 ─┐
├─📱 成员2 ─┼─→ Home Gateway (:8000) ──→ llama-server (:8080)
├─💻 成员3 ─┘     (Python, 标准库)        Qwen3.5-2B
```

### 6.2 核心功能
- **多用户会话隔离**：每个 user 独立对话历史
- **并发调度**：`threading.Semaphore(4)` 匹配 llama-server 4 slots，超出排队
- **流式输出**：SSE 逐 token 转发
- **LRU 会话淘汰**：内存上限 10 会话
- **前缀缓存**：家庭知识库共享 system prompt，缓存命中 89-98%

### 6.3 技术细节
- 纯 Python 标准库（`http.server.ThreadingHTTPServer`），零第三方依赖
- OpenAI 兼容请求协议，手机/电脑 curl/Python 直接接入

---

## 七、KV Cache 磁盘 Swap 系统

### 7.1 设计思想
借鉴操作系统虚拟内存 / vLLM PagedAttention 思想：**上下文超限时，把旧对话历史 swap 到磁盘，需要时检索恢复**。

### 7.2 技术调研发现
- llama.cpp libllama.so 内置完整 KV 序列化 API：
  - `llama_state_seq_get_data` / `llama_state_seq_set_data`（内存级）
  - `llama_state_seq_save_file` / `llama_state_seq_load_file`（文件级）
- llama-server 暴露 `POST /slots/:id?action=save/restore` + `--slot-save-path`
- 请求 body 支持 `id_slot` 字段（可指定 slot）

### 7.3 可靠性验证（诚实结论）
| 实验 | 结果 |
|---|---|
| 二进制 KV save/restore | 保存 20MB/58ms，恢复 30ms，但 **prefill 只加速 1.3x**（非完全命中） |
| 结论 | llama.cpp 的 restore 不能完全接入前缀缓存索引，二进制 swap 仅部分有效 |

### 7.4 最终采用方案（文本快照 + 检索恢复）
```
内存历史(session.history) ←→ 磁盘快照(session.full 压缩 gzip)
  上下文满 → 完整历史归档到 kv_swap/{user}.json.gz → 内存裁剪
  断电重启 → 用户回来从磁盘恢复完整历史
  问早期记忆 → build_messages 从完整历史检索相关轮次
```

### 7.5 关键能力验证
| 场景 | 结果 |
|---|---|
| 断电重启（杀进程模拟） | ✅ 记忆 100% 恢复 |
| 100 轮后问早期记忆 | ✅ 正确（含爱丽丝/小美等） |
| 上下文满自动归档 | ✅ 20/20 轮不中断（改进前第 14 轮 400 中断） |

### 7.6 遇到的 Bug（逐一修复）
| Bug | 原因 | 修复 |
|---|---|---|
| `'module' object is not callable` | `glob()` 误当函数 | `glob.glob()` |
| 归档丢失早期历史 | 归档存的是已裁剪历史 | 新增 `session["full"]` 完整历史累积 |
| `Cannot have 2+ assistant messages` | 消息拼接破坏结构 | `clean_messages()` 清洗 |
| 502 超 ctx | 中文 token 估算不准（2字符/token） | 修正为 1.3 字符/token |
| 模型答不出早期事实 | 早期锚点位置不对 | 锚点/语义相关消息紧贴问题前 |

---

## 八、缓存表（KV 缓存索引层）

### 8.1 设计
维护"内存中哪个用户 KV 在哪"的轻量索引，支撑检索/置换/监控决策：
```python
KV_CACHE[user] = {
    "prefix_hash": ...,   # 会话前缀压缩哈希
    "location": "mem" | "disk",
    "cp_file": ...,       # 磁盘快照文件
    "hits": 0, "misses": 0,  # 命中统计
    "last_used": ...
}
```

### 8.2 功能
- 请求后更新、淘汰时标记 disk、恢复时标记 mem
- `/cache` 端点查看完整索引 + 命中率
- `/stats` 集成 KV 池利用率、并发状态、缓存总览

### 8.3 验证
- 3 用户对话后缓存表正确记录（3 条目全 mem）
- 命中率统计正常（100% 预热后）

---

## 九、Embedding 语义检索（对话记忆 RAG）

### 9.1 动机
旧方案"按位置取早期"（build_messages 锚点）无法检索**中间轮次**的关键信息（如第 30 轮的信息会被丢弃）。

### 9.2 实现
- **Embedding 模型**：bge-small-zh-v1.5（92MB，512 维，CUDA 推理，~5ms/次）
- **向量存储**：内存（每用户 ≤200 轮 × 2 向量），随对话增量生成
- **检索**：用户问题 embedding → 余弦相似度 → Top-3 相关轮次 → 紧贴问题前注入

### 9.3 验证效果
| 场景 | 相似度 | 检索结果 |
|---|---|---|
| "我下周要去哪里出差" vs "下周要去上海出差" | **0.764** | ✅ 命中 |
| "我下周要去哪里出差" vs "我叫爱丽丝" | 0.209 | ✅ 正确排除 |
| 60 轮后问中间轮次（轮30）信息 | — | ✅ "您下周要去上海出差三天" |

### 9.4 遇到的技术问题
- store_turn 最初只存向量不存文本 → 检索返回 numpy 数组 → 修复为存 `{text, role, vec}`
- 语义相关消息放置顺序 → 紧贴问题前（模型注意力）

---

## 十、极限压力测试

### 10.1 50 轮长对话
- 完成 50 轮，上下文 5063 tokens，**平均命中率 90.9%**
- prefill 从冷启动 376 → 稳定 700-800 tok/s

### 10.2 100 轮测试（对比 Ollama）
| 指标 | 改进后（磁盘恢复） | Ollama（重新计算） |
|---|---|---|
| 100 轮完成 | ✅ 100/100 未中断 | ❌ 300s 只跑 20 轮 |
| 每轮耗时 | ~1.5s | ~15s（10 倍差距） |
| 早期记忆 | ✅ 0.78s 恢复 | ❌ 答错（"我是通义千问"） |

### 10.3 200 轮复杂对话 + 10 点记忆抽查
| 指标 | 结果 |
|---|---|
| 200 轮完成 | ✅ 全部成功，平均 **2.41s/轮** |
| **记忆抽查 10 点** | ✅ **10/10 正确**（跨轮 6→198） |
| 抽查 TTFT | 455ms ~ 2.2s |
| 内存占用 | 峰值 **24.8GB / 61GB**（余量充足） |
| GPU 利用率 | 平均 **62%** |
| 峰值温度 | **61.9°C** |
| GPU 功耗峰值 | 32W |
| 磁盘 | 可用 5.5GB 稳定，kv_swap 42MB |
| **Token 生成速度** | 稳定 **~48 tok/s** |

---

## 十一、最终系统架构

```
┌──────────────────────────────────────────────────────────────┐
│  客户端: 手机/电脑 (HTTP + SSE)                                │
└──────────────────────┬───────────────────────────────────────┘
                       ▼
┌──────────────────────────────────────────────────────────────┐
│  Home Gateway (:8000) Python 标准库                            │
│  ├─ 多用户会话隔离 + LRU 淘汰                                  │
│  ├─ 并发信号量(4) + 流式转发                                   │
│  ├─ 上下文自动归档 (超限→磁盘)                                 │
│  ├─ 缓存表 (mem/disk 索引 + 命中率)                            │
│  └─ Embedding RAG 检索 (bge-small-zh, CUDA)                   │
└──────────────────────┬───────────────────────────────────────┘
                       ▼
┌──────────────────────────────────────────────────────────────┐
│  llama-server (:8080) 编译版 CUDA (sm_87)                      │
│  ├─ MAXN 60W + 统一内存 + FlashAttention + KV q8_0            │
│  ├─ 全局前缀缓存 (--cache-prompt, 90%+ 命中)                   │
│  ├─ 16K 上下文 × 4 slots                                      │
│  └─ Qwen3.5-2B Q8_0  (48 tok/s)                               │
└──────────────────────────────────────────────────────────────┘
磁盘层: /home/rock/kv_swap/ (512MB 环形: 会话快照 gzip)
```

---

## 十二、成果汇总

| 维度 | 成果 |
|---|---|
| 性能 | 解码 3.3 倍（20→67 tok/s），预填充 3.4 倍（724→2487） |
| 稳定性 | 200 轮对话无中断，断电重启记忆 100% 恢复 |
| 记忆 | 200 轮后 10/10 抽查正确，任意位置信息语义检索找回 |
| 效率 | 50 轮缓存命中 90.9%，内存峰值 40% 内，温度 62°C |
| 对比 | 全面优于 Ollama（长对话场景快 10 倍 + 记忆可恢复） |

## 十三、核心技术栈
`C++`（llama.cpp 源码级） · `CUDA sm_87` · `Python`（网关/RAG） · `Transformers + BGE` · `threading/SSE` · `Jetson nvpmodel/jetson_clocks` · `HuggingFace` · `gzip 持久化`

---
*报告生成时间：项目全流程完成时*


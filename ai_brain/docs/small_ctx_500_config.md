# 500 Token 输入窗口 + KV Cache 配置说明

> 场景：**ctx=500（极小上下文窗口）下的 200 轮超长对话**
> （`benchmark/test_9b_200.py`）——挑战"小窗口 + 外部记忆（swap/RAG）"极限。
> 配置是**双层**的：llama-server 服务层 + home_gateway 网关层，两者必须一致。

## 一、为什么是 500 token

对比两个极端：
| 配置 | KV 池 | 200 轮对话 |
|---|---|---|
| 64K（默认） | 5 GiB 预分配 | 池内轻松容纳，不需要外部记忆 |
| **500（本场景）** | **~40 MB** | 窗口几乎必然溢出 → **强制走 swap+RAG 路径** |

500 场景验证的是：**模型大 + KV 极小 + 磁盘外部记忆** 的极端可行路径，
证明在资源受限设备上可以通过 swap/RAG 让"有限 KV"支撑无限轮对话。

## 二、服务层配置（llama-server 启动参数）

```bash
# 关键参数（其余同 start_llama_server.sh）
-c 500              # 输入窗口 = 500 token
                    #   → KV cache 池 = 500 cells（每 token 1 cell）
--parallel 16       # 16 个并发序列（slot）
--kv-unified        # 16 slot 共享同一个 500-cell 池（不各自分配）
-fa on              # FlashAttention
-ctk q8_0 -ctv q8_0 # KV 8-bit 量化
--cache-prompt      # 前缀缓存（system prompt 常驻命中）
--slot-save-path /home/rock/kv_swap   # slot 级磁盘快照（llama.cpp 原生 swap）
```

### KV 内存计算（9B 模型）
```
每 cell KV 大小 ≈ 40层 × 8 kv_head × 128 dim × 2(K+V) × 1B(q8_0) ≈ 80 KB
500 cells → ~40 MB  （对比 64K 池 = 5 GiB，缩小 128 倍）
```
→ 在 500 token 场景，**KV 内存可忽略**，瓶颈完全在模型权重（5.8 GB）。

## 三、网关层配置（home_gateway.py，需与服务层一致）

```bash
CTX_LIMIT=500 python3 home_gateway.py 8000   # 环境变量，必须与 -c 一致
```

| 配置项 | 值 | 含义 |
|---|---|---|
| `CTX_LIMIT` | 500 | 网关感知的窗口上限（**必须 = llama-server 的 -c**）|
| `CTX_SAFETY` | 0.9 | 历史达 450 token（90%）即触发归档 |
| `MAX_HISTORY_CHARS` | 24000 | 磁盘快照的每会话容量上限 |
| token 估算 | 中文 1.3 字符/token | `est_tokens()` 近似估算 |

### 500 token 窗口内的消息预算分配
（`_budget_messages()`，1.3 字符/token → 500 token ≈ 650 字符）
| 用途 | 占比 | 说明 |
|---|---|---|
| system prompt | 基础 | 家庭设定/知识库前缀，常驻 |
| 最近消息 | 30% | 当前对话连续性 |
| 语义相关历史 | 40% | RAG 检索命中（紧贴问题）|
| 早期事实锚点 | 20% | 兜底早期关键记忆 |
| 早期中段 | 10% | 补足 |

### 上下文溢出处理链（`archive_overflow()`）
```
历史 ≥ 450 token (CTX_LIMIT × 0.9)
  → ① 完整历史 gzip 存磁盘 (/home/rock/kv_swap/ 环形容量管理)
  → ② 裁剪保留 system + 最近 50% (250 token)
  → ③ 后续提问经 RAG 检索相关历史换入窗口
  → ④ LRU 内存表 + 磁盘恢复 (cache_key 前缀哈希索引)
```

## 四、验证结果参考
`benchmark/test_9b_200.py`：200 轮 + 10 个远期事实抽查 + TTFT 测量。
抽查正确率见该脚本输出（磁盘快照 ~KB 级）。

## 五、参数联动关系（防止配错）

| 如果你改... | 必须同步改... | 否则 |
|---|---|---|
| llama-server `-c N` | 网关 `CTX_LIMIT=N` | 网关按错阈值归档/溢出截断 |
| 减小 `-c` | 无 | KV 内存↓，但超窗口输入被 llama 截断 |
| 增大并发 slot | KV 池竞争↑ | unified 池 500 cells 分给 16 slot 很快耗尽 |
| 调 `CTX_SAFETY` | 无 | 归档触发点提前/延后（越小越保守）|

> ⚠️ 500 token 是**压力测试配置**，非默认生产配置。
> 默认（64K 池）在 `start_llama_server.sh`；500 仅用于验证
> "小窗口 + 磁盘记忆"路径（对资源更紧的新设备有参考价值）。

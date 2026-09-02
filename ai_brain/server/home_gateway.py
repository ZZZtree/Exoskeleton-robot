#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
🏠 家庭局域网 LLM 网关 (轻量级并发引擎抽象层)
==============================================
- 多路 TCP/HTTP 客户端 (手机/电脑/平板) 统一接入
- 多用户会话隔离 + 并发请求调度
- 内存可复用: 共享 system prompt 前缀缓存 + 会话 LRU + 上下文裁剪
- 流式输出 (SSE)

用法:
  python3 /home/rock/home_gateway.py [端口]      # 默认 8000
  curl -N -X POST http://<本机IP>:8000/chat \
       -d '{"user":"alice","message":"你好"}'

依赖: 仅 Python 标准库; 后端 llama-server 运行在 :8080
"""
import gzip
import glob
import hashlib
import json
import os
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    from embed_store import get_store as get_embed_store
except ImportError:
    get_embed_store = None

# ---------------- 配置 ----------------
LLAMA_API = "http://localhost:8080/v1/chat/completions"
# 家庭知识库 = 共享前缀 (vLLM 前缀缓存思想的落地点)
# 所有成员共享这段 KV → 只计算一次, 后续请求全部命中缓存
SYSTEM_PROMPT = (
    "你是家庭智能助手。"
    "家庭成员：alice(妈妈,喜欢烹饪和园艺)、bob(爸爸,喜欢运动和数码)、carol(女儿,12岁学生)。"
    "家庭规则：1.晚上10点后保持安静 2.饮食清淡健康 3.周日上午家庭活动。"
    "回答要求：用中文、简洁、不超过3句话，不知道就直说。"
    "常用信息：家在北京市朝阳区，水费支付宝缴纳，物业电话12345678。"
)
MAX_HISTORY_CHARS = 24000   # 每会话历史上限(字符,≈16K token), 超出裁剪保留最近
MAX_SESSIONS = 10           # 最大会话数, 超出按 LRU 淘汰
DEFAULT_MAX_TOKENS = 256
KV_SWAP_DIR = "/home/rock/kv_swap"   # KV 交换磁盘区域 (环形缓冲)
KV_SWAP_MAX_BYTES = 512 * 1024 * 1024  # 磁盘区域上限 512MB, 满则环形覆盖最旧
N_SLOTS = 16                    # llama-server 并发 slot 数 (n_parallel 16, kv-unified 共享池)
CTX_LIMIT = int(os.environ.get("CTX_LIMIT", 16384))  # llama-server 上下文 (与 -c 一致)
CTX_SAFETY = 0.9                # 触发归档的安全阈值 (达到90%即归档)


def est_tokens(text):
    """粗略估算 token 数 (中文家庭对话: ~2字符/token, 保守估计)。"""
    return max(1, len(text) // 2)


def clean_messages(msgs):
    """清洗消息结构: 移除连续同角色, 确保以 user 消息结尾 (llama.cpp 要求)。"""
    if not msgs:
        return msgs
    cleaned = [msgs[0]]                       # system 保留
    for m in msgs[1:]:
        if m.get("role") == cleaned[-1].get("role"):
            continue                          # 跳过连续同角色
        cleaned.append(m)
    while cleaned and cleaned[-1].get("role") != "user":
        cleaned.pop()                         # 结尾必须是 user
    return cleaned if cleaned else msgs


def build_messages(user, session):
    """检索恢复: 语义RAG检索相关轮次(贴问题) + 最近对话 + 早期锚点。
    顺序: system + 早期中段 + 最近对话 + 语义相关(贴问题) + 新问题"""
    archived = session.get("full") or load_session_from_disk(user)
    full = archived if archived else session["history"]
    budget = int(CTX_LIMIT * 0.7 * 1.3)     # 字符预算
    sys_msg = [full[0]]
    body = list(full[1:])
    if not body:
        return full
    # 抽出最后的问题 (单独放最后)
    question = None
    if body and body[-1].get("role") == "user":
        question = body.pop()
    if not body:
        return full + ([question] if question else [])
    # 语义检索: 与问题最相关的历史轮次 (RAG)
    relevant = []
    if get_embed_store is not None:
        try:
            relevant = get_embed_store().retrieve(user, question["content"])
        except Exception:
            relevant = []
    # 分段: 早期中段(前30%) + 最近(后40%) + 早期事实锚点(最早2轮)
    n = len(body)
    early = body[:4]                     # 最早 2 轮 (关键事实)
    rest_head = body[4:max(4, n * 30 // 100)]
    tail = body[-max(1, n * 40 // 100):]
    # 预算: 语义相关40%(RAG核心) + 最近30% + 早期锚点20% + 早期中段10%
    base = len(sys_msg[0].get("content", ""))
    t_b = int((budget - base) * 0.3)
    r_b = int((budget - base) * 0.2)
    e_b = int((budget - base) * 0.4)
    h_b = int((budget - base) * 0.1)
    result = list(sys_msg)
    for m in rest_head:
        if h_b <= 0: break
        h_b -= len(m.get("content", "")); result.append(m)
    # 早期事实锚点 (兜底)
    for m in early:
        if r_b <= 0: break
        r_b -= len(m.get("content", "")); result.append(m)
    for m in tail:
        if t_b <= 0: break
        t_b -= len(m.get("content", "")); result.append(m)
    # 语义相关轮次 (RAG, 紧贴问题前)
    for m in relevant:
        if e_b <= 0: break
        e_b -= len(m.get("content", "")); result.append(m)
    if question:
        result.append(question)
    return clean_messages(result)


def archive_overflow(user, session):
    """上下文满自动归档: 把完整历史 swap 到磁盘, 裁剪保留最近, 返回是否归档。"""
    text = "".join(m.get("content", "") for m in session["history"])
    if est_tokens(text) < int(CTX_LIMIT * CTX_SAFETY):
        return False
    # 1. 归档完整历史到磁盘 (swap out, 可恢复)
    save_session_to_disk(user, session)
    # 2. 裁剪: 保留 system + 最近约50% ctx tokens (中文约1.3字符/token)
    kept = [session["history"][0]]
    budget = int(CTX_LIMIT * 0.5 * 1.3)     # 字符预算
    for m in reversed(session["history"][1:]):
        budget -= len(m.get("content", ""))
        if budget < 0:
            break
        kept.insert(1, m)
    old_n = len(session["history"])
    session["history"] = kept
    print(f"[归档] {user} 上下文超限 → 完整历史({old_n}条)已存磁盘, 裁剪为{len(kept)}条",
          flush=True)
    return True


# ---------------- 缓存表 (KV 缓存索引层) ----------------
# 记录每个用户会话的 KV 缓存状态: 内存活跃 / 磁盘快照
# "压缩信息" = 前缀哈希 + token数 + 位置, 不存 KV 本身 (轻量索引)
KV_CACHE = {}   # user -> {prefix_hash, n_tokens, location, cp_file, last_used, hits, misses}
CACHE_LOCK = threading.Lock()


def cache_key(history):
    """计算会话历史的前缀检索键 (压缩哈希, 前2000字符决定)。"""
    text = "".join(m.get("content", "") for m in history)
    return hashlib.md5(text[:2000].encode()).hexdigest()[:16]


def cache_lookup(user, history):
    """查缓存表: 用户有可用缓存记录(mem/disk)即命中。"""
    with CACHE_LOCK:
        e = KV_CACHE.get(user)
        if e:
            e["hits"] += 1
            e["last_used"] = time.time()
            return dict(e)
    return None


def cache_update(user, history, location, cp_file=None):
    """更新缓存表条目 (内存活跃或磁盘快照)。"""
    with CACHE_LOCK:
        prev = KV_CACHE.get(user, {})
        KV_CACHE[user] = {
            "prefix_hash": cache_key(history),
            "n_tokens": len(history),
            "location": location,
            "cp_file": cp_file or prev.get("cp_file"),
            "last_used": time.time(),
            "hits": prev.get("hits", 0),
            "misses": prev.get("misses", 0),
        }


def cache_summary():
    """缓存表统计。"""
    with CACHE_LOCK:
        mem = sum(1 for e in KV_CACHE.values() if e["location"] == "mem")
        disk = sum(1 for e in KV_CACHE.values() if e["location"] == "disk")
        hits = sum(e["hits"] for e in KV_CACHE.values())
        misses = sum(e["misses"] for e in KV_CACHE.values())
        return {"entries": len(KV_CACHE), "mem": mem, "disk": disk,
                "hits": hits, "misses": misses,
                "hit_rate": f"{hits/(hits+misses)*100:.1f}%" if hits+misses else "N/A"}


def user_slot(user):
    """用户 → 固定 slot (确定性 hash): 每个用户 KV 常驻专属 slot。"""
    h = hashlib.md5(user.encode()).hexdigest()[:8]
    return int(h, 16) % N_SLOTS


# ---------------- KV 磁盘交换 (环形缓冲) ----------------
def _swap_path(user):
    return os.path.join(KV_SWAP_DIR, f"{user}.json.gz")


def _enforce_swap_capacity():
    """磁盘 swap 区域环形管理: 超过上限则删除最旧的文件。"""
    if not os.path.isdir(KV_SWAP_DIR):
        os.makedirs(KV_SWAP_DIR, exist_ok=True)
        return
    files = sorted(glob.glob(os.path.join(KV_SWAP_DIR, "*.json.gz")),
                   key=os.path.getmtime)
    total = sum(os.path.getsize(f) for f in files)
    while total > KV_SWAP_MAX_BYTES and len(files) > 1:
        oldest = files.pop(0)
        total -= os.path.getsize(oldest)
        os.remove(oldest)
        print(f"[磁盘swap] 环形覆盖: 删除最旧 {os.path.basename(oldest)} (回收{os.path.getsize(oldest)//1024}KB)")


def save_session_to_disk(user, session):
    """把会话完整历史 (KV快照) 换出到磁盘。"""
    try:
        _enforce_swap_capacity()
        history = session.get("full") or session.get("history") or []
        with gzip.open(_swap_path(user), "wt", encoding="utf-8") as f:
            json.dump(history, f, ensure_ascii=False)
    except Exception as e:
        print(f"[磁盘swap] 保存失败 {user}: {e}", flush=True)


def load_session_from_disk(user):
    """从磁盘换入会话 KV 快照 (历史对话), 找不到返回 None。"""
    try:
        p = _swap_path(user)
        if not os.path.exists(p):
            return None
        with gzip.open(p, "rt", encoding="utf-8") as f:
            history = json.load(f)
        print(f"[磁盘swap] 换入会话 {user} ({len(history)} 条消息)")
        return history
    except Exception as e:
        print(f"[磁盘swap] 读回失败 {user}: {e}")
        return None

# ---------------- 会话管理 ----------------
LOCK = threading.Lock()
SESSIONS = {}               # user -> {"history": [...], "last": ts}
CONCURRENCY = threading.Semaphore(16)  # 按需分配: 最多16个并发(匹配16 slots), 超出排队
KV_POOL_TOKENS = 65536   # llama-server KV池上限 (kv-unified 共享池, -c 65536)
KV_USED = 0                 # 当前估算已用 KV token
KV_STATS_LOCK = threading.Lock()


def get_session(user):
    """获取(或创建)用户会话: 内存 LRU + 磁盘 swap 换入。"""
    with LOCK:
        now = time.time()
        s = SESSIONS.get(user)
        if s is None:
            history = None
            # 磁盘 swap: 内存中没有时, 尝试从磁盘找回历史对话 (KV快照)
            if len(SESSIONS) >= MAX_SESSIONS:
                oldest = min(SESSIONS, key=lambda u: SESSIONS[u]["last"])
                evicted = SESSIONS.pop(oldest)
                print(f"[网关] 会话淘汰: {oldest} → 换出到磁盘")
                save_session_to_disk(oldest, evicted)   # 内存→磁盘 swap out
                cache_update(oldest, evicted["history"], "disk")  # 缓存表: 标记磁盘
            else:
                history = load_session_from_disk(user)  # 磁盘→内存 swap in
            if history is None:
                history = [{"role": "system", "content": SYSTEM_PROMPT}]
            s = {"history": history, "full": list(history), "last": now}
            SESSIONS[user] = s
            if len(history) > 1:
                cache_update(user, history, "mem")      # 缓存表: 从磁盘恢复后标记内存
            print(f"[网关] 新会话: {user} (当前 {len(SESSIONS)} 个)")
        s["last"] = now
        return s


def trim_history(history):
    """裁剪历史: 保留 system + 最近的对话, 控制内存占用。"""
    kept = [history[0]]          # system prompt 永远保留 (前缀缓存复用点)
    budget = MAX_HISTORY_CHARS
    for m in reversed(history[1:]):
        budget -= len(m.get("content", ""))
        if budget < 0:
            break
        kept.insert(1, m)
    return kept


# ---------------- 后端调用 ----------------
def call_llama(messages, max_tokens, id_slot=-1, stream_cb=None):
    """调用 llama.cpp; id_slot=-1 动态分配 (llama.cpp 全局前缀缓存效率最高,
    实测固定 slot 会干扰 --cache-prompt 命中, 故默认动态)。"""
    payload = {"messages": messages, "max_tokens": max_tokens,
               "id_slot": id_slot,
               "stream": stream_cb is not None}
    req = urllib.request.Request(
        LLAMA_API, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=600) as resp:
        if stream_cb is None:
            d = json.loads(resp.read())
            return d["choices"][0]["message"].get("content", "")
        buf = []
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                break
            try:
                obj = json.loads(data)
            except json.JSONDecodeError:
                continue
            c = obj["choices"][0].get("delta", {}).get("content")
            if c:
                buf.append(c)
                stream_cb(c)          # 逐块转发给客户端
        return "".join(buf)


# ---------------- HTTP 处理 ----------------
class GatewayHandler(BaseHTTPRequestHandler):
    server_version = "HomeGateway/1.0"

    def log_message(self, fmt, *args):
        print(f"[HTTP] {self.address_string()} {fmt % args}")

    def _send_json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self._send_json(200, {"status": "ok", "sessions": len(SESSIONS)})
        elif self.path == "/stats":
            with LOCK:
                info = {u: {"messages": len(s["history"]),
                            "chars": sum(len(m.get("content", "")) for m in s["history"]),
                            "idle_s": int(time.time() - s["last"])}
                        for u, s in SESSIONS.items()}
            with KV_STATS_LOCK:
                kv_used = KV_USED
            self._send_json(200, {
                "kv_pool_tokens": KV_POOL_TOKENS,
                "kv_used_estimate": kv_used,
                "kv_utilization": f"{kv_used / KV_POOL_TOKENS * 100:.1f}%",
                "concurrent_busy": 16 - CONCURRENCY._value,
                "concurrent_capacity": 16,
                "cache_table": cache_summary(),
                "sessions": info,
                "system_prompt_tokens": len(SYSTEM_PROMPT),
            })
        elif self.path == "/cache":
            self._send_json(200, {
                "summary": cache_summary(),
                "table": {u: {"location": e["location"], "tokens": e["n_tokens"],
                              "cp_file": e["cp_file"], "hits": e["hits"],
                              "misses": e["misses"],
                              "idle_s": int(time.time() - e["last_used"])}
                          for u, e in KV_CACHE.items()},
            })
        elif self.path == "/sessions":
            with LOCK:
                info = {u: {"messages": len(s["history"]),
                            "chars": sum(len(m.get("content", "")) for m in s["history"])}
                        for u, s in SESSIONS.items()}
            self._send_json(200, {"sessions": info,
                                  "system_prompt": SYSTEM_PROMPT})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/chat":
            self._send_json(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            req = json.loads(self.rfile.read(length) or b"{}")
        except Exception as e:
            self._send_json(400, {"error": f"bad request: {e}"})
            return

        user = str(req.get("user", "guest"))
        message = str(req.get("message", "")).strip()
        max_tokens = int(req.get("max_tokens", DEFAULT_MAX_TOKENS))
        want_stream = bool(req.get("stream", True))
        if not message:
            self._send_json(400, {"error": "message is empty"})
            return

        # 会话管理
        session = get_session(user)
        with LOCK:
            session["history"].append({"role": "user", "content": message})
            session["full"].append({"role": "user", "content": message})  # 完整历史累积
            # 上下文满自动归档: 接近 ctx 上限 → 完整历史存磁盘, 裁剪保留最近
            archive_overflow(user, session)
            # 检索恢复: 合并完整历史(早期记忆) + 当前 → 锚点裁剪
            msgs = build_messages(user, session)

        # 缓存表: 请求前查表 (命中率统计; 命中disk可触发恢复)
        cache_entry = cache_lookup(user, session["history"])
        if cache_entry is None:
            with CACHE_LOCK:
                if user in KV_CACHE:
                    KV_CACHE[user]["misses"] += 1

        # 按需分配: 获取并发槽位 (最多4个匹配KV池, 超出排队等待)
        CONCURRENCY.acquire()

        def stream_cb(chunk):
            try:
                self.wfile.write(f"data: {json.dumps({'d': chunk}, ensure_ascii=False)}\n\n".encode())
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass

        if want_stream:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            try:
                ans = call_llama(msgs, max_tokens, -1, stream_cb)
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
            except Exception as e:
                self.wfile.write(f"data: {json.dumps({'error': str(e)}, ensure_ascii=False)}\n\n".encode())
                self.wfile.flush()
                ans = ""
        else:
            try:
                ans = call_llama(msgs, max_tokens, -1)
            except Exception as e:
                CONCURRENCY.release()
                self._send_json(502, {"error": str(e)})
                return

        # 保存回答回会话 (内存可复用: 多轮共享前缀)
        if ans:
            with LOCK:
                session["history"].append({"role": "assistant", "content": ans})
                session["full"].append({"role": "assistant", "content": ans})  # 完整历史累积
                session["history"] = trim_history(session["history"])
                save_session_to_disk(user, session)   # 磁盘 swap: 每轮同步换出
            # 缓存表: 标记该用户 KV 为内存活跃
            cache_update(user, session["history"], "mem")
        # 语义向量存储 (RAG 检索索引)
        if get_embed_store is not None:
            try:
                get_embed_store().store_turn(
                    user, len(session["full"]) // 2,
                    [{"text": message, "role": "user"},
                     {"text": ans, "role": "assistant"}])
            except Exception:
                pass
        CONCURRENCY.release()

        # 估算当前 KV 占用 (按需分配监控)
        with KV_STATS_LOCK:
            global KV_USED
            total_chars = sum(len(m.get("content", "")) for m in session["history"])
            KV_USED = min(KV_POOL_TOKENS, total_chars // 2 + max_tokens)

        # 非流式响应 (流式已在前面发送SSE)
        if not want_stream:
            self._send_json(200, {"user": user, "reply": ans})
            return


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    httpd = ThreadingHTTPServer(("0.0.0.0", port), GatewayHandler)
    print("=" * 54)
    print(f"  🏠 家庭 LLM 网关已启动 (局域网)")
    print(f"     监听: 0.0.0.0:{port}")
    print(f"     后端: llama.cpp :8080 (Qwen3.5-2B Q8_0)")
    print(f"     会话: 共享system前缀缓存 + LRU(最多{MAX_SESSIONS}) + 历史裁剪")
    print(f"     测试: curl -N -X POST http://<本机IP>:{port}/chat "
          f"-d '{{\"user\":\"alice\",\"message\":\"你好\"}}'")
    print("=" * 54)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n网关已停止")
        httpd.server_close()


if __name__ == "__main__":
    main()


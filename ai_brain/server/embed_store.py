#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
对话记忆向量检索模块 (Embedding RAG)
- bge-small-zh 语义向量化 (CUDA)
- 按用户存储每轮对话的向量
- 用户问题 → cosine 相似度 → 检索 Top-K 相关轮次
用法: from embed_store import EmbedStore
"""
import os
import threading

import numpy as np
import torch

BGE_DIR = "/home/rock/models/bge-small-zh-v1.5"
EMBED_DIM = 512
VEC_DIR = "/home/rock/kv_swap/vecs"      # 向量表持久化目录


class EmbedStore:
    def __init__(self, top_k=3, threshold=0.35):
        self.top_k = top_k
        self.threshold = threshold          # 相似度低于此值视为不相关
        self._tok = None
        self._model = None
        self._lock = threading.Lock()
        self.vectors = {}                   # user -> [{"text","vec","turn"}]
        os.makedirs(VEC_DIR, exist_ok=True)

    def _vec_path(self, user):
        return os.path.join(VEC_DIR, f"{user}.npz")

    def save_user(self, user):
        """持久化该用户向量表到磁盘 (重启不丢)。"""
        try:
            entries = self.vectors.get(user)
            if not entries:
                return
            texts, vecs, turns = [], [], []
            for e in entries:
                for it in e["items"]:
                    texts.append(f"{it['role']}||{it['text']}")
                    vecs.append(it["vec"])
                    turns.append(e["turn"])
            np.savez_compressed(self._vec_path(user),
                                texts=np.array(texts, dtype=object),
                                vecs=np.array(vecs), turns=np.array(turns))
        except Exception as e:
            print(f"[向量表] 保存失败 {user}: {e}", flush=True)

    def load_user(self, user):
        """从磁盘加载用户向量表。"""
        try:
            p = self._vec_path(user)
            if not os.path.exists(p):
                return
            data = np.load(p, allow_pickle=True)
            texts, vecs, turns = data["texts"], data["vecs"], data["turns"]
            entries = {}
            for t, v, tn in zip(texts, vecs, turns):
                role, text = t.split("||", 1)
                entries.setdefault(int(tn), {"turn": int(tn), "items": []})
                entries[int(tn)]["items"].append(
                    {"text": text, "role": role, "vec": v})
            self.vectors[user] = [entries[k] for k in sorted(entries)]
            print(f"[向量表] 加载 {user}: {len(self.vectors[user])}轮", flush=True)
        except Exception as e:
            print(f"[向量表] 加载失败 {user}: {e}", flush=True)

    def _load(self):
        if self._model is None:
            from transformers import AutoModel, AutoTokenizer
            self._tok = AutoTokenizer.from_pretrained(BGE_DIR)
            self._model = AutoModel.from_pretrained(BGE_DIR).to("cuda").eval()

    def embed(self, text):
        """单条文本 → 512维归一化向量。"""
        self._load()
        inp = self._tok(text, padding=True, truncation=True, max_length=512,
                        return_tensors="pt").to("cuda")
        with torch.no_grad():
            out = self._model(**inp)
        vec = torch.nn.functional.normalize(out.last_hidden_state[:, 0], p=2, dim=1)
        return vec.cpu().numpy()[0]

    def store_turn(self, user, turn, texts):
        """存储一轮对话的消息向量。texts: [{"text", "role"}]"""
        with self._lock:
            entries = self.vectors.setdefault(user, [])
            items = []
            for t in texts:
                items.append({"text": t["text"], "role": t.get("role", "user"),
                              "vec": self.embed(t["text"])})
            entries.append({"turn": turn, "items": items})
            # 控制单用户向量规模 (最多保存200轮)
            if len(entries) > 200:
                del entries[: len(entries) - 200]
            self.save_user(user)             # 持久化到磁盘

    def retrieve(self, user, question, exclude_recent=5):
        """检索与问题语义最相关的轮次 (排除最近轮+去重, 返回消息 dict 列表)。"""
        with self._lock:
            if user not in self.vectors or not self.vectors[user]:
                self.load_user(user)         # 内存无则从磁盘加载
            entries = self.vectors.get(user)
            if not entries:
                return []
            max_turn = max(e["turn"] for e in entries)
            # 排除最近轮次 (已在上下文tail中, RAG 专注"远处记忆")
            candidates = [e for e in entries if e["turn"] <= max_turn - exclude_recent]
            if not candidates:
                candidates = entries
            q = self.embed(question)
            scored = []
            for e in candidates:
                best = max(float(np.dot(q, it["vec"])) for it in e["items"])
                if best >= self.threshold:
                    scored.append((best, e["turn"]))
            scored.sort(reverse=True)
            by_turn = {e["turn"]: e for e in entries}
            # 去重: 相同/相似的用户问题只保留一次 (避免重复普通问题挤占)
            seen = set()
            top_turns = []
            for _, turn in scored:
                user_texts = [it["text"] for it in by_turn[turn]["items"]
                              if it["role"] == "user"]
                key = user_texts[0][:20] if user_texts else str(turn)
                if key in seen:
                    continue
                seen.add(key)
                top_turns.append(turn)
                if len(top_turns) >= self.top_k:
                    break
            result = []
            for e in entries:
                if e["turn"] in top_turns:
                    for it in e["items"]:
                        result.append({"role": it["role"], "content": it["text"]})
            return result


# 全局单例
_store = None
_store_lock = threading.Lock()


def get_store():
    global _store
    if _store is None:
        with _store_lock:
            if _store is None:
                _store = EmbedStore()
    return _store

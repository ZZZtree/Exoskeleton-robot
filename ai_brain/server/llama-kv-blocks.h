#pragma once
// ============================================================
// llama_kv_blocks - KV Cache 块级分配器 (阶段1)
// 参考 vLLM PagedAttention 的 block 概念:
//   - 将 KV cell 划分为固定大小的块 (block)
//   - 块表 (cell -> block) 映射
//   - 按块粒度分配/回收, 统计块利用率
// 阶段1: 仅逻辑分块 + 块表 + 使用统计 (物理 buffer 仍连续, 内核不改)
//       为阶段2(物理分块/按需分配)与阶段3(内核分块索引)打基础
// ============================================================
#include <cstdint>
#include <cstdio>
#include <vector>

struct llama_kv_blocks {
    uint32_t block_size = 256;             // 每块包含的 cell 数
    uint32_t n_blocks   = 0;               // 块总数
    std::vector<uint32_t> cell_to_block;   // 块表: cell index -> block index
    std::vector<uint32_t> block_used;      // 每块当前已用 cell 数
    std::vector<uint32_t> free_blocks;     // 完全空闲的块 (可回收复用)

    // 初始化: kv_size = 总 cell 数, bs = 块大小
    void init(uint32_t kv_size, uint32_t bs) {
        block_size = bs > 0 ? bs : 256;
        n_blocks   = (kv_size + block_size - 1) / block_size;

        cell_to_block.resize(kv_size);
        block_used.assign(n_blocks, 0);
        free_blocks.clear();
        free_blocks.reserve(n_blocks);

        for (uint32_t i = 0; i < kv_size; ++i) {
            cell_to_block[i] = i / block_size;
        }
        // 初始所有块均空闲
        for (uint32_t b = 0; b < n_blocks; ++b) {
            free_blocks.push_back(b);
        }
    }

    // cell 被分配时调用
    void on_cell_alloc(uint32_t cell) {
        if (cell >= cell_to_block.size()) return;
        const uint32_t b = cell_to_block[cell];
        if (block_used[b] == 0) {
            // 该块从空闲转占用: 从 free_blocks 移除 (标记为已分配)
            for (auto it = free_blocks.begin(); it != free_blocks.end(); ++it) {
                if (*it == b) { free_blocks.erase(it); break; }
            }
        }
        ++block_used[b];
    }

    // cell 被释放时调用
    void on_cell_free(uint32_t cell) {
        if (cell >= cell_to_block.size()) return;
        const uint32_t b = cell_to_block[cell];
        if (block_used[b] > 0) --block_used[b];
        if (block_used[b] == 0) {
            free_blocks.push_back(b);  // 块完全空闲, 回收到空闲列表
        }
    }

    // 当前被占用的块数 (已分配)
    uint32_t used_blocks() const {
        return n_blocks - (uint32_t) free_blocks.size();
    }

    // 完全空闲的块数
    uint32_t get_free_blocks() const {
        return (uint32_t) free_blocks.size();
    }

    // 块空间利用率 = 已分配块 / 总块
    float utilization() const {
        return n_blocks ? (float) used_blocks() / (float) n_blocks : 0.0f;
    }

    // 打印块级统计 (诊断用)
    void print_stats(const char * tag) const {
        uint32_t partial = 0, full = 0;
        for (uint32_t b = 0; b < n_blocks; ++b) {
            if (block_used[b] > 0 && block_used[b] < block_size) ++partial;
            else if (block_used[b] == block_size) ++full;
        }
        std::printf("[KV-blocks] %s: %u blocks (%u cells/block), used=%u, free=%u, "
                    "partial=%u, full=%u, utilization=%.1f%%\n",
                    tag, n_blocks, block_size, used_blocks(), get_free_blocks(),
                    partial, full, utilization() * 100.0f);
    }
};

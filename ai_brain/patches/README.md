# llama.cpp 源码修改（patches）

本目录记录对 llama.cpp 的源码级修改，供移植时重建相同能力。

## 文件清单

| 文件 | 用途 | 放置位置 |
|---|---|---|
| `llama-kv-blocks.h` | KV Cache 块级分配器（PagedAttention 概念验证，独立类 + 单元测试通过） | `llama.cpp/src/` |

## 1. Jetson 统一内存优化（ggml-cuda.cu）

> 源码文件太大不进 git。修改内容与位置如下，移植时手动应用：

**文件**: `llama.cpp/ggml/src/ggml-cuda/ggml-cuda.cu`

**修改位置**: `ggml_cuda_device_malloc()` 函数（约 143-155 行）

**实际代码**（Jetson/UMA 优化）:
```cpp
// Jetson/UMA optimization: auto-enable CUDA managed (unified) memory on integrated GPUs
//   where CPU and GPU share the same physical memory, avoiding explicit host<->device copies.
//   Disable explicitly with GGML_CUDA_DISABLE_UNIFIED_MEMORY=1 if needed.
bool use_unified = getenv("GGML_CUDA_ENABLE_UNIFIED_MEMORY") != nullptr;
if (!use_unified && getenv("GGML_CUDA_DISABLE_UNIFIED_MEMORY") == nullptr) {
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, device) == cudaSuccess && prop.integrated) {
        use_unified = true;
    }
}

if (use_unified) {
    err = cudaMallocManaged(ptr, size);
    ...
}
```
- 关键点：在 `ggml_cuda_device_malloc` 中，若设备是 **integrated GPU**（Jetson/APU 类，CPU/GPU 共享物理内存），分配自动走 `cudaMallocManaged`
- 强制禁用：`export GGML_CUDA_DISABLE_UNIFIED_MEMORY=1`
- 效果：模型权重 + KV 缓存分配在统一内存池，避免显式 host↔device 拷贝

## 2. KV Cache 块级分配器（llama-kv-blocks.h）

参考 vLLM PagedAttention 的 block 概念实现的独立类：

```cpp
llama_kv_blocks blocks;
blocks.init(1024, 256);        // 1024 cells, 每块 256 → 4 块
blocks.on_cell_alloc(cell);    // cell 分配时更新块状态
blocks.on_cell_free(cell);     // cell 释放时回收块
blocks.print_stats("tag");     // 输出块利用率 (used/free/partial/full)
```

**已验证**（单元测试断言全通过）：
- 块表（cell→block）映射正确
- 按块粒度分配/回收正确
- 部分块/满块/空闲块统计正确

**定位**：分块存储的"分配层"基础（阶段 1）。未集成物理分块/内核
（阶段 2/3）——完整 PagedAttention 需重写 KV 张量布局与注意力内核，
在边缘设备上成本收益不匹配（评估记录见 `../docs/`）。

## 3. 移植时编译参数（Jetson AGX Orin 参考）
```bash
cmake -B build -DGGML_CUDA=ON \
  -DGGML_CUDA_F16=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DGGML_NATIVE=OFF
cmake --build build -j8
```

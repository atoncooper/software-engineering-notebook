# §6 - GPU 并行 · CUDA

> 一句话定位：本章把 GPU 的执行模型（SIMT）、内存层级、性能反模式（bank conflict / uncoalesced / low occupancy）一次讲透，并给出 vector add → reduce → GEMM 三个递进手把手案例。
>
> 标记：⭐高频 🔥工程重点 📜论文/标准 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 〇、思维导图

```
                      CUDA
                       │
       ┌───────────────┼───────────────┐
       │               │               │
   执行模型         内存层级         性能反模式
       │               │               │
   SIMT             全局内存         Uncoalesced
   Warp (32)        L2 缓存          Bank Conflict
   Block (CTA)      共享内存         Low Occupancy
   Grid             寄存器           Warp Divergence
   SM               常量/纹理        分支发散
       │               │               │
   调度             Coalesce         优化方法
   warp scheduler   Bank (32)        Tile/Shared
   occupancy        HBM              Vectorized Load
```

---

## 一、问题定义

### 1.1 解决什么问题

GPU 是**吞吐导向**处理器：单线程慢（1.5-2 GHz），但靠数千线程隐藏延迟。问题在于：

1. 怎么把问题映射到 Thread / Block / Grid？
2. 怎么利用共享内存（100x 快于 HBM）？
3. 怎么避免 bank conflict / uncoalesced 等性能陷阱？
4. 怎么估算 occupancy？

本章用三个递进案例（vec_add → reduce → GEMM）把这些问题讲透。

### 1.2 与 CPU 的本质区别

| 维度 | CPU | GPU |
|------|-----|-----|
| 设计目标 | 延迟导向 | 吞吐导向 |
| 核数 | 8-128 | 数千（CUDA core） |
| 单核性能 | 强（深度流水、分支预测） | 弱（顺序执行为主） |
| 缓存 | 大 L3（数十 MB） | 小 L2（数十 MB） + 共享内存 |
| 内存带宽 | ~100 GB/s | 3 TB/s（HBM） |
| 编程模型 | MIMD | SIMT |
| 适合任务 | 控制流密集 | 数据并行密集 |

---

## 二、核心概念与术语

| 术语 | 含义 |
|------|------|
| SM | Streaming Multiprocessor，GPU 计算单元（H100 132 个） |
| CUDA Core | SM 内基本算术单元（H100 每 SM 128 个 FP32） |
| Thread | 最小执行单位 |
| Warp | 32 个线程（NVIDIA）/ 64（AMD wavefront），SIMT 执行单位 |
| Block (CTA) | 线程块，运行在一个 SM 上，可共享内存同步 |
| Grid | Block 的集合 |
| Kernel | 在 GPU 上执行的函数 |
| Global Memory | HBM，所有线程可见，慢（~1μs） |
| Shared Memory | Block 内共享，快（~30ns），按 bank 组织 |
| Register | 线程私有，最快 |
| Constant Memory | 只读，所有线程可见，有缓存 |
| Texture Memory | 只读，2D 空间局部性优化 |
| Occupancy | 活动 warp 数 / 最大 warp 数 |
| Coalesce | Warp 内连续访存合并为少数事务 |
| Bank Conflict | 共享内存多线程访问同 bank 串行化 |
| Warp Divergence | Warp 内分支导致部分线程空闲 |

---

## 三、原理与机制

### 3.1 SIMT 执行模型 🎓

**SIMT (Single Instruction, Multiple Threads)**：32 个线程组成一个 warp，**同一时刻执行同一指令**。如果遇到 `if/else` 分支：

```cpp
if (threadIdx.x < 16) {
    doA();   // 前 16 线程执行
} else {
    doB();   // 后 16 线程执行
}
// 实际执行：doA 时后 16 线程 idle；doB 时前 16 线程 idle
```

⚠️ **warp divergence**：分支使 warp 利用率减半。

### 3.2 Thread / Block / Grid 层次

```
Grid (整个问题)
  │
  ├── Block (0,0)  ──→  运行在 SM0
  │     ├── Warp 0 (32 threads)
  │     ├── Warp 1 (32 threads)
  │     └── ...
  ├── Block (1,0)  ──→  运行在 SM1
  └── ...
```

**关键约束**：
- Block 内线程**可同步**（`__syncthreads()`），可共享内存；
- Block 间**不可同步**（除非用 cooperative kernel），不共享内存；
- Block 大小必须是 32 的倍数（warp 整数倍）；
- 一个 SM 可同时驻留多个 Block（取决于资源）。

### 3.3 内存层级与延迟

| 层级 | 延迟 | 带宽 | 可见性 |
|------|------|------|--------|
| Register | ~1 cycle | 极高 | 线程私有 |
| Shared Memory | ~30 cycle | 数 TB/s | Block 内共享 |
| L1/L2 Cache | ~200 cycle | — | 自动管理 |
| Global Memory (HBM) | ~400-800 cycle | 3.35 TB/s (H100) | 所有线程 |
| Host Memory | ~10 μs (PCIe) | 64 GB/s (PCIe5) | CPU↔GPU |

🔥 **黄金法则**：把热点数据放在 Shared Memory，比 Global Memory 快 10-30 倍。

### 3.4 Coalesced Memory Access

Warp 内 32 个线程访问 32 个**连续且对齐**的 float 时，GPU 会合并为 1 个 128B 事务（1 次访存）。

```
Coalesced:                       Uncoalesced:
tid 0  → A[0]                    tid 0  → A[0]
tid 1  → A[1]                    tid 1  → A[100]
tid 2  → A[2]                    tid 2  → A[200]
...                              ...
tid 31 → A[31]                   tid 31 → A[3100]
→ 1 个 128B 事务                  → 32 个 128B 事务（32 倍开销）
```

⚠️ 跨步访问、随机访问都会导致 uncoalesced。

### 3.5 Shared Memory Bank Conflict

Shared Memory 按 **32 个 bank** 组织（bank $i$ 存第 $i$、$i+32$、$i+64$... 个 word）。

```
无冲突：每线程访问不同 bank
tid 0 → bank 0
tid 1 → bank 1
...
tid 31 → bank 31
→ 1 cycle 完成

2 路冲突：两线程访问同 bank 不同地址
tid 0 → bank 0 (addr 0)
tid 16 → bank 0 (addr 32)
→ 2 cycle（串行化）

广播：多线程访问同 bank 同地址
tid 0, tid 16 → bank 0 (addr 0)
→ 1 cycle（广播，无冲突）
```

⚠️ 列优先矩阵按列访问最易触发 bank conflict。常用 **padding**（每行加 1 元素）消除。

### 3.6 Occupancy

$$
\text{Occupancy} = \frac{\text{活动 warp 数}}{\text{SM 最大 warp 数}}
$$

活动 warp 数受三资源限制（取最小）：
1. **线程数**：每 Block $T$ 线程，每 SM 最大 $B_{\max}$ 个 Block；
2. **共享内存**：每 Block 用 $S$ 字节，每 SM 共享 $S_{\max}$；
3. **寄存器**：每线程用 $R$ 寄存器，每 SM 共 $R_{\max}$。

**直觉**：高 occupancy 不等于高性能，但**低 occupancy 通常意味着无法隐藏延迟**。H100 LLM 训练 kernel 一般 50-80% occupancy。

### 3.7 Warp Divergence

```cpp
// 反模式
for (int i = 0; i < n; ++i)        // 各线程循环次数不同
    if (i < threadIdx.x) doWork();
// 前 threadIdx.x 次：全 warp 都参与
// 之后：threadIdx.x 较小的线程空闲
```

**对策**：把分支改为 loop tripcount 整齐，或用 prefix-sum + segment。

---

## 四、算法 / 流程

### 4.1 CUDA 编程基本流程

```
1. 分配 host 内存 + device 内存
2. 拷贝数据 host → device
3. 配置 grid/block dim
4. 启动 kernel
5. 拷回 device → host
6. 释放资源
```

### 4.2 GEMM Tiling 流程（核心优化）

```
for each tile (BM × BK) of A, (BK × BN) of B:
    1. 从 global mem 加载 tile 到 shared mem（coalesced）
    2. __syncthreads()
    3. 每 thread 计算自己负责的 RM × RN 子块
       for k in 0..BK:
           c += a[tm][k] * b[k][tn]    // 从 shared mem 读
    4. __syncthreads()
5. 写回 C
```

复杂度（$n\times n$ GEMM）：
- $W = 2n^3$ FLOP
- $C = 3n^2 \cdot 4$ Byte（读 A、B，写 C，FP32）
- $I = 2n/12 = n/6$ FLOP/Byte，$n=4096$ 时 $I\approx 683$ → 算力受限

---

## 五、工业实现对照

| 能力 | NVIDIA CUDA | AMD HIP/ROCm | Intel oneAPI | 国产（昇腾 Ascend C） |
|------|-------------|--------------|--------------|----------------------|
| 编程模型 | CUDA C++ | HIP（与 CUDA 几乎同 API） | SYCL / oneAPI DPC++ | Ascend C（相似 API） |
| Warp | 32 threads | 64 (wavefront) | sub_group 8-32 | Vector Core |
| 共享内存 | shared | shared (local) | local | Tiling Buffer |
| 张量单元 | Tensor Core (mma) | Matrix Core (wmma) | XMX (DPAS) | Cube Unit |
| 集合通信 | NCCL | RCCL | oneCCL | HCCL |
| Profiler | Nsight Compute | Omniperf | VTune / Advisor | Ascend Profiler |

---

## 六、代码示例

### 6.1 最小可运行版（教学）：Vector Add

```cpp
// file: vec_add.cu
// 编译: nvcc -O3 -arch=sm_80 vec_add.cu -o va
// 运行: ./va
#include <cuda_runtime.h>
#include <cstdio>

#define CK(x) do { auto _e=(x); if(_e!=cudaSuccess){ \
    printf("CUDA err %s:%d: %s\n",__FILE__,__LINE__,cudaGetErrorString(_e)); \
    exit(1);} } while(0)

__global__ void vec_add(const float* a, const float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}

int main() {
    const int N = 1 << 24;  // 16M
    float *hA = new float[N], *hB = new float[N], *hC = new float[N];
    for (int i = 0; i < N; ++i) { hA[i] = i; hB[i] = -i; }

    float *dA, *dB, *dC;
    CK(cudaMalloc(&dA, N*sizeof(float)));
    CK(cudaMalloc(&dB, N*sizeof(float)));
    CK(cudaMalloc(&dC, N*sizeof(float)));

    CK(cudaMemcpy(dA, hA, N*sizeof(float), cudaMemcpyHostToDevice));
    CK(cudaMemcpy(dB, hB, N*sizeof(float), cudaMemcpyHostToDevice));

    int block = 256;
    int grid = (N + block - 1) / block;
    vec_add<<<grid, block>>>(dA, dB, dC, N);
    CK(cudaDeviceSynchronize());

    CK(cudaMemcpy(hC, dC, N*sizeof(float), cudaMemcpyDeviceToHost));
    printf("hC[0]=%.1f hC[N-1]=%.1f (expected 0, 0)\n", hC[0], hC[N-1]);

    delete[] hA; delete[] hB; delete[] hC;
    CK(cudaFree(dA)); CK(cudaFree(dB)); CK(cudaFree(dC));
    return 0;
}
```

### 6.2 手把手：并行 Reduce（三步优化）

**Step 1 - Naive（错误反模式）**

```cpp
// ❌ 反模式：所有线程对单一全局变量原子加 → 极慢
__global__ void reduce_naive(const float* in, float* out, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) atomicAdd(out, in[i]);  // 数百万次原子加 → 慢
}
```

**Step 2 - 树形归约（共享内存）**

```cpp
// ✅ 块内树形归约，每块输出一个部分和
__global__ void reduce_block(const float* in, float* out, int n) {
    extern __shared__ float s[];       // 动态共享内存
    int tid = threadIdx.x;
    int i = blockIdx.x * blockDim.x + tid;

    s[tid] = (i < n) ? in[i] : 0.0f;
    __syncthreads();

    // 树形归约（注意 stride 从 blockDim.x/2 开始，避免越界）
    for (int s2 = blockDim.x / 2; s2 > 0; s2 >>= 1) {
        if (tid < s2) s[tid] += s[tid + s2];
        __syncthreads();
    }

    if (tid == 0) out[blockIdx.x] = s[0];  // 每块写一个部分和
}
```

**Step 3 - Warp 归约（无同步）**

```cpp
// ✅ 当 s2 <= 32（一个 warp）时，warp 内隐式同步，省去 __syncthreads
__device__ float warp_reduce(float v) {
    #pragma unroll
    for (int off = 16; off > 0; off >>= 1)
        v += __shfl_xor_sync(0xFFFFFFFF, v, off);  // warp 内 shuffle
    return v;
}

__global__ void reduce_warp(const float* in, float* out, int n) {
    extern __shared__ float s[];
    int tid = threadIdx.x;
    int i = blockIdx.x * blockDim.x + tid;
    float v = (i < n) ? in[i] : 0.0f;

    v = warp_reduce(v);              // 每个 warp 得一个值
    if ((tid & 31) == 0) s[tid >> 5] = v;  // warp 0..warps-1
    __syncthreads();

    if (tid < (blockDim.x >> 5)) {   // 第一个 warp 归约所有 warp 结果
        v = s[tid];
        v = warp_reduce(v);
        if (tid == 0) out[blockIdx.x] = v;
    }
}
```

🔥 **性能对比**（H100，N=16M）：
- Naive atomicAdd: ~5000 μs
- 树形归约: ~120 μs
- Warp shuffle: ~45 μs（100 倍加速）

### 6.3 生产级版（工程）：Tiled GEMM

```cpp
// file: gemm_tiled.cu
// 编译: nvcc -O3 -arch=sm_80 gemm_tiled.cu -o gemm
// 简化教学版（FP32，无 Tensor Core）
#include <cuda_runtime.h>
#include <cstdio>

#define BM 64
#define BN 64
#define BK 16
#define TM 8
#define TN 8

#define CK(x) do { auto _e=(x); if(_e!=cudaSuccess){ \
    printf("err %s:%d\n",__FILE__,__LINE__); exit(1);} } while(0)

__global__ void gemm_tiled(const float* A, const float* B, float* C, int n) {
    __shared__ float sA[BM][BK];
    __shared__ float sB[BK][BN];

    int bx = blockIdx.x, by = blockIdx.y;
    int tx = threadIdx.x, ty = threadIdx.y;
    int row = bx * BM + ty * TM;
    int col = by * BN + tx * TN;

    float acc[TM][TN] = {0};

    for (int k0 = 0; k0 < n; k0 += BK) {
        // 1. 协作加载 tile 到 shared mem
        #pragma unroll
        for (int i = 0; i < TM; ++i)
        #pragma unroll
        for (int j = 0; j < 1; ++j)
            sA[ty * TM + i][tx + j] = A[(row + i) * n + k0 + tx + j];

        #pragma unroll
        for (int i = 0; i < 1; ++i)
        #pragma unroll
        for (int j = 0; j < TN; ++j)
            sB[ty + i][tx * TN + j] = B[(k0 + ty + i) * n + col + j];

        __syncthreads();

        // 2. 计算子块乘积
        #pragma unroll
        for (int k = 0; k < BK; ++k) {
            #pragma unroll
            for (int i = 0; i < TM; ++i) {
                float a = sA[ty * TM + i][k];
                #pragma unroll
                for (int j = 0; j < TN; ++j)
                    acc[i][j] += a * sB[k][tx * TN + j];
            }
        }
        __syncthreads();
    }

    // 3. 写回 C
    #pragma unroll
    for (int i = 0; i < TM; ++i)
    #pragma unroll
    for (int j = 0; j < TN; ++j)
        C[(row + i) * n + col + j] = acc[i][j];
}

int main() {
    const int n = 1024;
    float *hA = new float[n*n], *hB = new float[n*n], *hC = new float[n*n];
    for (int i = 0; i < n*n; ++i) { hA[i] = 1.0f; hB[i] = 2.0f; }

    float *dA, *dB, *dC;
    CK(cudaMalloc(&dA, n*n*sizeof(float)));
    CK(cudaMalloc(&dB, n*n*sizeof(float)));
    CK(cudaMalloc(&dC, n*n*sizeof(float)));
    CK(cudaMemcpy(dA, hA, n*n*sizeof(float), cudaMemcpyHostToDevice));
    CK(cudaMemcpy(dB, hB, n*n*sizeof(float), cudaMemcpyHostToDevice));

    dim3 block(BN/TN, BM/TM);
    dim3 grid(n/BN, n/BM);

    cudaEvent_t s, e; cudaEventCreate(&s); cudaEventCreate(&e);
    cudaEventRecord(s);
    for (int t = 0; t < 10; ++t)
        gemm_tiled<<<grid, block>>>(dA, dB, dC, n);
    cudaEventRecord(e);
    CK(cudaDeviceSynchronize());

    float ms = 0; cudaEventElapsedTime(&ms, s, e);
    double flops = 2.0 * n*n*n * 10;
    printf("GEMM %dx%d: %.1f ms, %.1f GFLOP/s\n",
           n, n, ms/10, flops/(ms/1000.0)/1e9);

    delete[] hA; delete[] hB; delete[] hC;
    CK(cudaFree(dA)); CK(cudaFree(dB)); CK(cudaFree(dC));
    return 0;
}
```

🔥 **生产级优化方向**（不展开）：
- 用 `wmma`/`mma` 调用 Tensor Core（A100/H100 上 8-16 倍加速）；
- 向量化加载（`float4`）；
- 双缓冲（pipeline 加载与计算）；
- 自动调参（用 CUTLASS 库）。

🏭 **工业基线**（H100 FP16 Tensor Core GEMM）：
- 理论峰值 989 TFLOP/s；
- CUTLASS 实测 ~950 TFLOP/s（96%）；
- 手写朴素 tile 通常 200-400 TFLOP/s（20-40%）。

### 6.4 手把手：用 Nsight Compute 找瓶颈

```bash
# 1. 编译带 lineinfo
nvcc -O3 -arch=sm_80 -lineinfo -o gemm gemm_tiled.cu

# 2. 采集
ncu --set full -k gemm_tiled ./gemm

# 3. 关键指标
#    - Achieved Occupancy: 目标 >50%
#    - L2 Cache Hit Rate
#    - Shared Memory Bank Conflict: 应为 0
#    - Compute Throughput vs Memory Throughput
#    - Warp Stall Reasons
```

---

## 七、常见陷阱与最佳实践 ⚠️

| 陷阱 | 后果 | 对策 |
|------|------|------|
| Uncoalesced 访问 | 带宽降 32 倍 | 按行访问、向量化 `float4` |
| Bank Conflict | 共享内存带宽降 N 倍 | padding（每行加 1 元素） |
| Warp Divergence | 部分线程空闲 | 把循环整理为相同 tripcount |
| Low Occupancy | 无法隐藏延迟 | 调整 block size / 减寄存器 |
| Block size 非 32 倍数 | 最后 warp 不满 | 用 128/256 |
| 忘记 `__syncthreads()` | 数据竞争 | 共享内存写后必同步 |
| 在循环内分配 device 内存 | 极慢 | 提前分配复用 |
| 用 `printf` 调试大 kernel | 输出爆炸 | 用 `if(tid==0) printf` |
| 假设 atomicAdd 快 | 实际极慢 | 改用块内归约 + 块间归约 |
| 用 FP32 代替 FP16 | 损失 4 倍算力 | 训练用 BF16/FP16 + Tensor Core |

---

## 八、与其他章节关系

| 章节 | 关系 |
|------|------|
| §1 体系结构 | GPU 内存层级与缓存层级类比 |
| §2 性能模型 | Roofline 用于 GPU 调优 |
| §7 GPU 高级优化 | Tensor Core / Triton / Kernel Fusion |
| §10 数值线性代数 | GEMM 是 BLAS 核心 |
| §13 集合通信 | NCCL 基于 CUDA |
| §14 分布式训练 | 训练 kernel 调优基础 |

---

## 九、面试速答 ⭐

| 问 | 答 |
|----|-----|
| SIMT vs SIMD？ | SIMT 单指令多线程，warp 内 32 线程同指令；SIMD 单指令多数据，向量寄存器 |
| Warp 多少线程？ | NVIDIA 32，AMD 64 |
| Block 间能同步吗？ | 不能（除非 cooperative kernel）；只能 Block 内 `__syncthreads()` |
| Coalesced 要求？ | warp 内 32 线程访问连续且对齐地址 |
| Shared Memory 多少 bank？ | 32（NVIDIA） |
| Bank conflict 怎么消除？ | padding 或 swizzle |
| Occupancy 公式？ | 活动 warp / SM 最大 warp，受线程数/共享内存/寄存器三者限制 |
| 高 occupancy 一定快吗？ | 不一定，但低 occupancy 通常意味着无法隐藏延迟 |
| Reduce 怎么优化？ | 块内树形 → warp shuffle（无同步） |
| GEMM 怎么优化？ | Tiling + 共享内存 + Tensor Core + 向量化 + 双缓冲 |
| Tensor Core 是什么？ | 4×4×4 矩阵乘单元，FP16 输入 FP32 累加 |
| 为什么 CUDA core ≠ SM？ | SM 是计算单元（含多 CUDA core + Tensor Core + 调度器），H100 每 SM 128 CUDA core + 4 Tensor Core |

---

## 十、综合面试题

1. **基础**：为什么 warp 大小是 32？
   - 答：(1) 平衡 SIMT 效率与 divergence 损失；(2) 32 是 GPU 调度最小单位；(3) 历史：G80 时代定为 32，向后兼容。

2. **进阶**：你的 kernel occupancy 只有 25%，但实测 FLOP/s 接近 Roofline 上限。怎么解释？
   - 答：说明 kernel 是计算密集型，靠 ILP（指令级并行）和流水线隐藏延迟，不依赖 occupancy。这种情况调高 occupancy 反而可能降低性能（寄存器压力增大）。

3. **深度**：GEMM 在 4096×4096 FP32 上实测 200 TFLOP/s，H100 FP32 峰值 67 TFLOP/s。可能吗？
   - 答：FP32 峰值 67 TFLOP/s，不可能跑出 200 TFLOP/s FP32。除非：(1) 实际跑的是 FP16/BF16 Tensor Core（989 TFLOP/s 峰值）；(2) 用了稀疏计算（2x）。问题在于把 FP32 kernel 与 FP16 峰值比较——MFU 必须同精度。

4. **设计**：写一个 1D 卷积（kernel size 3）的高效 CUDA 实现。
   - 答：(1) 每线程处理多个元素（reduce launch overhead）；(2) 用 constant memory 存 weight（小且广播）；(3) 用 shared memory 缓存输入 tile + halo；(4) 边界用 clamp/padding；(5) 用 `float4` 向量化加载。

5. **工程**：NCCL all-reduce 比 naive atomicAdd 快 1000 倍。原因？
   - 答：(1) NCCL 用 Ring 算法（带宽最优，详见 §13），原子加是 O(N) 串行化；(2) NCCL 用 NVLink/RDMA 绕过 CPU；(3) NCCL 拓扑感知，按 GPU 层级组织。

6. **学术**：证明 Tiled GEMM 的算术强度比朴素 GEMM 高。
   - 答：朴素 GEMM 每 thread 读 a, b 各一次算 1 次乘加，$I = 2/8 = 0.25$ FLOP/Byte (FP32)。Tiled GEMM（BM×BK×BN tile）：tile 加载 $2\cdot BM\cdot BK + 2\cdot BK\cdot BN$ 字节，计算 $2\cdot BM\cdot BK\cdot BN$ FLOP，$I = \frac{2\cdot BM\cdot BK\cdot BN}{4(BM\cdot BK + BK\cdot BN)} = \frac{BM\cdot BN}{2(BM+BN)}$。$BM=BN=64$ 时 $I=16$，比朴素高 64 倍。

---

## 十一、参考与延伸

### 11.1 教材
- 《Programming Massively Parallel Processors》—— Kirk & Hwu（第 4 版）
- 《CUDA C++ Programming Guide》—— NVIDIA
- 《CUDA C++ Best Practices Guide》—— NVIDIA
- 《Professional CUDA C Programming》—— Cheng, Grossman, McKercher

### 11.2 论文
- 📜 Lindholm et al., "NVIDIA Tesla: A Unified Graphics and Computing Architecture" (2008, SIMT 起源)
- 📜 Volkov & Demmel, "Benchmarking GPUs to Tune Dense Linear Algebra" (2008)
- 📜 Hong & Kim, "An Analytical GPU Performance Model for Microarchitecture-Level Optimization" (2010)

### 11.3 工具与库
- CUDA Toolkit / nvcc / Nsight Compute / Nsight Systems
- CUTLASS（NVIDIA 高性能 GEMM 模板库）
- cuBLAS / cuDNN / NCCL
- Triton（OpenAI，Python 写 GPU kernel）
- AMD ROCm / HIP / MIOpen

### 11.4 跨文件链接
- [01-并行计算基础与体系结构.md](./01-并行计算基础与体系结构.md) — SIMT vs SIMD 体系结构
- [02-性能模型与度量.md](./02-性能模型与度量.md) — Roofline 调优
- [07-GPU高级优化.md](./07-GPU高级优化.md) — Tensor Core / Triton
- [10-并行数值线性代数.md](./10-并行数值线性代数.md) — BLAS/GEMM
- [13-通信与集体原语.md](./13-通信与集体原语.md) — NCCL
- [../README.md](./README.md)

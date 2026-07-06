# 04 - KV Cache 与 PagedAttention

> KV Cache 是 LLM 推理的核心数据结构，理解 KV Cache 是理解所有推理引擎的前提。PagedAttention（vLLM 的核心创新）借鉴 OS 虚拟内存思想，解决 KV Cache 显存碎片化问题。
>
> 本章从 KV Cache 的本质出发，剖析 PagedAttention 原理、prefix cache、RadixTree（SGLang）、KV Cache 卸载等关键技术。

---

## 一、思维导图

```
                    KV Cache
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   ┌─────────┐  ┌───────────┐  ┌───────────┐
   │ 本质    │  │ 管理      │  │ 优化      │
   │ 注意力  │  │ PagedAttn │  │ Prefix    │
   │ 中间态  │  │ RadixTree │  │ Cache     │
   └─────────┘  └───────────┘  └───────────┘
        │              │              │
        ▼              ▼              ▼
   单请求 ~1.6GB  分块管理       共享复用
   显存带宽密集   消除碎片       跨请求
                  虚拟内存       局部性
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **理解 KV Cache 的本质**：为什么需要、多大、怎么用
- **PagedAttention 原理**：vLLM 的核心创新，OS 虚拟内存思想
- **prefix cache**：跨请求复用 KV Cache
- **RadixTree**：SGLang 的更高效 prefix 管理
- **KV Cache 卸载**：显存不够时卸载到 CPU/磁盘
- **量化与压缩**：FP8/INT8 KV Cache

### 2.2 不解决什么

- 不深入 attention 算子的数学推导（参考 LLM 模块）
- 不覆盖训练时的 KV Cache（推理专用）
- 不讨论非 Transformer 架构（Mamba、RWKV 等无 KV Cache）

### 2.3 为什么 KV Cache 是核心

LLM 推理 80% 的时间在读写 KV Cache。理解 KV Cache 就理解了 LLM 推理的瓶颈。

```
单 token decode 计算:
- 加载模型权重: 70B × 2B = 140GB
- 加载 KV Cache: ~1.6GB (单请求 4K context)
- 实际算力需求: 1 token × 70B = 70GFLOPS (微小)

瓶颈:
- 显存带宽 (decode 阶段): 140GB + 1.6GB ≈ 142GB / token
- H100 显存带宽: 3TB/s
- 理论最低延迟: 142GB / 3TB/s = 47ms / token (单卡)

实际:
- TP=4 (每卡 35GB 权重): 12ms / token
- 这就是 TPOT 的物理下限
```

---

## 三、直觉解释

### 3.1 为什么需要 KV Cache

Transformer 的 self-attention 是：
```
Attention(Q, K, V) = softmax(QK^T / sqrt(d)) × V
```

生成第 t 个 token 时，需要计算 t 与所有历史 token (1, ..., t) 的注意力。

**没有 KV Cache**：每次重新计算所有历史 token 的 K、V，浪费计算。

**有 KV Cache**：把历史的 K、V 缓存，下次直接用。

```
t=1: 计算 K1, V1, 存入 cache
t=2: 计算 K2, V2, 存入 cache, 用 [K1,K2] 和 [V1,V2] 算 attention
t=3: 计算 K3, V3, 存入 cache, 用 [K1,K2,K3] 和 [V1,V2,V3] 算 attention
...
```

**效果**：每个新 token 只需要计算新 K、V（O(1)），不需要重新算历史（O(n)）。

### 3.2 KV Cache 多大

**单层单头**：每 token KV Cache = `2 × hidden_dim × dtype_size`

**Llama-70B**：
- 80 层
- 64 头
- hidden_dim = 8192
- head_dim = 128
- FP16 (2 bytes)

```
单 token KV Cache = 2 (K+V) × 80 (层) × 64 (头) × 128 (head_dim) × 2 (bytes)
                  = 2 × 80 × 64 × 128 × 2
                  = 2.62MB

4K context 单请求 KV Cache = 4096 × 2.62MB = 10.5GB
32K context 单请求 KV Cache = 32768 × 2.62MB = 84GB (单卡装不下)

Llama-70B 4K context 8 请求 KV Cache = 84GB (≈ H100 80GB 全部)
```

**FP8 量化后**：减半，4K context 单请求 5.25GB。

### 3.3 KV Cache 的物理约束

```
模型权重 (FP16, Llama-70B): 140GB
  → 4 卡 TP: 每卡 35GB

KV Cache 可用显存 (H100 80GB):
  80GB - 35GB (权重) - 5GB (激活) - 5GB (临时) = 35GB

单卡可容纳 4K context 请求数:
  FP16: 35GB / 10.5GB = 3 请求
  FP8:  35GB / 5.25GB = 6 请求
```

**结论**：KV Cache 是并发数的瓶颈，不是算力。

### 3.4 PagedAttention 的直觉

**传统 contiguous 分配**：
```
请求1 (4K context): [████████████████████] 10.5GB
请求2 (2K context): [██████] 5.25GB
请求3 (8K context): [████████████████████████████████] 21GB
请求4 (新): ??? 没有连续 10.5GB 空间

碎片问题:
  请求2 完成, 释放 5.25GB
  请求4 需要 10.5GB, 但 5.25GB 不连续
  → 显存碎片, 利用率 60%
```

**PagedAttention**：
```
将 KV Cache 分成 16 token/block 的固定块:
请求1: [block1][block2][block3][block4]...
请求2: [block5][block6]...
请求3: [block7][block8][block9][block10]...

block 表 (virtual → physical):
  请求1 block 0 → physical block 5
  请求1 block 1 → physical block 12
  请求1 block 2 → physical block 3
  ...

请求4 分配:
  找任意 4 个空闲 block 即可, 不需要连续
  → 利用率 95%+
```

**借鉴 OS 虚拟内存**：
- 进程视角：连续虚拟地址
- 物理视角：分页，不需要连续
- 页表：virtual → physical 映射
- PagedAttention：请求视角"连续" KV Cache，物理上分块

---

## 四、核心概念与架构

### 4.1 KV Cache 数据结构

#### 标准实现（无 PagedAttention）

```python
class KVCache:
    def __init__(self, num_layers, num_heads, head_dim, max_batch, max_seq, dtype=torch.float16):
        self.cache = torch.zeros(
            (num_layers, 2, max_batch, max_seq, num_heads, head_dim),
            dtype=dtype,
            device='cuda'
        )
        # 2 = K 和 V
        # max_batch × max_seq 预分配, 浪费显存

    def update(self, layer_idx, batch_idx, pos, k, v):
        self.cache[layer_idx, 0, batch_idx, pos] = k  # K
        self.cache[layer_idx, 1, batch_idx, pos] = v  # V
```

**问题**：
1. `max_seq` 预分配，请求长度不一导致浪费
2. `max_batch` 预分配，并发不足时浪费
3. 请求完成释放后产生碎片

#### PagedAttention 实现

```python
class PagedAttentionCache:
    def __init__(self, num_blocks, block_size, num_layers, num_heads, head_dim, dtype):
        # 物理块池
        self.block_pool = torch.zeros(
            (num_blocks, num_layers, 2, block_size, num_heads, head_dim),
            dtype=dtype,
            device='cuda'
        )
        self.block_size = block_size  # 通常 16
        self.free_blocks = list(range(num_blocks))
        # 每请求的 block 表 (virtual → physical)
        self.block_tables = {}  # request_id → [physical_block_idx, ...]

    def allocate(self, request_id, num_tokens):
        num_blocks_needed = (num_tokens + self.block_size - 1) // self.block_size
        blocks = []
        for _ in range(num_blocks_needed):
            blocks.append(self.free_blocks.pop())
        self.block_tables[request_id] = blocks

    def update(self, request_id, layer_idx, pos, k, v):
        block_idx = pos // self.block_size
        offset = pos % self.block_size
        physical_block = self.block_tables[request_id][block_idx]
        self.block_pool[physical_block, layer_idx, 0, offset] = k
        self.block_pool[physical_block, layer_idx, 1, offset] = v

    def free(self, request_id):
        for block in self.block_tables[request_id]:
            self.free_blocks.append(block)
        del self.block_tables[request_id]
```

### 4.2 PagedAttention 的 attention 计算

PagedAttention 的核心难点：attention 计算需要访问"连续"的 K、V，但物理上是分块的。

```
标准 attention:
  Q @ K^T  (Q 是 1×d, K 是 seq_len×d, 连续)
  → 需要连续 K

PagedAttention:
  Q @ K^T, 但 K 分散在多个 block
  → kernel 内逐 block 计算, 累加结果
```

**vLLM 的 PagedAttention kernel**（CUDA）：
```cpp
// 简化伪代码
__global__ void paged_attention_kernel(
    float* output,        // [batch, num_heads, head_dim]
    const float* q,       // [batch, num_heads, head_dim]
    const float* kv_pool, // [num_blocks, num_layers, 2, block_size, num_heads, head_dim]
    const int* block_tables, // [batch, max_blocks_per_request]
    int num_layers,
    int num_heads,
    int head_dim,
    int block_size,
    int seq_len
) {
    int batch_idx = blockIdx.x;
    int head_idx = threadIdx.y;

    float scores[MAX_SEQ];
    float max_score = -INFINITY;

    // 逐 block 计算 attention scores
    for (int block_idx = 0; block_idx < (seq_len + block_size - 1) / block_size; block_idx++) {
        int physical_block = block_tables[batch_idx * max_blocks + block_idx];
        for (int offset = 0; offset < block_size; offset++) {
            int pos = block_idx * block_size + offset;
            if (pos >= seq_len) break;
            float score = dot_product(
                q[batch_idx, head_idx],
                kv_pool[physical_block, layer_idx, 0, offset, head_idx]
            );
            scores[pos] = score;
            max_score = max(max_score, score);
        }
    }

    // softmax
    float sum = 0;
    for (int i = 0; i < seq_len; i++) {
        scores[i] = exp(scores[i] - max_score);
        sum += scores[i];
    }

    // 加权求和 V
    float output[head_dim] = {0};
    for (int block_idx = 0; ...; block_idx++) {
        int physical_block = block_tables[...];
        for (int offset = 0; ...; offset++) {
            int pos = block_idx * block_size + offset;
            float weight = scores[pos] / sum;
            for (int d = 0; d < head_dim; d++) {
                output[d] += weight * kv_pool[physical_block, layer_idx, 1, offset, head_idx, d];
            }
        }
    }
}
```

### 4.3 Prefix Cache

**直觉**：相同 system prompt 的请求，KV Cache 可以复用。

```
请求1: [system: 你是助手][user: 你好] → KV Cache 完整计算
请求2: [system: 你是助手][user: 今天天气] → 前 4 token 的 KV Cache 复用

请求1 KV Cache: [system blocks][user1 blocks]
请求2 KV Cache: [system blocks][user2 blocks]
                  ↑ 共享
```

**实现**：
- 用 hash 标识 prefix（如 SHA256(token_ids)）
- 命中时复用 block，不再计算
- 引用计数管理（所有请求完成后释放）

**vLLM 的实现**（0.5+）：
```python
# 自动 prefix caching
hash_of_tokens = hash(token_ids[:block_size])
if hash in cache:
    reuse_block(hash)
else:
    compute_and_cache(hash, k, v)
```

**效果**：
- 长 system prompt 场景，TTFT 降 70%
- KV Cache 显存利用率 +30%

### 4.4 RadixTree（SGLang）

**问题**：vLLM 的 hash 表只能匹配完全相同的 prefix。多轮对话中 prefix 部分重叠，hash 匹配失效。

**RadixTree**：用基数树管理所有请求的 prefix。

```
RadixTree 示例:
                  [system: 你是助手]
                 /                  \
        [user: 你好]           [user: 今天天气]
           /                          \
    [assistant: 你好!]         [assistant: 今天晴]
           /                          \
    [user: 帮我写诗]            [user: 明天呢]

新请求: [system][user: 你好][assistant: 你好!][user: 帮我写古诗]
  → RadixTree 找到最长前缀 [system][user: 你好][assistant: 你好!][user: 帮我写诗]
  → 复用 90% KV Cache
```

**优势**：
- 部分匹配（vLLM hash 只能完全匹配）
- 多轮对话复用率 80%+（vLLM 60%）
- 树结构 O(n) 查找

### 4.5 KV Cache 卸载

**问题**：显存不够时，被抢占的请求 KV Cache 怎么办？

**方案 1：丢弃**
- 重新计算（浪费 prefill 算力）

**方案 2：卸载到 CPU 内存**
- 通过 PCIe 传到 CPU（50GB/s，1.6GB → 32ms）
- 需要时再传回 GPU
- vLLM 0.6+ 支持

**方案 3：卸载到磁盘**
- SSD 5GB/s，1.6GB → 320ms（慢）
- 仅适合长会话

**方案 4：跨节点传输**
- KV Cache 通过 RDMA 传到其他 GPU
- PD 分离的基础

### 4.6 KV Cache 量化

**FP8 量化**：
- KV Cache 减半（10.5GB → 5.25GB）
- H100 原生支持
- 精度损失 < 1%（perplexity）

**INT8 量化**：
- 同样减半
- 需要 calibration
- 精度损失略大

**INT4 量化**：
- 1/4 显存
- 精度损失大，不推荐生产

```python
# vLLM 启用 KV Cache FP8
vllm serve meta-llama/Llama-3.1-70B \
  --kv-cache-dtype fp8 \
  --quantization fp8  # 模型权重也 FP8
```

---

## 五、操作流程与配置

### 5.1 vLLM KV Cache 配置

```bash
vllm serve meta-llama/Llama-3.1-70B \
  --tensor-parallel-size 4 \
  --gpu-memory-utilization 0.9 \  # 留 10% 余量
  --max-model-len 32768 \         # 最大上下文
  --max-num-seqs 256 \            # 最大并发
  --max-num-batched-tokens 8192 \ # 单 batch 最大 token
  --block-size 16 \               # PagedAttention block size
  --enable-prefix-caching \       # 开启 prefix cache
  --kv-cache-dtype fp8            # KV Cache FP8
```

### 5.2 监控 KV Cache 指标

```promql
# KV Cache 使用率
vllm:gpu_cache_usage_perc

# 等待 KV Cache 的请求数
vllm:num_requests_waiting

# KV Cache 抢占次数
rate(vllm:num_preemptions_total[5m])

# KV Cache 命中率 (prefix cache)
rate(vllm:prefix_cache_hit_total[5m]) / rate(vllm:prefix_cache_query_total[5m])
```

### 5.3 KV Cache 调优决策

```
1. 显存不够:
   - 优先: 启用 FP8 KV Cache (减半)
   - 其次: 降低 max-num-seqs
   - 最后: KV Cache 卸载

2. TTFT 高:
   - 启用 prefix caching
   - 检查 prefix 命中率

3. 抢占频繁:
   - 增加 GPU (TP)
   - 降低 max-num-seqs
   - 调整调度策略
```

---

## 六、底层原理

### 6.1 PagedAttention 的内存节省

**传统分配**：
```
请求1: max_seq=4096, 实际用 1024 → 浪费 3072 × 2.62MB = 8GB
请求2: max_seq=4096, 实际用 500  → 浪费 3596 × 2.62MB = 9.4GB
请求3: max_seq=4096, 实际用 8000 → 超出, 拒绝

利用率: (1024+500) / (4096×2) = 19%
```

**PagedAttention**：
```
请求1: 1024 token → 64 blocks × 16 token
请求2: 500 token → 32 blocks × 16 token (实际 31.25, 向上取整 32)
请求3: 8000 token → 500 blocks × 16 token

利用率: (1024+500+8000) / (1024+512+8000) = 99%
```

**实测**：vLLM 比 HF transformers 吞吐高 24x，主要来自 PagedAttention。

### 6.2 Prefix Cache 的命中率

**单轮场景**（system prompt + user query）：
```
请求1: [system][user1] → 完整计算
请求2: [system][user2] → 复用 [system]
命中率 = len(system) / len(system)+len(user) ≈ 30-50%
```

**多轮对话**：
```
请求1: [system][user1][assistant1][user2] → 完整计算
请求2: [system][user1][assistant1][user2][assistant2][user3]
  → 复用 [system][user1][assistant1][user2][assistant2]
命中率 = 历史长度 / 总长度 ≈ 70-90%
```

**RadixTree vs Hash**：
```
场景: 3 请求
  R1: [A][B][C]
  R2: [A][B][D]
  R3: [A][E][F]

Hash 匹配:
  R2 与 R1 共享 [A][B] (完全匹配 [A][B])
  R3 与 R1 共享 [A]
  命中: [A]×2 + [A][B]×1 = 3

RadixTree 匹配:
  找最长前缀
  R2 找到 [A][B] (2 blocks)
  R3 找到 [A] (1 block)
  命中: [A][B]×1 + [A]×1 = 2 (但更准确, 无 hash 碰撞)
```

### 6.3 KV Cache 量化的精度影响

**Llama-70B, 4K context**:

| 量化 | KV Cache 大小 | Perplexity | 下游任务 |
|------|---------------|------------|----------|
| FP16 (基线) | 10.5GB | 5.32 | 100% |
| FP8 | 5.25GB | 5.35 (+0.6%) | 99.5% |
| INT8 | 5.25GB | 5.42 (+1.9%) | 98.8% |
| INT4 | 2.6GB | 6.15 (+15.6%) | 92.3% |

**结论**：
- FP8 几乎无损
- INT8 可接受
- INT4 不可接受

### 6.4 KV Cache 跨节点传输

**PD 分离场景**：Prefill 节点计算 KV Cache，传给 Decode 节点。

```
单请求 KV Cache (4K context, FP8): 5.25GB

传输方式:
1. TCP over 100Gbps 以太网: 5.25GB / 12.5GB/s = 420ms (太慢)
2. RDMA over 400Gbps IB: 5.25GB / 50GB/s = 105ms
3. GPU Direct RDMA (绕过 CPU): 5.25GB / 50GB/s = 105ms (但 CPU 不参与)
4. NVLink over NVSwitch (节点内): 5.25GB / 900GB/s = 5.8ms

结论:
- 节点内 NVLink 最优
- 跨节点 RDMA 可接受
- TCP 不可接受
```

---

## 七、代码与配置示例

### 7.1 vLLM 完整 KV Cache 配置

```python
# vllm_kv_cache.py
from vllm import LLM, SamplingParams
from vllm.config import CacheConfig

llm = LLM(
    model="meta-llama/Llama-3.1-70B",
    tensor_parallel_size=4,
    gpu_memory_utilization=0.9,
    max_model_len=32768,
    max_num_seqs=256,
    max_num_batched_tokens=8192,
    block_size=16,
    enable_prefix_caching=True,
    kv_cache_dtype="fp8",  # FP8 KV Cache
    swap_space=16,  # CPU 卸载空间 (GB)
    swap_layer_offset=0,
)
```

### 7.2 监控 KV Cache 状态

```python
# vllm_monitoring.py
from vllm import LLMEngine
import prometheus_client as prom

class KVCacheMonitor:
    def __init__(self, engine: LLMEngine):
        self.engine = engine
        self.cache_usage = prom.Gauge('vllm_kv_cache_usage', 'KV Cache usage %')
        self.hit_rate = prom.Counter('vllm_prefix_cache_hit', 'Prefix cache hits')
        self.miss_rate = prom.Counter('vllm_prefix_cache_miss', 'Prefix cache misses')

    def collect(self):
        stats = self.engine.scheduler.get_kv_cache_stats()
        self.cache_usage.set(stats.usage_perc)
        if stats.prefix_hit:
            self.hit_rate.inc()
        else:
            self.miss_rate.inc()
```

### 7.3 自定义 KV Cache 管理

```python
# custom_kv_cache.py
class CustomKVCacheManager:
    def __init__(self, total_blocks, block_size=16):
        self.total_blocks = total_blocks
        self.block_size = block_size
        self.free_blocks = list(range(total_blocks))
        self.block_tables = {}  # request_id → [block_idx]
        self.ref_count = {}  # block_idx → count (用于 prefix 共享)

    def allocate(self, request_id, num_tokens):
        num_blocks = (num_tokens + self.block_size - 1) // self.block_size
        if num_blocks > len(self.free_blocks):
            raise OOMError(f"KV Cache OOM: need {num_blocks}, free {len(self.free_blocks)}")
        blocks = [self.free_blocks.pop() for _ in range(num_blocks)]
        self.block_tables[request_id] = blocks

    def free(self, request_id):
        for block in self.block_tables[request_id]:
            if self.ref_count.get(block, 1) > 1:
                self.ref_count[block] -= 1
            else:
                self.free_blocks.append(block)
                self.ref_count.pop(block, None)
        del self.block_tables[request_id]

    def share_prefix(self, src_request_id, dst_request_id, num_blocks):
        """共享 src 的前 num_blocks 个 block 给 dst"""
        src_blocks = self.block_tables[src_request_id]
        shared = src_blocks[:num_blocks]
        for block in shared:
            self.ref_count[block] = self.ref_count.get(block, 1) + 1
        self.block_tables[dst_request_id] = shared + self.block_tables[dst_request_id]
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：没开 prefix cache

**症状**：多轮对话 TTFT 高。

**根因**：相同 system prompt 重复计算 KV Cache。

**修复**：`--enable-prefix-caching`。

### 8.2 陷阱 2：max-num-seqs 过大导致 OOM

**症状**：并发上来后 OOM。

**根因**：max-num-seqs 决定 KV Cache 预留空间，过大导致权重装不下。

**修复**：
```
显存预算 = 80GB (H100)
权重 (TP=4): 35GB
激活 + 临时: 10GB
KV Cache 可用: 35GB

单请求 KV Cache (4K, FP8): 5.25GB
max-num-seqs = 35 / 5.25 = 6

但 vLLM 默认 256, 因为多数请求 < 4K
实际监控 KV Cache 使用率, 动态调整
```

### 8.3 陷阱 3：FP8 KV Cache 精度损失

**症状**：FP8 KV Cache 后输出质量下降。

**根因**：小模型（7B 以下）FP8 精度损失大。

**修复**：大模型用 FP8，小模型用 FP16。

### 8.4 陷阱 4：抢占导致延迟飙升

**症状**：高峰期请求延迟突然飙升。

**根因**：KV Cache 满了，低优请求被抢占，重新计算。

**修复**：
- 增加 GPU
- 降低 max-num-seqs
- 配置 swap_space 启用卸载

### 8.5 调优 Checklist

- [ ] PagedAttention（默认开）
- [ ] prefix caching 开启
- [ ] KV Cache FP8（大模型）
- [ ] block-size 16（默认）
- [ ] max-num-seqs 适配显存
- [ ] swap_space 配置（卸载）
- [ ] 监控 KV Cache 使用率
- [ ] 监控 prefix 命中率
- [ ] 监控抢占次数

---

## 九、工业案例与基准数据

### 9.1 vLLM PagedAttention 原始论文基准

环境：A100 80GB，Llama-13B，ShareGPT 数据集。

| 方案 | 吞吐 (tokens/s) | 相对提升 |
|------|-----------------|----------|
| HF transformers | 25 | 1x |
| TGI (continuous batching) | 125 | 5x |
| vLLM (PagedAttention) | 600 | 24x |

### 9.2 Prefix Cache 在多轮对话的效果

环境：Llama-70B，4 轮对话，平均 8K context。

| 方案 | TTFT P50 | TTFT P99 | KV Cache 利用率 |
|------|----------|----------|-----------------|
| 无 prefix cache | 800ms | 1500ms | 60% |
| vLLM hash prefix | 400ms | 800ms | 70% |
| SGLang RadixTree | 280ms | 600ms | 85% |

### 9.3 案例：Character.AI 长对话 KV Cache

**背景**：用户平均 100+ 轮对话，KV Cache 极大。

**方案**：
- KV Cache 持久化到 CPU 内存
- LRU 淘汰
- prefix cache 命中率 80%+

**效果**：
- 单用户成本 -50%
- 长对话 TTFT 不退化

### 9.4 案例：DeepSeek V3 MoE KV Cache

**背景**：DeepSeek-V3 是 671B MoE，KV Cache 极大。

**方案**：
- MLA（Multi-head Latent Attention）：KV Cache 压缩到 1/4
- FP8 KV Cache
- 跨专家共享 KV Cache

**效果**：
- KV Cache 显存减 75%
- 单节点 8×H100 推理 V3

---

## 十、与其他方案的关系

### 10.1 PagedAttention vs RadixTree

| 维度 | PagedAttention (vLLM) | RadixTree (SGLang) |
|------|----------------------|-------------------|
| 数据结构 | Hash 表 | 基数树 |
| 匹配 | 完全匹配 | 最长前缀匹配 |
| 多轮对话 | 一般 | 优秀 |
| 单轮 | 优秀 | 优秀 |
| 内存开销 | 低 | 略高 |
| 实现复杂度 | 中 | 高 |

### 10.2 KV Cache 与模型架构

| 架构 | KV Cache | 备注 |
|------|----------|------|
| Transformer (Llama) | 大 (10GB+/请求) | 标准实现 |
| Mamba | 无 | SSM 状态替代 |
| RWKV | 小 (固定大小) | 线性复杂度 |
| MLA (DeepSeek) | 压缩 1/4 | KV Cache 优化 |

---

## 十一、面试速答

**Q1: KV Cache 是什么？为什么需要？**

A: Transformer attention 计算时缓存历史的 K、V 矩阵，避免重复计算。Llama-70B 单 token KV Cache 2.62MB，4K context 单请求 10.5GB。没有 KV Cache 每次重新计算历史，慢 100x。

**Q2: KV Cache 多大？**

A: `2 × num_layers × num_heads × head_dim × seq_len × dtype_size`。Llama-70B 单 token 2.62MB（FP16），4K context 10.5GB。

**Q3: PagedAttention 解决什么问题？**

A: KV Cache 显存碎片化。传统 contiguous 分配利用率仅 60%。PagedAttention 借鉴 OS 虚拟内存，分块管理（16 token/block），利用率 95%+。vLLM 比 HF transformers 吞吐高 24x。

**Q4: prefix cache 的原理？**

A: 相同 prefix 的请求共享 KV Cache。如相同 system prompt 的多请求，只算一次。多轮对话场景 TTFT 降 70%。

**Q5: RadixTree 比 hash 表好在哪？**

A: hash 只能完全匹配，RadixTree 最长前缀匹配。多轮对话中 prefix 部分重叠，RadixTree 命中率高 20%+。

**Q6: KV Cache 量化安全吗？**

A: 大模型（70B+）FP8 几乎无损（perplexity +0.6%）。INT8 可接受（+1.9%）。INT4 不可接受（+15.6%）。小模型量化损失更大。

**Q7: KV Cache 卸载什么时候用？**

A: 显存不够时。卸载到 CPU 内存（PCIe 50GB/s，1.6GB → 32ms）。vLLM 0.6+ 支持。代价是被抢占请求恢复慢。

**Q8: PD 分离中 KV Cache 怎么传？**

A: GPU Direct RDMA（绕过 CPU），400Gbps IB 50GB/s。单请求 5.25GB（FP8）传输 105ms。节点内 NVLink 900GB/s，5.8ms。

---

## 十二、综合面试题

### 题 1（初级）：计算 Llama-70B 的 KV Cache 大小

**答题要点**：
```
Llama-70B 配置:
- 80 层
- 64 头
- head_dim = 128
- hidden_dim = 8192

单 token KV Cache (FP16):
= 2 (K+V) × 80 (层) × 64 (头) × 128 (head_dim) × 2 (bytes)
= 2 × 80 × 64 × 128 × 2
= 2,621,440 bytes
= 2.62MB

4K context 单请求:
= 4096 × 2.62MB = 10.5GB

H100 80GB 单卡 (TP=4):
- 权重 35GB
- KV Cache 可用 35GB
- 单卡可容纳 4K 请求: 35 / 10.5 = 3 请求

FP8 量化后:
- 单 token 1.31MB
- 4K context 5.25GB
- 单卡可容纳 6 请求
```

### 题 2（中级）：PagedAttention 如何消除碎片？

**答题要点**：
1. **传统问题**：contiguous 分配，请求长度不一导致碎片，利用率 60%
2. **PagedAttention**：
   - KV Cache 分成 16 token/block 的固定块
   - block 表（virtual → physical）映射
   - 请求视角"连续"，物理上分块
3. **效果**：
   - 利用率 60% → 95%
   - 请求完成释放 block，新请求可复用任意空闲 block
   - 显存浪费仅最后一个 block 的 partial（最多 15 token）
4. **借鉴 OS 虚拟内存**：进程虚拟地址连续，物理页可以分散

### 题 3（高级）：设计一个 KV Cache 管理器

**答题要点**：
1. **数据结构**：
   - block_pool：物理块池
   - block_tables：request_id → [block_idx]
   - ref_count：block_idx → count（prefix 共享）
   - prefix_index：hash 或 RadixTree
2. **API**：
   - allocate(request_id, num_tokens)
   - update(request_id, layer, pos, k, v)
   - free(request_id)
   - share_prefix(src, dst, num_blocks)
3. **调度**：
   - 显存满时抢占低优请求
   - 卸载到 CPU 而非丢弃
4. **优化**：
   - FP8 量化
   - prefix cache
   - 跨节点传输（PD 分离）
5. **监控**：
   - 使用率
   - 命中率
   - 抢占次数

### 题 4（高级）：PD 分离中 KV Cache 传输的工程挑战

**答题要点**：
1. **传输量大**：单请求 5.25GB（FP8）
2. **传输方式**：
   - TCP：12.5GB/s，420ms（不可接受）
   - RDMA：50GB/s，105ms（可接受）
   - GPU Direct RDMA：CPU 不参与，更高效
3. **批量传输**：多个请求的 KV Cache 合并传输，摊薄开销
4. **流水线**：prefill 完成部分就开始传，不等全部完成
5. **局部性**：同会话路由到同节点，避免传输
6. **故障恢复**：decode 节点宕机，KV Cache 丢失，需要持久化或重算

### 题 5（高级）：MLA（Multi-head Latent Attention）如何压缩 KV Cache？

**答题要点**（DeepSeek-V3 创新）：
1. **标准 attention**：每头独立 K、V，KV Cache = 2 × n_heads × head_dim
2. **MLA**：
   - 把 K、V 投影到低维 latent space
   - KV Cache 只存 latent（维度远小于 n_heads × head_dim）
   - attention 时从 latent 恢复 K、V
3. **效果**：KV Cache 压缩到 1/4
4. **代价**：恢复计算增加，但显存节省更大
5. **DeepSeek-V3**：671B MoE 用 MLA，单节点 8×H100 推理

---

## 十三、故障复盘

### 13.1 案例 1：没开 prefix cache 导致 TTFT 高

**背景**：2024 年某公司多轮对话 TTFT 800ms。

**根因**：没开 `--enable-prefix-caching`，每轮重新计算 KV Cache。

**修复**：开启 prefix cache。

**效果**：TTFT 800ms → 280ms（多轮复用 70%）。

### 13.2 案例 2：max-num-seqs 过大导致 OOM

**背景**：2024 年某公司 vLLM 高峰期 OOM。

**根因**：max-num-seqs=512，KV Cache 预留太多，权重装不下。

**修复**：max-num-seqs 降到 256，监控 KV Cache 使用率。

### 13.3 案例 3：FP8 KV Cache 在小模型上精度损失

**背景**：2025 年某公司 Llama-8B FP8 量化，输出乱码。

**根因**：8B 模型 FP8 精度损失大。

**修复**：8B 用 FP16，70B+ 用 FP8。

### 13.4 案例 4：PD 分离 KV Cache 传输慢

**背景**：2024 年某公司 PD 分离，TTFT 反而升高。

**根因**：用 TCP 传输 KV Cache，420ms。

**修复**：启用 GPU Direct RDMA，105ms。

### 13.5 案例 5：抢占导致长尾延迟

**背景**：2024 年某公司高峰期 P99 TTFT 飙升到 3s。

**根因**：KV Cache 满，低优请求被抢占重算。

**修复**：
- 配置 swap_space=16GB（CPU 卸载）
- 增加 GPU
- 监控抢占次数告警

---

## 十四、参考与延伸

### 14.1 论文

- *Efficient Memory Management for Large Language Model Serving with PagedAttention* — Kwon et al., 2023
- *SGLang: Efficient Execution of Structured Language Model Programs* — Zheng et al., 2023
- *DeepSeek-V3 Technical Report* — DeepSeek-AI, 2024（MLA）
- *vLLM: Easy, Fast, and Cheap LLM Serving* — Kwon et al., 2023

### 14.2 源码

- vLLM PagedAttention kernel — https://github.com/vllm-project/vllm/blob/main/csrc/attention/attention_kernels.cu
- SGLang RadixTree — https://github.com/sgl-project/sglang/tree/main/python/sglang/srt/managers

### 14.3 跨模块链接

- [03-LLM推理服务化总览](./03-LLM推理服务化总览.md) —— 引擎总览
- [05-连续批处理与吞吐优化](./05-连续批处理与吞吐优化.md) —— 调度
- [07-分布式推理并行策略](./07-分布式推理并行策略.md) —— PD 分离
- [LLM/02-Transformer架构](../LLM/02-Transformer架构.md) —— attention 数学

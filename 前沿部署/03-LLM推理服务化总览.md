# 03 - LLM 推理服务化总览

> LLM 推理服务化是 AI 原生部署的核心战场。本章梳理主流推理引擎（vLLM、SGLang、TGI、TensorRT-LLM、LMDeploy、MLC-LLM、Ollama）的架构、适用场景、性能基线，给出选型决策框架。
>
> 后续章节（04 KV Cache、05 continuous batching、06 量化、07 分布式、08 多模态）在引擎内部展开。

---

## 一、思维导图

```
                      LLM 推理服务化
                           │
        ┌──────────┬───────┼────────┬──────────┐
        ▼          ▼       ▼        ▼          ▼
   ┌─────────┐┌────────┐┌──────┐┌────────┐┌────────┐
   │ vLLM    ││SGLang  ││ TGI  ││TRT-LLM ││LMDeploy│
   │ 通用首选││ 复杂路由││HF官方││NVIDIA  ││OpenMMLab│
   └─────────┘└────────┘└──────┘└────────┘└────────┘
        │          │       │        │          │
   PagedAttention  RadixTree   BF16    TRT Plugin   TurboMind
   continuous batch  路由     Python  INT8/FP8     Tensor Parallel
        │          │       │        │          │
   ┌────┴────┐┌────┴────┐┌──┴───┐┌───┴────┐┌────┴────┐
   │ 商用:   ││ 商用:  ││商用:││商用:   ││商用:    │
   │ 阿里EAS ││ 商汤  ││HF Cloud│ NVIDIA Triton │ 阿里 PAI
   │ 字节    ││ 字节  ││      ││        ││         │
   └─────────┘└────────┘└──────┘└────────┘└─────────┘

辅助层:
  - Ollama (本地开发)
  - MLC-LLM (端侧/移动)
  - llama.cpp (CPU 量化)
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **引擎选型**：在 vLLM/SGLang/TGI/TRT-LLM 等之间做合理选择
- **架构理解**：每个引擎的核心创新与适用场景
- **性能基线**：用公开 benchmark 数据建立选型依据
- **协议规范**：OpenAI 兼容 API、流式、工具调用的统一标准

### 2.2 不解决什么

- 不深入每个引擎的内部实现（04-08 章节展开）
- 不覆盖训练框架（Megatron、DeepSpeed）
- 不讨论闭源引擎（OpenAI 自研、Anthropic 自研）
- 不做端侧推理（12 章专门讨论）

### 2.3 推理引擎 vs 训练框架

| 维度 | 训练框架 | 推理引擎 |
|------|----------|----------|
| 目标 | 最大化训练吞吐 | 最大化推理吞吐 + 低延迟 |
| batch | 静态大 batch（千级） | 动态 batch（1-256） |
| 内存 | 优化器状态、梯度 | KV Cache、激活 |
| 精度 | FP32/BF16 | FP16/INT8/FP8/INT4 |
| 通信 | AllReduce（梯度同步） | TP/PP（层内/层间切分） |
| 代表 | Megatron、DeepSpeed | vLLM、SGLang、TRT-LLM |

---

## 三、直觉解释

### 3.1 推理引擎的核心职责

一个推理引擎需要解决五个问题：

1. **显存管理**：模型权重 + KV Cache + 激活，显存不够会 OOM
2. **请求调度**：多请求如何 batch、抢占、调度
3. **并行策略**：单卡装不下时如何切分（TP/PP/EP）
4. **流式输出**：SSE/WebSocket 协议
5. **协议兼容**：OpenAI API、工具调用、结构化输出

### 3.2 推理引擎的演进

```
2022 之前: HuggingFace transformers (研究用, 慢)
2022:     FasterTransformer (NVIDIA, 编译优化)
2023 Q1:  TGI (HF, 第一代生产级)
2023 Q2:  vLLM (PagedAttention 革命)
2023 Q4:  TensorRT-LLM (NVIDIA, 编译 + 量化)
2024 Q1:  SGLang (RadixTree, 路由优化)
2024 Q2:  LMDeploy (OpenMMLab, TurboMind)
2024 Q4:  vLLM 0.6 (PD 分离、FP8)
2025:     DeepGEMM、FlashInfer (kernel 层优化)
```

### 3.3 引擎选型的核心考量

```
1. 模型支持: 是否支持目标模型架构 (Llama/Mistral/Qwen/DeepSeek)
2. 性能: 吞吐 / 延迟 / 显存
3. 量化: FP8/INT8/INT4 支持
4. 并行: TP/PP/EP 支持
5. 生态: OpenAI API 兼容、K8s 部署、监控
6. 团队: 是否有能力调优
7. 成本: 引擎本身免费, 但调优人力成本高
```

经验法则：**默认 vLLM，特殊场景换其他**。

---

## 四、核心概念与架构

### 4.1 主流推理引擎对比矩阵

| 引擎 | 开发方 | 核心创新 | 优势 | 劣势 | 适用场景 |
|------|--------|----------|------|------|----------|
| vLLM | UC Berkeley | PagedAttention | 通用、生态好、易用 | 大模型性能不如 TRT-LLM | 通用首选 |
| SGLang | UC Berkeley | RadixTree + 复杂路由 | 多轮对话、结构化输出 | 生态不如 vLLM | Agent、复杂链路 |
| TGI | HuggingFace | BF16 + Flash Attention | HF 生态、模型支持广 | 性能一般 | HF Cloud、研究 |
| TensorRT-LLM | NVIDIA | TRT 编译 + FP8 | 性能最优（NVIDIA 卡） | 编译复杂、闭源 kernel | 生产高性能 |
| LMDeploy | OpenMMLab | TurboMind + 编译 | 国内生态、量化好 | 模型支持不如 vLLM | 国内生产 |
| MLC-LLM | TVM 社区 | TVM 编译 + 跨平台 | 端侧、移动、Web | 性能不如专用 | 端侧推理 |
| Ollama | ollama团队 | llama.cpp 封装 | 本地易用 | 性能弱 | 本地开发 |
| llama.cpp | ggerganov | C++ + GGUF 量化 | CPU 推理、量化好 | 性能弱 | 边缘、CPU |

### 4.2 vLLM 架构

```
┌─────────────────────────────────────────┐
│           OpenAI Compatible API         │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│           AsyncLLMEngine                │
│  (请求队列、流式输出、多 worker)         │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│           LLMEngine                     │
│  ┌─────────────────────────────────┐    │
│  │   Scheduler                     │    │
│  │   - continuous batching         │    │
│  │   - preemption (优先级抢占)     │    │
│  │   - prefix caching              │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │   Block Manager (PagedAttention)│    │
│  │   - KV Cache 分块管理           │    │
│  │   - 虚拟内存思想                │    │
│  │   - 显存碎片消除                │    │
│  └─────────────────────────────────┘    │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│           Workers (per GPU)             │
│  - Model Runner (forward pass)          │
│  - KV Cache (HBM)                       │
│  - TP Communication (NCCL)              │
└─────────────────────────────────────────┘
```

**核心创新**：
1. **PagedAttention**：借鉴 OS 虚拟内存，KV Cache 分块管理，消除碎片
2. **continuous batching**：请求动态加入/退出
3. **prefix caching**：相同 system prompt 的 KV Cache 复用
4. **FP8 量化**：H100 上 FP8 支持
5. **PD 分离**：vLLM 0.6+ 支持

### 4.3 SGLang 架构

```
┌─────────────────────────────────────────┐
│         Frontend (Python DSL)           │
│  - sgl.function (复杂链路)              │
│  - sgl.gen (生成)                       │
│  - sgl.image (多模态)                   │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│         Scheduler                       │
│  - Radix Tree (prefix 高效复用)         │
│  - 路由优化 (multi-modal fan-out)       │
│  - Cache-Aware Scheduling               │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│         Model Server                    │
│  - TP/PP 并行                           │
│  - FP8/INT8 量化                        │
│  - continuous batching                  │
└─────────────────────────────────────────┘
```

**核心创新**：
1. **RadixTree**：用基数树管理 prefix，比 vLLM 的 hash 表更高效
2. **复杂链路原生支持**：multi-turn、tree-of-thought、agent 工具调用
3. **结构化输出**：JSON mode、regex constraint
4. **并发分支**：一次请求生成多个变体（用于 best-of-N）

### 4.4 TensorRT-LLM 架构

```
┌─────────────────────────────────────────┐
│      Triton Inference Server            │
│  (HTTP/gRPC API、流式、多模型)            │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│      TensorRT-LLM Engine                │
│  ┌─────────────────────────────────┐    │
│  │  Compiled Engine (TRT 编译)     │    │
│  │  - 算子融合 (Attention + MLP)   │    │
│  │  - INT8/FP8 quantization        │    │
│  │  - In-flight batching           │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │  KV Cache Manager               │    │
│  │  - Paged KV Cache               │    │
│  └─────────────────────────────────┘    │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│      NVIDIA GPUs (TP + PP)              │
│  - NVLink for TP                        │
│  - InfiniBand for PP                    │
└─────────────────────────────────────────┘
```

**核心创新**：
1. **TRT 编译**：离线编译为优化 engine，运行时无解释开销
2. **算子融合**：Attention + MLP 融合，减少 kernel launch
3. **INT8/FP8 量化**：NVIDIA 硬件原生支持
4. **In-flight batching**：等同于 continuous batching
5. **插件系统**：自定义算子（如 FlashAttention）

**劣势**：
- 编译耗时长（小模型分钟级，大模型小时级）
- 模型支持滞后（新模型要等 NVIDIA 适配）
- 调优复杂（数百参数）

### 4.5 OpenAI 兼容 API 协议

主流引擎都支持 OpenAI 兼容 API，降低迁移成本：

```python
# OpenAI 兼容请求
import openai

client = openai.OpenAI(
    base_url="http://vllm:8000/v1",  # 任何兼容引擎
    api_key="dummy"
)

# 非流式
response = client.chat.completions.create(
    model="meta-llama/Llama-3.1-70B",
    messages=[{"role": "user", "content": "Hello"}],
    max_tokens=100,
    temperature=0.7,
)
print(response.choices[0].message.content)

# 流式
stream = client.chat.completions.create(
    model="meta-llama/Llama-3.1-70B",
    messages=[{"role": "user", "content": "Write a poem"}],
    stream=True,
)
for chunk in stream:
    if chunk.choices[0].delta.content:
        print(chunk.choices[0].delta.content, end="", flush=True)
```

### 4.6 工具调用协议

OpenAI Function Calling 协议已成为事实标准：

```python
# 工具调用
response = client.chat.completions.create(
    model="meta-llama/Llama-3.1-70B",
    messages=[{"role": "user", "content": "What's the weather in SF?"}],
    tools=[{
        "type": "function",
        "function": {
            "name": "get_weather",
            "description": "Get weather for a city",
            "parameters": {
                "type": "object",
                "properties": {
                    "city": {"type": "string"}
                },
                "required": ["city"]
            }
        }
    }],
    tool_choice="auto"
)

# 模型输出: tool_call
# 调用 get_weather("SF")
# 返回结果给模型
```

主流引擎对工具调用的支持程度：
- vLLM 0.6+：完整支持
- SGLang：完整支持 + 结构化输出
- TGI：支持
- TRT-LLM：通过 Triton 支持

---

## 五、操作流程与配置

### 5.1 vLLM 部署

#### 单机单卡

```bash
# 拉取镜像
docker pull vllm/vllm-openai:v0.6.0

# 启动
docker run -d --gpus all \
  -p 8000:8000 \
  -v /models:/models \
  -e HF_TOKEN=hf_xxx \
  vllm/vllm-openai:v0.6.0 \
  --model meta-llama/Llama-3.1-70B \
  --tensor-parallel-size 1 \
  --gpu-memory-utilization 0.9 \
  --max-model-len 8192 \
  --enable-prefix-caching
```

#### 单机多卡（TP）

```bash
docker run -d --gpus all \
  -p 8000:8000 \
  -v /models:/models \
  vllm/vllm-openai:v0.6.0 \
  --model meta-llama/Llama-3.1-70B \
  --tensor-parallel-size 4 \  # 4 卡 TP
  --gpu-memory-utilization 0.9 \
  --max-model-len 32768
```

#### K8s 部署

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: vllm
spec:
  replicas: 2
  template:
    spec:
      containers:
      - name: vllm
        image: vllm/vllm-openai:v0.6.0
        args:
        - --model
        - meta-llama/Llama-3.1-70B
        - --tensor-parallel-size
        - "4"
        - --gpu-memory-utilization
        - "0.9"
        - --max-model-len
        - "32768"
        - --enable-prefix-caching
        - --kv-cache-dtype
        - fp8
        resources:
          limits:
            nvidia.com/gpu: 4
        ports:
        - containerPort: 8000
        readinessProbe:
          httpGet: {path: /health, port: 8000}
          initialDelaySeconds: 300  # 模型加载慢
        livenessProbe:
          httpGet: {path: /health, port: 8000}
          initialDelaySeconds: 600
---
apiVersion: v1
kind: Service
metadata:
  name: vllm
spec:
  selector: {app: vllm}
  ports:
  - port: 80
    targetPort: 8000
```

### 5.2 SGLang 部署

```bash
# 单机多卡
docker run -d --gpus all \
  -p 30000:30000 \
  -v /models:/models \
  lmsysorg/sglang:v0.3.0 \
  --model-path meta-llama/Llama-3.1-70B \
  --tp 4 \
  --enable-radix-attention \
  --enable-torch-compile \
  --host 0.0.0.0 --port 30000
```

### 5.3 TensorRT-LLM 部署

```bash
# 1. 编译模型 (耗时)
python3 build.py \
  --model_dir /models/llama-70b \
  --dtype float16 \
  --use_fp8 \
  --output_dir /trt_engines/llama-70b \
  --tp_size 4

# 2. 启动 Triton Server
docker run -d --gpus all \
  -p 8000:8000 -p 8001:8001 -p 8002:8002 \
  -v /trt_engines:/engines \
  -v /models:/models \
  nvcr.io/nvidia/tritonserver:24.07-trtllm-python-py3 \
  tritonserver --model-repository=/models
```

### 5.4 性能 benchmark

```bash
# vLLM benchmark
python3 -m vllm.entrypoints.openai.api_server \
  --model meta-llama/Llama-3.1-70B \
  --tensor-parallel-size 4 &

# 等待启动
sleep 300

# benchmark
python3 benchmarks/benchmark_serving.py \
  --backend vllm \
  --base-url http://localhost:8000 \
  --model meta-llama/Llama-3.1-70B \
  --dataset-name random \
  --random-input-len 1024 \
  --random-output-len 256 \
  --num-prompts 1000 \
  --request-rate 10
```

---

## 六、底层原理

### 6.1 推理引擎的五个核心模块

#### 模块 1：显存管理

```
GPU 显存分配 (H100 80GB):
├── 模型权重 (Llama-70B FP16): 140GB → 不够, 需 TP
│   └── 4 卡 TP: 每卡 35GB
├── KV Cache: 40GB/卡 (可调)
├── 激活: 5GB/卡
└── 临时缓冲: 5GB/卡
```

**PagedAttention**（vLLM）：
- KV Cache 分成固定大小 block（如 16 token/block）
- block 表（virtual → physical）映射
- 消除显存碎片，利用率从 60% → 95%

**RadixTree**（SGLang）：
- 用基数树管理 prefix
- 共享 prefix 的请求共享 KV Cache
- 比 vLLM hash 表更高效（prefix 复用率 +20%）

#### 模块 2：请求调度

```
请求生命周期:
1. 到达: 加入 waiting queue
2. 调度: scheduler 选 waiting 中的请求加入 running
3. prefill: 计算 KV Cache (算力密集)
4. decode: 逐 token 生成 (显存密集)
5. 完成 / 抢占: 退出或回 waiting

continuous batching:
- 每个 step 重新评估 running 队列
- 新请求可加入, 完成请求退出
- 不需要等批凑齐
```

**抢占机制**（vLLM）：
- 显存不够时, 抢占低优请求
- 被抢占请求的 KV Cache 可卸载到 CPU 内存
- 高优请求完成后恢复

#### 模块 3：并行策略

```
Tensor Parallelism (TP):
  单层切分到多卡
  通信: AllReduce (每层 2 次)
  适合: 单机多卡 (NVLink)

Pipeline Parallelism (PP):
  层间切分, micro-batch 流水
  通信: pipeline bubble
  适合: 跨机 (IB)

Expert Parallelism (EP) - MoE:
  专家分布到不同卡
  通信: All-to-All (路由)
  适合: MoE 模型

Data Parallelism (DP):
  完整模型多副本
  通信: 无 (独立)
  适合: 提升吞吐
```

详见 [07-分布式推理并行策略](./07-分布式推理并行策略.md)。

#### 模块 4：流式输出

```
SSE 协议:
HTTP/1.1 200 OK
Content-Type: text/event-stream

data: {"choices":[{"delta":{"content":"Hello"}}]}

data: {"choices":[{"delta":{"content":" world"}}]}

data: {"choices":[{"finish_reason":"stop"}]}

data: [DONE]
```

实现要点：
- HTTP/1.1 chunked encoding
- 禁用 proxy buffering
- 客户端断开检测
- 错误处理（中途出错怎么通知客户端）

#### 模块 5：协议兼容

OpenAI 兼容 API 的关键字段：
```python
# 请求
{
  "model": "llama-70b",
  "messages": [...],
  "temperature": 0.7,
  "top_p": 0.9,
  "max_tokens": 100,
  "stream": true,
  "tools": [...],  # 工具调用
  "response_format": {"type": "json_object"},  # JSON mode
  "n": 1,  # 生成 N 个候选
  "seed": 42  # 可重复
}

# 响应
{
  "id": "...",
  "choices": [{
    "message": {"role": "assistant", "content": "..."},
    "finish_reason": "stop",
    "usage": {"prompt_tokens": 10, "completion_tokens": 100}
  }]
}
```

### 6.2 各引擎的优化技术对比

| 优化 | vLLM | SGLang | TGI | TRT-LLM | LMDeploy |
|------|------|--------|-----|---------|----------|
| PagedAttention | ✅（首创） | ✅ | ❌ | ✅ | ✅ |
| continuous batching | ✅ | ✅ | ✅ | ✅（In-flight） | ✅ |
| prefix caching | ✅ | ✅（RadixTree 更优） | ✅ | ✅ | ✅ |
| FP8 量化 | ✅（0.6+） | ✅ | ❌ | ✅（最优） | ✅ |
| INT8 量化 | ✅ | ✅ | ✅ | ✅ | ✅ |
| TP | ✅ | ✅ | ✅ | ✅ | ✅ |
| PP | ✅（0.6+） | ✅ | ❌ | ✅ | ✅ |
| EP（MoE） | ✅（0.6+） | ✅ | ❌ | ✅ | ✅ |
| Speculative Decoding | ✅ | ✅ | ❌ | ✅ | ✅ |
| 结构化输出 | 部分 | ✅（最强） | 部分 | 部分 | 部分 |
| PD 分离 | ✅（0.6+） | ❌ | ❌ | ❌ | ❌ |

### 6.3 推理性能的关键瓶颈

#### 瓶颈 1：显存带宽（decode 阶段）

```
decode 一个 token:
- 加载模型权重: 70B × 2B (FP16) = 140GB
- 加载 KV Cache: 单请求 ~1.6GB (4K context)
- 计算: 1 token × 140GB ≈ 140GB

显存带宽 (H100): 3TB/s
理论最低延迟: 140GB / 3TB/s = 47ms

实际延迟: 25ms (TP=4, 每卡 35GB, 35/3000=12ms)
```

**结论**：decode 是显存带宽密集，TP 能有效降延迟。

#### 瓶颈 2：算力（prefill 阶段）

```
prefill 1024 token prompt:
- FLOPS: 2 × 70B × 1024 = 143 TFLOPS
- H100 FP16 算力: 989 TFLOPS
- 理论最低延迟: 143/989 = 145ms (单卡)
- TP=4: 36ms
```

**结论**：prefill 是算力密集，TP 也能加速。

#### 瓶颈 3：通信（TP 并行）

```
TP 通信 (每层 AllReduce):
- 数据量: batch_size × hidden_dim × 2B
- Llama-70B hidden=8192, batch=64: 1MB/层 × 80 层 = 80MB
- NVLink 带宽: 900GB/s
- 通信延迟: 80MB / 900GB/s = 0.09ms (可忽略)

跨机 TP (无 NVLink):
- IB 带宽: 50GB/s (400Gbps)
- 通信延迟: 80MB / 50GB/s = 1.6ms
- 占总延迟比例: 1.6/12 = 13% (可接受)
```

**结论**：TP 优先单机（NVLink），跨机用 PP。

---

## 七、代码与配置示例

### 7.1 vLLM 完整生产配置

```yaml
# vllm-production.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: vllm-llama70b
  labels: {app: vllm-llama70b}
spec:
  replicas: 4
  strategy:
    rollingUpdate: {maxSurge: 1, maxUnavailable: 0}
  template:
    metadata:
      labels: {app: vllm-llama70b}
    spec:
      terminationGracePeriodSeconds: 300  # 流式连接优雅停止
      containers:
      - name: vllm
        image: vllm/vllm-openai:v0.6.0
        args:
        - --model
        - meta-llama/Llama-3.1-70B
        - --tensor-parallel-size
        - "4"
        - --gpu-memory-utilization
        - "0.9"
        - --max-model-len
        - "32768"
        - --max-num-seqs
        - "256"
        - --max-num-batched-tokens
        - "8192"
        - --enable-prefix-caching
        - --kv-cache-dtype
        - fp8
        - --quantization
        - fp8
        - --enforce-eager  # 生产用 eager, 调试时关闭
        - --host
        - 0.0.0.0
        - --port
        - "8000"
        resources:
          limits:
            nvidia.com/gpu: 4
            memory: 256Gi
            cpu: "32"
          requests:
            nvidia.com/gpu: 4
            memory: 256Gi
            cpu: "32"
        env:
        - name: HF_TOKEN
          valueFrom:
            secretKeyRef: {name: hf-secret, key: token}
        - name: VLLM_NO_USAGE_STATS
          value: "1"
        ports:
        - containerPort: 8000
        readinessProbe:
          httpGet: {path: /health, port: 8000}
          initialDelaySeconds: 300
          periodSeconds: 10
        livenessProbe:
          httpGet: {path: /health, port: 8000}
          initialDelaySeconds: 600
          periodSeconds: 30
        volumeMounts:
        - name: dshm
          mountPath: /dev/shm
        - name: model-cache
          mountPath: /root/.cache/huggingface
        lifecycle:
          preStop:
            exec:
              command: ["sleep", "60"]  # 等流式连接完成
      volumes:
      - name: dshm
        emptyDir:
          medium: Memory
          sizeLimit: 16Gi
      - name: model-cache
        persistentVolumeClaim:
          claimName: hf-cache-pvc
---
apiVersion: v1
kind: Service
metadata:
  name: vllm-llama70b
spec:
  selector: {app: vllm-llama70b}
  ports:
  - port: 80
    targetPort: 8000
---
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: vllm-llama70b-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: vllm-llama70b
  minReplicas: 4
  maxReplicas: 16
  metrics:
  - type: Pods
    pods:
      metric: {name: vllm_pending_requests}
      target: {type: AverageValue, averageValue: "10"}
  - type: Resource
    resource:
      name: cpu
      target: {type: Utilization, averageUtilization: 70}
```

### 7.2 Nginx 流式代理配置

```nginx
upstream vllm {
    server vllm-llama70b:80;
    keepalive 32;
}

server {
    listen 80;
    client_max_body_size 50M;

    # 流式请求
    location /v1/chat/completions {
        proxy_pass http://vllm;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;

        # 流式关键配置
        proxy_buffering off;
        proxy_cache off;
        proxy_read_timeout 600s;
        proxy_send_timeout 600s;
        chunked_transfer_encoding on;

        # SSE 关闭 gzip
        gzip off;
    }

    # 非流式
    location /v1 {
        proxy_pass http://vllm;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_read_timeout 60s;
    }
}
```

### 7.3 客户端调用示例

```python
# OpenAI SDK (最简单)
from openai import OpenAI

client = OpenAI(
    base_url="https://api.myorg.com/v1",
    api_key="sk-xxx"
)

# 流式
def chat_stream(messages, **kwargs):
    stream = client.chat.completions.create(
        model="llama-3.1-70b",
        messages=messages,
        stream=True,
        **kwargs
    )
    for chunk in stream:
        delta = chunk.choices[0].delta
        if delta.content:
            yield delta.content

# 工具调用
def chat_with_tools(messages, tools):
    response = client.chat.completions.create(
        model="llama-3.1-70b",
        messages=messages,
        tools=tools,
        tool_choice="auto"
    )
    msg = response.choices[0].message
    if msg.tool_calls:
        for call in msg.tool_calls:
            args = json.loads(call.function.arguments)
            result = execute_tool(call.function.name, args)
            messages.append(msg)
            messages.append({
                "role": "tool",
                "tool_call_id": call.id,
                "content": json.dumps(result)
            })
        return chat_with_tools(messages, tools)
    return msg.content
```

### 7.4 监控指标

```yaml
# Prometheus ServiceMonitor
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: vllm-monitor
spec:
  selector:
    matchLabels: {app: vllm-llama70b}
  endpoints:
  - port: http
    path: /metrics
    interval: 15s
---
# 关键指标
# vllm:num_requests_running - 运行中请求数
# vllm:num_requests_waiting - 排队请求数
# vllm:gpu_cache_usage_perc - KV Cache 使用率
# vllm:time_to_first_token_seconds - TTFT 分布
# vllm:time_per_output_token_seconds - TPOT 分布
# vllm:e2e_request_latency_seconds - 端到端延迟
```

```promql
# PromQL 告警
# 排队过多
alert: VLLMHighQueueDepth
expr: avg(vllm:num_requests_waiting) > 50
for: 5m
annotations:
  summary: "VLLM queue depth high"

# TTFT 过高
alert: VLLMHighTTFT
expr: histogram_quantile(0.99, rate(vllm:time_to_first_token_seconds_bucket[5m])) > 1
for: 5m
annotations:
  summary: "VLLM P99 TTFT > 1s"
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：没开 continuous batching

**症状**：vLLM 吞吐只有理论值 1/10。

**根因**：默认 `--max-num-seqs=1`，实际只批 1 个请求。

**修复**：
```bash
--max-num-seqs 256
--max-num-batched-tokens 8192
```

### 8.2 陷阱 2：模型加载慢导致 readiness 失败

**症状**：K8s readiness probe 失败，Pod 一直重启。

**根因**：Llama-70B 加载 5 分钟，readiness initialDelay 30s 不够。

**修复**：`initialDelaySeconds: 600`。

### 8.3 陷阱 3：流式被 Nginx 切断

**症状**：长回复在 60s 处断。

**根因**：Nginx `proxy_read_timeout 60s` 默认 + `proxy_buffering on`。

**修复**：
```nginx
proxy_buffering off;
proxy_read_timeout 600s;
```

### 8.4 陷阱 4：FP8 量化精度损失

**症状**：FP8 量化后输出质量下降。

**根因**：FP8 在某些模型上精度损失大（特别是小模型）。

**修复**：
- 大模型（70B+）FP8 安全
- 小模型用 INT8 或 FP16
- 评估用 perplexity + 下游任务

### 8.5 陷阱 5：TP 跨机性能差

**症状**：TP=8 跨机比 TP=8 单机慢 3x。

**根因**：跨机无 NVLink，AllReduce 走 IB 慢。

**修复**：单机内用 TP，跨机用 PP。

### 8.6 调优 Checklist

- [ ] continuous batching 开启
- [ ] prefix caching 开启
- [ ] max-num-seqs 适配显存
- [ ] gpu-memory-utilization 0.9+
- [ ] FP8/INT8 量化（大模型）
- [ ] TP 单机内（NVLink）
- [ ] readinessProbe initialDelay 够长
- [ ] Nginx 流式配置
- [ ] Prometheus 监控
- [ ] HPA on 排队数

---

## 九、工业案例与基准数据

### 9.1 基准：Llama-3.1-70B 在不同引擎上的表现

环境：8×H100 80GB，NVLink，输入 1024 token，输出 256 token，1000 请求。

| 引擎 | 吞吐 (tokens/s) | P50 TTFT | P99 TTFT | 显存利用 |
|------|-----------------|----------|----------|----------|
| vLLM 0.6 (FP16) | 1800 | 180ms | 450ms | 85% |
| vLLM 0.6 (FP8) | 2800 | 130ms | 320ms | 70% |
| SGLang 0.3 (FP16) | 2100 | 160ms | 400ms | 85% |
| TGI 4.0 (FP16) | 1200 | 220ms | 600ms | 80% |
| TRT-LLM (FP8) | 3200 | 110ms | 280ms | 75% |
| LMDeploy (FP8) | 2600 | 140ms | 350ms | 72% |

观察：
- TRT-LLM 性能最优（编译优化）
- vLLM FP8 性价比最高（开源 + FP8）
- SGLang 在多轮对话场景更优（RadixTree）

### 9.2 案例 1：阿里 PAI-EAS 选型

**背景**：通义千问推理服务化。

**选型**：
- 起初用 TGI（HF 官方，模型支持广）
- 2023 Q3 切换 vLLM（PagedAttention 革命）
- 2024 Q1 引入 TRT-LLM（性能极致）
- 2024 Q3 自研 cnao（统一调度）

**结果**：
- TGI → vLLM：吞吐 +50%
- vLLM → TRT-LLM：吞吐 +30%
- 自研 cnao + PD 分离：吞吐 +20%

### 9.3 案例 2：字节豆包选型

**背景**：豆包亿级日活。

**选型**：
- 早期用 vLLM
- 2024 年部分场景切 SGLang（Agent 链路）
- 自研 VeRLm 平台（统一多引擎）

**结果**：
- vLLM 适合通用 API
- SGLang 适合 Agent（RadixTree 多轮复用）
- VeRLm 屏蔽底层差异

### 9.4 案例 3：DeepSeek 选型

**背景**：DeepSeek-V3 (671B MoE)。

**选型**：
- 自研推理引擎（不开源）
- 借鉴 vLLM 的 PagedAttention
- 自研 MoE 专家路由
- FP8 量化（H100 原生）

**结果**：
- 单节点 8×H100 推理 V3
- 吞吐 60K tokens/s
- 成本 $0.27/M token（行业最低）

### 9.5 案例 4：OpenAI 选型（推测）

**背景**：GPT-4 推理。

**选型**（基于公开论文）：
- 自研推理引擎
- PD 分离（DistServe 思想）
- speculative decoding
- TP + PP 混合

**结果**：
- TTFT 300ms（GPT-4 Turbo）
- 比 GPT-4 成本低 70%

---

## 十、与其他方案的关系

### 10.1 引擎 vs 平台

| 类型 | 代表 | 提供方 | 特点 |
|------|------|--------|------|
| 引擎 | vLLM、SGLang | 开源 | 自部署 |
| 平台 | PAI-EAS、VeRLm | 阿里、字节 | 托管 + 调度 |
| Serverless | Modal、Replicate | 创业公司 | 按秒计费 |
| 闭源 API | OpenAI、Anthropic | 闭源 | 模型即服务 |

### 10.2 引擎选型决策树

```
                ┌──────────────────────┐
                │ 是否 NVIDIA GPU?     │
                └──────────┬───────────┘
                           │
            ┌──────────────┴──────────────┐
            ▼                             ▼
        [NVIDIA]                      [非 NVIDIA]
            │                             │
   ┌────────┴────────┐              ┌─────┴─────┐
   ▼                 ▼              ▼           ▼
[追求极致性能]    [通用首选]      [AMD GPU]    [端侧]
   │                 │              │           │
   ▼                 ▼              ▼           ▼
TRT-LLM           vLLM           vLLM         MLC-LLM
                                  (ROCm)      Ollama
```

### 10.3 引擎组合使用

生产环境常组合使用：

```
[Client] → [API Gateway]
              ├──→ [vLLM] (通用 Llama-70B)
              ├──→ [SGLang] (Agent 链路)
              ├──→ [TRT-LLM] (高性能 Llama-405B)
              └──→ [Ollama] (本地小模型 fallback)
```

---

## 十一、面试速答

**Q1: vLLM 的核心创新？**

A: PagedAttention，借鉴 OS 虚拟内存，KV Cache 分块管理，消除显存碎片，利用率从 60% → 95%。配合 continuous batching，吞吐相比 HF transformers 提升 24x。

**Q2: vLLM vs SGLang 区别？**

A: vLLM 通用首选，生态好。SGLang 用 RadixTree 管理 prefix，多轮对话和复杂链路（Agent）场景更优。SGLang 还原生支持结构化输出（JSON mode、regex）。

**Q3: TRT-LLM 为什么快？**

A: 离线编译为优化 engine，运行时无解释开销。算子融合（Attention + MLP），减少 kernel launch。NVIDIA 硬件原生 INT8/FP8 支持。劣势是编译耗时长、模型支持滞后。

**Q4: OpenAI 兼容 API 的意义？**

A: 主流引擎都支持，降低迁移成本。客户端用 openai SDK 即可，只需改 base_url。工具调用、流式、JSON mode 协议统一。

**Q5: 推理引擎的五个核心模块？**

A: 显存管理（KV Cache）、请求调度（continuous batching）、并行策略（TP/PP/EP）、流式输出（SSE）、协议兼容（OpenAI API）。

**Q6: TP 为什么优先单机？**

A: TP 通信是 AllReduce（每层 2 次），单机 NVLink 900GB/s，跨机 IB 50GB/s（18x 慢）。跨机用 PP，PP 通信量少（pipeline bubble）。

**Q7: FP8 量化的注意事项？**

A: 大模型（70B+）FP8 精度损失可接受（< 1% perplexity）。小模型（7B 以下）FP8 损失大，建议 INT8 或 FP16。H100 原生支持 FP8，A100 不支持。

**Q8: 如何选推理引擎？**

A: 默认 vLLM（通用 + 生态）。NVIDIA 卡 + 追求极致性能用 TRT-LLM。Agent 链路用 SGLang。端侧用 MLC-LLM。本地开发用 Ollama。

---

## 十二、综合面试题

### 题 1（初级）：vLLM 的 PagedAttention 解决什么问题？

**答题要点**：
1. **问题**：KV Cache 显存碎片化。传统 contiguous 分配，请求长度不一导致碎片，利用率仅 60%。
2. **方案**：借鉴 OS 虚拟内存。KV Cache 分成 16 token/block 的固定块。block 表映射 virtual → physical。
3. **效果**：
   - 显存利用率 60% → 95%
   - 同显存可容纳更多并发请求
   - 吞吐提升 2-3x
4. **配合**：continuous batching 让请求动态加入/退出，进一步提吞吐。

### 题 2（中级）：对比 vLLM 和 TensorRT-LLM，如何选？

**答题要点**：
1. **vLLM 优势**：
   - 开源、生态好
   - 模型支持快（新模型几天内适配）
   - 易用（一行命令启动）
   - PD 分离（0.6+）
2. **TRT-LLM 优势**：
   - 性能最优（编译优化、算子融合）
   - INT8/FP8 原生支持
   - NVIDIA 官方维护
3. **vLLM 劣势**：
   - 性能比 TRT-LLM 低 10-20%
4. **TRT-LLM 劣势**：
   - 编译耗时长（大模型小时级）
   - 模型支持滞后
   - 调优复杂（数百参数）
5. **选型**：
   - 通用首选 vLLM
   - 追求极致性能（已稳定模型）用 TRT-LLM
   - 新模型先用 vLLM，稳定后切 TRT-LLM

### 题 3（高级）：设计一个支持多模型的推理平台

**答题要点**：
1. **架构**：
```
[Client] → [API Gateway]
              ↓
         [Model Router] (基于模型名路由)
              ↓
   ┌──────────┬──────────┐
   ▼          ▼          ▼
[vLLM]    [SGLang]   [TRT-LLM]
(Llama)  (Agent)    (Llama-405B)
```
2. **调度**：基于模型名 + 负载 + KV Cache 局部性
3. **资源池**：GPU 池化，按模型动态分配
4. **多租户**：每租户独立 quota + 限流
5. **监控**：每模型独立指标（TTFT/TPOT/吞吐/利用率）
6. **弹性**：每模型独立 HPA
7. **成本**：模型间共享 GPU（MPS/MIG）
8. **部署**：K8s + GPU Operator + Volcano

**加分项**：提阿里 PAI-EAS、字节 VeRLm 案例，提具体技术选型。

### 题 4（高级）：vLLM 0.6 的 PD 分离如何实现？

**答题要点**：
1. **架构**：
   - Prefill 集群（高算力 H100）
   - Decode 集群（高显存密度 A10）
   - KV Cache 跨节点传输
2. **协议**：Producer（Prefill）→ Consumer（Decode）
3. **传输**：NCCL/RDMA，可选 GPU Direct RDMA
4. **路由**：Router 选 Prefill 节点 → 计算完 KV Cache → 传给 Decode 节点
5. **优势**：TTFT 降 50%，吞吐 +30%（DistServe 论文）
6. **劣势**：架构复杂，小模型不必（7B 以下）
7. **配置**：
```python
kv_transfer_config = KvTransferConfig(
    kv_role="producer",
    kv_connector_module="PyNcclConnector",
    kv_rank=0,
    kv_world_size=2,
)
```

### 题 5（高级）：如何评估推理引擎的性能？

**答题要点**：
1. **指标**：
   - 吞吐（tokens/s）
   - TTFT P50/P99
   - TPOT P50/P99
   - 显存利用率
   - GPU 利用率
2. **基准**：vLLM benchmark_serving.py
3. **场景**：
   - 不同输入长度（512/1024/4096）
   - 不同输出长度（128/256/1024）
   - 不同并发（1/10/100/1000）
4. **对比**：固定输入，对比多引擎
5. **长期**：连续 24 小时压测，看稳定性
6. **真实流量**：用生产流量回放

---

## 十三、故障复盘

### 13.1 案例 1：vLLM 没开 continuous batching 导致吞吐低

**背景**：2024 年某公司部署 vLLM，吞吐只有 200 tokens/s。

**现象**：
- GPU 利用率 15%
- 显存利用 40%
- 单卡 QPS 1

**根因**：
- 默认 `--max-num-seqs=1`
- 实际只批 1 个请求
- 没开 continuous batching

**修复**：
```bash
--max-num-seqs 256
--max-num-batched-tokens 8192
--enable-prefix-caching
```

**效果**：
- 吞吐 200 → 1800 tokens/s（9x）
- GPU 利用率 15% → 75%

### 13.2 案例 2：TRT-LLM 编译耗时长阻塞发布

**背景**：2024 年某公司用 TRT-LLM，每次模型更新编译 4 小时。

**现象**：
- 模型权重更新后必须重新编译
- 编译 4 小时阻塞发布
- 紧急修复无法及时上线

**根因**：
- TRT 编译耗时长（70B 模型 4 小时）
- 没有缓存机制

**修复**：
- 编译结果缓存（CI/CD artifact）
- 双版本并行（旧版本服务，新版本编译）
- 紧急修复用 vLLM 临时替代

**效果**：
- 发布周期从 4 小时 → 30 分钟
- 紧急修复 SLA 兑现

### 13.3 案例 3：SGLang RadixTree 内存泄漏

**背景**：2025 年某公司用 SGLang，运行 24 小时后 OOM。

**现象**：
- 显存缓慢增长
- 24 小时后 OOM
- 重启后恢复

**根因**：
- SGLang 早期版本 RadixTree 内存泄漏
- prefix cache 没有正确淘汰

**修复**：
- 升级 SGLang 版本
- 配置 `--max-radix-cache-size` 限制
- 监控显存增长告警

**效果**：
- 稳定运行不再 OOM

### 13.4 案例 4：FP8 量化导致输出乱码

**背景**：2025 年某公司 Llama-8B 用 FP8 量化，输出乱码。

**现象**：
- 部分请求输出乱码
- perplexity 上升 5x
- 小模型尤其严重

**根因**：
- 8B 小模型 FP8 精度损失大
- 部分层（attention）对精度敏感

**修复**：
- 8B 模型用 INT8 或 FP16
- 70B+ 才用 FP8
- 量化前评估 perplexity

**效果**：
- 输出正常
- 大模型仍用 FP8 省显存

### 13.5 案例 5：跨机 TP 性能差 3x

**背景**：2024 年某公司 TP=8 跨机部署，比单机慢 3x。

**现象**：
- 单机 TP=8 (8 卡 NVLink): 1800 tokens/s
- 跨机 TP=8 (2 机 × 4 卡): 600 tokens/s

**根因**：
- 跨机无 NVLink
- AllReduce 走 IB 50GB/s（vs NVLink 900GB/s）
- 通信开销占 60%

**修复**：
- 单机内用 TP（最多 8 卡）
- 跨机用 PP（通信量少）
- TP=4 + PP=2 跨 2 机

**效果**：
- 性能恢复 1700 tokens/s

---

## 十四、参考与延伸

### 14.1 官方文档

- vLLM Documentation — https://docs.vllm.ai/
- SGLang — https://github.com/sgl-project/sglang
- HuggingFace TGI — https://huggingface.co/docs/text-generation-inference/
- TensorRT-LLM — https://github.com/NVIDIA/TensorRT-LLM
- LMDeploy — https://github.com/InternLM/lmdeploy
- MLC-LLM — https://github.com/mlc-ai/mlc-llm
- Ollama — https://ollama.com/

### 14.2 论文

- *Efficient Memory Management for Large Language Model Serving with PagedAttention* — Kwon et al., 2023
- *SGLang: Efficient Execution of Structured Language Model Programs* — Zheng et al., 2023
- *Orca: A Distributed Serving System for Transformer-Based Generative Models* — Yu et al., 2022
- *DistServe: Disaggregating Prefill and Decoding* — Zhong et al., 2024
- *FlashAttention-2: Faster Attention with Better Parallelism and Work Partitioning* — Dao, 2023

### 14.3 大厂工程博客

- vLLM Blog — https://blog.vllm.ai/
- SGLang Blog — https://lmsys.org/blog/
- NVIDIA TensorRT-LLM — https://developer.nvidia.com/blog
- HuggingFace TGI — https://huggingface.co/blog
- 阿里 PAI-EAS — https://www.alibabacloud.com/zh/blog
- 字节 VeRLm — 字节跳动技术团队

### 14.4 跨模块链接

- [01-部署演进与前沿范式](./01-部署演进与前沿范式.md) —— AI 原生范式总览
- [02-前沿部署的核心矛盾与权衡](./02-前沿部署的核心矛盾与权衡.md) —— 延迟/吞吐权衡
- [04-KV-Cache与PagedAttention](./04-KV-Cache与PagedAttention.md) —— KV Cache 深入
- [05-连续批处理与吞吐优化](./05-连续批处理与吞吐优化.md) —— continuous batching
- [06-模型量化与压缩部署](./06-模型量化与压缩部署.md) —— FP8/INT8
- [07-分布式推理并行策略](./07-分布式推理并行策略.md) —— TP/PP/EP
- [09-Agent系统部署与沙箱](./09-Agent系统部署与沙箱.md) —— Agent 链路
- [LLM/01-大模型基础](../LLM/01-大模型基础.md) —— 模型侧基础

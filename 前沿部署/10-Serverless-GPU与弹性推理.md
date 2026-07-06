# 10 - Serverless GPU 与弹性推理

> Serverless GPU 是 AI 原生部署的弹性极致：按秒计费、Scale-to-Zero、无需运维。但冷启动、单价高、有状态推理难是其核心矛盾。本章梳理 Serverless GPU 平台谱系、适用场景、冷启动优化、与稳态推理的混合部署。

---

## 一、思维导图

```
                Serverless GPU
                      │
        ┌─────────────┼─────────────┐
        ▼             ▼             ▼
   ┌─────────┐  ┌───────────┐  ┌───────────┐
   │ 平台    │  │ 计费模型  │  │ 冷启动    │
   │ Modal   │  │ 按秒      │  │ 模型加载  │
   │ Replicate│ │ 按请求    │  │ 快照恢复  │
   │ Baseten │  │ 按 token  │  │ 预热      │
   │ PAI-EAS │  │           │  │           │
   └─────────┘  └───────────┘  └───────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **Serverless GPU 平台对比**：Modal/Replicate/Baseten/PAI-EAS
- **计费模型**：按秒/按请求/按 token
- **冷启动优化**：模型加载、快照、预热
- **混合部署**：稳态 + 突发的取舍

### 2.2 不解决什么

- 不深入 GPU 调度（11 章冷启动）
- 不覆盖边缘推理（12 章）

---

## 三、直觉解释

### 3.1 Serverless GPU 的核心价值

```
传统 GPU 部署:
  预留 8×H100, 24×7 在线
  利用率: 均值 30%
  成本: $1.8/小时 × 8 × 24 × 30 = $10.4K/月

Serverless GPU:
  按需启动, 用完释放
  Scale-to-Zero: 无流量时 0 成本
  单价: $5/小时 (1.94x 预留)
  适合: 突发流量
```

### 3.2 适用场景

```
适合 Serverless GPU:
  - 突发流量 (白天高, 夜间零)
  - A/B 实验 (短期大量推理)
  - 开发测试 (偶尔用)
  - 多模型轮换 (不同时段不同模型)

不适合:
  - 24×7 稳态 (预留更便宜)
  - 长任务 (Serverless 超时限制)
  - 实时低延迟 (冷启动不可控)
```

### 3.3 平台对比

| 平台 | 单价 (H100) | 冷启动 | 适合 | 备注 |
|------|-------------|--------|------|------|
| Modal | $5/h | 1-30s | 突发、开发 | SDK 友好 |
| Replicate | $0.000225/s | 1-10s | API 调用 | 模型市场 |
| Baseten | $4/h | 1-5s | 生产 | 稳定 |
| AWS Inferentia | $1.5/h | 5-30s | AWS 生态 | 自研芯片 |
| 阿里 PAI-EAS | $3/h | 5-30s | 国内 | 托管 |
| 字节 VeRLm | 内部 | 5-30s | 字节内部 | 自研 |

---

## 四、核心概念与架构

### 4.1 Modal 架构

```python
# Modal Serverless GPU
import modal

app = modal.App("my-llm")

image = modal.Image.debian_slim().pip_install(
    "vllm", "transformers", "torch"
)

@app.function(
    image=image,
    gpu="A100-80GB",
    min_containers=0,  # Scale-to-Zero
    max_containers=10,
    container_idle_timeout=300,  # 5 分钟空闲后释放
)
@modal.asgi_app()
def serve():
    from vllm import LLM
    llm = LLM(model="meta-llama/Llama-3.1-70B")
    # 启动时加载模型

    from fastapi import FastAPI
    web_app = FastAPI()

    @web_app.post("/generate")
    def generate(prompt: str):
        output = llm.generate(prompt)
        return {"text": output}

    return web_app
```

### 4.2 冷启动分解

```
Serverless GPU 冷启动:
  1. 容器启动: 100ms (Firecracker)
  2. Python 解释器: 500ms
  3. 依赖 import: 2-5s (PyTorch, Transformers)
  4. 模型权重加载: 10s-2min (取决于大小)
     - Llama-7B: 14GB, ~10s
     - Llama-70B: 140GB, ~2min
     - Llama-70B FP8: 70GB, ~1min
  5. CUDA 初始化: 500ms
  6. KV Cache 预分配: 1s

总冷启动:
  - 7B: ~15s
  - 70B FP8: ~65s
  - 70B FP16: ~125s
```

### 4.3 冷启动优化

#### 1. 模型权重本地缓存

```python
# Modal Volume 持久化模型
vol = modal.Volume.from_name("llama-weights", create_if_missing=True)

@app.function(image=image, volumes={"/models": vol})
def serve():
    # 模型从本地 Volume 加载, 而非 HuggingFace 下载
    llm = LLM(model="/models/llama-70b")
```

效果：省去下载时间（140GB / 1Gbps = 1120s → 0s）。

#### 2. 快照恢复

```python
# Firecracker snapshot
@app.function(image=image, snapshot=True)
def serve():
    # 启动时恢复快照 (Python + 依赖已加载)
    llm = LLM(model="...")  # 这部分仍需加载
```

效果：省去 Python + 依赖 import（5s → 100ms）。

#### 3. 保持温热

```python
# min_containers=1 保持至少 1 个温热实例
@app.function(
    image=image,
    gpu="A100",
    min_containers=1,  # 至少 1 个常驻
    max_containers=10,
)
def serve():
    ...
```

效果：冷启动 0（温热实例常驻），但空闲时也计费。

#### 4. 模型预热

```python
# 定时预热 (Cron)
@modal.Cron("0 8 * * *")  # 每天 8 点
def warmup():
    # 触发实例启动
    serve.remote("warmup")
```

效果：早晨流量来之前已温热。

### 4.4 计费模型对比

```
按秒计费 (Modal):
  $5/h = $0.00139/s
  100 次推理, 每次 5s
  成本: 100 × 5 × $0.00139 = $0.69

按请求计费 (Replicate):
  $0.000225/s × 5s = $0.001125/请求
  100 次: $0.11

按 token 计费 (OpenAI API):
  $0.005/M input token
  100 次 × 1000 token = 0.1M token
  成本: $0.0005

预留 (K8s):
  $1.8/h × 24h = $43.2/天
  100 次推理, $43.2

权衡:
  - 突发 100 次: Serverless $0.69 vs 预留 $43.2
  - 稳态 10K 次/天: Serverless $69 vs 预留 $43.2
```

---

## 五、操作流程与配置

### 5.1 Modal 完整部署

```python
# modal_llm.py
import modal

app = modal.App("llama-70b-serverless")

image = modal.Image.debian_slim().pip_install(
    "vllm==0.6.0",
    "transformers",
    "torch",
).apt_install("libcurl4-openssl-dev")

vol = modal.Volume.from_name("llama-weights", create_if_missing=True)

@app.function(
    image=image,
    gpu="A100-80GB",
    min_containers=0,
    max_containers=10,
    container_idle_timeout=300,
    volumes={"/models": vol},
    timeout=600,
)
@modal.asgi_app()
def serve():
    import os
    from vllm import LLM
    from fastapi import FastAPI

    llm = LLM(
        model="/models/llama-70b",
        tensor_parallel_size=1,
        gpu_memory_utilization=0.9,
        enable_prefix_caching=True,
    )

    web_app = FastAPI()

    @web_app.post("/v1/chat/completions")
    def chat(req: dict):
        if req.get("stream"):
            # 流式
            return StreamingResponse(
                stream(req),
                media_type="text/event-stream"
            )
        # 非流式
        output = llm.generate(req["messages"][0]["content"])
        return {"choices": [{"message": {"content": output}}]}

    return web_app

# 部署
# modal deploy modal_llm.py
```

### 5.2 混合部署（K8s + Serverless）

```yaml
# K8s base (稳态)
apiVersion: apps/v1
kind: Deployment
metadata:
  name: vllm-base
spec:
  replicas: 8  # base 容量
  template:
    spec:
      containers:
      - name: vllm
        image: vllm/vllm-openai:v0.6.0
        resources:
          limits: {nvidia.com/gpu: 4}
---
# KEDA 触发 Serverless (peak)
apiVersion: keda.sh/v1alpha1
kind: ScaledObject
metadata:
  name: modal-peak
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: modal-peak-proxy
  minReplicaCount: 0
  maxReplicaCount: 4
  triggers:
  - type: prometheus
    metadata:
      serverAddress: http://prometheus:9090
      metricName: vllm_pending_requests
      threshold: "20"
      query: |
        sum(vllm:num_requests_waiting{deployment="vllm-base"})
```

---

## 六、底层原理

### 6.1 Serverless GPU 调度

```
请求到达 → 控制面
  ↓
检查温热实例
  ├── 有: 直接路由
  └── 无: 启动新实例
        ├── 拉镜像 (缓存命中 < 1s, 否则分钟级)
        ├── 启动容器 (Firecracker 125ms)
        ├── 加载模型 (10s-2min)
        └── 就绪后路由请求
```

### 6.2 Scale-to-Zero 的代价

```
Scale-to-Zero 节省成本:
  无流量时: $0/h
  对比预留: $1.8/h × 24 = $43.2/天

代价:
  - 首请求冷启动 10s-2min
  - 用户体验差
  - SLA 难兑现
```

### 6.3 计费数学

```
场景: 日均 100K 请求, 平均 5s/请求, 峰值 10x

按秒计费 (Modal $5/h):
  总 GPU 时间 = 100K × 5s = 500K s = 139h
  成本 = 139 × $5 = $695/天

预留 (K8s $1.8/h):
  峰值需要 10x = 假设 10 卡应对峰值
  10 × $1.8 × 24 = $432/天
  但均值只用 1 卡, 利用率 10%

混合 (base 3 卡 + Serverless peak):
  base: 3 × $1.8 × 24 = $130/天
  Serverless: 峰值 7 卡 × 5h × $5 = $175/天
  总: $305/天 (最优)
```

---

## 七、代码与配置示例

### 7.1 Modal 流式

```python
from fastapi.responses import StreamingResponse
import json

@web_app.post("/v1/chat/completions")
async def chat(req: dict):
    if req.get("stream"):
        async def stream():
            for chunk in llm.stream_generate(req["messages"][0]["content"]):
                data = {"choices": [{"delta": {"content": chunk}}]}
                yield f"data: {json.dumps(data)}\n\n"
            yield "data: [DONE]\n\n"
        return StreamingResponse(stream(), media_type="text/event-stream")
```

### 7.2 监控

```python
# Modal 自动提供指标
# 通过 modal app logs 查看
# 也可集成 Prometheus

@app.function(gpu="A100")
def serve():
    # 自定义指标
    from prometheus_client import Counter, Histogram
    request_count = Counter('requests', 'Total requests')
    latency = Histogram('latency_seconds', 'Request latency')

    @web_app.post("/generate")
    def generate():
        request_count.inc()
        with latency.time():
            return llm.generate(...)
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：Scale-to-Zero 导致冷启动频繁

**症状**：低流量时段首请求慢。

**修复**：min_containers=1 保持温热。

### 8.2 陷阱 2：模型加载慢

**症状**：冷启动 2 分钟。

**修复**：Volume 本地缓存 + 快照恢复。

### 8.3 陷阱 3：稳态用 Serverless 贵

**症状**：日均 10K 请求，Serverless 比预留贵 2x。

**修复**：稳态迁 K8s，突发留 Serverless。

### 8.4 陷阱 4：长任务超时

**症状**：30 分钟推理被 Serverless 超时切断。

**修复**：拆分为多个短任务，或用预留。

### 8.5 调优 Checklist

- [ ] 模型权重本地缓存
- [ ] 快照恢复
- [ ] min_containers 配置（按流量）
- [ ] 定时预热
- [ ] 流式输出
- [ ] 监控冷启动率
- [ ] 混合部署（base + peak）

---

## 九、工业案例与基准数据

### 9.1 冷启动基准

| 模型 | 冷启动（无优化） | 优化后 |
|------|-----------------|--------|
| Llama-7B | 15s | 2s（快照 + 缓存） |
| Llama-70B FP8 | 65s | 10s |
| Llama-70B FP16 | 125s | 20s |

### 9.2 案例 1：Modal 上跑 Llama-70B

**配置**：A100 80GB，FP8，Volume 缓存。

**效果**：
- 冷启动 10s
- 推理 25ms/token
- 单次 5s 推理成本 $0.007

### 9.3 案例 2：Replicate 模型市场

**背景**：Replicate 提供模型 API 市场。

**方案**：
- 模型权重全局缓存
- 按秒计费
- 自动 Scale-to-Zero

**效果**：开发者无需部署，直接 API 调用。

### 9.4 案例 3：阿里 PAI-EAS Serverless

**背景**：阿里 PAI-EAS 弹性推理。

**方案**：
- K8s + ECI（弹性容器实例）
- 模型权重 OSS 缓存
- 定时预热

**效果**：
- 冷启动 30s
- 按秒计费
- 与 PAI 生态集成

---

## 十、与其他方案的关系

### 10.1 Serverless vs 预留

| 维度 | Serverless | 预留 |
|------|-----------|------|
| 单价 | 高（1.5-3x） | 低 |
| 弹性 | 极好 | 差 |
| 冷启动 | 有 | 无 |
| 适合 | 突发 | 稳态 |
| 运维 | 简单 | 复杂 |

---

## 十一、面试速答

**Q1: Serverless GPU 适合什么场景？**

A: 突发流量、A/B 实验、开发测试、多模型轮换。不适合 24×7 稳态（贵 2-3x）和长任务（超时）。

**Q2: 冷启动怎么优化？**

A: 1) 模型权重本地缓存（Volume）；2) 快照恢复（省 Python + 依赖 import）；3) min_containers=1 保持温热；4) 定时预热。

**Q3: Serverless GPU 计费模型？**

A: 按秒（Modal $5/h）、按请求（Replicate）、按 token（OpenAI）。按秒适合长任务，按 token 适合短任务。

**Q4: 混合部署如何选 base/peak 比例？**

A: 分析流量：稳态用预留（base），突发用 Serverless（peak）。base 覆盖均值，peak 覆盖峰值-均值。KEDA 监控排队触发 Serverless。

**Q5: Scale-to-Zero 的代价？**

A: 首请求冷启动 10s-2min，SLA 难兑现。低流量场景省钱，但用户体验差。生产建议 min_containers=1。

---

## 十二、综合面试题

### 题 1（中级）：设计 LLM 推理的混合部署

**答题要点**：
1. 流量分析：日均 1M 请求，峰值 10x
2. 资源：
   - base K8s: 4×H100（预留，覆盖均值）
   - peak Serverless: Modal（按需，覆盖突发）
3. KEDA 监控排队 > 20 触发 Serverless
4. 模型权重 Volume 缓存
5. 成本：base $1.8×4×24×30=$5.2K + peak $5×4×8×30=$4.8K = $10K/月
6. 对比全预留：4×10=$21.6K/月（峰值 10 卡）
7. 节省 54%

### 题 2（高级）：Serverless GPU 平台选型

**答题要点**：
1. Modal：SDK 友好，开发体验好，适合创业公司
2. Replicate：模型市场，API 调用简单
3. Baseten：生产稳定，适合企业
4. AWS Inferentia：AWS 生态，自研芯片便宜
5. 阿里 PAI-EAS：国内，托管
6. 选型：开发用 Modal，生产用 Baseten/PAI-EAS，AWS 生态用 Inferentia

---

## 十三、故障复盘

### 13.1 案例 1：冷启动失控

**背景**：2024 年某公司 Scale-to-Zero，早晨流量来时冷启动 65s。

**修复**：min_containers=1 + 定时预热。

### 13.2 案例 2：稳态用 Serverless 贵 3x

**背景**：2025 年某公司日均 10K 请求用 Modal，月成本 $20K。

**修复**：迁 K8s 预留，月成本 $7K。

### 13.3 案例 3：长任务超时

**背景**：2024 年某公司 30 分钟推理被 Modal 600s 超时切断。

**修复**：拆分为 5 分钟子任务，或迁预留。

---

## 十四、参考与延伸

### 14.1 平台

- Modal — https://modal.com/
- Replicate — https://replicate.com/
- Baseten — https://baseten.co/
- 阿里 PAI-EAS — https://www.alibabacloud.com/zh/product/pai-eas

### 14.2 跨模块链接

- [01-部署演进与前沿范式](./01-部署演进与前沿范式.md) —— Serverless 范式
- [11-冷启动优化与Scale-to-Zero](./11-冷启动优化与Scale-to-Zero.md) —— 冷启动深入
- [02-前沿部署的核心矛盾与权衡](./02-前沿部署的核心矛盾与权衡.md) —— 成本 vs 弹性

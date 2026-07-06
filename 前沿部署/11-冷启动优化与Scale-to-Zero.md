# 11 - 冷启动优化与 Scale-to-Zero

> 冷启动是 Serverless 的核心矛盾。LLM 推理冷启动尤其严重（模型权重 GB 级）。本章剖析冷启动的根因、量化分析、优化技术（快照、预热、保持温热、按需加载），以及 Scale-to-Zero 的工程实践。

---

## 一、思维导图

```
                  冷启动优化
                      │
        ┌─────────────┼─────────────┐
        ▼             ▼             ▼
   ┌─────────┐  ┌───────────┐  ┌───────────┐
   │ 根因    │  │ 量化      │  │ 优化      │
   │ 容器    │  │ 分解      │  │ 快照      │
   │ Python  │  │ 10s-2min  │  │ 预热      │
   │ 模型    │  │           │  │ 保持温热  │
   │ CUDA    │  │           │  │ 按需加载  │
   └─────────┘  └───────────┘  └───────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **冷启动根因**：容器、Python、模型、CUDA 各阶段耗时
- **量化分析**：不同模型的冷启动时间
- **优化技术**：快照、预热、保持温热、按需加载
- **Scale-to-Zero 实践**：KEDA/Knative 配置

### 2.2 不解决什么

- 不深入 Firecracker 内部（17 章）
- 不覆盖 Serverless 平台对比（10 章）

---

## 三、直觉解释

### 3.1 冷启动的五个阶段

```
1. 容器启动 (100ms-1s)
   - Firecracker 125ms
   - 容器 runtime 50ms
   - 镜像拉取 (缓存命中 < 1s, 否则分钟级)

2. Python 解释器 (500ms)
   - Python 启动
   - site-packages 加载

3. 依赖 import (2-5s)
   - PyTorch: 1-2s
   - Transformers: 0.5-1s
   - vLLM: 1-2s

4. CUDA 初始化 (500ms)
   - CUDA context 创建
   - GPU 内存映射

5. 模型权重加载 (10s-2min)
   - 从磁盘/网络读
   - 反序列化
   - 拷贝到 GPU

   Llama-7B (14GB): ~10s (本地 SSD)
   Llama-70B FP8 (70GB): ~60s
   Llama-70B FP16 (140GB): ~120s

6. KV Cache 预分配 (1-2s)
   - 分配显存
   - 初始化

总冷启动:
  - 7B: ~15s
  - 70B FP8: ~65s
  - 70B FP16: ~125s
```

### 3.2 冷启动的影响

```
用户感知:
  - 首请求延迟 10s-2min
  - 流式响应首 token 慢
  - 体验差

SLA 影响:
  - P99 TTFT 飙升
  - SLA 难兑现

成本:
  - 冷启动期间也计费 (但无产出)
  - 频繁冷启动浪费
```

### 3.3 Scale-to-Zero 的权衡

```
Scale-to-Zero 收益:
  - 无流量时 0 成本
  - 适合突发流量

代价:
  - 首请求冷启动
  - SLA 风险

权衡:
  - 完全 Scale-to-Zero: 省钱, 但首请求慢
  - 保持温热 (min=1): 不省钱, 但 SLA 好
  - 定时预热: 折中, 流量预测准确时最优
```

---

## 四、核心概念与架构

### 4.1 快照恢复

```
传统启动:
  1. 容器启动 (100ms)
  2. Python 启动 (500ms)
  3. import torch (2s)
  4. import vllm (2s)
  5. CUDA init (500ms)
  6. 模型加载 (60s)
  总: 65s

快照恢复 (Firecracker snapshot):
  1. 恢复快照 (含 1-5 步状态) (200ms)
  2. 模型加载 (60s)  ← 仍需
  总: 60.2s

进一步: 模型加载也快照
  1. 恢复快照 (含模型在 GPU 显存) (500ms)
  总: 500ms
```

**实现**：
- Firecracker snapshot：保存 VM 完整状态（CPU 寄存器、内存）
- 模型在显存：恢复后立即可用
- 挑战：GPU 显存快照技术不成熟（CUDA 状态复杂）

### 4.2 模型权重本地缓存

```
无缓存:
  从 HuggingFace 下载 140GB
  1Gbps 网络: 1120s (18 分钟)

有缓存 (本地 SSD):
  读 140GB
  NVMe 5GB/s: 28s

有缓存 (内存):
  读 140GB
  DDR5 100GB/s: 1.4s

实践:
  - Modal Volume (持久化)
  - K8s PVC (持久化卷)
  - 节点本地缓存 (DaemonSet 预拉)
```

### 4.3 预热策略

```python
# 定时预热
@modal.Cron("0 8 * * *")  # 每天 8 点
def warmup():
    serve.remote("warmup")

# 流量预测预热
def predict_traffic():
    # 基于历史流量预测
    # 高峰前 10 分钟预热
    pass

# 排队触发预热
def on_queue_depth(queue_depth):
    if queue_depth > 0 and warm_instances == 0:
        warmup()  # 立即预热
```

### 4.4 保持温热

```python
# Modal
@app.function(
    min_containers=1,  # 至少 1 个常驻
    container_idle_timeout=600,  # 10 分钟空闲后释放
)

# K8s + KEDA
spec:
  minReplicaCount: 1  # 至少 1 个
  idleReplicaCount: 0  # 空闲可降到 0
  cooldownPeriod: 300  # 5 分钟冷却
```

### 4.5 按需加载（Stargz/Nydus）

```
传统镜像: 全部拉取后启动
  10GB 镜像: 10s 拉取 + 启动

按需加载 (Lazy Pull):
  启动时只拉取必需文件
  其他按需拉取

Stargz / eStargz:
  - 镜像分层 + 索引
  - 启动时拉取入口文件
  - 推理时拉取模型权重
  - 启动 30s → 3s

Nydus (阿里):
  - 类似 Stargz
  - Dragonfly P2P 分发
  - 万节点并发拉取
```

---

## 五、操作流程与配置

### 5.1 KEDA Scale-to-Zero

```yaml
apiVersion: keda.sh/v1alpha1
kind: ScaledObject
metadata:
  name: vllm-serverless
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: vllm
  minReplicaCount: 0  # Scale-to-Zero
  maxReplicaCount: 10
  cooldownPeriod: 300  # 5 分钟冷却
  pollingInterval: 30
  triggers:
  - type: prometheus
    metadata:
      serverAddress: http://prometheus:9090
      metricName: vllm_pending_requests
      threshold: "5"
      query: |
        sum(vllm:num_requests_waiting)
```

### 5.2 Knative Service

```yaml
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: vllm
spec:
  template:
    metadata:
      annotations:
        autoscaling.knative.dev/min-scale: "0"
        autoscaling.knative.dev/max-scale: "10"
        autoscaling.knative.dev/target: "1"
        autoscaling.knative.dev/scale-to-zero-pod-retention-period: "300s"
    spec:
      timeoutSeconds: 600
      containers:
      - image: vllm/vllm-openai:v0.6.0
        resources:
          limits: {nvidia.com/gpu: 1}
```

### 5.3 快照恢复配置

```python
# Firecracker snapshot (概念)
@app.function(
    image=image,
    gpu="A100",
    snapshot=True,  # 启用快照
    snapshot_id="llama-70b-warm",  # 快照 ID
)
def serve():
    # 启动时恢复快照
    # Python + 依赖 + 模型已在显存
    pass

# 创建快照 (预热后)
@app.function(gpu="A100")
def create_snapshot():
    llm = LLM(model="llama-70b")
    # 标记此状态为快照
    modal.Snapshot.create("llama-70b-warm")
```

---

## 六、底层原理

### 6.1 冷启动各阶段优化

| 阶段 | 耗时 | 优化技术 | 优化后 |
|------|------|----------|--------|
| 容器启动 | 100ms-1s | Firecracker | 125ms |
| 镜像拉取 | 分钟级 | 按需加载 (Stargz) | 3s |
| Python 启动 | 500ms | 快照 | 0 |
| 依赖 import | 2-5s | 快照 | 0 |
| CUDA init | 500ms | 快照 | 0 |
| 模型加载 | 10s-2min | 本地缓存 + 快照 | 0.5-2s |
| KV Cache 预分配 | 1-2s | 预分配 + 快照 | 0 |

**理论极限**：快照恢复 200ms-1s。

### 6.2 快照恢复原理

```
Firecracker Snapshot:
  1. 保存 VM 状态 (CPU 寄存器, 内存)
  2. 恢复时:
     a. 创建新 VM
     b. 加载内存镜像
     c. 恢复 CPU 状态
     d. VM 继续运行 (从快照点)

挑战:
  - GPU 显存快照 (CUDA 状态复杂)
  - 内存镜像大 (GB 级)
  - 快照存储成本

实践:
  - Firecracker v1.5+ 支持基本 GPU 快照
  - Modal 等平台部分支持
  - 完整 GPU 快照仍研究课题
```

### 6.3 流量预测

```python
# 流量预测预热
import numpy as np
from sklearn.linear_model import LinearRegression

class TrafficPredictor:
    def __init__(self):
        self.model = LinearRegression()

    def train(self, history):
        # history: [(hour, day_of_week, traffic), ...]
        X = np.array([[h, d] for h, d, _ in history])
        y = np.array([t for _, _, t in history])
        self.model.fit(X, y)

    def predict(self, hour, day_of_week):
        return self.model.predict([[hour, day_of_week]])[0]

    def should_warmup(self, current_traffic, predicted_traffic):
        # 预测流量上升时预热
        return predicted_traffic > current_traffic * 1.5

# 实际部署
predictor = TrafficPredictor()
predictor.train(history_data)

# 每 5 分钟检查
@modal.Cron("*/5 * * * *")
def check_warmup():
    current = get_current_traffic()
    predicted = predictor.predict(get_hour()+1, get_dow())
    if predictor.should_warmup(current, predicted):
        serve.remote("warmup")
```

---

## 七、代码与配置示例

### 7.1 完整 Serverless + 冷启动优化

```python
# optimized_serverless.py
import modal

app = modal.App("llama-70b-optimized")

image = modal.Image.debian_slim().pip_install(
    "vllm==0.6.0", "transformers", "torch"
)

# 模型权重 Volume (避免每次下载)
vol = modal.Volume.from_name("llama-70b-weights", create_if_missing=True)

@app.function(
    image=image,
    gpu="A100-80GB",
    min_containers=0,  # Scale-to-Zero
    max_containers=10,
    container_idle_timeout=300,
    volumes={"/models": vol},
    timeout=600,
)
@modal.asgi_app()
def serve():
    from vllm import LLM
    from fastapi import FastAPI

    # 模型从 Volume 加载 (本地, 快)
    llm = LLM(
        model="/models/llama-70b",
        tensor_parallel_size=1,
        gpu_memory_utilization=0.9,
        enable_prefix_caching=True,
        kv_cache_dtype="fp8",
    )

    web_app = FastAPI()

    @web_app.post("/v1/chat/completions")
    async def chat(req: dict):
        # 流式
        if req.get("stream"):
            async def stream():
                for chunk in llm.stream_generate(req["messages"][0]["content"]):
                    yield f"data: {json.dumps({'choices': [{'delta': {'content': chunk}}]})}\n\n"
                yield "data: [DONE]\n\n"
            return StreamingResponse(stream(), media_type="text/event-stream")
        # 非流式
        output = llm.generate(req["messages"][0]["content"])
        return {"choices": [{"message": {"content": output}}]}

    return web_app

# 定时预热 (早晨 8 点)
@modal.Cron("0 8 * * *")
def warmup():
    serve.remote("warmup")

# 健康检查 (5 分钟一次, 保持温热)
@modal.Cron("*/5 * * * *")
def health_check():
    serve.remote("health")
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：模型权重每次重新下载

**症状**：冷启动 18 分钟（下载 140GB）。

**修复**：Volume 本地缓存。

### 8.2 陷阱 2：Scale-to-Zero 频繁触发

**症状**：低流量时段反复冷启动。

**修复**：min_containers=1 或 idle_timeout 调长。

### 8.3 陷阱 3：预热不准

**症状**：预热早了浪费，预热晚了冷启动。

**修复**：流量预测 + 排队触发双重保险。

### 8.4 陷阱 4：镜像大

**症状**：镜像 10GB，拉取慢。

**修复**：多阶段构建 + Stargz 按需加载。

### 8.5 调优 Checklist

- [ ] 模型权重 Volume 缓存
- [ ] 快照恢复（如平台支持）
- [ ] 流量预测预热
- [ ] min_containers 按业务调
- [ ] idle_timeout 适配
- [ ] Stargz 按需加载
- [ ] 监控冷启动率

---

## 九、工业案例与基准数据

### 9.1 冷启动优化效果

| 优化 | Llama-7B | Llama-70B FP8 |
|------|----------|---------------|
| 无优化 | 15s + 下载 | 65s + 下载 |
| + Volume 缓存 | 15s | 65s |
| + 快照 | 2s | 10s |
| + 预热 | 0s（温热） | 0s（温热） |

### 9.2 案例 1：Modal 冷启动优化

**背景**：Modal 用户冷启动 65s。

**优化**：
- Volume 缓存模型
- 快照恢复
- min_containers=1

**效果**：冷启动 65s → 0s（温热）。

### 9.3 案例 2：阿里 ECI Stargz

**背景**：阿里 ECI 容器镜像大，拉取慢。

**优化**：Stargz 按需加载 + Dragonfly P2P 分发。

**效果**：
- 镜像拉取 30s → 3s
- 万节点并发拉取带宽降 80%

### 9.4 案例 3：AWS Lambda SnapStart

**背景**：Java Lambda 冷启动慢（JVM 启动）。

**优化**：SnapStart（基于 Firecracker snapshot）。

**效果**：冷启动 5s → 200ms。

---

## 十、与其他方案的关系

### 10.1 冷启动优化技术对比

| 技术 | 效果 | 复杂度 | 适用 |
|------|------|--------|------|
| 模型缓存 | -下载时间 | 低 | 必用 |
| 快照恢复 | -Python + 依赖 | 中 | 平台支持时 |
| 保持温热 | 0 冷启动 | 低 | 高 SLA |
| 定时预热 | 0 冷启动（预测准） | 中 | 流量规律 |
| Stargz | -镜像拉取 | 中 | 大镜像 |
| 排队触发 | -冷启动感知 | 低 | 兜底 |

---

## 十一、面试速答

**Q1: LLM 推理冷启动为什么这么慢？**

A: 主要慢在模型权重加载（140GB Llama-70B）。其他：容器、Python、依赖 import、CUDA 初始化，共 5-10s。模型加载占大头。

**Q2: 如何优化冷启动？**

A: 1) 模型权重 Volume 本地缓存（省下载）；2) 快照恢复（省 Python + 依赖）；3) 保持温热（min_containers=1）；4) 定时预热；5) Stargz 按需加载镜像。

**Q3: Scale-to-Zero 的代价？**

A: 首请求冷启动 10s-2min，SLA 难兑现。生产建议 min_containers=1 或定时预热。

**Q4: KEDA 和 Knative 的 Scale-to-Zero 区别？**

A: KEDA 是 K8s HPA 扩展，基于 Prometheus 等指标。Knative 是完整 Serverless 框架，内置 Scale-to-Zero + 流量管理。KEDA 更轻量，Knative 功能全。

**Q5: 流量预测预热怎么做？**

A: 1) 历史流量数据训练回归模型；2) 预测下时段流量；3) 流量上升时提前预热；4) 兜底：排队触发即时预热。

---

## 十二、综合面试题

### 题 1（中级）：优化 Llama-70B Serverless 冷启动

**答题要点**：
1. **现状**：冷启动 65s（容器 1s + Python 5s + 模型 60s）
2. **优化**：
   - Volume 缓存模型（省下载，已假设有）
   - 快照恢复（省 Python + 依赖 5s）→ 60s
   - 保持温热（min=1）→ 0s
   - 定时预热（早晨 8 点）→ 0s
3. **效果**：65s → 0s（温热）/ 10s（快照）

### 题 2（高级）：设计 Scale-to-Zero 策略

**答题要点**：
1. **流量分析**：
   - 工作日 9-18 点高流量
   - 夜间低流量
   - 周末中等
2. **策略**：
   - 工作日 9-18 点：min=2（温热）
   - 工作日夜间：min=0（Scale-to-Zero）+ 定时预热（早 8 点）
   - 周末：min=1
3. **监控**：
   - 冷启动率 < 5%
   - SLA P99 < 500ms
4. **成本**：
   - 全温热：$5×24×30=$3600/月
   - 优化后：$5×10×22 + $5×24×8 = $1100+$960=$2060/月
   - 节省 43%

---

## 十三、故障复盘

### 13.1 案例 1：模型权重每次下载

**背景**：2024 年某公司冷启动 18 分钟。

**修复**：Volume 缓存。

### 13.2 案例 2：Scale-to-Zero 频繁

**背景**：2025 年某公司夜间反复冷启动。

**修复**：min_containers=1。

### 13.3 案例 3：预热不准

**背景**：2024 年某公司预热早了，浪费成本。

**修复**：流量预测 + 排队触发双重保险。

---

## 十四、参考与延伸

### 14.1 工具

- KEDA — https://keda.sh/
- Knative — https://knative.dev/
- Stargz — https://github.com/containerd/stargz-snapshotter
- Nydus — https://nydus.dev/
- Firecracker Snapshot — https://github.com/firecracker-microvm/firecracker/blob/main/docs/snapshotting.md

### 14.2 跨模块链接

- [10-Serverless-GPU与弹性推理](./10-Serverless-GPU与弹性推理.md) —— Serverless 平台
- [17-微虚拟机与沙箱运行时](./17-微虚拟机与沙箱运行时.md) —— Firecracker
- [02-前沿部署的核心矛盾与权衡](./02-前沿部署的核心矛盾与权衡.md) —— 成本 vs 弹性

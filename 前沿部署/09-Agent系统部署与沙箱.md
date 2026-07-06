# 09 - Agent 系统部署与沙箱

> Agent 是 LLM 应用的核心形态。Agent 部署与传统推理服务有本质差异：长任务、工具调用、多 Agent 协作、代码执行沙箱。本章梳理 Agent 部署架构、沙箱运行时、长任务调度、生产实践。

---

## 一、思维导图

```
                  Agent 系统部署
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   ┌─────────┐  ┌───────────┐  ┌───────────┐
   │ 架构    │  │ 沙箱      │  │ 调度      │
   │ 单 Agent│  │ Firecracker│  │ 长任务    │
   │ 多 Agent│  │ gVisor    │  │ 状态持久化│
   │ 拓扑    │  │ E2B/Modal │  │ 故障恢复  │
   └─────────┘  └───────────┘  └───────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **Agent 部署架构**：单 Agent / 多 Agent 拓扑
- **代码执行沙箱**：Firecracker / gVisor / E2B / Modal
- **长任务调度**：小时级任务、可恢复
- **工具调用编排**：并行、嵌套、超时
- **故障恢复**：Agent 中断后恢复

### 2.2 不解决什么

- 不深入 Agent 算法（参考 Agent开发 模块）
- 不覆盖 prompt engineering
- 不讨论 RAG 架构

---

## 三、直觉解释

### 3.1 Agent 与 LLM 推理的差异

```
LLM 推理:
  请求 → 推理 → 响应
  耗时: 秒级
  无状态

Agent:
  请求 → 规划 → 工具调用 (多次) → 推理 → 响应
  耗时: 分钟到小时
  有状态 (中间结果, 工具调用历史)

例: 编程 Agent
  1. 用户: "写一个排序算法并测试"
  2. Agent: 调用 write_file 工具
  3. Agent: 调用 exec 工具 (运行代码)
  4. Agent: 看到错误, 修改代码
  5. Agent: 再调用 exec
  6. ... (循环)
  7. 最终返回

  耗时: 10 分钟
  状态: 中间代码、执行结果、对话历史
```

### 3.2 Agent 部署的工程挑战

1. **长任务**：单请求 10 分钟到 1 小时，连接保持难
2. **工具调用**：可能并行、嵌套，需要编排
3. **代码执行**：用户代码不可信，需要沙箱
4. **状态管理**：中断后可恢复
5. **多 Agent 协作**：拓扑管理、消息传递
6. **成本控制**：长任务 token 消耗大

### 3.3 沙箱运行时选型

```
代码执行沙箱选型:

Firecracker (AWS):
  - MicroVM, 125ms 启动
  - 隔离强 (硬件虚拟化)
  - Lambda/Fargate 底座
  - 适合: 多租户, Serverless

gVisor (Google):
  - 用户态内核, 拦截 syscall
  - 隔离强, 性能损耗 10-30%
  - 适合: K8s Pod security

Kata Containers:
  - 基于 QEMU 的轻量 VM
  - 兼容 K8s
  - 适合: 企业 K8s

E2B (开源):
  - 专为 AI Agent 设计
  - Firecracker 底层
  - Python SDK 友好
  - 适合: AI 创业公司

Modal:
  - Serverless 沙箱
  - 按秒计费
  - 适合: 突发

容器 + Seccomp:
  - 轻量, 但隔离弱
  - 适合: 内部可信环境
```

---

## 四、核心概念与架构

### 4.1 单 Agent 架构

```
[Client] → [Agent Server] → [LLM Inference]
                │
                ├──→ [Tool: code_exec] → [Sandbox (Firecracker)]
                ├──→ [Tool: web_search] → [Search API]
                ├──→ [Tool: file_read] → [Storage]
                └──→ [Tool: db_query] → [Database]

状态管理:
  - 会话历史 (Redis)
  - 工具调用记录 (DB)
  - 中间结果 (Storage)
```

### 4.2 多 Agent 拓扑

```
1. Supervisor/Worker:
   [Supervisor Agent]
       ├──→ [Worker Agent 1]
       ├──→ [Worker Agent 2]
       └──→ [Worker Agent 3]

2. Pipeline (顺序):
   [Planner] → [Coder] → [Tester] → [Reviewer]

3. Debate (辩论):
   [Agent A] ↔ [Agent B] ↔ [Judge]

4. Tree (树状):
            [Root]
           /  |  \
       [A1] [A2] [A3]
       /    |    \
     ...   ...   ...

5. Graph (图, 复杂):
   灵活连接, 任意 Agent 可通信
```

### 4.3 代码执行沙箱架构

```
Agent 调用 code_exec 工具:
  1. Agent Server 向 Sandbox Manager 请求沙箱
  2. Sandbox Manager 启动 Firecracker MicroVM (125ms)
  3. 代码 + 输入传入沙箱 (gRPC)
  4. 沙箱内执行 (限制 CPU/内存/网络/文件)
  5. 输出 + 状态返回
  6. 沙箱销毁 (或复用)

资源限制:
  - CPU: 1 vCPU
  - 内存: 512MB
  - 磁盘: 1GB (tmpfs)
  - 网络: 默认禁用, 白名单
  - 时间: 30s 超时
  - syscall: seccomp 限制
```

### 4.4 长任务调度

```python
# 长任务状态机
class AgentTask:
    states = ["pending", "planning", "executing", "tool_calling",
              "waiting_tool", "completed", "failed", "cancelled"]

    def __init__(self, task_id, prompt):
        self.task_id = task_id
        self.prompt = prompt
        self.state = "pending"
        self.history = []  # 对话历史
        self.tool_calls = []  # 工具调用记录
        self.checkpoint = None  # 断点恢复

    async def run(self):
        try:
            self.state = "planning"
            self.checkpoint = self.save_checkpoint()

            while not self.is_done():
                self.state = "executing"
                # 调用 LLM
                response = await self.llm.generate(self.history)

                if response.has_tool_call:
                    self.state = "tool_calling"
                    tool_result = await self.execute_tool(response.tool_call)
                    self.history.append(response)
                    self.history.append(tool_result)
                    self.checkpoint = self.save_checkpoint()
                else:
                    self.state = "completed"
                    return response.content

        except Exception as e:
            self.state = "failed"
            # 可恢复: 从 checkpoint 重启
            raise

    def save_checkpoint(self):
        # 保存到 Redis / DB
        return {
            "state": self.state,
            "history": self.history,
            "tool_calls": self.tool_calls,
        }
```

### 4.5 工具调用编排

```python
# 并行工具调用
class ToolOrchestrator:
    async def execute_parallel(self, tool_calls):
        # 多个工具并行执行
        tasks = [self.execute_tool(tc) for tc in tool_calls]
        results = await asyncio.gather(*tasks, return_exceptions=True)
        return results

    async def execute_with_timeout(self, tool_call, timeout=30):
        try:
            return await asyncio.wait_for(
                self.execute_tool(tool_call),
                timeout=timeout
            )
        except asyncio.TimeoutError:
            return {"error": "timeout"}

    async def execute_with_retry(self, tool_call, retries=3):
        for i in range(retries):
            try:
                return await self.execute_tool(tool_call)
            except Exception as e:
                if i == retries - 1:
                    return {"error": str(e)}
                await asyncio.sleep(2 ** i)  # 指数退避
```

---

## 五、操作流程与配置

### 5.1 Firecracker 沙箱部署

```python
# E2B 沙箱 (基于 Firecracker)
from e2b import Sandbox

# 启动沙箱
sandbox = Sandbox.create(
    template="python-3.11",
    cpu=1,
    memory=512,  # MB
    timeout=30,  # s
)

# 执行代码
result = sandbox.run_python("""
import sys
print(f"Python {sys.version}")
print("Hello from sandbox")
""")
print(result.stdout)

# 文件操作
sandbox.filesystem.write("/tmp/test.py", "print('hi')")
sandbox.run_python_file("/tmp/test.py")

# 销毁
sandbox.close()
```

### 5.2 Modal 沙箱部署

```python
import modal

sandbox_image = modal.Image.debian_slim().pip_install("numpy")

@app.function(image=sandbox_image, cpu=1, memory=512, timeout=30)
def execute_code(code: str):
    exec(code)
    return "done"

# 调用
result = execute_code.remote("print('hello')")
```

### 5.3 K8s + Kata Containers

```yaml
# Kata Containers Pod (强隔离)
apiVersion: v1
kind: Pod
metadata:
  name: agent-sandbox
  annotations:
    io.kubernetes.cri.container-type: "sandbox"
    io.katacontainers.config.runtime.enable_vhost_net: "true"
spec:
  runtimeClassName: kata  # 使用 Kata 运行时
  containers:
  - name: sandbox
    image: python:3.11-slim
    resources:
      limits: {cpu: 1, memory: 512Mi}
    securityContext:
      readOnlyRootFilesystem: true
      runAsNonRoot: true
      allowPrivilegeEscalation: false
      capabilities:
        drop: ["ALL"]
      seccompProfile:
        type: RuntimeDefault
```

### 5.4 Agent Server 部署

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: agent-server
spec:
  replicas: 10
  template:
    spec:
      containers:
      - name: agent
        image: myorg/agent-server:v1.0
        env:
        - name: LLM_ENDPOINT
          value: http://vllm:8000/v1
        - name: SANDBOX_PROVIDER
          value: e2b  # or modal / firecracker
        - name: E2B_API_KEY
          valueFrom:
            secretKeyRef: {name: e2b-secret, key: api-key}
        - name: REDIS_URL
          value: redis:6379
        - name: MAX_TASK_DURATION
          value: "3600"  # 1 小时
        resources:
          limits: {cpu: 2, memory: 4Gi}
        ports:
        - containerPort: 8080
```

---

## 六、底层原理

### 6.1 沙箱隔离机制

```
Firecracker:
  - Rust 实现
  - 基于 KVM (硬件虚拟化)
  - 5MB 镜像
  - 125ms 启动
  - 最小设备模拟 (virtio)
  - Lambda/Fargate 底座

gVisor:
  - Go 实现
  - 用户态内核 (拦截 syscall)
  - 兼容 K8s
  - 性能损耗 10-30% (syscall 密集型更慢)
  - Google Cloud Run 底座

Kata Containers:
  - 基于 QEMU/KVM
  - 兼容 K8s (runtimeClass)
  - 启动秒级
  - 性能损耗 5-10%

对比:
  隔离强度: Firecracker ≈ Kata > gVisor > 容器
  启动速度: Firecracker (125ms) > gVisor (秒) > Kata (秒)
  性能损耗: 容器 < Firecracker < Kata < gVisor
```

### 6.2 长任务状态管理

```
Agent 任务状态:
  - 会话历史 (Redis, 1小时 TTL)
  - 工具调用记录 (PostgreSQL, 永久)
  - 中间文件 (S3/OSS)
  - Checkpoint (Redis, 频繁更新)

故障恢复:
  1. Agent Server 宕机
  2. 任务标记 failed
  3. 用户重试: 从最新 checkpoint 恢复
  4. LLM 重新生成最近一步 (可能略有不同)

成本:
  - Checkpoint 频率: 每工具调用后
  - 存储: 1MB/任务 (历史 + 状态)
  - Redis: 10K 任务并发, 10GB
```

### 6.3 多 Agent 通信

```
消息传递模式:

1. 共享内存 (Redis pub/sub):
   Agent A → Redis channel → Agent B
   简单, 但耦合

2. 消息队列 (Kafka/RabbitMQ):
   Agent A → Queue → Agent B
   解耦, 持久化

3. 直接 RPC (gRPC):
   Agent A → gRPC → Agent B
   低延迟, 但耦合

4. 黑板模式 (Blackboard):
   所有 Agent 读写共享黑板
   灵活, 但并发控制复杂
```

---

## 七、代码与配置示例

### 7.1 完整 Agent Server

```python
# agent_server.py
from fastapi import FastAPI, BackgroundTasks
from pydantic import BaseModel
import openai
import asyncio
from e2b import Sandbox
import redis
import json

app = FastAPI()
client = openai.OpenAI(base_url="http://vllm:8000/v1", api_key="dummy")
redis_client = redis.Redis(host="redis", port=6379)

class AgentRequest(BaseModel):
    task_id: str
    prompt: str
    max_steps: int = 20

class Agent:
    def __init__(self, task_id, prompt, max_steps=20):
        self.task_id = task_id
        self.prompt = prompt
        self.max_steps = max_steps
        self.history = [{"role": "user", "content": prompt}]
        self.sandbox = None

    async def run(self):
        try:
            for step in range(self.max_steps):
                # Checkpoint
                self.save_checkpoint()

                # LLM 推理
                response = client.chat.completions.create(
                    model="llama-3.1-70b",
                    messages=self.history,
                    tools=self.get_tools(),
                    tool_choice="auto",
                    stream=False,
                )
                msg = response.choices[0].message
                self.history.append(msg.model_dump())

                if msg.tool_calls:
                    for tc in msg.tool_calls:
                        result = await self.execute_tool(tc)
                        self.history.append({
                            "role": "tool",
                            "tool_call_id": tc.id,
                            "content": json.dumps(result),
                        })
                else:
                    # 完成
                    self.mark_completed()
                    return msg.content

            self.mark_failed("max steps reached")
            return "Task incomplete"

        except Exception as e:
            self.mark_failed(str(e))
            raise

    async def execute_tool(self, tool_call):
        name = tool_call.function.name
        args = json.loads(tool_call.function.arguments)

        if name == "code_exec":
            return await self.code_exec(args["code"])
        elif name == "web_search":
            return await self.web_search(args["query"])
        # ...

    async def code_exec(self, code):
        if not self.sandbox:
            self.sandbox = Sandbox.create(template="python-3.11", timeout=30)
        try:
            result = self.sandbox.run_python(code)
            return {"stdout": result.stdout, "stderr": result.stderr}
        except Exception as e:
            return {"error": str(e)}

    def save_checkpoint(self):
        redis_client.set(
            f"agent:{self.task_id}:checkpoint",
            json.dumps({"history": self.history, "step": len(self.history)})
        )

@app.post("/agent/run")
async def run_agent(req: AgentRequest, bg: BackgroundTasks):
    agent = Agent(req.task_id, req.prompt, req.max_steps)
    bg.add_task(agent.run)
    return {"task_id": req.task_id, "status": "started"}

@app.get("/agent/{task_id}/status")
async def get_status(task_id: str):
    state = redis_client.get(f"agent:{task_id}:state")
    return {"task_id": task_id, "state": state}
```

### 7.2 多 Agent 编排（LangGraph）

```python
# multi_agent.py
from langgraph.graph import StateGraph, END
from typing import TypedDict, Annotated
import operator

class AgentState(TypedDict):
    messages: Annotated[list, operator.add]
    next: str

def supervisor(state):
    # 决定下一个 Agent
    response = client.chat.completions.create(
        model="llama-3.1-70b",
        messages=state["messages"] + [{
            "role": "system",
            "content": "Decide next agent: coder, tester, or END"
        }],
    )
    return {"next": response.choices[0].message.content}

def coder(state):
    response = client.chat.completions.create(
        model="llama-3.1-70b",
        messages=state["messages"] + [{"role": "system", "content": "Write code"}],
    )
    return {"messages": [response.choices[0].message], "next": "tester"}

def tester(state):
    # 测试代码
    return {"messages": [...], "next": "supervisor"}

# 构建图
workflow = StateGraph(AgentState)
workflow.add_node("supervisor", supervisor)
workflow.add_node("coder", coder)
workflow.add_node("tester", tester)
workflow.set_entry_point("supervisor")
workflow.add_conditional_edges("supervisor", lambda s: s["next"])
workflow.add_edge("coder", "tester")
workflow.add_edge("tester", "supervisor")
workflow.add_edge(END, END)

app = workflow.compile()
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：沙箱启动慢拖累 Agent

**症状**：每次 code_exec 都启动新沙箱，耗时 1s+。

**根因**：Firecracker 启动 125ms，但镜像拉取 + 初始化更长。

**修复**：沙箱池化（预热 N 个沙箱），复用。

### 8.2 陷阱 2：长任务超时

**症状**：30 分钟任务被 Nginx 60s 切断。

**根因**：HTTP 长连接超时。

**修复**：
- 用 WebSocket / SSE 流式
- 异步任务 + 轮询状态
- Nginx 超时调到 3600s

### 8.3 陷阱 3：工具调用死循环

**症状**：Agent 反复调用同一工具。

**根因**：LLM 陷入循环，没有终止条件。

**修复**：
- max_steps 限制
- 检测重复工具调用（连续 3 次相同则终止）
- 记忆窗口（限制历史长度）

### 8.4 陷阱 4：沙箱资源耗尽

**症状**：用户代码 `while True` 占满 CPU。

**根因**：没限制资源。

**修复**：
- CPU/内存/时间限制
- seccomp 限制 syscall
- 网络白名单

### 8.5 陷阱 5：多 Agent 死锁

**症状**：Agent 互相等待，无限阻塞。

**根因**：循环依赖。

**修复**：
- 全局超时
- 环检测
- 拓扑校验

### 8.6 调优 Checklist

- [ ] 沙箱池化
- [ ] 长任务异步 + 轮询
- [ ] max_steps 限制
- [ ] 工具调用超时
- [ ] 沙箱资源限制
- [ ] 多 Agent 环检测
- [ ] Checkpoint 频繁保存
- [ ] 监控任务耗时分布

---

## 九、工业案例与基准数据

### 9.1 沙箱启动耗时对比

| 沙箱 | 启动时间 | 隔离强度 | 适用 |
|------|----------|----------|------|
| Firecracker | 125ms | 强 | Serverless, 多租户 |
| gVisor | 500ms | 强 | K8s |
| Kata | 1s | 强 | 企业 K8s |
| 容器 + seccomp | 50ms | 弱 | 内部可信 |

### 9.2 案例 1：OpenAI Codex 沙箱

**背景**：Codex 代码执行。

**方案**（推测）：
- Firecracker MicroVM
- 每会话独立沙箱
- 资源限制 1 vCPU / 512MB

**效果**：单沙箱 125ms 启动，并发 10K。

### 9.3 案例 2：Anthropic Claude Code

**背景**：Claude 代码 Agent。

**方案**：
- 容器 + seccomp
- 沙箱池化
- 长任务 Checkpoint

**效果**：长任务 1 小时，可恢复。

### 9.4 案例 3：Cursor Agent

**背景**：Cursor 编程助手。

**方案**：
- 沙箱本地执行（用户机器）
- 不上云（隐私）
- 流式 SSE

**效果**：低延迟（无网络），隐私好。

### 9.5 案例 4：Devin 多 Agent

**背景**：Devin 自主编程 Agent。

**方案**：
- Supervisor + Worker 拓扑
- 长任务（小时级）
- Checkpoint 恢复

**效果**：完成 SWE-bench 13.86%。

---

## 十、与其他方案的关系

### 10.1 Agent vs LLM 推理

| 维度 | LLM 推理 | Agent |
|------|----------|-------|
| 耗时 | 秒 | 分钟-小时 |
| 状态 | 无 | 有（Checkpoint） |
| 工具 | 无 | 多工具编排 |
| 沙箱 | 不需要 | 代码执行必需 |
| 连接 | 短 | 长连接 |
| 成本 | 低 | 高（多次 LLM 调用） |

---

## 十一、面试速答

**Q1: Agent 部署与传统推理的差异？**

A: Agent 是长任务（分钟-小时），有状态（Checkpoint），需要工具调用编排，代码执行需要沙箱。传统推理是秒级无状态。

**Q2: 沙箱选型？**

A: 多租户用 Firecracker（强隔离，125ms 启动）。K8s 用 gVisor/Kata。内部可信用容器+seccomp。AI 创业用 E2B（Firecracker 底层，SDK 友好）。

**Q3: 长任务怎么处理连接？**

A: 异步任务 + 轮询状态。HTTP 长连接易超时。用 WebSocket/SSE 流式，或任务 ID + 状态查询 API。

**Q4: 工具调用死循环怎么办？**

A: max_steps 限制。检测重复工具调用（连续 3 次相同终止）。记忆窗口限制历史。

**Q5: 多 Agent 死锁？**

A: 全局超时。环检测（拓扑校验）。Agent 间消息超时。

**Q6: Agent 中断如何恢复？**

A: Checkpoint。每工具调用后保存状态（history + 工具调用记录）。重试时从 Checkpoint 恢复，LLM 重新生成最近一步。

**Q7: 沙箱资源限制？**

A: CPU/内存/时间/seccomp/网络白名单。用户代码不可信，必须限制。Firecracker + seccomp 最安全。

**Q8: Agent 成本控制？**

A: 1) 用小模型做规划，大模型做关键推理；2) prefix cache 复用 system prompt；3) max_steps 限制；4) 早期终止（检测无进展）。

---

## 十二、综合面试题

### 题 1（中级）：设计编程 Agent 部署

**答题要点**：
1. 架构：Agent Server + LLM + Sandbox
2. 沙箱：E2B (Firecracker)，池化预热
3. 长任务：异步 + 轮询，max_steps=30
4. 工具：code_exec, file_read, web_search
5. 状态：Redis Checkpoint
6. 监控：任务耗时、工具调用数、失败率

### 题 2（高级）：多 Agent 系统设计

**答题要点**：
1. 拓扑：Supervisor + Worker（Planner, Coder, Tester, Reviewer）
2. 通信：Redis pub/sub
3. 状态：共享黑板 + 各 Agent 私有状态
4. 故障：全局超时 + Checkpoint
5. 死锁检测：拓扑无环 + Agent 间超时
6. 成本：小模型规划 + 大模型执行

### 题 3（高级）：沙箱安全设计

**答题要点**：
1. 隔离：Firecracker MicroVM（硬件虚拟化）
2. 资源：CPU 1 vCPU, 内存 512MB, 时间 30s
3. 文件：tmpfs 只读根 + 临时目录
4. 网络：默认禁用，白名单
5. syscall：seccomp 限制
6. capability：drop ALL
7. 用户：非 root
8. 监控：异常 syscall 检测（Falco）

---

## 十三、故障复盘

### 13.1 案例 1：沙箱启动慢

**背景**：2024 年某公司每次 code_exec 启动新沙箱，1s+。

**修复**：沙箱池化，预热 10 个，复用。

**效果**：启动 1s → 50ms。

### 13.2 案例 2：长任务超时

**背景**：2025 年某公司 30 分钟任务被切断。

**修复**：异步任务 + 轮询 + WebSocket。

### 13.3 案例 3：工具调用死循环

**背景**：2024 年某公司 Agent 反复调用 web_search。

**修复**：max_steps + 重复检测。

### 13.4 案例 4：沙箱资源耗尽

**背景**：2025 年某公司用户代码 `while True` 占满 CPU。

**修复**：CPU/时间限制 + seccomp。

### 13.5 案例 5：多 Agent 死锁

**背景**：2025 年某公司 Agent A 等 B，B 等 A。

**修复**：全局超时 + 环检测。

---

## 十四、参考与延伸

### 14.1 工具与平台

- E2B — https://e2b.dev/
- Modal Sandbox — https://modal.com/
- Firecracker — https://github.com/firecracker-microvm/firecracker
- gVisor — https://gvisor.dev/
- Kata Containers — https://katacontainers.io/
- LangGraph — https://github.com/langchain-ai/langgraph

### 14.2 论文

- *SWE-agent: Agent-Computer Interfaces Enable Automated Software Engineering* — Yang et al., 2024
- *MetaGPT: Meta Programming for Multi-Agent Collaborative Framework* — Hong et al., 2023
- *AutoGen: Enabling Next-Gen LLM Applications via Multi-Agent Conversation* — Wu et al., 2023

### 14.3 跨模块链接

- [03-LLM推理服务化总览](./03-LLM推理服务化总览.md) —— LLM 推理基础
- [08-多模态与流式生成部署](./08-多模态与流式生成部署.md) —— 流式协议
- [17-微虚拟机与沙箱运行时](./17-微虚拟机与沙箱运行时.md) —— 沙箱深入
- [Agent开发/01-Agent基础](../Agent开发/01-Agent基础.md) —— Agent 算法侧
- [云计算安全/12-容器安全](../云计算安全/12-容器安全.md) —— 沙箱安全

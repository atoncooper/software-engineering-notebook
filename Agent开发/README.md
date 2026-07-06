# Agent 开发学习笔记

> 大模型从「对话工具」走向「自主智能体」的工程化笔记。
>
> 目标：以 **Agent Harness** 为核心脉络, 从 **范式基础** → **推理规划** → **工具执行** → **记忆上下文** → **Harness 工程化** → **协议与多 Agent** → **评估安全** → **工业实战** 八层递进, 构建可追溯、可复用的 Agent 开发知识体系。
>
> 工业视角贯穿全程：工业级 Agent 系统 (Cursor / Devin / Claude Code) 都是 **Harness 架构**, 由 Runtime / Tool Manager / Context Manager / Memory / Planner / Evaluator / Retry / Checkpoint / Tracing / Logger / Scheduler / Session / Event Bus 等模块组成。本笔记以 Harness 为主线, 拆解每个模块的工程实现。

---

## 一、模块定位

本模块不是「LangChain API 速查表」, 而是一套以 **Agent Harness** 为核心、面向 **范式 + 推理 + 工具 + 记忆 + Harness 工程化 + 协议 + 多 Agent + 评估安全 + 工业实战** 八位一体的 Agent 开发学习笔记。

### 1.1 什么是 Agent Harness?

真正复杂的 Agent 系统不是简单的「LLM + Prompt + 工具」, 而是一个 **Harness（运行时脚手架）**, 类似操作系统的角色, 调度 LLM、管理状态、隔离工具、恢复故障。一个工业级 Harness 通常包含:

```
                ┌─────────────────────────────────────┐
                │            Agent Harness            │
                └─────────────────────────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        ▼                         ▼                         ▼
   ┌──────────┐            ┌──────────────┐         ┌──────────────┐
   │ Runtime  │            │  Scheduler   │         │  Event Bus   │
   │ 执行环境 │◄──────────►│ 任务调度     │◄───────►│ 事件驱动     │
   └────┬─────┘            └──────┬───────┘         └──────┬───────┘
        │                         │                        │
        ▼                         ▼                        ▼
   ┌──────────┐            ┌──────────────┐         ┌──────────────┐
   │Tool Mgr  │            │   Planner    │         │   Tracing    │
   │ 工具管理 │            │ 任务分解     │         │ 轨迹追踪     │
   └────┬─────┘            └──────┬───────┘         └──────────────┘
        │                         │                        │
        ▼                         ▼                        ▼
   ┌──────────┐            ┌──────────────┐         ┌──────────────┐
   │Context   │            │  Evaluator   │         │   Logger     │
   │Manager   │            │ 质量评估     │         │ 日志         │
   │上下文管理│            └──────┬───────┘         └──────────────┘
   └────┬─────┘                   │
        │                         ▼
        ▼                  ┌──────────────┐
   ┌──────────┐            │    Retry     │
   │  Memory  │            │ 重试策略     │
   │ 记忆系统 │            └──────┬───────┘
   └────┬─────┘                   │
        │                         ▼
        ▼                  ┌──────────────┐
   ┌──────────┐            │ Checkpoint   │
   │ Session  │            │ 状态快照     │
   │ 会话管理 │            └──────────────┘
   └──────────┘
```

| Harness 模块 | 职责 | 本笔记章节 |
|-------------|------|-----------|
| **Runtime** | Agent 执行环境, 与 LLM/工具/沙箱交互 | Ch 08, Ch 12 |
| **Tool Manager** | 工具注册/检索/调用/参数验证 | Ch 07 |
| **Context Manager** | 上下文组装/压缩/截断 | Ch 10 |
| **Memory** | 短期/长期/工作记忆 | Ch 10 |
| **Planner** | 任务分解/子任务调度 | Ch 05 |
| **Evaluator** | 输出质量评估/终止判断 | Ch 06, Ch 19 |
| **Retry** | 失败重试/退避策略 | Ch 13 |
| **Checkpoint** | 状态快照/恢复 | Ch 13 |
| **Tracing** | 轨迹追踪/Span 埋点 | Ch 14 |
| **Logger** | 结构化日志 | Ch 14 |
| **Scheduler** | 任务调度/优先级/并发 | Ch 12 |
| **Session** | 会话管理/多轮状态 | Ch 13 |
| **Event Bus** | 事件驱动/模块解耦 | Ch 14 |

### 1.2 与其他模块的边界

| 模块 | 关注点 | 与本模块关系 |
|------|--------|--------------|
| [LLM](../LLM/) | 大模型原理与训练 | 本模块假设已掌握, 作为 Agent 的「大脑」 |
| [前沿部署](../前沿部署/) | 部署、推理服务化、沙箱 | 本模块专注 Agent **Harness 逻辑**, 部署侧参考前沿部署 |
| [分布式系统](../分布式系统/) | 一致性、复制、调度 | 多 Agent 协作的理论基础 |
| [infra开发](../infra开发/) | 网关、可观测性 | Harness 的 Tracing/Logger 借鉴其方法 |
| [云计算安全](../云计算安全/) | 沙箱、隔离、供应链 | Agent 沙箱执行的安全底座 |
| [深度学习](../深度学习/) | 模型训练与微调 | Agent 评估与自我改进的理论基础 |

### 1.3 笔记统一结构 (13 节)

1. **问题定义与边界**：解决什么、不解决什么
2. **直觉解释**：先讲直觉, 再上代码
3. **核心概念与架构**：组件关系、数据流
4. **操作流程与配置**：可复制的实现清单
5. **底层原理**：Prompt 工程、调度、状态机的源码级剖析
6. **代码与配置示例**：LangGraph / Claude SDK / Python 完整示例
7. **常见陷阱与调优**：性能、稳定性、成本
8. **工业案例与基准数据**：大厂 Agent 实践、任务完成率、token 成本
9. **与其他方案的关系**：横向对比、选型建议
10. **面试速答**：高频问题的一句话答案
11. **综合面试题**：由浅入深, 含答题要点
12. **故障复盘**：真实生产事故案例、根因、修复、防范
13. **参考与延伸**：官方文档、论文、跨文件链接

---

## 二、目录结构规划

```
Agent开发/
├── README.md                                # 本文件
│
│   ── 第一部分: 范式与 Harness 基础 ──
├── 01-Agent概念演进与范式.md                  # RPA→LLM Agent→Agentic AI
├── 02-Agent-Harness总览.md                   # 13 个模块全景 / 工业级架构
├── 03-Agent-vs-工作流-vs-Pipeline.md         # 边界、何时用 Agent
│
│   ── 第二部分: 推理与规划 (Planner / Evaluator) ──
├── 04-推理范式-CoT-ToT-GoT.md                 # CoT/ToT/GoT/Self-Consistency
├── 05-Planner与任务分解.md                    # ReAct/Plan-and-Execute/ReWOO/LLMCompiler
├── 06-Evaluator与自我修正.md                  # Reflexion/Self-Refine/Self-Critique
│
│   ── 第三部分: 工具与执行 (Tool Manager / Runtime) ──
├── 07-Tool-Manager与Function-Calling.md       # 工具注册/检索/调用/验证
├── 08-代码执行沙箱与Runtime.md                # E2B/Modal/Jupyter/Firecracker
├── 09-浏览器与Computer-Use.md                 # Browser Use/Playwright/Claude Computer Use
│
│   ── 第四部分: 记忆与上下文 (Memory / Context Manager) ──
├── 10-Memory与Context-Manager.md             # 短期/长期/工作记忆 + 上下文压缩
├── 11-Agentic-RAG.md                         # Self-RAG/CRAG/多步检索/知识图谱
│
│   ── 第五部分: Harness 工程化核心 ──
├── 12-Runtime与Scheduler.md                  # 调度核心/优先级/并发/DAG
├── 13-Session-Checkpoint-Retry.md            # 会话管理/状态快照/重试策略
├── 14-Event-Bus-Tracing-Logger.md            # 事件驱动/轨迹追踪/结构化日志
├── 15-Agent框架对比-Harness实现.md            # LangGraph/AutoGen/Claude SDK/AutoGPT
│
│   ── 第六部分: 协议与多 Agent ──
├── 16-MCP协议.md                             # Model Context Protocol/Server/Client
├── 17-A2A协议与Agent互操作.md                 # Agent-to-Agent/Agent Card/Discovery
├── 18-多Agent拓扑与群体智能.md                # Supervisor/Debate/Swarm/ChatDev/MetaGPT
│
│   ── 第七部分: 评估与安全 ──
├── 19-Agent评估与基准.md                      # SWE-bench/WebArena/tau-bench + Evaluation Harness
├── 20-Agent安全与防护.md                      # Prompt Injection/Tool Abuse/沙箱审计
│
│   ── 第八部分: 工业实战 ──
├── 20-工业实战-编码Agent.md                   # Cursor/Devin/Claude Code/Cline/OpenHands
├── 21-工业实战-Deep-Research-Agent.md         # Perplexity/Manus/ChatGPT Deep Research
├── 22-工业实战-Computer-Use与浏览器Agent.md   # Claude Computer Use/Browser Use/WebVoyager
├── 23-工业实战-多模态Agent.md                 # GPT-4o/Claude 3.5/多模态工具调用
├── 24-工业实战-大厂Agent平台.md               # OpenAI Assistants/字节豆包/阿里通义/百度文心
├── 25-工业实战-故障复盘集.md                   # Agent 生产事故根因与防范
└── 26-Agent未来方向.md                        # Agent OS/Self-improving/Long-running Task
```

### 状态图例

- ✅ 已建：笔记已完成
- ⏳ 待建：目录已占位, 内容规划中

---

## 三、章节索引

### 第一部分：范式与 Harness 基础

#### 01-Agent 概念演进与范式 ⏳

Agent 概念的演进路径：规则系统 → RPA → LLM Agent → Agentic AI → Autonomous Agent。每代范式的核心驱动力、解决的核心矛盾、引入的新问题。Agent 的核心定义（自主性 / 反应性 / 主动性 / 社会性）。从 ReAct (2022) 到 Claude Computer Use (2024) 的范式跃迁。

#### 02-Agent Harness 总览 ⏳

工业级 Agent 系统的 **Harness 架构全景**。Harness 是 Agent 的运行时脚手架, 类似操作系统, 调度 LLM、管理状态、隔离工具、恢复故障。本章拆解 13 个核心模块的职责、接口、协作关系:

- **Runtime**: Agent 执行环境, 与 LLM/工具/沙箱交互的核心循环
- **Tool Manager**: 工具注册 / 检索 / 调用 / 参数验证 / 结果解析
- **Context Manager**: 上下文组装 / 压缩 / 截断 / 优先级
- **Memory**: 短期 (Context Window) / 长期 (Vector DB) / 工作 (Scratchpad)
- **Planner**: 任务分解 / 子任务调度 / DAG 构建
- **Evaluator**: 输出质量评估 / 终止判断 / 自我批评
- **Retry**: 失败重试 / 退避策略 / 熔断
- **Checkpoint**: 状态快照 / 恢复 / 版本管理
- **Tracing**: 轨迹追踪 / Span 埋点 / OpenTelemetry
- **Logger**: 结构化日志 / 分级 / 采样
- **Scheduler**: 任务调度 / 优先级 / 并发控制 / DAG
- **Session**: 会话管理 / 多轮状态 / 用户隔离
- **Event Bus**: 事件驱动 / 模块解耦 / 流式输出

Harness 设计原则：模块解耦 / 接口标准化 / 状态可恢复 / 可观测 / 可插拔。

#### 03-Agent vs 工作流 vs Pipeline ⏳

三者边界与选型：
- **Pipeline**：固定流程, 无 LLM 决策（ETL、CI/CD）
- **Workflow**：固定节点 + LLM 增强, 路径确定（RAG、客服流程）
- **Agent**：LLM 自主决策路径, 动态规划（编码 Agent、研究 Agent）

何时**不该**用 Agent：确定性流程、强 SLA、高成本敏感、低容错。Anthropic 的「Workflow vs Agent」框架。**Harness 是 Agent 的标志**：Workflow 不需要完整 Harness, Agent 必须有。

---

### 第二部分：推理与规划 (Planner / Evaluator)

#### 04-推理范式 CoT / ToT / GoT ⏳

LLM 推理的核心范式：
- **Chain-of-Thought (CoT)**：线性推理链, Wei et al. 2022
- **Self-Consistency**：多采样投票, 提升准确率
- **Tree of Thoughts (ToT)**：树形搜索 + 回溯, Yao et al. 2023
- **Graph of Thoughts (GoT)**：图结构, 节点合并与复用
- **Chain-of-Thought Self-Consistency**：推理路径多样性

零样本 CoT ("Let's think step by step") vs 少样本 CoT。推理边界：CoT 在数学/逻辑任务 +20-40%, 在常识任务几乎无效。**Harness 集成**：Planner 调用 CoT 生成子任务, Evaluator 评估推理质量。

#### 05-Planner 与任务分解 ⏳

Harness 的 **Planner 模块**：将用户请求分解为可执行子任务图。

主流规划范式：
- **ReAct**：Reasoning + Acting 交错, Thought-Action-Observation 循环
- **Plan-and-Execute**：先全局规划, 再分步执行（LangChain）
- **ReWOO**：规划一次, 全部执行, 减少中间 LLM 调用
- **LLMCompiler**：并行任务图, DAG 调度
- **ADaPT**：自适应任务分解, 失败时递归细化

Planner 与 Scheduler 的协作：Planner 生成 DAG, Scheduler 调度执行。任务分解的权衡：粒度粗（少调用, 容错差）vs 粒度细（高准确率, 高成本）。

#### 06-Evaluator 与自我修正 ⏳

Harness 的 **Evaluator 模块**：评估 Agent 输出质量, 决定终止或修正。

让 Agent 从错误中学习：
- **Reflexion**：失败后反思, 写入记忆, 下次改进
- **Self-Refine**：自我批评 + 自我修正循环
- **Self-Critique**：LLM-as-Judge 评估自身输出
- **CRITIC**：外部工具验证（计算器、搜索引擎）辅助批评

Evaluator 的工程实现：终止条件 (max_steps / quality_threshold / budget_limit) / 评估指标 (task_success / partial_credit / efficiency) / 反馈回路。反思的边界：简单任务过度反思反而降低性能；复杂任务反思 +10-30%。

---

### 第三部分：工具与执行 (Tool Manager / Runtime)

#### 07-Tool Manager 与 Function Calling ⏳

Harness 的 **Tool Manager 模块**：工具全生命周期管理。

- **工具注册**：schema 声明 / 元数据 / 版本管理
- **工具检索**：千级工具场景下的检索（RAG-MCP）
- **工具调用**：OpenAI Function Calling / Parallel / Nested
- **参数验证**：JSON Schema 严格校验 / 类型安全
- **结果解析**：结构化输出 / 错误处理 / 重试

工具调用的可靠性：JSON 解析失败重试、参数验证、超时控制、幂等性设计。**Tool Manager 与 Runtime 协作**：所有工具调用经 Runtime 路由, 在沙箱执行。

#### 08-代码执行沙箱与 Runtime ⏳

Harness 的 **Runtime 模块**：Agent 执行环境的隔离与生命周期。

Agent 执行代码的隔离方案：
- **E2B**：开源 MicroVM 沙箱, 毫秒级启动
- **Modal Sandbox**：云原生, 支持 GPU
- **Jupyter Kernel**：异步执行, 状态保持
- **Firecracker MicroVM**：AWS Lambda 底座, 强隔离
- **Docker 容器**：轻量, 但隔离弱于 MicroVM

Runtime 的工程问题：依赖管理 / 文件系统持久化 / 网络隔离 / 资源限制 / 输出截断 / 进程生命周期。Runtime 与 Checkpoint 协作：状态快照可恢复。

#### 09-浏览器与 Computer Use ⏳

让 Agent 操作图形界面 (浏览器 / 桌面)：
- **Browser Use**：开源, Playwright + 视觉模型
- **Playwright MCP**：浏览器自动化协议
- **Claude Computer Use**：屏幕截图 + 鼠标键盘控制
- **WebVoyager / WebArena**：浏览器 Agent 基准
- **OmniParser**：UI 元素解析, 视觉 → 结构化

浏览器 Agent 的挑战：动态网页 / 反爬 / 验证码 / 多 tab 状态 / 长任务记忆。**Runtime 隔离**：浏览器在沙箱中运行, 防止 Agent 误操作系统级资源。

---

### 第四部分:记忆与上下文 (Memory / Context Manager)

#### 10-Memory 与 Context Manager ⏳

Harness 的 **Memory + Context Manager 模块**：Agent 的记忆层次与上下文管理。

**记忆层次**：
- **短期记忆**：Context Window, 当前对话
- **工作记忆**：Scratchpad, 当前任务状态
- **长期记忆**：Vector DB / Knowledge Graph, 跨会话
- **情景记忆 (Episodic)**：过往交互记录
- **语义记忆 (Semantic)**：抽象知识
- **程序记忆 (Procedural)**：技能与流程

**Context Manager 职责**：
- 上下文组装：System Prompt + Tools + History + Memory
- 上下文压缩：摘要 / 选择性遗忘 / LLM 压缩
- 上下文截断：滑动窗口 / 优先级保留
- Token 预算：分模块配额, 防爆

记忆系统案例：
- **MemGPT / Letta**：OS 启发的记忆分层, 主动召回
- **A-Mem**：Zettelkasten 笔记法, 关联网络
- **Mem0**：个性化记忆, 用户级

记忆操作：写入 / 检索 / 遗忘 / 压缩 / 反思。

#### 11-Agentic RAG ⏳

RAG 与 Agent 的深度融合, Memory 模块的高级形态：
- **Naive RAG**：单次检索, 简单拼接
- **Agentic RAG**：多步检索, 自主决策
- **Self-RAG**：检索决策 + 输出验证
- **Corrective RAG (CRAG)**：检索结果评估 + 纠正
- **Adaptive RAG**：查询难度评估, 选择策略

多步检索场景：复杂问题（「对比 X 和 Y 在 Z 方面的差异」）、时间敏感查询、多源融合。知识图谱 + RAG：GraphRAG、HippoRAG。**与 Planner 协作**：检索作为子任务, 由 Planner 调度。

---

### 第五部分:Harness 工程化核心

> 本部分是工业级 Agent 区别于 Demo 的关键。LangGraph、Claude Agent SDK、Devin 都有完整工程化实现。

#### 12-Runtime 与 Scheduler ⏳

Harness 的 **调度核心**：Runtime 执行循环 + Scheduler 任务调度。

**Runtime 执行循环**：
```
while not done:
    state = context_manager.assemble()
    action = llm.reason(state)
    result = tool_manager.execute(action)
    context_manager.update(result)
    evaluator.evaluate(result)
    if should_checkpoint: checkpoint.save()
```

**Scheduler 调度策略**：
- **优先级队列**：用户优先级 / 任务紧急度
- **并发控制**：单 Agent 并发工具调用 / 多 Agent 隔离
- **DAG 调度**：Plan-and-Execute 的子任务图
- **公平调度**：多租户场景
- **抢占式**：长任务让位短任务

并行与串行的权衡：ReWOO 全并行（少调用, 容错差）vs ReAct 串行（多调用, 容错好）。LLMCompiler 的并行 DAG 执行。

#### 13-Session / Checkpoint / Retry ⏳

Harness 的 **可靠性与状态恢复**三件套。

**Session 会话管理**：
- 会话生命周期：创建 / 恢复 / 终止
- 多轮状态：Context Window 持续累积
- 用户隔离：Session ID 路由
- 会话迁移：跨实例恢复

**Checkpoint 状态快照**：
- 触发时机：每 N 步 / 关键节点 / 用户中断
- 快照内容：Context + Memory + Tool state + Plan
- 存储：Redis (热) / S3 (冷)
- 恢复：从最近 Checkpoint 续传
- LangGraph Checkpointer / Claude Agent SDK Resume

**Retry 重试策略**：
- LLM 调用失败：指数退避 + 抖动
- 工具调用失败：参数修正重试 / 切换工具
- JSON 解析失败：Structured Output / 修复提示
- 熔断：连续失败 N 次后降级
- 幂等性：重试不产生副作用

**长任务恢复**：Devin / Manus 的数小时任务必须依赖 Checkpoint, 崩溃后可续传。

#### 14-Event Bus / Tracing / Logger ⏳

Harness 的 **事件驱动与可观测性**。

**Event Bus 事件总线**：
- 发布订阅模式, 解耦 Harness 模块
- 事件类型：ToolStart / ToolEnd / LLMCall / Checkpoint / Error
- 流式输出：SSE / WebSocket 通过 Event Bus 透传
- 插件扩展：第三方监听事件做监控 / 审计

**Tracing 轨迹追踪**：
- Span / Trace 模型 (OpenTelemetry GenAI 语义约定)
- 完整记录 Thought / Action / Observation
- LangSmith / Langfuse / Phoenix 集成
- 调试回放：从 Trace 重现 Agent 行为

**Logger 结构化日志**：
- JSON 结构化, 便于检索
- 分级：DEBUG / INFO / WARN / ERROR
- 采样：高频事件采样, 防日志爆炸
- 关联：TraceID / SessionID / UserID 串联

**Event Bus + Tracing + Logger** 构成 Harness 的可观测三件套。

#### 15-Agent 框架对比 - Harness 实现 ⏳

主流 Agent 框架作为 **Harness 实现** 的横向对比：

| 框架 | Runtime | Tool Mgr | Memory | Planner | Checkpoint | Tracing |
|------|---------|----------|--------|---------|-----------|---------|
| **LangGraph** | StateGraph | ToolNode | State | Planner | Checkpointer | LangSmith |
| **Claude Agent SDK** | Loop | Tools API | Files API | 内置 | Resume | File Logger |
| **OpenAI Agents SDK** | Runner | Tools | Handoffs | 内置 | - | Tracing |
| **AutoGen** | Conversation | Tools | Memory | Planner | - | Tracing |
| **CrewAI** | Crew | Tools | Memory | Tasks | - | Tracing |
| **Smolagents** | CodeAgent | Tools | - | 内置 | - | Tracing |
| **Pydantic AI** | Agent | Tools | History | - | - | Logfire |

选型维度：复杂度 / 多 Agent / 状态管理 / 流式 / 可观测 / 商用支持。**框架 = Harness 的具体实现**, 选框架本质是选 Harness。

---

### 第六部分:协议与多 Agent

#### 16-MCP (Model Context Protocol) ⏳

Anthropic 提出的 Agent 工具调用标准, **Tool Manager 的协议化**：
- **协议设计**：JSON-RPC 2.0, stdio / SSE 传输
- **Server**：暴露 tools / resources / prompts
- **Client**：Claude Desktop / Cursor / Cline / Zed
- **Tool Discovery**：MCP Server 注册与发现
- **与 Function Calling 对比**：MCP 是协议层, Function Calling 是 API 层

MCP 生态：GitHub / Slack / Filesystem / Puppeteer / Postgres 等 Server。开源实现：TypeScript SDK / Python SDK。

#### 17-A2A 协议与 Agent 互操作 ⏳

Agent 之间的通信标准, **多 Agent Harness 的协议层**：
- **A2A (Agent-to-Agent) Protocol**：Google 2025 提出
- **Agent Card**：Agent 能力声明, JSON-LD
- **Agent Discovery**：/.well-known/agent.json
- **Task delegation**：跨 Agent 任务委派
- **MCP vs A2A**：MCP 是 Agent ↔ Tool, A2A 是 Agent ↔ Agent

互操作挑战：能力描述 / 信任边界 / 安全沙箱 / 审计追溯。

#### 18-多 Agent 拓扑与群体智能 ⏳

多 Agent 系统的拓扑与协作模式：

**主流拓扑**：
- **Supervisor / Worker**：中心调度, 任务分发
- **Hierarchical**：层级管理, Manager → Leads → Workers
- **Debate**：多 Agent 辩论, 提升准确性
- **Round Robin**：轮流发言, 平等协作
- **Hub and Spoke**：中心化通信, 减少耦合
- **Blackboard**：共享黑板, 异步协作

**群体智能案例**：
- **ChatDev**：软件公司模拟, 角色（CEO/CTO/程序员/测试）
- **MetaGPT**：SOP 驱动, 标准作业流程
- **CAMEL**：Role-Playing, 两个 Agent 协作
- **Swarm (OpenAI)**：轻量编排, Handoff 原语
- **Agentic Emergence**：涌现行为, 简单规则 → 复杂智能

多 Agent 的工程问题：死锁 / 活锁 / 雪崩 / token 爆炸 / 状态一致性。**Event Bus 在多 Agent 中的关键作用**：解耦 Agent 间通信。

---

### 第七部分:评估与安全

#### 19-Agent 评估与基准 ⏳

Agent 评估的方法论与基准, **Evaluator 模块的工程化**：

**评估维度**：
- **任务完成率**：成功率 / 部分完成 / 平均步数
- **效率指标**：token 数 / 调用次数 / 时长 / 成本
- **轨迹评估**：过程正确性, 不仅看结果
- **LLM-as-Judge**：GPT-4 评估, 偏差与缓解
- **人工评估**：标注成本高, 但是金标准

**Evaluation Harness (评估脚手架)**：
- **LM-Evaluation-Harness (EleutherAI)**：LLM 评估金标准
- **SWE-bench harness**：编码 Agent 评估接口
- **agent-harness**：通用 Agent 测试床
- 自建 Harness：任务定义 / 执行隔离 / 结果采集 / 评分

**主流基准**：
- **SWE-bench / SWE-bench Verified**：真实 GitHub PR 修复
- **WebArena / VisualWebArena**：网页任务
- **AgentBench**：多场景综合
- **tau-bench**：客服场景, 工具调用
- **GAIA**：通用 AI 助手, 多步推理
- **MLE-bench**：Kaggle 竞赛, ML 工程
- **OSWorld**：桌面操作

#### 20-Agent 安全与防护 ⏳

Agent 安全面临的独特威胁（详见 [云计算安全](../云计算安全/) 模块的沙箱部分）：
- **Prompt Injection**：用户输入 / 工具返回中注入恶意指令
- **Tool Abuse**：Agent 被诱导误用工具（删文件、转账）
- **Data Exfiltration**：敏感数据通过工具泄露
- **Denial of Wallet**：诱导 Agent 无限调用, 烧 token
- **Jailbreak**：绕过安全策略

**Harness 集成防御**：
- **Tool Manager**：参数 schema 严格验证 + 工具白名单
- **Runtime**：所有工具在沙箱执行
- **Evaluator**：实时评估 Agent 行为异常
- **Logger + Tracing**：完整审计, 事后追溯
- **Session**：速率限制 / token 预算
- **Event Bus**：高风险操作触发 human-in-the-loop

**人在环 (Human-in-the-loop)**：高风险操作（支付 / 删除 / 发送）人工确认。OWASP Top 10 for LLM Applications（LLM01-10）。

---

### 第八部分:工业实战

> 本部分汇总大厂 Agent 实践、垂直领域 Agent、故障复盘与未来方向, 是面试加分项与生产参考的双保险。每个案例都拆解其 **Harness 实现**。

#### 21-工业实战 - 编码 Agent ⏳

- **Cursor**：端云协同, Tab 模型 + Composer Agent, 自研 Harness
- **Devin (Cognition)**：全栈工程师 Agent, SWE-bench 13.86%, 完整 Harness + Checkpoint
- **Claude Code**：Anthropic 官方 CLI, Claude Agent SDK 底座, Session/Resume
- **Cline**：开源 VSCode 插件, MCP 支持, 轻量 Harness
- **OpenHands (原 OpenDevin)**：开源, SWE-bench 53%, 自研 Harness
- **GitHub Copilot Workspace**：从 issue 到 PR

编码 Agent 的 Harness 特点：长任务 Checkpoint / 沙箱执行 / 文件系统持久化 / 测试反馈循环。SWE-bench 排行榜演进。

#### 22-工业实战 - Deep Research Agent ⏳

- **Perplexity Deep Research**：多步检索 + 综合报告
- **Manus**：通用任务 Agent, 浏览器 + 代码, 数小时任务
- **ChatGPT Deep Research**：OpenAI, 数十分钟生成报告
- **Gemini Deep Research**：Google, 多源融合
- **开源**：GPT-Researcher, STORM (Stanford)

Deep Research 的 Harness 架构：Planner 规划检索 → 多步检索 (Tool Manager) → 信息抽取 → 综合写作 → Evaluator 引用验证。报告质量评估。

#### 23-工业实战 - Computer Use 与浏览器 Agent ⏳

- **Claude Computer Use**：屏幕截图 + 鼠标键盘
- **Browser Use**：开源, 视觉 + DOM 双模
- **WebVoyager**：网页任务基准
- **Anthropic Computer Use API**：computer_20241022 工具
- **多模态 GUI Agent**：见 Talk, OmniParser, OS-Atlas

Harness 挑战：UI 多样性 / 延迟（截图 token 大）/ 错误恢复 / 长任务状态。Runtime 隔离：浏览器/桌面在沙箱中。

#### 24-工业实战 - 多模态 Agent ⏳

- **GPT-4o**：原生多模态, 语音/图像/视频
- **Claude 3.5 Sonnet**：视觉 + 工具调用
- **Gemini 2.0**：多模态 + Agent
- **多模态工具调用**：图像生成 / 语音合成 / 视频理解
- **实时语音 Agent**：GPT-4o Realtime, 低延迟对话

多模态 Agent 的 Harness 工程问题：上下文长度爆炸（图像 token）/ 流式多模态 / 工具结果的多模态表示 / Context Manager 压缩策略。

#### 25-工业实战 - 大厂 Agent 平台 + 故障复盘集 ⏳

**大厂 Agent 平台**：
- **OpenAI Assistants API / Responses API**：官方平台, 工具集成
- **Anthropic Claude Agent SDK**：Claude Code 底座
- **字节豆包 Agent**：扣子 (Coze), 工作流 + Agent
- **阿里通义 Agent**：百炼平台, 企业级
- **百度文心智能体**：AppBuilder
- **腾讯元宝 Agent**：基于混元

大厂平台的共同 Harness 能力：低代码编排 / 工具市场 / 知识库 / 多模型 / 监控 / 商业化。

**故障复盘集** — 真实 Agent 生产事故的根因、修复、防范：

| 案例 | 现象 | 根因 | 修复 | 防范 |
|------|------|------|------|------|
| Agent 死循环 | 同一工具反复调用 100+ 次 | 工具结果未变化, LLM 未识别 | 状态机 + 循环检测 | 调用历史去重 |
| Token 爆炸 | 单次任务消耗 10M+ token | 上下文无限累积 | Context Manager 压缩 | 上下文预算控制 |
| Prompt Injection | Agent 执行 rm -rf | 工具返回含恶意指令 | 数据/指令分离 + 沙箱 | 输入消毒 |
| 多 Agent 死锁 | Agent 互相等待 | 循环依赖 + 无超时 | Scheduler 超时 + 拓扑校验 | DAG 验证 |
| 工具误用转账 | Agent 错误调用支付 API | 参数验证缺失 | 二次确认 + 金额上限 | 高危工具白名单 |
| 长任务状态丢失 | 2 小时任务中途崩溃 | 状态仅在内存 | Checkpoint + 恢复 | 持久化状态机 |
| Hallucination 工具 | 调用不存在的 API | LLM 编造工具名 | Tool Manager 白名单 | Function calling 强约束 |
| 浏览器 Agent 卡死 | 验证码 / 反爬 | 无降级策略 | 失败转人工 | 异常处理流程 |

#### 26-Agent 未来方向 ⏳

Agent 技术的前沿探索：
- **Agent OS**：操作系统级 Agent, 资源调度 / 进程管理 / Harness 标准化
- **Long-running Task**：数小时 / 数天的长任务, Checkpoint 与 Resume 是关键
- **Self-improving Agent**：从经验中学习, 持续优化, Evaluator 反馈到 Memory
- **Agent Marketplace**：Agent 交易市场, 复用与组合, A2A 协议支撑
- **Personal Agent**：个人专属, 长期记忆, 隐私保护
- **Embodied Agent**：机器人 / 物理世界交互, Runtime 延伸到物理沙箱
- **AGI 路径**：Agent 作为 AGI 的载体？Harness 作为 AGI 的操作系统？

未来挑战：可靠性 / 成本 / 安全 / 伦理 / 法律 / 就业。Harness 标准化（如 MCP / A2A）是 Agent 工业化的关键。

---

## 四、知识地图

```
                ┌─────────────────────────────────────┐
                │         范式与 Harness 基础         │
                │  演进 / Harness 13 模块 / 边界     │
                └─────────────────┬───────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        ▼                         ▼                         ▼
┌─────────────────┐     ┌───────────────────┐     ┌───────────────────┐
│  推理与规划     │     │  工具与执行       │     │  记忆与上下文      │
│  Planner / CoT  │     │  Tool Manager     │     │  Memory            │
│  Evaluator      │     │  Runtime / 沙箱   │     │  Context Manager   │
│  Reflexion      │     │  Computer Use     │     │  Agentic RAG       │
└────────┬────────┘     └─────────┬─────────┘     └────────┬──────────┘
         │                        │                        │
         └────────────────────────┼────────────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │       Harness 工程化核心            │
                │  Runtime / Scheduler                │
                │  Session / Checkpoint / Retry       │
                │  Event Bus / Tracing / Logger       │
                │  框架 = Harness 实现                │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │          协议与多 Agent             │
                │  MCP (Agent ↔ Tool)                 │
                │  A2A (Agent ↔ Agent)                │
                │  Supervisor / Debate / Swarm        │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │            评估与安全               │
                │  Evaluation Harness / SWE-bench     │
                │  Prompt Injection 防御              │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │            工业实战                 │
                │  编码 / Research / Computer Use /   │
                │  多模态 / 大厂平台 / 故障 / 未来    │
                └─────────────────────────────────────┘
```

---

## 五、推荐学习路径

- **应用开发者**：01 → 02 → 03 → 07 → 15 → 16 → 21 → 25
- **Harness / Infra 工程师**：01 → 02 → 12 → 13 → 14 → 15 → 19 → 20
- **多 Agent 系统研究**：01 → 02 → 04 → 05 → 18 → 19
- **Agent 安全工程师**：01 → 07 → 08 → 16 → 20 → 25
- **面试准备**：01 → 02 → 03 → 04 → 05 → 07 → 12 → 15 → 19 → 21 → 25
- **垂直领域深入**：编码 (21) / Research (22) / Computer Use (23) / 多模态 (24)

---

## 六、写作约定

- **环境基线**：Python 3.11+ / Node.js 20+ / Claude 3.5+ / GPT-4o+ / LangGraph 0.2+
- **模型版本**：Claude Sonnet 4.5 / GPT-4o / Llama 3.1+ / Qwen 2.5+
- **代码示例**：可复制运行, 标注输出与副作用
- **图示**：优先 ASCII 图说明架构；复杂图示标注来源
- **原理剖析**：关键概念对应到 LLM 推理、调度算法、状态机, 不省略中间步骤
- **代码**：LangGraph / Claude SDK / Python / TypeScript 给出生产级模板与注释
- **跨文件链接**：相关概念使用相对路径链接, 便于跳转
- **优先级**：正确性 > 完整性 > 速度
- **基准数据**：所有性能数据标注来源（论文 / 博客 / 实测）, 不臆造
- **Harness 视角**：每章明确标注其对应的 Harness 模块, 形成统一脉络

---

## 七、参考资源

### 官方文档

- LangChain / LangGraph — https://langchain-ai.github.io/langgraph/
- Anthropic Claude Agent SDK — https://github.com/anthropics/claude-code-sdk-python
- OpenAI Agents SDK — https://github.com/openai/openai-agents-python
- Microsoft AutoGen — https://microsoft.github.io/autogen/
- CrewAI — https://docs.crewai.com/
- LlamaIndex Agents — https://docs.llamaindex.ai/
- HuggingFace Smolagents — https://github.com/huggingface/smolagents
- Pydantic AI — https://ai.pydantic.dev/
- Model Context Protocol — https://modelcontextprotocol.io/
- A2A Protocol — https://github.com/google/A2A
- LM-Evaluation-Harness — https://github.com/EleutherAI/lm-evaluation-harness
- SWE-bench — https://www.swebench.com/
- OpenTelemetry GenAI — https://opentelemetry.io/docs/specs/semconv/gen-ai/

### 论文

- *ReAct: Synergizing Reasoning and Acting in Language Models* — Yao et al., 2022
- *Toolformer: Language Models Can Teach Themselves to Use Tools* — Schick et al., 2023
- *Reflexion: Language Agents with Verbal Reinforcement Learning* — Shinn et al., 2023
- *Tree of Thoughts: Deliberate Problem Solving with Large Language Models* — Yao et al., 2023
- *Chain-of-Thought Prompting Elicits Reasoning in Large Language Models* — Wei et al., 2022
- *Self-Refine: Iterative Refinement with Self-Feedback* — Madaan et al., 2023
- *Voyager: An Open-Ended Embodied Agent with Large Language Models* — Wang et al., 2023
- *MetaGPT: Meta Programming for Multi-Agent Collaborative Framework* — Hong et al., 2023
- *ChatDev: Communicative Agents for Software Development* — Qian et al., 2023
- *SWE-bench: Can Language Models Resolve Real-World GitHub Issues?* — Jimenez et al., 2023
- *WebArena: A Realistic Web Environment for Building Autonomous Agents* — Zhou et al., 2023
- *Generative Agents: Interactive Simulacra of Human Behavior* — Park et al., 2023
- *MemGPT: Towards LLMs as Operating Systems* — Packer et al., 2023
- *Self-RAG: Learning to Retrieve, Generate, and Critique through Self-Reflection* — Asai et al., 2023
- *LLMCompiler: An LLM Compiler for Parallel Function Calling* — Kim et al., 2023
- *ReWOO: Decoupling Reasoning from Acting for Web-Assisted LLMs* — Xu et al., 2023

### 大厂工程博客

- Anthropic Engineering — https://www.anthropic.com/engineering (Claude Code, Computer Use, MCP, Harness 设计)
- OpenAI Engineering — https://openai.com/engineering/ (Assistants API, Deep Research, Agents SDK)
- Cursor Blog — https://cursor.com/blog (编码 Agent, 自研 Harness)
- LangChain Blog — https://blog.langchain.dev/ (LangGraph, Agent Harness 工程)
- Cognition Labs — https://cognition.ai/blog (Devin, 长任务 Checkpoint)
- Microsoft AutoGen Blog — https://microsoft.github.io/autogen/blog/
- HuggingFace Agents — https://huggingface.co/blog (Smolagents)
- 字节跳动技术团队 — 豆包 / 扣子
- 阿里云通义 — 百炼平台
- Google DeepMind — Gemini, A2A

### 书籍

- 《Building LLM Applications》— LangChain 团队
- 《Hands-On Large Language Models》— Jay Alammar, Maarten Grootendorst
- 《Agent-based Modeling and Simulation》— Agent 理论
- 《Multi-Agent Systems: Algorithmic, Game-Theoretic, and Logical Foundations》— Shoham & Leyton-Brown
- 《The Pragmatic Programmer》— 工程化思维
- 《Designing Data-Intensive Applications》— 状态机、可靠性

### 相关模块

- [LLM](../LLM/) — 大模型原理, Agent 的「大脑」
- [前沿部署](../前沿部署/) — Agent 部署与沙箱
- [分布式系统](../分布式系统/) — 多 Agent 协作的理论基础
- [infra开发](../infra开发/) — 网关、可观测性
- [云计算安全](../云计算安全/) — 沙箱隔离与供应链安全
- [深度学习](../深度学习/) — 模型训练与微调

---

## 八、TODO / 路线图

- [x] 目录占位、README 落地
- [ ] 01-Agent 概念演进与范式
- [ ] 02-Agent Harness 总览 (13 模块全景)
- [ ] 03-Agent vs 工作流 vs Pipeline
- [ ] 04-推理范式 CoT/ToT/GoT
- [ ] 05-Planner 与任务分解
- [ ] 06-Evaluator 与自我修正
- [ ] 07-Tool Manager 与 Function Calling
- [ ] 08-代码执行沙箱与 Runtime
- [ ] 09-浏览器与 Computer Use
- [ ] 10-Memory 与 Context Manager
- [ ] 11-Agentic RAG
- [ ] 12-Runtime 与 Scheduler
- [ ] 13-Session / Checkpoint / Retry
- [ ] 14-Event Bus / Tracing / Logger
- [ ] 15-Agent 框架对比 (Harness 实现)
- [ ] 16-MCP 协议
- [ ] 17-A2A 协议与 Agent 互操作
- [ ] 18-多 Agent 拓扑与群体智能
- [ ] 19-Agent 评估与基准 (含 Evaluation Harness)
- [ ] 20-Agent 安全与防护
- [ ] 21-工业实战-编码 Agent
- [ ] 22-工业实战-Deep Research Agent
- [ ] 23-工业实战-Computer Use 与浏览器 Agent
- [ ] 24-工业实战-多模态 Agent
- [ ] 25-工业实战-大厂 Agent 平台 + 故障复盘集
- [ ] 26-Agent 未来方向
- [ ] 跨模块知识链接（LLM / 前沿部署 / 分布式系统）

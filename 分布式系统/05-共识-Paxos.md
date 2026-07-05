# 第 5 章 共识 —— Paxos

> **本章导读**
> Paxos 是 Lamport 1989 提出的共识算法，被公认为「最难理解」的算法之一。但其正确性证明严谨，是工业级共识算法的奠基——Google Chubby、Spanner、Microsoft Azure Cosmos DB 都基于 Paxos 变种。本章系统化 Basic Paxos（单值共识）、Multi-Paxos（多值/日志共识）、Paxos 工程难点（日志空洞、成员变更）。Raft（[§5.3](./05-共识-Raft.md)）是 Paxos 的「易理解版本」，但理解 Paxos 能让你看懂所有共识算法的本质。
>
> **学完能回答**：
> 1. Basic Paxos 的 Proposer / Acceptor / Learner 三角色？
> 2. Prepare / Accept 两阶段流程？
> 3. Multi-Paxos 如何优化为单阶段？
> 4. Paxos 的工程难点是什么？
> 5. Paxos 与 Raft 的本质区别？
>
> **前置**：[05-共识-问题与FLP](./05-共识-问题与FLP.md) · **预计时长**：4-5 小时 · **标记**：⭐🔥

---

## 5.0 章节地图（Paxos 部分）

```
                       Paxos
                        │
        ┌───────────────┼───────────────┐
        │               │               │
   Basic Paxos    Multi-Paxos       工程难点
        │               │               │
   ┌────┼────┐     ┌────┴────┐    ┌────┴────┐
  Prepare Accept   单Leader  日志空洞  成员变更
   阶段  阶段      优化      恢复     联合共识
```

---

## 5.1 Basic Paxos

📜 [Lamport 1998] *The Part-Time Parliament*（原文 1989 提交，1998 正式发表）
📜 [Lamport 2001] *Paxos Made Simple*

### 5.1.1 三角色

| 角色 | 职责 | 数量 |
|------|------|------|
| **Proposer**（提议者） | 提议值，推动共识 | 1+ |
| **Acceptor**（接受者） | 对提议投票 | 通常 $2f+1$ |
| **Learner**（学习者） | 学习已决定的值 | 1+ |

实际部署中，一个节点可同时扮演多个角色。

### 5.1.2 算法目标

- 多个 Proposer 可同时提议不同值
- 最终只有一个值被决定
- 决定后不可更改

### 5.1.3 算法流程 ⭐

#### Phase 1: Prepare

```pseudocode
# Proposer
function prepare(value):
    n = generate_unique_number()  # 全局唯一递增编号
    for each acceptor:
        send(Prepare(n))
    # 等待 majority 响应

# Acceptor
function onReceivePrepare(n):
    if n > promised_n:
        promised_n = n
        return Promise(n, accepted_proposal)
    else:
        return Reject
```

Acceptor 响应 Promise 时，附带**已接受的最大编号提议**（若有）：

- `Promise(n, None)`：从未接受过任何提议
- `Promise(n, (n_a, v_a))`：已接受过编号 $n_a$、值 $v_a$ 的提议

#### Phase 2: Accept

```pseudocode
# Proposer 收到 majority Promise 后
function accept(prepare_responses):
    # 若有任何 Promise 带回 accepted_proposal
    # 必须用最大编号对应的值
    if any_accepted(prepare_responses):
        v = highest_numbered_value(prepare_responses)
    else:
        v = my_proposed_value  # 可用自己的值
    
    for each acceptor:
        send(Accept(n, v))

# Acceptor
function onReceiveAccept(n, v):
    if n >= promised_n:
        accepted_proposal = (n, v)
        return Accepted(n, v)
    else:
        return Reject
```

Proposer 收到 majority Accepted 后，值 $v$ 被决定，通知 Learner。

### 5.1.4 关键不变式 ⭐

Paxos 通过两个不变式保证正确性：

> **P1**：Acceptor 必须接受它收到的第一个提议
>
> **P2**：若值 $v$ 被 chosen（多数 Acceptor 接受），则任何更高编号的 chosen 值必为 $v$

**P2 的强化形式**（保证 Safety）：

> **P2c**：对于任何提议 $(n, v)$ 被提出，存在一个多数集合 $S$，使得 $S$ 中任一 Acceptor 要么未接受过任何提议，要么接受过的最大编号提议的值为 $v$。

Phase 1 的 Promise 机制正是为满足 P2c：Proposer 必须用 Promise 中带回的最大编号值。

### 5.1.5 示例

```
Proposer 1                  Acceptor A    Acceptor B    Acceptor C
   │                            │             │             │
   │ Prepare(n=1)               │             │             │
   ├───────────────────────────►│             │             │
   │ Prepare(n=1)               │             │             │
   ├──────────────────────────────────────────►             │
   │ Prepare(n=1)               │             │             │
   ├────────────────────────────────────────────────────────►
   │                            │             │             │
   │ Promise(1, None)           │             │             │
   │◄───────────────────────────┤             │             │
   │ Promise(1, None)           │             │             │
   │◄──────────────────────────────────────────┤             │
   │                            │             │             │
   │ # 收到 majority，进入 Accept                            │
   │ Accept(1, v=X)             │             │             │
   ├───────────────────────────►│             │             │
   │ Accept(1, v=X)             │             │             │
   ├──────────────────────────────────────────►             │
   │                            │             │             │
   │ Accepted(1, X)             │             │             │
   │◄───────────────────────────┤             │             │
   │ Accepted(1, X)             │             │             │
   │◄──────────────────────────────────────────┤             │
   │                            │             │             │
   │ # majority Accepted，X 被 chosen
   ▼
```

### 5.1.6 竞争与 Live-lock ⚠️

多个 Proposer 竞争时可能 live-lock：

```
Proposer 1: Prepare(1) ────►
                        ◄─── Promise(1)
Proposer 2:        Prepare(2) ────►
                           ◄─── Promise(2)
Proposer 1: Accept(1) ────► (被拒，因 Acceptor 已 promise 2)
Proposer 1: Prepare(3) ────►
                        ◄─── Promise(3)
Proposer 2: Accept(2) ────► (被拒)
Proposer 2: Prepare(4) ────►
...
```

**解决**：

- 选一个「Distinguished Proposer」（单 Leader）
- 随机化 backoff
- 实际系统几乎都用单 Leader 优化（Multi-Paxos）

---

## 5.2 Multi-Paxos

### 5.2.1 动机

Basic Paxos 每次决定一个值需 2 个 RTT（Prepare + Accept）。对于「日志复制」场景（连续多值），开销过大。

Multi-Paxos 优化：**选稳定 Leader，跳过 Prepare 阶段**。

### 5.2.2 核心思想

```
Basic Paxos (per value):
    Prepare ────►  Promise  ────►  Accept  ────►  Accepted
    └────── RTT 1 ──────┘    └────── RTT 2 ──────┘

Multi-Paxos (steady state):
    Accept ────►  Accepted
    └────── RTT 1 ──────┘
```

### 5.2.3 实现

1. **Leader 选举**：选一个 Distinguished Proposer
2. **Prepare 一次**：Leader 对所有日志位置发一次 Prepare，获取 Promise
3. **后续只走 Accept**：Leader 直接对每个日志位置发 Accept
4. **Leader 失效**：新 Leader 重新 Prepare（仅对未完成的日志位置）

### 5.2.4 日志空洞问题 ⚠️

Multi-Paxos 中日志位置可能空洞：

```
日志位置: 1  2  3  4  5
内容:     A  ?  C  ?  ?
              ↑     ↑↑
              空洞  未提议
```

空洞原因：

- Leader 在 Accept 阶段故障
- 部分日志位置未达成共识

**修复**：

- 新 Leader 通过 Prepare 重新填充空洞
- 或显式标记空洞（NoOp）

### 5.2.5 成员变更

Paxos 成员变更复杂：

- **Joint Consensus**：新旧配置两阶段切换
- **Single-member changes**：每次只增删一个节点，安全性更易保证
- 详见 [05-共识-Raft](./05-共识-Raft.md)（Raft 实现更清晰）

---

## 5.3 Paxos 工程化 📜

📜 [Chandra, Griesemer, Redstone 2007] *Paxos Made Live* —— Google Chubby 工程经验。

### 5.3.1 工程难点

1. **磁盘持久化**：Promised/Accepted 状态必须落盘，否则故障后违反 Safety
2. **成员变更**：原论文未详述，工程需自行设计
3. **日志空洞**：需 NoOp 填充
4. **Leader 选举**：原论文未指定，工程需结合 Failure Detector
5. **快照与日志压缩**：长期运行日志无限增长
6. **客户端语义**：Exactly-Once 需客户端去重

### 5.3.2 Chubby 工程经验 🔥

- **Multi-Paxos + Master Lease**：Master 通过 Lease 保证一段时间内无其他 Master
- **Fast Paxos 优化**：减少消息数
- **Snapshot**：周期性快照 + 日志截断
- **客户端缓存**：Master 通知客户端缓存失效

### 5.3.3 Paxos 变种

| 变种 | 改进 | 典型 |
|------|------|------|
| **Multi-Paxos** | 单 Leader，跳过 Prepare | Chubby |
| **Fast Paxos** | 减少轮数（3 轮→2 轮） | — |
| **EPaxos** | 无 Leader，多 Leader 并行 | 实验性 |
| **Mencius** | 多 Leader 轮转 | — |
| **Spanner Paxos** | 跨 Paxos Group 2PC | Spanner |
| **Raft** | 易理解 + 工程优化 | etcd, TiKV |

---

## 5.4 🎓 学术深度

### 5.4.1 Safety 证明直觉

**核心**：P2c 不变式 + Promise 机制保证。

- 若值 $v$ 在编号 $n$ 被 chosen，则任何 $n' > n$ 的 Prepare 必带回 $v$
- 故 $n'$ 的 Accept 必用 $v$
- 故后续 chosen 值仍为 $v$

### 5.4.2 Liveness 证明

**FLP 限制下**：

- 不能保证 Termination
- 但可保证「在部分同步假设下 Termination」

**Multi-Paxos Liveness**：

- 单 Leader + 部分同步 → 大部分时间终止
- Leader 故障 → 选举 + 新 Prepare → 恢复终止

### 5.4.3 EPaxos 📜

📜 [Serafini, Duggan, Abraham, Schiper, 2012] *EPaxos: Leader Election for Rich Modern Data Centers*：

- **无 Leader**：任何副本可提议
- **依赖图**：并发不冲突的命令可乱序执行
- **优势**：低延迟、高吞吐（跨地域）
- **代价**：复杂度高、冲突时退化为多轮

---

## 5.5 🏭 工业实战

### 5.5.1 Google Chubby 🔥

- 分布式锁服务，基于 Multi-Paxos
- 5 副本，多数 Quorum
- Master Lease（12s）保证 Master 唯一
- 客户端 caching + 通知失效
- 用于 Bigtable、GFS 选主

### 5.5.2 Google Spanner

- 跨地域数据库
- 每个 Tablet 一个 Paxos Group（5 副本，跨 DC）
- Paxos 复制 + TrueTime 跨 Group 事务
- 详见 [§8 存储-Spanner](./08-存储-Spanner与NewSQL.md)

### 5.5.3 Microsoft Azure Cosmos DB

- Multi-Master Paxos 变种
- 五种一致性级别（Strong / Bounded staleness / Session / Consistent prefix / Eventual）

### 5.5.4 Phxpaxos（微信）

- 开源 C++ Paxos 实现
- 工程化日志压缩、成员变更

### 5.5.5 ⚠️ 工业陷阱

- ⚠️ **磁盘 fsync**：未 fsync 的 Promise/Accept 状态故障后丢失，违反 Safety
- ⚠️ **Leader 选举抖动**：Failure Detector 灵敏度过高
- ⚠️ **日志压缩误删**：未确认 committed 的日志被截断
- ⚠️ **成员变更期间故障**：需特别处理

---

## 5.6 面试要点

### ⭐ Q1：Paxos 的三角色是什么？

**骨架**：

1. Proposer：提议值，推动共识
2. Acceptor：对提议投票，多数同意即决定
3. Learner：学习已决定的值

**关键点**：职责分工

**加分项**：提到实际部署中一个节点可扮演多角色

### ⭐ Q2：Basic Paxos 的两阶段流程？

**骨架**：

1. **Prepare**：Proposer 发 Prepare(n)，Acceptor 回 Promise(n, accepted_proposal)（若 n 大于已 promise 的）
2. **Accept**：Proposer 用 Promise 带回的最大编号值（或自己的值）发 Accept(n, v)，Acceptor 接受并回 Accepted

**关键点**：两阶段 + Promise 带回已接受值

**加分项**：提到 majority 要求

### ⭐ Q3：Paxos 如何保证 Safety？

**骨架**：

1. P2c 不变式：高编号提议的值必为已 chosen 值
2. Phase 1 的 Promise 带回已接受的最大编号值
3. Proposer 必须用该值
4. 故 chosen 后不可变更

**关键点**：P2c + Promise 机制

**加分项**：提到 majority 交集

### ⭐ Q4：Multi-Paxos 优化了什么？

**骨架**：

1. 选稳定 Leader
2. Leader 一次性 Prepare 所有日志位置
3. 后续只走 Accept（1 RTT）
4. Leader 故障则新 Leader 重新 Prepare

**关键点**：单 Leader 跳过 Prepare

**加分项**：提到日志空洞问题

### ⭐ Q5：Paxos 的 Live-lock 问题怎么解决？

**骨架**：

1. 多 Proposer 竞争导致互相打断
2. 解决：选 Distinguished Proposer（单 Leader）
3. 随机化 backoff

**关键点**：单 Leader

**加分项**：提到 Multi-Paxos 的根本解决

### ⭐ Q6：Paxos 和 Raft 的区别？

**骨架**：

1. Paxos 更通用（Basic/Multi），Raft 专为日志复制
2. Paxos 不指定 Leader 选举，Raft 明确
3. Raft 强约束日志匹配，更易理解
4. Raft 有完善的成员变更、快照机制

**关键点**：易理解性 + 工程完整性

**加分项**：提到 Raft 的「易理解」是设计目标

### ⭐ Q7：Paxos 工程化有哪些难点？

**骨架**：

1. 磁盘持久化（fsync）
2. 日志空洞填充
3. 成员变更
4. Leader 选举
5. 快照与日志压缩
6. 客户端 Exactly-Once

**关键点**：理论→工程的 gap

**加分项**：提到 Chubby 工程经验

### ⭐ Q8：为什么 Paxos 被「神话」为最难？

**骨架**：

1. 原论文以「议会」故事隐喻，难懂
2. Basic + Multi + 成员变更分散
3. 工程化细节原论文未给
4. 后续论文 *Paxos Made Simple*、*Paxos Made Live* 缓解

**关键点**：表达问题 + 工程细节缺失

**加分项**：提到 Raft 的「为易理解而设计」

---

## 5.7 论文与延伸阅读

### 📜 经典论文

- 📜 [Lamport 1998] *The Part-Time Parliament* —— Paxos 原始论文
- 📜 [Lamport 2001] *Paxos Made Simple* —— 简化版
- 📜 [Chandra, Griesemer, Redstone 2007] *Paxos Made Live* —— Google Chubby 工程经验
- 📜 [Lamport 2005] *Generalized Consensus and Paxos* —— Generalized Consensus
- 📜 [Serafini et al. 2012] *EPaxos* —— 无 Leader Paxos

### 🔗 教材与资料

- 📖 Lynch《Distributed Algorithms》
- 🔗 [Paxos Lecture (Diego Ongaro)](https://www.youtube.com/watch?v=JEpsLvY9SVE)（访问于 2026-06-23）
- 🔗 [MIT 6.824 Lab 3 (Multi-Paxos)](https://pdos.csail.mit.edu/6.824/)（访问于 2026-06-23）

---

## 本章 TODO

- [ ] 补充图 5-3：Basic Paxos 时序图（含竞争场景）
- [ ] 补充图 5-4：Multi-Paxos 日志空洞场景
- [ ] 核对 P2c 不变式表述
- [ ] 交叉引用 [05-共识-Raft](./05-共识-Raft.md)

## 交叉引用

- **前置**：[05-共识-问题与FLP](./05-共识-问题与FLP.md)
- **后续**：[05-共识-Raft](./05-共识-Raft.md)
- **横向**：[05-共识-ZAB](./05-共识-ZAB.md)
- **横向**：[05-共识-对比与工业实战](./05-共识-对比与工业实战.md)
- **学术附录**：[§21 学术-FLP证明](./21-学术-FLP证明.md)

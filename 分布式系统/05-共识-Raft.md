# 第 5 章 共识 —— Raft

> **本章导读**
> Raft 是 Diego Ongaro 2014 年在博士论文中提出的共识算法，设计目标只有一个：**易理解**（Understandability）。Raft 与 Paxos 在 Safety 上等价，但通过三个解耦机制（Leader 选举、日志复制、安全性）让算法更易实现与教学。Raft 是 etcd、TiKV、Consul、CockroachDB 等工业系统的共识基础。本章覆盖 Leader 选举、日志复制、Safety 规则、成员变更、快照、工程实现。
>
> **学完能回答**：
> 1. Raft 的 Leader 选举流程？为什么用随机化超时？
> 2. 日志复制的 commit 规则？为什么不能直接 commit 旧任期日志？
> 3. Raft 的 Safety 五大规则？
> 4. 成员变更（Joint Consensus vs 单步变更）的区别？
> 5. Raft 的快照与日志压缩机制？
>
> **前置**：[05-共识-问题与FLP](./05-共识-问题与FLP.md)、[05-共识-Paxos](./05-共识-Paxos.md) · **预计时长**：5-6 小时 · **标记**：⭐🔥

---

## 5.0 章节地图（Raft 部分）

```
                       Raft
                        │
        ┌───────────────┼───────────────┐
        │               │               │
   Leader 选举      日志复制         Safety 规则
        │               │               │
   ┌────┼────┐     ┌────┴────┐    ┌────┴────┐
  随机超时 任期  AppendEntries  Commit  Election
   分裂投票        │              Restriction  Safety
                   │
              日志匹配
                   │
              成员变更 / 快照
```

- 5.1 Raft 概述
- 5.2 Leader 选举
- 5.3 日志复制
- 5.4 Safety 规则 ⭐
- 5.5 成员变更
- 5.6 日志压缩与快照
- 5.7 🎓 形式化与证明
- 5.8 🏭 工业实现
- 5.9 面试要点
- 5.10 论文与延伸阅读

---

## 5.1 Raft 概述

📜 [Ongaro, Ousterhout 2014] *In Search of an Understandable Consensus Algorithm*

### 5.1.1 设计目标

1. **Understandability**（易理解）——首要目标
2. **Correctness**（正确性）——证明严谨
3. **Performance**（性能）——与 Paxos 相当

### 5.1.2 三大子问题

Raft 将共识分解为三个独立子问题：

1. **Leader Election**（领导选举）：选一个 Leader
2. **Log Replication**（日志复制）：Leader 接受写，复制到 followers
3. **Safety**（安全性）：保证日志一致

### 5.1.3 节点状态

| 状态 | 职责 |
|------|------|
| **Follower** | 被动接受 Leader 的请求 |
| **Candidate** | 选举中，争取成为 Leader |
| **Leader** | 处理所有客户端写，复制日志 |

```
  启动
    │
    ▼
Follower ◄──── 超时 ────┐
    │                    │
    │ 选举超时            │
    ▼                    │
Candidate ──收到多数票──► Leader
    │                    │
    │ 发现更高 Term       │
    └────────────────────┘
```

### 5.1.4 Term（任期）

> **Term**：Raft 的逻辑时钟，单调递增。

- 每次 Leader 选举开启新 Term
- Term 充当「版本号」，过期的 Term 自动失效
- 节点看到更高 Term 时自动转为 Follower

---

## 5.2 Leader 选举 ⭐

### 5.2.1 心跳与超时

- Leader 周期性发 `AppendEntries`（heartbeat）维持 Leader 地位
- Follower 在 **election timeout** 内未收到心跳，转为 Candidate

### 5.2.2 选举流程

```pseudocode
# Follower
function onElectionTimeout():
    state = Candidate
    currentTerm += 1
    voteFor = self
    votes_received = 1  # 自己一票
    reset_election_timeout()
    
    # 并行发送 RequestVote RPC
    for each peer:
        async send(RequestVote(currentTerm, candidate_id, lastLogIndex, lastLogTerm))
    
    # 等待多数票
    wait until (votes_received >= majority) or (received higher term) or (election timeout)

# 收到 RequestVote
function onReceiveRequestVote(req):
    if req.term < currentTerm:
        return Reply(voteGranted=false)
    if req.term > currentTerm:
        currentTerm = req.term
        state = Follower
        voteFor = None
    
    # 检查是否可投票
    if voteFor == None or voteFor == req.candidate_id:
        if is_log_up_to_date(req.lastLogIndex, req.lastLogTerm):
            voteFor = req.candidate_id
            reset_election_timeout()
            return Reply(voteGranted=true)
    
    return Reply(voteGranted=false)
```

### 5.2.3 「Log Up-to-Date」判定 ⭐

> 候选人的日志必须**至少与多数派一样新**才能获票。

比较规则：

1. 比较 `lastLogTerm`：大的更新
2. 若 `lastLogTerm` 相同，比较 `lastLogIndex`：大的更新

**意义**：保证被选出的 Leader 含所有已 committed 日志（见 §5.4 Election Safety）。

### 5.2.4 随机化 Election Timeout ⭐

> **关键**：每个节点的 election timeout **随机化**（如 150-300ms），避免多个节点同时发起选举导致分裂投票。

```pseudocode
election_timeout = random(150ms, 300ms)
```

**作用**：

- 大概率只有一个节点先超时，发起选举
- 获得多数票，成为 Leader
- 避免无限 live-lock

🔥 **工程要点**：

- spread 范围需大于广播时间（避免选举期间心跳被延迟）
- 实际取值需基于网络 RTT 实测

### 5.2.5 选举结果

- Candidate 收到 majority 票 → 转 Leader，立即发心跳
- Candidate 收到更高 Term → 转 Follower
- Election timeout 到，无 majority → 新 Term 重新选举

---

## 5.3 日志复制

### 5.3.1 日志结构

```
日志: [(term=1, cmd=A), (term=1, cmd=B), (term=2, cmd=C), (term=3, cmd=D)]
索引:   1              2              3              4
```

每条日志项包含：

- **term**：写入时的 Leader 任期
- **cmd**：客户端命令

### 5.3.2 复制流程 ⭐

```pseudocode
# Leader 接收客户端写
function onClientWrite(cmd):
    # 1. 本地追加日志
    log.append(currentTerm, cmd)
    index = log.last_index
    
    # 2. 并行发 AppendEntries 给所有 followers
    for each follower:
        async send(AppendEntries(
            term=currentTerm,
            leader_id=self,
            prevLogIndex=nextIndex[follower] - 1,
            prevLogTerm=log.term_at(nextIndex[follower] - 1),
            entries=log[from nextIndex[follower] to end],
            leaderCommit=commitIndex
        ))
    
    # 3. 等待多数 ACK
    wait until (majority replicated at index)
    
    # 4. 提交
    commitIndex = max(commitIndex, index)
    
    # 5. 应用到状态机
    apply_to_state_machine(log[commitIndex])
    
    # 6. 返回客户端

# Follower 接收 AppendEntries
function onReceiveAppendEntries(req):
    if req.term < currentTerm:
        return Reply(success=false, term=currentTerm)
    
    # 重置选举超时（收到 Leader 心跳）
    reset_election_timeout()
    
    # 检查日志匹配
    if log.term_at(req.prevLogIndex) != req.prevLogTerm:
        return Reply(success=false)  # 日志不连续，要求 Leader 回退
    
    # 删除冲突日志，追加新日志
    for each entry in req.entries:
        if log[entry.index] exists and log[entry.index].term != entry.term:
            # 冲突，删除此位置及之后所有
            log.truncate(entry.index)
        log.append(entry)
    
    # 更新 commitIndex
    if req.leaderCommit > commitIndex:
        commitIndex = min(req.leaderCommit, log.last_index)
        apply_to_state_machine()
    
    return Reply(success=true)
```

### 5.3.3 日志匹配属性 ⭐

> **Log Matching Property**：
> 1. 若两条日志项 index 与 term 相同，则 cmd 相同
> 2. 若两条日志项 index 与 term 相同，则之前所有日志项也相同

**保证机制**：

- AppendEntries 携带 `prevLogIndex` 和 `prevLogTerm`
- Follower 验证匹配后才追加
- 不匹配则拒绝，Leader 退回 `nextIndex` 重试

### 5.3.4 nextIndex 与 matchIndex ⭐

Leader 为每个 follower 维护：

- `nextIndex[i]`：下次发给 follower $i$ 的日志索引
- `matchIndex[i]`：已确认与 follower $i$ 匹配的最大索引

**初始化**：新 Leader 上任时，`nextIndex[i] = log.last_index + 1`，`matchIndex[i] = 0`。

**更新**：

- AppendEntries 成功 → `matchIndex[i] = entries.last_index`, `nextIndex[i] = matchIndex[i] + 1`
- AppendEntries 失败 → `nextIndex[i] -= 1`，重试

### 5.3.5 Commit 规则 ⭐⚠️

> **关键**：Leader 只 commit 当前 term 的日志，不直接 commit 旧 term 日志。

**为什么**：

考虑场景：

```
Term 2 Leader 写入 idx=3 (term=2)，未 commit 就故障
Term 3 Leader 选出，复制 idx=3 到多数，但未 commit
Term 3 Leader 故障
Term 4 选举：某 follower 可能没有 idx=3，但被选为 Leader（满足 majority）
若新 Leader 直接 commit idx=3，会覆盖已被其他节点应用的日志 → Safety 违反
```

**解决**：Raft 规定 Leader 只能 commit **当前 term** 的日志。当当前 term 日志被 commit 时，之前所有日志也被 commit（通过 Log Matching Property）。

### 5.3.6 Commit 流程

```pseudocode
# Leader 周期性检查
function update_commit():
    for N in range(log.last_index, commitIndex, -1):
        if log[N].term != currentTerm:
            continue  # 不能直接 commit 旧 term
        
        # 统计有多少 follower matchIndex >= N
        count = 1  # 自己
        for each follower:
            if matchIndex[follower] >= N:
                count += 1
        
        if count >= majority:
            commitIndex = N
            apply_to_state_machine()
            break
```

---

## 5.4 Safety 规则 ⭐

Raft 通过五大 Safety 规则保证正确性：

### 5.4.1 Election Safety

> **每个 Term 至多一个 Leader 被选出**

**保证**：

- 一个 Term 内一个节点最多投一票（voteFor 唯一）
- 获 majority 票才能当 Leader
- 故一个 Term 至多一个 Leader

### 5.4.2 Leader Append-Only

> **Leader 永不修改或删除自己的日志，只追加**

**保证**：

- Leader 不接受其他 Leader 的 AppendEntries
- 客户端写只追加

### 5.4.3 Log Matching

> **若两条日志项 index 与 term 相同，则 cmd 与之前所有日志项都相同**

**保证**：见 §5.3.3。

### 5.4.4 Leader Completeness ⭐

> **若日志项在某 Term 被 committed，则所有更高 Term 的 Leader 必含该日志项**

**保证**：

- Election Safety + Log Up-to-Date 判定
- 被选为 Leader 的节点其日志至少与 majority 一样新
- 而 committed 日志已在 majority 中
- 故 Leader 必含 committed 日志

**证明直觉**：

1. 设日志项 $e$ 在 Term $T$ 被 committed
2. 则 $e$ 在 majority $M$ 中
3. 设 Term $T' > T$ 的 Leader 被选出
4. 该 Leader 获 majority $M'$ 票
5. $M$ 与 $M'$ 必有交集（majority 交集）
6. 交集中的节点既含 $e$ 又投票
7. Log Up-to-Date 判定要求投票时候选人日志至少一样新
8. 故候选人必含 $e$

### 5.4.5 State Machine Safety

> **若某节点在 index $i$ 应用了 cmd $v$，则其他节点在 index $i$ 不会应用不同 cmd**

**保证**：由 Leader Completeness + Log Matching 推出。

---

## 5.5 成员变更

### 5.5.1 问题

成员变更的核心风险：**新旧配置同时存在时，可能选出两个 Leader**。

```
旧配置 (3 节点): A B C
新配置 (5 节点): A B C D E

切换瞬间：
- 旧配置中 A B 选出新 Leader（majority=2）
- 新配置中 C D E 选出另一 Leader（majority=3）
→ 脑裂
```

### 5.5.2 Joint Consensus（联合共识）📜

两阶段切换：

1. **Joint Config**：新旧配置同时生效，任何决策需新旧各自 majority
2. **New Config**：仅新配置生效

```pseudocode
function change_membership(new_members):
    # 1. 提议联合配置 (Cold ∪ Cnew)
    send_joint_config(Cold, Cnew)
    # 等待联合配置 committed
    
    # 2. 提议新配置 (Cnew)
    send_new_config(Cnew)
    # 等待新配置 committed
```

**保证**：联合配置期间，任何决策需新旧各 majority，避免脑裂。

### 5.5.3 单步成员变更 ⭐

📜 [Diego Ongaro PhD Thesis]：

> **思想**：每次只增删一个节点，保证新旧配置的 majority 必有交集。

**例**：3 节点 → 4 节点（加 1 个）

- 旧 majority: 2/3
- 新 majority: 3/4
- 交集：至少 1 个共同节点

**优势**：

- 单阶段，无需联合共识
- 实现简单
- etcd、TiKV 采用此方案

**限制**：每次只能增删一个节点，多次变更需串行。

### 5.5.4 ⚠️ 成员变更陷阱

- ⚠️ **不能直接切换配置**：会脑裂
- ⚠️ **新节点初始无日志**：需先加入为「non-voting member」，追平日志后转 voting
- ⚠️ **移除 Leader**：Leader 需先转为 follower 再下线
- 🔥 **生产建议**：用单步变更，逐步操作

---

## 5.6 日志压缩与快照

### 5.6.1 问题

日志无限增长：

- 磁盘占用
- 重启回放时间长
- 新加入节点需全量同步

### 5.6.2 快照机制

```pseudocode
function take_snapshot():
    # 1. 序列化当前状态机
    state = state_machine.serialize()
    
    # 2. 记录已应用位置
    last_included_index = commitIndex
    last_included_term = log[commitIndex].term
    
    # 3. 写入快照文件
    write_snapshot(state, last_included_index, last_included_term)
    
    # 4. 截断已快照的日志
    log.discard_up_to(last_included_index)
```

### 5.6.3 InstallSnapshot RPC

新加入节点或落后太多时，Leader 发送快照：

```pseudocode
# Leader
function sync_follower(follower):
    if nextIndex[follower] <= log.discarded_index:
        # 日志已被截断，需发快照
        send(InstallSnapshot(last_included_index, last_included_term, snapshot_data))
    else:
        send(AppendEntries(...))

# Follower
function onReceiveInstallSnapshot(req):
    # 接收快照
    write_snapshot(req.data)
    
    # 截断冲突日志
    if log[req.last_included_index].term == req.last_included_term:
        # 部分匹配，保留之后日志
        log.discard_up_to(req.last_included_index)
    else:
        # 全部丢弃
        log.discard_all()
    
    # 加载快照到状态机
    state_machine.load(req.data)
    commitIndex = req.last_included_index
    lastApplied = req.last_included_index
```

### 5.6.4 快照策略 🔥

| 策略 | 触发条件 | 适用 |
|------|---------|------|
| **基于日志大小** | 日志超过 N MB | 通用 |
| **基于时间** | 每 N 分钟 | 中等更新频率 |
| **基于命令数** | 每 N 条命令 | 高频写入 |

⚠️ **陷阱**：

- ⚠️ 快照期间阻塞写（需异步快照）
- ⚠️ 快照过大导致传输慢（需分块）
- ⚠️ 快照与日志的原子性（先写新快照再删旧日志）

---

## 5.7 🎓 形式化与证明

### 5.7.1 Leader Completeness 证明

见 §5.4.4。

### 5.7.2 与 Paxos 等价性

- Raft 与 Multi-Paxos 在 Crash-Stop + $f < n/2$ 下表达能力等价
- Raft 的「易理解」通过约束 Leader 强模型实现
- Paxos 更通用（允许多 Proposer），但工程化复杂

### 5.7.3 Raft 不保证 Liveness

- FLP 限制下不能保证 Termination
- 但随机化 election timeout 使「无限不终止」概率为 0
- 实际系统视为「几乎必然终止」

---

## 5.8 🏭 工业实现

### 5.8.1 etcd 🔥

- Go 实现，K8s 底层依赖
- Raft + MVCC + Watch
- Lease Read + ReadIndex 保证线性一致读
- 详见 [§10 协调服务](./10-协调服务.md)

### 5.8.2 TiKV 🔥

- Rust 实现，TiDB 底层
- **Multi-Raft**：每个 Region 一个 Raft Group
- PD（Placement Driver）调度 Region
- 详见 [§8 存储](./08-存储-Spanner与NewSQL.md)

### 5.8.3 Consul

- Go 实现，服务发现 + KV
- 类似 etcd

### 5.8.4 CockroachDB

- Go 实现，分布式 SQL
- Range-level Raft Group

### 5.8.5 etcd-raft 实现细节 🔥

- **WAL**：日志持久化（fsync）
- **Snapshot**：周期性压缩
- **Lease Read**：Leader 不走 Raft 协议的快速读
- **ReadIndex**：保证线性一致读（确认仍是 Leader + commitIndex 一致）
- **PreVote**：避免网络分区节点回归时触发选举

### 5.8.6 ⚠️ 工业陷阱

- ⚠️ **Election timeout 过小**：网络抖动触发频繁选举
- ⚠️ **Snapshot 阻塞写**：需异步快照
- ⚠️ **未 fsync WAL**：故障后丢日志，违反 Safety
- ⚠️ **成员变更并发**：多个变更同时进行会脑裂
- ⚠️ **跨地域 Raft**：RTT 高导致 election timeout 难调
- 🔥 **生产建议**：

  - election timeout = 10 × 网络 P99 RTT
  - heartbeat interval = election timeout / 5
  - WAL fsync 开启
  - 异步快照 + 流式传输
  - 跨地域用 Multi-Raft 而非单 Raft

---

## 5.9 面试要点

### ⭐ Q1：Raft 的 Leader 选举流程？

**骨架**：

1. Follower 在 election timeout 内未收到心跳 → 转 Candidate
2. currentTerm++，自投一票，并行发 RequestVote
3. 收到 majority 票 → 转 Leader，立即发心跳
4. 收到更高 Term → 转 Follower
5. Election timeout 到，无 majority → 新 Term 重选

**关键点**：random election timeout 避免分裂投票

**加分项**：提到 Log Up-to-Date 判定

### ⭐ Q2：为什么 Raft 用随机化 election timeout？

**骨架**：

1. 避免多个节点同时发起选举（分裂投票）
2. 大概率只有一个节点先超时
3. 获多数票成为 Leader
4. 避免无限 live-lock

**关键点**：避免分裂投票

**加分项**：提到 timeout 范围需大于广播时间

### ⭐ Q3：Raft 日志复制的流程？

**骨架**：

1. 客户端写 → Leader 本地追加日志
2. 并行发 AppendEntries 给 followers
3. 等待 majority ACK
4. Leader commit + apply
5. 下次 AppendEntries 通知 followers commit

**关键点**：majority ACK 后 commit

**加分项**：提到 nextIndex/matchIndex

### ⭐ Q4：Raft 为什么不能直接 commit 旧 term 日志？

**骨架**：

1. 旧 term 日志可能在新 term 被覆盖
2. 若直接 commit 旧 term 日志，可能违反 Safety
3. 只 commit 当前 term 日志，通过 Log Matching 间接 commit 之前

**关键点**：避免 Safety 违反

**加分项**：能描述具体场景（图 5.9 of Raft paper）

### ⭐ Q5：Raft 的 Log Matching Property？

**骨架**：

1. 若两条日志项 index + term 相同，则 cmd 相同
2. 若两条日志项 index + term 相同，则之前所有日志项也相同

**保证**：AppendEntries 携带 prevLogIndex/prevLogTerm，Follower 验证匹配

**关键点**：prevLogIndex + prevLogTerm 验证

**加分项**：提到不匹配时 nextIndex 回退

### ⭐ Q6：Raft 的 Leader Completeness 怎么保证？

**骨架**：

1. Election Safety：一个 Term 一个 Leader
2. Log Up-to-Date 判定：候选人日志至少与 majority 一样新
3. committed 日志在 majority 中
4. majority 之间有交集
5. 故被选 Leader 必含 committed 日志

**关键点**：majority 交集 + Log Up-to-Date

**加分项**：完整证明思路

### ⭐ Q7：Raft 成员变更为什么有脑裂风险？

**骨架**：

1. 新旧配置同时存在
2. 旧配置 majority 与新配置 majority 可能无交集
3. 各自选 Leader → 脑裂
4. 解决：Joint Consensus（两阶段）或单步变更

**关键点**：majority 可能无交集

**加分项**：提到单步变更的限制（每次只能 1 节点）

### ⭐ Q8：Raft 和 Paxos 的区别？

**骨架**：

1. Paxos 更通用（多 Proposer），Raft 强 Leader
2. Raft 三子问题解耦（选举/复制/Safety）
3. Raft 明确 Leader 选举、成员变更、快照
4. Raft 设计目标：易理解

**关键点**：易理解 + 工程完整

**加分项**：提到 Safety 上等价

### ⭐ Q9：Raft 快照机制？

**骨架**：

1. 周期性快照状态机
2. 截断已快照的日志
3. 新节点或落后节点用 InstallSnapshot 同步
4. 异步快照避免阻塞写

**关键点**：快照 + 日志截断 + InstallSnapshot

**加分项**：提到快照原子性

### ⭐ Q10：etcd 的线性一致读怎么实现？

**骨架**：

1. **ReadIndex**：读前 Leader 确认仍是 Leader（一轮心跳）+ 读 commitIndex
2. **Lease Read**：Leader 在 Lease 内无需 ReadIndex，直接读
3. 都保证读到最新已 commit 数据

**关键点**：ReadIndex 或 Lease Read

**加分项**：提到 Lease Read 的风险（时钟漂移）

---

## 5.10 论文与延伸阅读

### 📜 经典论文

- 📜 [Ongaro, Ousterhout 2014] *In Search of an Understandable Consensus Algorithm* —— Raft 原始论文
- 📜 [Ongaro 2014] *Consensus: Bridging Theory and Practice*（PhD Thesis）—— Raft 完整版 + 成员变更 + 快照
- 📜 [Howard et al. 2015] *Raft Refloated: Do We Have Consensus?* —— Raft 实现差异

### 🔗 教材与资料

- 📖 [The Raft Paper](https://raft.github.io/raft.pdf)（访问于 2026-06-23）
- 📖 [Raft Consensus Algorithm](https://raft.github.io/)（访问于 2026-06-23）—— 可视化
- 🔗 [MIT 6.824 Lab 6 (Raft)](https://pdos.csail.mit.edu/6.824/)（访问于 2026-06-23）
- 🔗 [etcd-raft 文档](https://pkg.go.dev/go.etcd.io/etcd/raft)（访问于 2026-06-23）

---

## 本章 TODO

- [ ] 补充图 5-5：Raft 状态机转换图
- [ ] 补充图 5-6：日志复制时序图（含冲突回退）
- [ ] 补充图 5-7：单步成员变更 majority 交集示意
- [ ] 补充图 5-8：Snapshot 与 InstallSnapshot 流程
- [ ] 核对 etcd PreVote 与 CheckQuorum 机制
- [ ] 交叉引用 [§10 协调服务](./10-协调服务.md)（etcd 实战）
- [ ] 交叉引用 [§8 存储-Spanner与NewSQL](./08-存储-Spanner与NewSQL.md)（TiKV Multi-Raft）

## 交叉引用

- **前置**：[05-共识-问题与FLP](./05-共识-问题与FLP.md)
- **前置**：[05-共识-Paxos](./05-共识-Paxos.md)（对比理解）
- **横向**：[05-共识-ZAB](./05-共识-ZAB.md)
- **横向**：[05-共识-拜占庭共识](./05-共识-拜占庭共识.md)
- **横向**：[05-共识-对比与工业实战](./05-共识-对比与工业实战.md)
- **应用**：[§10 协调服务](./10-协调服务.md)（etcd）
- **应用**：[§8 存储-Spanner与NewSQL](./08-存储-Spanner与NewSQL.md)（TiKV）

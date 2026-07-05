# 第 6 章 分布式事务（四）—— Percolator 与 Spanner

> **本章导读**
> 2PC/TCC/Saga 是「应用层」事务方案，Percolator 与 Spanner 是「存储层」事务方案——数据库原生支持跨节点 ACID。Percolator（Google 2010）用 Bigtable + 乐观锁 + 时间戳实现跨行事务，是 TiDB 事务的基础。Spanner（Google 2012）用 Paxos + TrueTime + 2PC 实现全球外部一致性，是 NewSQL 的标杆。本章深入两者设计与实现。
>
> **学完能回答**：
> 1. Percolator 的三角色（Client/Tso/Worker）？
> 2. Percolator 的乐观锁 + 时间戳如何工作？
> 3. Spanner 的 TrueTime + Commit Wait 如何实现外部一致性？
> 4. Spanner 的跨 Paxos Group 2PC 流程？
> 5. Percolator 与 Spanner 的本质区别？
>
> **前置**：[06-事务-2PC与3PC](./06-事务-2PC与3PC.md)、[第 3 章 时间与时钟](./03-时间与时钟.md)、[第 5 章 共识](./05-共识-Paxos.md) · **预计时长**：3-4 小时 · **标记**：⭐🔥🎓

---

## 6.0 章节地图（本文件）

```
       Percolator 与 Spanner
              │
        ┌─────┴─────┐
        │           │
    Percolator   Spanner
        │           │
   ┌────┼────┐  ┌───┴───┐
  Bigtable 乐观  TrueTime  跨 Group
  + 锁 + 时间戳   Paxos    2PC
        │           │
   ┌────┴────┐  Commit Wait
  Client TSO  Snapshot Isolation
  Worker
        │
       TiDB
```

- 6.1 Percolator 概述
- 6.2 Percolator 数据模型
- 6.3 Percolator 事务流程 ⭐
- 6.4 Percolator 故障恢复
- 6.5 Spanner 概述
- 6.6 Spanner 数据模型
- 6.7 Spanner 事务流程 ⭐
- 6.8 Spanner 外部一致性 ⭐
- 6.9 🎓 Calvin 确定性事务
- 6.10 🏭 工业实战（TiDB / CockroachDB）
- 6.11 面试要点
- 6.12 论文与延伸阅读

---

## 6.1 Percolator 概述

📜 [Peng, Dabek 2010] *Large-scale Incremental Processing Using Distributed Transactions and Notifications*

### 6.1.1 背景

Google 索引系统从 MapReduce 迁移到 Percolator：

- MapReduce：批处理，索引延迟高（天级）
- Percolator：增量处理，索引延迟降到分钟级

### 6.1.2 设计目标

- 跨行、跨表 ACID 事务
- 基于 Bigtable（无原生事务）
- 适合 PB 级数据
- 高吞吐增量处理

### 6.1.3 核心思想

> 用 **时间戳 + 乐观锁** 在 Bigtable 之上实现 ACID 事务。事务分两阶段：Prewrite（写锁）+ Commit（写时间戳，释放锁）。

---

## 6.2 Percolator 数据模型

### 6.2.1 Bigtable 多版本

Percolator 基于 Bigtable，每行数据按时间戳多版本存储：

```
Bigtable:
┌────────────────┬────────────┬──────────────────────────┐
│ Row Key        │ Column     │ Cell (value, timestamp)  │
├────────────────┼────────────┼──────────────────────────┤
│ user:alice     │ balance    │ (100, ts=7)              │
│                │            │ (90, ts=5)               │
│                │ data:lock  │ (tx_id_1, ts=7) ← 锁     │
│                │ data:write │ (tx_id_1, ts=7) ← 提交记录│
└────────────────┴────────────┴──────────────────────────┘
```

### 6.2.2 三类列 ⭐

每行数据有三种特殊列：

| 列 | 作用 |
|----|------|
| **data** | 实际数据（多版本） |
| **write** | 提交记录（指向 data 的版本） |
| **lock** | 锁标记（事务进行中） |

### 6.2.3 时间戳服务（TSO）

> **Timestamp Oracle（TSO）**：全局单调递增时间戳分配器。

- 单点（但通过 Paxos HA）
- 批量分配（提高吞吐）
- 每个事务获取两个时间戳：start_ts（读）+ commit_ts（写）

---

## 6.3 Percolator 事务流程 ⭐

### 6.3.1 两阶段：Prewrite + Commit

```
Client                          Bigtable            TSO
   │                                │                 │
   │ 1. get start_ts                │                 │
   ├────────────────────────────────────────────────► │
   │◄───────────────────────────────────────────── ts │
   │                                │                 │
   │ 2. 读取数据（基于 start_ts）   │                 │
   ├──────────────────────────────►│                 │
   │◄──────────────────────────────│                 │
   │                                │                 │
   │ 3. Prewrite（写锁 + 写数据）   │                 │
   ├──────────────────────────────►│                 │
   │◄──────────────────────────────│                 │
   │                                │                 │
   │ 4. get commit_ts               │                 │
   ├────────────────────────────────────────────────► │
   │◄───────────────────────────────────────────── ts │
   │                                │                 │
   │ 5. Commit（写 write 列，清锁） │                 │
   ├──────────────────────────────►│                 │
   │◄──────────────────────────────│                 │
```

### 6.3.2 详细流程

#### 步骤 1：获取 start_ts

```pseudocode
function begin_transaction():
    start_ts = tso.get_timestamp()
    return start_ts
```

#### 步骤 2：读取（基于 start_ts 的快照）

```pseudocode
function get(row, column, start_ts):
    # 检查是否有锁（其他事务进行中）
    lock = bigtable.get(row, "lock", column)
    if lock exists and lock.ts > start_ts:
        # 锁是后续事务的，不影响
        pass
    elif lock exists:
        # 锁是当前事务或未完成事务
        wait_or_resolve(lock)
    
    # 从 write 列找 ≤ start_ts 的最新提交
    write_record = bigtable.get(row, "write", column, ts <= start_ts)
    if write_record:
        # 写记录指向 data 版本
        data_ts = write_record.data_ts
        return bigtable.get(row, "data", column, ts=data_ts)
    else:
        return None  # 无数据
```

#### 步骤 3：Prewrite（写锁 + 写数据）

```pseudocode
function prewrite(row, column, value, start_ts, primary_key):
    # 1. 冲突检查：是否有 ≥ start_ts 的写
    if bigtable.exists(row, "write", column, ts >= start_ts):
        abort("write conflict")
    
    # 2. 冲突检查：是否有锁
    if bigtable.exists(row, "lock", column):
        abort("lock conflict")
    
    # 3. 写数据（用 start_ts 作为版本）
    bigtable.write(row, "data", column, value, ts=start_ts)
    
    # 4. 写锁（标记事务进行中）
    bigtable.write(row, "lock", column, {
        primary: primary_key,  # 主键
        start_ts: start_ts,
        kind: PREWRITE
    }, ts=start_ts)
```

#### 步骤 4：Commit

```pseudocode
function commit(writes, start_ts):
    # 1. 获取 commit_ts
    commit_ts = tso.get_timestamp()
    
    # 2. 提交主键（primary）
    primary = writes[0]
    bigtable.write(primary.row, "write", primary.column, {
        start_ts: start_ts,
        commit_ts: commit_ts
    }, ts=commit_ts)
    bigtable.delete(primary.row, "lock", primary.column)
    
    # 3. 提交次键（secondaries），异步
    for each secondary in writes[1:]:
        bigtable.write(secondary.row, "write", secondary.column, {
            start_ts: start_ts,
            commit_ts: commit_ts
        }, ts=commit_ts)
        bigtable.delete(secondary.row, "lock", secondary.column)
    
    return commit_ts
```

### 6.3.3 主键（Primary Key）的作用 ⭐

> Percolator 事务中，**第一个写操作的 key 是主键**。事务的 commit/abort 由主键决定。

- Prewrite 阶段：所有 key 都尝试加锁
- Commit 阶段：先 commit 主键（标记事务已提交），再异步 commit 次键
- 故障恢复：检查主键状态决定事务是 commit 还是 abort

### 6.3.4 事务隔离级别

> Percolator 实现 **Snapshot Isolation（SI）**：
> - 读：基于 start_ts 的快照
> - 写：检查无冲突（≥ start_ts 的写）
> - 不解决 Write Skew 异常（详见 §8）

---

## 6.4 Percolator 故障恢复 ⭐

### 6.4.1 锁清理

Client 崩溃可能留下未清理的锁。其他事务遇到这些锁时需「清理」：

```pseudocode
function cleanup_lock(lock):
    primary_key = lock.primary
    # 检查主键状态
    primary_write = bigtable.get(primary_key.row, "write", primary_key.column)
    primary_lock = bigtable.get(primary_key.row, "lock", primary_key.column)
    
    if primary_write exists:
        # 主键已 commit，事务已提交
        # 次键也 commit
        commit_secondary(lock)
    elif primary_lock exists:
        # 主键仍锁定，事务未完成
        # 决定 rollback
        rollback(lock)
    else:
        # 主键无 write 无 lock，事务 abort
        rollback(lock)
```

### 6.4.2 Rollback

```pseudocode
function rollback(lock):
    # 删除数据 + 删除锁
    bigtable.delete(lock.row, "data", lock.column, ts=lock.start_ts)
    bigtable.delete(lock.row, "lock", lock.column)
```

### 6.4.3 Commit 次键（主键已提交）

```pseudocode
function commit_secondary(lock):
    bigtable.write(lock.row, "write", lock.column, {
        start_ts: lock.start_ts,
        commit_ts: tso.get_timestamp()
    })
    bigtable.delete(lock.row, "lock", lock.column)
```

---

## 6.5 Spanner 概述

📜 [Corbett et al. 2012] *Spanner: Google's Globally-Distributed Database*

### 6.5.1 设计目标

- 全球分布
- 跨地域 ACID 事务
- 外部一致性（External Consistency）
- 高可用

### 6.5.2 核心机制

1. **Paxos Group**：每个 Tablet 一个 Paxos Group（5 副本跨 DC）
2. **TrueTime**：GPS + 原子钟提供时间区间
3. **2PC**：跨 Paxos Group 事务
4. **Commit Wait**：保证外部一致性

---

## 6.6 Spanner 数据模型

### 6.6.1 层级结构

```
Universe（全球）
  │
  ├─ Zone 1（数据中心 1）
  │    ├─ Tablet 1 (Paxos Group 1)
  │    ├─ Tablet 2 (Paxos Group 2)
  │    └─ ...
  ├─ Zone 2
  ├─ Zone 3
  └─ ...
```

### 6.6.2 数据分片

- 数据按 Key Range 分片为 **Tablet**
- 每个 Tablet 由一个 **Paxos Group** 复制
- Tablet 在不同 Zone（DC）有副本

### 6.6.3 目录（Directory）

> Spanner 的数据迁移单位是 **Directory**（一组连续 Key）。

- Directory 可在 Tablet 间迁移
- 跨地域访问时，Directory 可移到就近 Zone

---

## 6.7 Spanner 事务流程 ⭐

### 6.7.1 事务类型

| 类型 | 适用 | 机制 |
|------|------|------|
| **读写事务** | 跨行写 | 2PC + Paxos |
| **只读事务** | 跨行读 | 快照读（基于时间戳） |
| **DML** | 单行 | 单 Paxos Group |

### 6.7.2 读写事务（2PC + Paxos）⭐

```
Client                    Coordinator Group    Participant Groups
   │                            │                       │
   │ 1. Acquire locks           │                       │
   ├──────────────────────────────────────────────────► │
   │                            │                       │
   │ 2. Prepare (Paxos)         │                       │
   ├───────────────────────────►│◄──────────────────────┤
   │                            │                       │
   │ 3. Select commit_ts        │                       │
   │   (TrueTime)               │                       │
   │                            │                       │
   │ 4. Commit Wait             │                       │
   │   (wait TT.after(s))       │                       │
   │                            │                       │
   │ 5. Commit (Paxos)          │                       │
   ├───────────────────────────►├──────────────────────►│
   │                            │                       │
   │ 6. Release locks           │                       │
```

### 6.7.3 详细流程

#### 步骤 1：获取锁

```pseudocode
function begin_rw_transaction():
    # 在所有涉及的 Key 上获取锁
    for each key in transaction.writes:
        acquire_lock(key)
    # 读取（基于当前 Paxos Group 状态）
    for each key in transaction.reads:
        read(key)
```

#### 步骤 2：Prepare（每个 Paxos Group）

```pseudocode
function prepare():
    # 选 Coordinator Group
    coordinator = select_coordinator(writes)
    
    for each paxos_group in writes.groups:
        # 在该 Group 内 Paxos 复制 prepare 记录
        prepare_ts = paxos_group.last_assigned_ts + 1
        paxos_group.append(prepare_entry, prepare_ts)
    
    # 返回 prepare_ts 给 Coordinator
    return prepare_ts
```

#### 步骤 3：Coordinator 选 commit_ts ⭐

```pseudocode
function select_commit_ts(prepare_tss):
    # commit_ts 必须：
    # 1. 大于所有 prepare_ts
    # 2. 大于 TrueTime.now().latest
    s = max(max(prepare_tss), TT.now().latest)
    return s
```

#### 步骤 4：Commit Wait ⭐

```pseudocode
function commit_wait(s):
    # 等待 TrueTime 确认 s 已过
    while not TT.after(s):
        sleep(epsilon)
    # 现在 s < 真实时间，可安全 commit
```

#### 步骤 5：Commit

```pseudocode
function commit(s):
    # Coordinator Paxos 复制 commit 记录
    coordinator.paxos_append(commit_entry, ts=s)
    
    # 通知所有 Participant Groups
    for each group in participants:
        group.paxos_append(commit_entry, ts=s)
        group.release_locks()
```

### 6.7.4 只读事务（快照读）

```pseudocode
function read_transaction():
    # 选读时间戳
    read_ts = TT.now().latest  # 安全的读时间戳
    
    for each key in reads:
        # 在对应 Paxos Group 上做快照读
        value = paxos_group.read(key, ts=read_ts)
    
    # 无需锁，无需 2PC
```

---

## 6.8 Spanner 外部一致性 ⭐🎓

### 6.8.1 外部一致性定义

> 若事务 $T_1$ 在 $T_2$ 开始前完成，则任何事务看到的顺序中 $T_1$ 在 $T_2$ 之前。

### 6.8.2 Commit Wait 的作用

**问题**：

- Coordinator 选 commit_ts $s$ 基于 TrueTime
- TrueTime 有误差 $\epsilon$
- 若不等待，可能 $s > t_{true}$（s 还未到）
- 后续事务 $T_2$ 可能选更小 commit_ts，违反外部一致性

**解决**：

- Commit Wait：等待 $TT.after(s)$ 为 true
- 保证 $s < t_{true}$
- 后续事务 $T_2$ 的 commit_ts 必 > $s$

### 6.8.3 证明直觉

假设 $T_1$ 在 $T_2$ 开始前完成：

1. $T_1$ commit_ts = $s_1$，等待 $TT.after(s_1)$ 后返回
2. 此时真实时间 $t > s_1$
3. $T_2$ 开始时间 $t_{start} > t > s_1$
4. $T_2$ commit_ts $s_2 \geq TT.now().latest \geq t_{start} > s_1$
5. 故 $s_1 < s_2$，外部一致性成立

### 6.8.4 Safe Read Timestamp

只读事务选 `read_ts = TT.now().latest`，保证：

- 读到所有 commit_ts ≤ read_ts 的事务
- 不会读到未完成事务
- 满足外部一致性

---

## 6.9 🎓 Calvin 确定性事务

📜 [Thomson, Abadi 2012] *Calvin: Fast Distributed Transactions for Partitioned Database Systems*

### 6.9.1 思路

> Calvin 在事务执行前**先确定全局顺序**，再并行执行。避免锁与 2PC。

### 6.9.2 流程

1. **Sequencer**：收集事务请求，确定全局顺序
2. **Scheduler**：按顺序获取锁
3. **Worker**：并行执行

### 6.9.3 优势

- 确定性，无锁竞争
- 适合多副本（同一顺序在所有副本执行）

### 6.9.4 限制

- 需预知读写集（不支持动态查询）
- 不适合长事务

---

## 6.10 🏭 工业实战

### 6.10.1 TiDB（Percolator 风格）🔥

- TiDB 事务层基于 Percolator
- TiKV 替代 Bigtable（多 Region + Raft）
- TSO 单点（PD 内）+ HA
- 提供 SI 隔离级别

```go
// TiDB 事务示例
err = db.Transaction(func(tx *sql.Tx) error {
    _, err := tx.Exec("UPDATE account SET balance = balance - 100 WHERE id = ?", from)
    if err != nil { return err }
    _, err = tx.Exec("UPDATE account SET balance = balance + 100 WHERE id = ?", to)
    return err
})
// 内部走 Percolator 协议：
# 1. 客户端从 PD 获取 start_ts
# 2. 读取数据（基于 start_ts 快照）
# 3. Prewrite（写锁 + 写数据）到 TiKV
# 4. 获取 commit_ts
# 5. Commit（写 write 列，清锁）
```

### 6.10.2 CockroachDB（HLC 风格）

- 类似 Spanner，但用 HLC 替代 TrueTime
- 无需专用硬件
- Range-level Raft Group

### 6.10.3 Spanner（Google 内部）

- 全球部署
- 支持 SQL（Spanner SQL）
- 用于 Google AdWords、Play 等

### 6.10.4 YugabyteDB

- PostgreSQL 兼容
- 类似 Spanner 架构
- 开源

### 6.10.5 ⚠️ 工业陷阱

- ⚠️ **TSO 单点**：Percolator 的 TSO 需 HA
- ⚠️ **Commit Wait 延迟**：Spanner 跨地域写延迟增加 $\epsilon$
- ⚠️ **锁冲突**：高并发写场景需优化
- ⚠️ **Snapshot Isolation 的 Write Skew**：需 Serializable 隔离
- 🔥 **生产建议**：

  - 跨地域用 Spanner/CockroachDB
  - 单地域用 TiDB
  - 评估 Commit Wait 延迟是否可接受

---

## 6.11 面试要点

### ⭐ Q1：Percolator 的数据模型？

**骨架**：

1. 基于 Bigtable，每行三类列：data / write / lock
2. data：实际数据，多版本
3. write：提交记录，指向 data 版本
4. lock：事务进行中的锁标记

**关键点**：三列设计 + 多版本

**加分项**：提到 TSO 提供时间戳

### ⭐ Q2：Percolator 事务的两阶段流程？

**骨架**：

1. **Prewrite**：冲突检查 → 写数据（start_ts 版本）→ 写锁
2. **Commit**：获取 commit_ts → 写 write 列（主键先）→ 清锁 → 异步 commit 次键

**关键点**：Prewrite + Commit + 主键

**加分项**：提到主键决定事务状态

### ⭐ Q3：Percolator 的主键（Primary Key）作用？

**骨架**：

1. 第一个写操作的 key 是主键
2. Commit 阶段先 commit 主键（标记事务已提交）
3. 故障恢复时检查主键状态决定 commit/abort

**关键点**：主键决定事务状态

**加分项**：提到故障恢复流程

### ⭐ Q4：Percolator 的故障恢复？

**骨架**：

1. Client 崩溃留下锁
2. 其他事务遇到锁时清理
3. 检查主键：已 commit 则 commit 次键；未 commit 则 rollback

**关键点**：主键状态决定次键命运

**加分项**：提到 lock 的 primary 字段

### ⭐ Q5：Spanner 如何实现外部一致性？

**骨架**：

1. TrueTime 提供时间区间 $TT.now() = [earliest, latest]$
2. Commit Wait：commit 后等待 $TT.after(s)$ 为 true
3. 保证 $s < t_{true}$
4. 后续事务 commit_ts 必 > $s$，外部一致性成立

**关键点**：TrueTime + Commit Wait

**加分项**：完整证明思路

### ⭐ Q6：Spanner 的跨 Paxos Group 事务流程？

**骨架**：

1. 获取锁
2. Prepare：每个 Group Paxos 复制 prepare 记录
3. Coordinator 选 commit_ts（max(prepare_ts, TT.latest)）
4. Commit Wait
5. Commit：Coordinator + 所有 Participant Paxos 复制 commit
6. 释放锁

**关键点**：2PC + Paxos

**加分项**：提到 Coordinator 的角色

### ⭐ Q7：Percolator 与 Spanner 的本质区别？

**骨架**：

1. Percolator：单地域，TSO 时间戳，乐观锁，SI 隔离
2. Spanner：跨地域，TrueTime 时间区间，Paxos + 2PC，外部一致性
3. Percolator 适合 PB 级单地域，Spanner 适合全球

**关键点**：时间机制 + 地域范围

**加分项**：提到 TiDB 基于 Percolator，CockroachDB 类似 Spanner

### ⭐ Q8：Percolator 实现什么隔离级别？

**骨架**：

1. **Snapshot Isolation（SI）**
2. 读：基于 start_ts 快照
3. 写：检查无 ≥ start_ts 的写
4. **不解决 Write Skew 异常**

**关键点**：SI + Write Skew 缺陷

**加分项**：提到 Serializable SI（SSI）解决方案

### ⭐ Q9：Spanner 的 Commit Wait 为什么必要？

**骨架**：

1. commit_ts $s$ 基于 TrueTime，有误差
2. 若不等待，可能 $s > t_{true}$
3. 后续事务可能选更小 commit_ts，违反外部一致性
4. 等待 $TT.after(s)$ 保证 $s < t_{true}$

**关键点**：TrueTime 误差

**加分项**：提到 $\epsilon$ 典型 7ms

### ⭐ Q10：TiDB 事务基于什么？

**骨架**：

1. 基于 Percolator
2. TiKV 替代 Bigtable（多 Region + Raft）
3. TSO 单点（PD 内）+ HA
4. SI 隔离级别

**关键点**：Percolator 思想 + TiKV 实现

**加分项**：提到与 Spanner 的对比

---

## 6.12 论文与延伸阅读

### 📜 经典论文

- 📜 [Peng, Dabek 2010] *Large-scale Incremental Processing Using Distributed Transactions and Notifications* —— Percolator
- 📜 [Corbett et al. 2012] *Spanner: Google's Globally-Distributed Database* —— Spanner
- 📜 [Thomson, Abadi 2012] *Calvin: Fast Distributed Transactions for Partitioned Database Systems* —— Calvin
- 📜 [Cowling, Myers 2013] *Spanner, Tamper, and the CAP Theorem* —— Spanner 与 CAP

### 🔗 教材与资料

- 📖 [TiDB 事务模型](https://docs.pingcap.com/zh/tidb/stable/transaction-overview)（访问于 2026-06-23）
- 🔗 [CockroachDB 事务架构](https://www.cockroachlabs.com/docs/stable/architecture/transaction-layer.html)（访问于 2026-06-23）

---

## 本章 TODO

- [ ] 补充图 6-10：Percolator 三列数据模型
- [ ] 补充图 6-11：Percolator 两阶段事务时序
- [ ] 补充图 6-12：Spanner 跨 Paxos Group 2PC
- [ ] 补充图 6-13：Commit Wait 与外部一致性
- [ ] 核对 TiDB 最新事务实现
- [ ] 交叉引用 [§8 存储-Spanner与NewSQL](./08-存储-Spanner与NewSQL.md)

## 交叉引用

- **前置**：[06-事务-2PC与3PC](./06-事务-2PC与3PC.md)
- **前置**：[第 3 章 时间与时钟](./03-时间与时钟.md)（TrueTime + HLC）
- **前置**：[第 5 章 共识](./05-共识-Paxos.md)（Paxos Group）
- **后续**：[06-事务-工业实战与选型](./06-事务-工业实战与选型.md)
- **横向**：[§8 存储-Spanner与NewSQL](./08-存储-Spanner与NewSQL.md)（Spanner/TiDB/CockroachDB）

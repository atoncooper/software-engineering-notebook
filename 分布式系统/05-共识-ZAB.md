# 第 5 章 共识 —— ZAB（ZooKeeper Atomic Broadcast）

> **本章导读**
> ZAB 是 ZooKeeper 专用的共识协议，与 Raft 同期独立设计，思想高度相似（强 Leader + 任期 + 日志复制）。但 ZAB 引入了 **zxid**（64 位事务 ID，含 epoch + counter），并在 Phase 类别、Leader 选举规则上与 Raft 有细节差异。理解 ZAB 是理解 ZooKeeper 内部机制（Watcher、Ephemeral Node、Session）的前提。
>
> **学完能回答**：
> 1. ZAB 与 Raft 的核心区别？
> 2. zxid 的结构？为什么用 zxid 而非 (term, index)？
> 3. ZAB 的两个阶段（Discovery + Sync + Broadcast）？
> 4. ZAB 的 Leader 选举规则？
> 5. ZooKeeper 的写为什么线性一致，读默认不是？
>
> **前置**：[05-共识-Raft](./05-共识-Raft.md) · **预计时长**：2-3 小时 · **标记**：⭐🔥

---

## 5.0 章节地图（ZAB 部分）

```
                      ZAB
                       │
        ┌──────────────┼──────────────┐
        │              │              │
     zxid 结构     两阶段流程      Leader 选举
        │              │              │
   ┌────┼────┐    ┌────┴────┐    ┌────┴────┐
  epoch counter  Discovery  Sync  最近 zxid
                  Broadcast       最大优先
```

- 5.1 ZAB 概述
- 5.2 zxid 结构
- 5.3 阶段流程
- 5.4 Leader 选举
- 5.5 与 Raft 对比
- 5.6 🏭 ZooKeeper 实现
- 5.7 面试要点
- 5.8 论文与延伸阅读

---

## 5.1 ZAB 概述

📜 [Junqueira, Reed, Serafini 2008] *Zab: A High-throughput Broadcast Protocol*
📜 [Junqueira, Reed, Serafini 2011] *Zab: High-performance broadcast for primary-backup systems*

### 5.1.1 设计目标

- 为 ZooKeeper 量身定制
- 主备复制（Primary-Backup）+ 全序广播
- 高吞吐、低延迟
- 快速故障恢复

### 5.1.2 与 Raft 的相似

- 强 Leader 模型
- Leader 任期（epoch）机制
- 多数 Quorum
- 日志复制 + Log Matching

### 5.1.3 与 Raft 的差异

| 维度 | Raft | ZAB |
|------|------|-----|
| 编号 | (term, index) 分离 | zxid = (epoch, counter) 复合 |
| 阶段名 | Leader Election + Log Replication | Discovery + Sync + Broadcast |
| 阶段数 | 2 | 3 |
| Leader 选举规则 | Log Up-to-Date（term 优先，index 次之） | zxid 最大优先 |
| 客户端 cmd 编号 | Leader 决定 | Leader 预分配 zxid |
| 同步方式 | AppendEntries 推 | Follower 拉取 + Leader 推 |

---

## 5.2 zxid 结构 ⭐

### 5.2.1 64 位复合 ID

```
zxid (64 bit)
┌────────────┬────────────────────┐
│  epoch     │  counter           │
│  (高 32 位) │  (低 32 位)         │
└────────────┴────────────────────┘
```

- **epoch**：Leader 任期（类似 Raft term）
- **counter**：该 epoch 内事务的递增序号

### 5.2.2 性质

- zxid 全局单调递增
- 同一 epoch 内 counter 递增
- epoch 切换时 counter 清零（新 Leader 重新计数）
- 比较 zxid 即可比较事件先后

### 5.2.3 与 (term, index) 的对比

| 维度 | (term, index) | zxid |
|------|---------------|------|
| 表示 | 元组 | 单一 64 位整数 |
| 比较 | 先 term 后 index | 直接整数比较 |
| 紧凑 | 较长 | 紧凑 |
| 应用 | Raft | ZAB |

**优势**：zxid 单整数比较更高效，便于 ZooKeeper 全局顺序保证。

---

## 5.3 阶段流程 ⭐

### 5.3.1 三阶段

```
启动 / Leader 故障
        │
        ▼
┌─────────────────┐
│ Phase 1: Discovery │  发现阶段：发现最大 zxid，选 Leader
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Phase 2: Sync   │  同步阶段：Leader 与 Follower 同步日志
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Phase 3: Broadcast │  广播阶段：处理客户端写
└─────────────────┘
```

### 5.3.2 Phase 1: Discovery（发现）

```pseudocode
# Follower
function discovery():
    # 向所有节点询问 epoch
    for each peer:
        send(CEPOCH, my_epoch)
    
    # 收集响应，找出最大 epoch
    max_epoch = max(responses.epochs)
    
    # 选 zxid 最大的节点为 Leader
    leader = max_by_zxid(peers)
    
    # 新 epoch = max_epoch + 1
    new_epoch = max_epoch + 1
    send(NEWEPOCH, leader, new_epoch)
```

**目的**：

- 发现集群中最大 zxid（最新数据）
- 选该节点为 Leader（保证 Leader 含所有已 committed 日志）
- 分配新 epoch

### 5.3.3 Phase 2: Sync（同步）

```pseudocode
# Leader
function sync():
    # 接受 followers 的 NEWLEADER 请求
    # 为每个 follower 计算 diff
    for each follower:
        diff = compute_diff(follower.last_zxid, leader.last_zxid)
        send(diff or snapshot)
    
    # 等待 majority follower ACK
    wait until majority_acked()
    
    # 进入 Broadcast 阶段
    send(NEWLEADER, committed=true)
```

**同步方式**：

- **DIFF**：发送缺失的日志
- **TRUNC**：截断 follower 多余的未 committed 日志
- **SNAP**：发送全量快照（follower 落后太多）

### 5.3.4 Phase 3: Broadcast（广播）

```pseudocode
# Leader 接收客户端写
function onClientWrite(cmd):
    # 1. 分配 zxid
    zxid = (epoch, counter++)
    
    # 2. 本地追加日志（待 ACK）
    log.append(zxid, cmd, state=PENDING)
    
    # 3. 发 PROPOSAL 给 followers
    for each follower:
        send(PROPOSAL, zxid, cmd)

# Follower 接收 PROPOSAL
function onReceiveProposal(req):
    log.append(req.zxid, req.cmd, state=PENDING)
    send(ACK, req.zxid)

# Leader 收到 ACK
function onReceiveAck(ack):
    if majority_acked(ack.zxid):
        # 4. 发 COMMIT
        log.update(ack.zxid, state=COMMITTED)
        for each follower:
            send(COMMIT, ack.zxid)
        # 5. 应用到状态机
        apply(log[ack.zxid])
```

⚠️ **关键**：ZAB 的两步广播（PROPOSAL → ACK → COMMIT）类似 2PC，但 Leader 多数 ACK 即 commit（非全部）。

---

## 5.4 Leader 选举

### 5.4.1 触发

- Leader 故障（Follower 检测心跳超时）
- 集群启动

### 5.4.2 选举规则 ⭐

> **规则**：选 **zxid 最大**的节点为 Leader。
> zxid 相同时，选 **myid**（节点 ID）最大的。

```pseudocode
function leader_election():
    while not leader_elected:
        # 1. 向所有节点发 VOTE
        send(VOTE, my_id, my_zxid)
        
        # 2. 收集投票
        for each peer:
            vote = receive(VOTE)
            if (vote.zxid > my_zxid) or
               (vote.zxid == my_zxid and vote.id > my_id):
                # 改投对方
                my_vote = vote
        
        # 3. 检查是否过半
        if count_votes(my_vote) >= majority:
            leader = my_vote.id
            leader_elected = true
```

### 5.4.3 与 Raft Log Up-to-Date 对比

| 维度 | Raft | ZAB |
|------|------|-----|
| 比较对象 | (lastLogTerm, lastLogIndex) | zxid |
| 比较方式 | term 优先，index 次之 | zxid 整数比较 |
| 等价性 | 等价 | 等价 |

二者本质相同：保证被选 Leader 含所有已 committed 日志。

---

## 5.5 与 Raft 对比

### 5.5.1 详细对比表

| 维度 | Raft | ZAB |
|------|------|-----|
| **设计目标** | 易理解 | 高吞吐 |
| **任期** | term | epoch |
| **编号** | (term, index) | zxid (epoch, counter) |
| **Leader 选举** | random timeout | 同步询问 |
| **选举规则** | Log Up-to-Date | zxid 最大 |
| **复制方向** | Leader 推 | Leader 推 + Follower 拉 |
| **阶段** | Election + Replication | Discovery + Sync + Broadcast |
| **客户端写** | AppendEntries 含 cmd | PROPOSAL → ACK → COMMIT |
| **成员变更** | Joint Consensus / 单步 | 手动 + 重启 |
| **快照** | InstallSnapshot | SNAP |
| **典型系统** | etcd, TiKV | ZooKeeper |

### 5.5.2 本质相似性

- 都是强 Leader + 多数 Quorum + 日志复制
- 都满足 Safety（Leader Completeness + Log Matching）
- 都依赖部分同步假设绕过 FLP
- 性能相近

### 5.5.3 设计哲学差异

| Raft | ZAB |
|------|-----|
| 易理解优先 | 性能优先 |
| 简化算法描述 | 优化具体场景 |
| 通用共识 | 为 ZooKeeper 量身定制 |

---

## 5.6 🏭 ZooKeeper 实现

### 5.6.1 数据模型

- **ZNode**：树形结构节点（类似文件系统）
  - Persistent：持久（客户端断开不删）
  - Ephemeral：临时（客户端 Session 失效即删）
  - Sequential：顺序节点（自动追加递增编号）
- **Watcher**：客户端可注册一次性监听
- **zxid**：每个 ZNode 变更都有对应 zxid

### 5.6.2 一致性保证 ⭐⚠️

| 操作 | 一致性 |
|------|-------|
| **写**（create/set/delete） | **线性一致**（过 ZAB 共识） |
| **读**（get/exists） | **顺序一致**（本地读，可能旧值） |
| **sync 后读** | 线性一致 |

⚠️ **关键陷阱**：ZooKeeper 的读默认是 **本地读**，可能读到旧值。要线性一致读需 `sync()` 后再读。

### 5.6.3 Session 机制

- 客户端与服务器建立 Session
- Session 有 timeout（心跳维持）
- Session 失效时，所有 Ephemeral 节点删除
- Session 是 ZooKeeper 实现分布式锁、领导选举的基础

### 5.6.4 Watcher 机制 ⚠️

- **一次性触发**：Watcher 触发后需重新注册
- **顺序保证**：客户端看到的 Watcher 事件顺序与 ZAB 顺序一致
- ⚠️ **羊群效应**：大量客户端 Watch 同一节点，事件触发时全部唤醒 → 性能问题
  - 解决：用「顺序节点 + Watch 前一个节点」实现公平锁

### 5.6.5 ⚠️ 工业陷阱

- ⚠️ **读旧值**：默认本地读，金融场景需 `sync()`
- ⚠️ **羊群效应**：避免 Watch 同一节点
- ⚠️ **Session 超时调优**：过短触发频繁重连，过长故障检测慢
- ⚠️ **GC 长暂停**：JVM Full GC 会让 Session 失效
- ⚠️ **集群规模限制**：ZooKeeper 不适合大规模集群（5-7 节点典型）
- 🔥 **生产建议**：

  - 用 Curator 库（封装好分布式锁、领导选举）
  - 避免在 ZNode 存大数据（< 1MB）
  - Watcher 用一次性 + 重注册模式

---

## 5.7 面试要点

### ⭐ Q1：ZAB 和 Raft 的区别？

**骨架**：

1. 编号：Raft (term, index)，ZAB zxid (epoch, counter)
2. 阶段：Raft 2 阶段，ZAB 3 阶段（Discovery + Sync + Broadcast）
3. 选举规则：本质相同（zxid 最大 ≡ Log Up-to-Date）
4. 哲学：Raft 易理解，ZAB 高吞吐

**关键点**：本质相似，细节差异

**加分项**：提到 ZAB 为 ZooKeeper 量身定制

### ⭐ Q2：zxid 是什么？

**骨架**：

1. 64 位复合 ID
2. 高 32 位 epoch，低 32 位 counter
3. 全局单调递增
4. 比较 zxid 即比较事件先后

**关键点**：epoch + counter 复合结构

**加分项**：提到与 (term, index) 的对比

### ⭐ Q3：ZAB 的三阶段是什么？

**骨架**：

1. **Discovery**：发现最大 zxid，选 Leader
2. **Sync**：Leader 与 Follower 同步日志（DIFF/TRUNC/SNAP）
3. **Broadcast**：处理客户端写（PROPOSAL → ACK → COMMIT）

**关键点**：三阶段名 + 流程

**加分项**：提到 Sync 的三种同步方式

### ⭐ Q4：ZooKeeper 的读写一致性？

**骨架**：

1. 写：线性一致（过 ZAB 共识）
2. 读默认：顺序一致（本地读，可能旧值）
3. sync 后读：线性一致

**关键点**：读默认非线性一致

**加分项**：提到生产陷阱

### ⭐ Q5：ZooKeeper 的羊群效应是什么？怎么解决？

**骨架**：

1. 大量客户端 Watch 同一节点
2. 节点变化时全部唤醒 → 性能问题
3. 解决：顺序节点 + Watch 前一个节点（公平锁）

**关键点**：Watch 前一个节点

**加分项**：提到 Curator 库封装

### ⭐ Q6：ZAB 的 Leader 选举规则？

**骨架**：

1. 选 zxid 最大的节点
2. zxid 相同选 myid 最大的
3. 等价于 Raft 的 Log Up-to-Date

**关键点**：zxid 最大优先

**加分项**：证明等价性

### ⭐ Q7：ZooKeeper 的 Ephemeral 节点有什么用？

**骨架**：

1. 临时节点，Session 失效即删
2. 用于：分布式锁、领导选举、服务发现
3. 客户端断开 → Session 失效 → 节点删除 → 释放锁

**关键点**：Session 绑定

**加分项**：提到与持久节点的区别

### ⭐ Q8：ZooKeeper 的 Watcher 是一次性的吗？

**骨架**：

1. **一次性触发**
2. 触发后需重新注册
3. 保证事件顺序与 ZAB 顺序一致

**关键点**：一次性 + 重注册

**加分项**：提到 Curator 的 Cache 模式自动重注册

---

## 5.8 论文与延伸阅读

### 📜 经典论文

- 📜 [Junqueira, Reed, Serafini 2008] *Zab: A High-throughput Broadcast Protocol*
- 📜 [Junqueira, Reed, Serafini 2011] *Zab: High-performance broadcast for primary-backup systems*
- 📜 [Hunt, Konar, Junqueira, Reed 2010] *ZooKeeper: Wait-free coordination for Internet-scale systems*

### 🔗 教材与资料

- 🔗 [ZooKeeper Official Docs](https://zookeeper.apache.org/doc/current/)（访问于 2026-06-23）
- 🔗 [Apache Curator](https://curator.apache.org/)（访问于 2026-06-23）
- 🔗 [ZAB vs Raft 对比](https://arxiv.org/abs/2104.05055)（访问于 2026-06-23）

---

## 本章 TODO

- [ ] 补充图 5-9：ZAB 三阶段流程图
- [ ] 补充图 5-10：zxid 结构示意
- [ ] 核对 ZAB 与 Raft 在成员变更上的差异
- [ ] 交叉引用 [§10 协调服务](./10-协调服务.md)（ZooKeeper 工业实践）
- [ ] 交叉引用 [§11 分布式锁](./11-分布式锁.md)（ZooKeeper 锁实现）

## 交叉引用

- **前置**：[05-共识-Raft](./05-共识-Raft.md)
- **前置**：[05-共识-Paxos](./05-共识-Paxos.md)
- **横向**：[05-共识-拜占庭共识](./05-共识-拜占庭共识.md)
- **横向**：[05-共识-对比与工业实战](./05-共识-对比与工业实战.md)
- **应用**：[§10 协调服务](./10-协调服务.md)（ZooKeeper）
- **应用**：[§11 分布式锁](./11-分布式锁.md)（ZooKeeper 锁）

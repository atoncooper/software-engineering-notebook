# 存储系统 —— Spanner 与 NewSQL

> 章号: §8.4
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业 🎓学术
> 前置: [[03-时间与时钟]] [[05-共识-Paxos]] [[06-事务-Percolator与Spanner]] [[08-3-存储-Bigtable与HBase]]

---

## 0. NewSQL 的诞生动机

数据库的发展经历三个阶段:

1. **RDBMS (1970s~)**:强一致 + SQL + ACID,但单机扩展受限
2. **NoSQL (2000s)**:高可用 + 横向扩展,但放弃 SQL 和事务(Bigtable/Dynamo)
3. **NewSQL (2010s)**:兼具 SQL + ACID + 横向扩展

很多业务既要 RDBMS 的强一致事务和 SQL,又要 NoSQL 的水平扩展:

- 金融核心交易(强一致 + 高 QPS)
- 电商订单(分布式事务 + 海量数据)
- 全球化应用(跨 Region 强一致)

Google **Spanner** (2012) 是 NewSQL 的奠基,核心创新:

- **TrueTime API**:用 GPS/原子钟解决分布式时钟问题
- **Paxos Group + 2PC**:每个分片一个 Paxos Group,跨分片用 2PC
- **External Consistency**:线性一致 + 全序(基于 TrueTime)
- **Global SQL**:跨 Region 分布式 SQL

开源后裔:CockroachDB、TiDB、YugabyteDB。

---

## 1. 定义与动机

### 1.1 Spanner 的目标

> 📜 Chang et al., 2012 OSDI — *Spanner: Google's Globally-Distributed Database*

1. **全球分布**:数据跨多个数据中心,故障时自动切换
2. **外部一致性 (External Consistency)**:线性一致 + 全序(严格强于线性一致)
3. **SQL + ACID**:完整的 RDBMS 语义
4. **横向扩展**:PB 级数据,千节点规模

### 1.2 为什么传统方案不行

| 方案 | 局限 |
|------|------|
| RDBMS 主从复制 | 主挂切换数据可能丢;跨 Region 强同步延迟高 |
| Bigtable + 应用层事务 | 应用层实现复杂,正确性难保证 |
| Dynamo/Cassandra | 最终一致,不适合金融 |
| 2PC 跨分片 | 阻塞、单点,工程上难用 |

Spanner 的方案:用 Paxos 解决单分片高可用 + 2PC 解决跨分片原子性 + TrueTime 解决时钟问题。

### 1.3 NewSQL vs NoSQL vs RDBMS

| 维度 | RDBMS | NoSQL | NewSQL |
|------|-------|-------|--------|
| SQL | ✓ | ✗ | ✓ |
| ACID | ✓ | 弱/无 | ✓ |
| 横向扩展 | ✗ | ✓ | ✓ |
| 跨 Region | ✗ | ✓ | ✓ |
| 强一致 | ✓ | 弱 | ✓ |
| 性能 | 中 | 高 | 中-高 |

---

## 2. Spanner 原理与架构

### 2.1 整体架构

```
                ┌──────────────────────────────┐
                │  Application (F1/SQL)        │
                └──────────────────────────────┘
                              ↓
                ┌──────────────────────────────┐
                │  Spanner SQL Layer           │
                │  (查询解析、优化、分布式执行)   │
                └──────────────────────────────┘
                              ↓
                ┌──────────────────────────────┐
                │  Tablet (分片) + Paxos Group  │
                │  每个 Tablet 一个 Paxos Group  │
                └──────────────────────────────┘
                              ↓
                ┌──────────────────────────────┐
                │  Colossus (底层存储)          │
                └──────────────────────────────┘
                              ↑
                ┌──────────────────────────────┐
                │  TrueTime API                │
                │  (GPS + Atomic Clock)        │
                └──────────────────────────────┘
```

### 2.2 Tablet 与 Paxos Group

- **Tablet**:Spanner 的数据分片,类似 Bigtable Tablet,按 key 范围划分
- 每个 Tablet 对应一个 **Paxos Group**(3 或 5 副本,跨数据中心分布)
- Tablet Leader 处理写,任意副本可读(默认读 Leader 保证线性一致)

```
Tablet A: keys [a, m)
  Replica 1 (DC1) - Leader
  Replica 2 (DC2) - Follower
  Replica 3 (DC3) - Follower
  → Paxos Group

Tablet B: keys [m, z)
  Replica 1 (DC2) - Leader
  Replica 2 (DC3) - Follower
  Replica 3 (DC1) - Follower
  → Paxos Group
```

### 2.3 TrueTime

> 📜 Spanner 论文核心创新

#### 2.3.1 问题

传统 NTP 时钟:

- 漂移大(几十毫秒)
- 不确定性大(无法精确知道当前时间)
- 无法支持"基于时间戳的线性一致"

#### 2.3.2 TrueTime API

```cpp
TT.now()   : [earliest, latest]  // 返回区间,真实时间必在其中
TT.after(t): bool                // t 之前是否已确定发生
TT.before(t): bool               // t 之后是否已确定发生
```

TrueTime 保证区间宽度 $\epsilon < 7ms$(典型 4ms),通过:

- 每个 DC 配 GPS 接收器 + 原子钟
- 时间服务器对比多源,取最可信
- 客户端定期同步,估计漂移

#### 2.3.3 Commit Wait

事务提交时:

```
1. 选 timestamp s = TT.now().latest + 待提交的 commit 队列中已有的时间偏移
2. 等 Commit Wait:TT.after(s) == true (即真实时间已超过 s)
3. 此时所有副本都能确定 s 是唯一可能的时间戳,可以提交
```

Commit Wait 等待时间 $\approx \epsilon$,典型 7ms。代价是增加事务延迟,换来 external consistency。

### 2.4 事务模型

详见 [[06-事务-Percolator与Spanner]]。Spanner 事务分两类:

#### 2.4.1 读写事务 (跨 Tablet)

```
1. Client 加锁(写锁):
   - 对要写的 Tablet 选 Leader,发 acquire lock 请求
   - Leader 通过 Paxos 复制锁
2. Client 选 timestamp:
   - 选所有参与 Tablet Leader 的 lastAssignedTimestamp 最大值 + 1
3. Client 发 commit:
   - 对所有 Tablet 发 commit(timestamp=s)
   - 每个 Tablet Leader 通过 Paxos 复制 commit log
4. Tablet Leader 等待 Commit Wait (TT.after(s))
5. Tablet Leader 应用 commit,释放锁
6. Client 收到 ACK
```

#### 2.4.2 只读事务 (Snapshot Read)

```
1. Client 选 timestamp t = TT.now().latest
   (或 Tablet 自治分配)
2. 在每个 Tablet 的 Leader 或 Replica 上读 t 时刻的快照
3. 无需加锁、无需 2PC,直接读
```

> 🎓 关键:Snapshot Read 利用 TrueTime 的"外部一致性",t 之后开始的写不会影响 t 时刻的读。

### 2.5 外部一致性证明

> 📜 Spanner 论文 Section 4

**定理**:Spanner 提供 external consistency,即:若事务 $T_1$ 在 $T_2$ 开始前完成,则 $T_1$ 的 timestamp $s_1 < s_2$。

证明 sketch:

1. $T_1$ 在 $T_2$ 开始前完成 → $T_1$ 的 commit 在 $T_2$ 开始前已被 Replica 看到
2. Replica 看到 $T_1$ commit 时,真实时间 $t > s_1$(因为 Commit Wait)
3. $T_2$ 的 timestamp $s_2 \geq$ TT.now().latest > $t > s_1$
4. 所以 $s_2 > s_1$

> 🎓 TrueTime + Commit Wait 是 Spanner 的核心魔法。Commit Wait 用 7ms 延迟换"时间戳严格全序"。

### 2.6 Spanner 数据模型

Spanner 支持两种模型:

#### 2.6.1 表 + Interleave (层级表)

```sql
CREATE TABLE Singers (
  SingerId INT64 NOT NULL,
  FirstName STRING(1024),
  LastName STRING(1024),
) PRIMARY KEY (SingerId);

CREATE TABLE Albums (
  SingerId INT64 NOT NULL,
  AlbumId INT64 NOT NULL,
  AlbumTitle STRING(MAX),
) PRIMARY KEY (SingerId, AlbumId),
INTERLEAVE IN PARENT Singers ON DELETE CASCADE;
```

Interleave 让子表的物理存储与父表相邻,提升 parent + child 联合查询性能。

#### 2.6.2 分片键

`PRIMARY KEY` 决定数据如何分布:

- 主键的第一列是分片键
- Spanner 自动按分片键分裂 Tablet

---

## 3. TiDB (PingCAP)

TiDB 是 Spanner 思想的开源实现,但有几个关键差异:

### 3.1 架构

```
              ┌────────────────────┐
              │  MySQL Client      │
              └────────────────────┘
                       ↓
              ┌────────────────────┐
              │  TiDB (SQL Layer)  │  ← 无状态,可水平扩展
              └────────────────────┘
                       ↓
              ┌────────────────────┐
              │  PD (Placement Driver)  │  ← 元数据、调度
              └────────────────────┘
                       ↓
              ┌─────────────────────────────────┐
              │  TiKV (Storage, Multi-Raft)     │  ← 每个 Range 一个 Raft Group
              └─────────────────────────────────┘
```

### 3.2 TiKV:Multi-Raft

- TiKV 把数据按 key range 切分为若干 Range(默认 96MB)
- 每个 Range 一个 Raft Group(3 副本)
- Raft Leader 分布在不同 TiKV 节点(避免单点过载)

```
Range 1 [a, m): Leader=TiKV1, Followers=[TiKV2, TiKV3]
Range 2 [m, z): Leader=TiKV2, Followers=[TiKV1, TiKV4]
Range 3 [z, ~): Leader=TiKV3, Followers=[TiKV4, TiKV5]
```

### 3.3 TiDB vs Spanner

| 维度 | Spanner | TiDB |
|------|---------|------|
| 时钟 | TrueTime (GPS+原子钟) | HLC (混合逻辑时钟,TSO) |
| 共识 | Paxos | Raft |
| Commit Wait | 有 (7ms) | 无 (依赖 TSO 单点) |
| 跨 Region | 原生支持 | 跨 DC 异步复制 |
| 协议 | 私有 | MySQL 兼容 |
| 存储 | Colossus | TiKV (RocksDB) |

### 3.4 TiDB 事务 (Percolator 风格)

TiDB 用 Percolator 风格事务(详见 [[06-事务-Percolator与Spanner]]):

- 客户端 2PC,PD 作为 TSO
- 数据列 + 锁列 + 写列(类似 Percolator)
- 单点 TSO 简化时钟(但 TSO 本身需要高可用)

### 3.5 TiDB 热点处理

```sql
-- 显式打散自增 ID
CREATE TABLE events (
    id BIGINT NOT NULL PRIMARY KEY CLUSTERED,
    data VARCHAR(255)
) SHARD_ROW_ID_BITS = 4 PRE_SPLIT_REGIONS = 4;
-- 4 位 shard → 16 个 pre-split region
```

PD 监控热点,自动分裂热 Region。

---

## 4. CockroachDB

CockroachDB (Cockroach Labs,2015) 是另一个 Spanner 开源实现,设计目标:

- **Geo-distributed**:跨 DC 强一致
- **Survive failures**:任一节点故障不影响
- **PostgreSQL compatible**:协议兼容

### 4.1 架构

```
CockroachDB 节点(对等):
  - 每节点承载多个 Range
  - 每 Range 一个 Raft Group
  - HLC 时钟(无 TrueTime)
  - 跨 Range 事务用 2PC + Parallel Commits
```

### 4.2 CockroachDB vs TiDB

| 维度 | CockroachDB | TiDB |
|------|-------------|------|
| 协议 | PostgreSQL | MySQL |
| 存储 | Pebble (RocksDB fork) | TiKV (RocksDB) |
| 事务 | HLC + Parallel Commits | TSO + Percolator |
| Geo | 原生多 Region 设计 | 跨 DC 异步 |
| 调度 | 内置 | 独立 PD |

### 4.3 CockroachDB 的 Parallel Commits

> 📜 CockroachDB 2020 — *Parallel Commits*

传统 2PC 有"stale write"问题:Phase 1 完成但 Phase 2 未发起,记录会停留(其他事务无法读)。

Parallel Commits:

1. Phase 1 同时发"staging"标记 + 实际写入
2. Phase 2 异步发"commit",记录立即对其他事务可见
3. 故障时通过 staging 状态恢复

降低事务延迟约 50%。

---

## 5. 🎓 学术深度

### 5.1 Spanner 论文的核心贡献

1. **TrueTime 工程化**:用硬件(GPS/原子钟)解决分布式时钟,理论上不可能(FLP)→ 工程上可行(部分同步假设)
2. **External Consistency**:线性一致 + 全序,严格强于传统线性一致
3. **Paxos + 2PC 组合**:单分片高可用 + 跨分片原子
4. **Commit Wait 数学证明**:用区间时钟实现严格全序

### 5.2 TrueTime 与 FLP 的关系

> FLP 说"异步系统无法达成共识"。TrueTime 假设"时钟有界漂移"(部分同步),绕过 FLP 限制。

- TrueTime 依赖时钟硬件(GPS/原子钟)
- 如果时钟真的"无界漂移"(假设失败),Spanner 安全性受损
- 工程上 GPS/原子钟的可靠性足够(年故障率 < 0.01%)

### 5.3 External Consistency vs Linearizability

| 一致性 | 含义 |
|-------|------|
| Linearizability | 单对象强一致,操作有全序 |
| Sequential Consistency | 单对象弱一致(允许 reorder) |
| External Consistency | 多对象 + 全序 + 实时顺序 |

External Consistency = Linearizability + Real-Time Order。Spanner 提供 external consistency,意味着跨多个对象的事务也严格按真实时间排序。

### 5.4 TSO (Timestamp Oracle) vs TrueTime

| 方案 | 实现 | 优势 | 劣势 |
|------|------|------|------|
| TrueTime (Spanner) | GPS + 原子钟 | 全局单调,无单点 | 硬件依赖 |
| TSO (TiDB) | 中心化时间戳分配 | 简单,无硬件 | 单点瓶颈(需 HA) |
| HLC (CockroachDB) | 混合逻辑时钟 | 去中心化 | 实现复杂 |

> 🎓 TSO 是工程上的妥协:用单点换简单,通过 PD 集群 Raft 保证 TSO HA。CockroachDB 用 HLC 彻底去中心化,但实现复杂。

### 5.5 Calvin:确定性事务

> 📜 Thomson et al., 2012 — *Calvin: Fast Distributed Transactions*

Calvin 是另一种 NewSQL 路径:

- 事务先进入全局 log(顺序确定)
- 各节点按 log 顺序执行(确定性)
- 无需 2PC、无需锁

优势:确定性、可恢复。劣势:不适合 ad-hoc 查询。

详见 [[06-事务-Percolator与Spanner]]。

### 5.6 FaunaDB

商业化的 Calvin 实现,提供 ACID + 横向扩展,但生态较小。

---

## 6. 🏭 工业实战

### 6.1 Spanner 工业应用

- Google AdWords:广告核心数据
- Google Play:应用元数据
- Google Photos:元数据(图片数据存 Colossus)
- YouTube:部分数据
- 第三方:通过 Spanner on GCP 使用

### 6.2 TiDB 工业应用

- 美团:核心交易、订单
- 拼多多:商品、订单
- 知乎:用户、内容
- B 站:视频元数据、用户
- 小红书:用户、内容

### 6.3 CockroachDB 工业应用

- Netflix:部分元数据
- DoorDash:订单
- Bose:全球库存
- 多家金融公司:核心交易

### 6.4 NewSQL 选型

| 场景 | 推荐 |
|------|------|
| 私有云 + MySQL 兼容 | TiDB |
| 私有云 + PostgreSQL 兼容 | CockroachDB / YugabyteDB |
| 云上全球分布 | GCP Spanner / CockroachCloud |
| 中小规模 | 传统 RDBMS + 读写分离 |
| 海量数据 + 弱一致 | Cassandra / HBase |
| 海量数据 + 强一致 + ACID | TiDB / CockroachDB |

### 6.5 NewSQL 不是银弹

NewSQL 的代价:

- **延迟**:跨 Region 强一致延迟高(秒级)
- **复杂度**:运维复杂,需理解分布式事务
- **成本**:多副本 + 共识开销,资源消耗大
- **生态**:不如传统 RDBMS 成熟(工具、培训)

适用场景:数据量 > 10TB、QPS > 10w、需要 ACID、跨 Region。其他场景传统 RDBMS 仍最优。

---

## 7. 面试要点

### 7.1 高频问答

**Q1: Spanner 的核心创新是什么?**

> (1) TrueTime API:用 GPS/原子钟提供有界时钟误差(<7ms);(2) Commit Wait:提交时等 Commit Wait,保证时间戳严格全序;(3) Paxos Group + 2PC:单分片高可用 + 跨分片原子;(4) External Consistency:线性一致 + 实时全序。

**Q2: Spanner 为什么需要 TrueTime?**

> 传统 NTP 时钟漂移大,无法保证跨节点时间戳全序。Spanner 需要外部一致性(实时顺序的事务全序),必须有时钟保证。TrueTime 用 GPS+原子钟把误差降到 7ms 以内,配合 Commit Wait 实现严格全序。

**Q3: Spanner 和 TiDB 的核心区别?**

> 时钟:Spanner 用 TrueTime(硬件),TiDB 用 TSO(中心化时间戳分配,PD 提供)。共识:Spanner 用 Paxos,TiDB 用 Raft。Commit Wait:Spanner 有(7ms),TiDB 无。跨 Region:Spanner 原生,TiDB 异步复制。TiDB 协议兼容 MySQL,Spanner 私有。

**Q4: Commit Wait 是什么?为什么需要?**

> 事务 commit 时,选 timestamp s = TT.now().latest,然后等 TT.after(s) = true(真实时间超过 s)再应用。需要因为:确保 s 之后开始的读不会读到旧值(外部一致性)。代价是增加 7ms 提交延迟,换来严格时间序。

**Q5: NewSQL 解决了什么问题?**

> 兼具 RDBMS 的 SQL+ACID 和 NoSQL 的横向扩展。传统 RDBMS 单机扩展受限,NoSQL 放弃事务/SQL。NewSQL 用分布式共识 + 分布式事务实现"强一致 + 横向扩展 + SQL"三合一,适合金融、电商核心等场景。

**Q6: TiDB 怎么处理热点?**

> (1) SHARD_ROW_ID_BITS:打散自增 ID;(2) PRE_SPLIT_REGIONS:建表时预分裂多个 Region;(3) PD 监控热点 Region 自动分裂;(4) 业务层 key 设计(避免单调自增)。详见 [[07-分片与路由]]。

**Q7: Spanner 的外部一致性怎么证明?**

> 若 T1 在 T2 开始前完成: T1 commit 时真实时间 t1 > s1(Commit Wait); T2 开始时真实时间 t2 > t1; T2 timestamp s2 >= TT.now().latest > t2 > t1 > s1; 所以 s2 > s1。

**Q8: TiDB、CockroachDB、Spanner 的选型?**

> 私有云 + MySQL 兼容 → TiDB;私有云 + PG 兼容 → CockroachDB;云上全球分布 → GCP Spanner。生态、协议、运维经验都是考量。

### 7.2 易错点 ⚠️

1. **"Spanner 是 NoSQL"** — 错。Spanner 提供 SQL + ACID,是 NewSQL。
2. **"TrueTime 是绝对时钟"** — 不是。TrueTime 返回区间,真实时间在区间内,有 7ms 不确定性。
3. **"NewSQL 总是比 RDBMS 好"** — 错。小规模场景 RDBMS 仍最优(简单、成熟、成本低)。
4. **"TiDB 完全兼容 MySQL"** — 大部分兼容,但触发器、存储过程等有限制。
5. **"Spanner 写延迟低"** — 不。Commit Wait 至少 7ms,跨 Region 几十 ms。

---

## 8. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Chang et al., *Spanner* | 2012 OSDI | TrueTime + External Consistency |
| Corbett et al., *Spanner implementation* | 2012 | 工程细节 |
| Peng & Dabek, *F1* | 2013 | Spanner 上的 SQL 层 |
| Thomson et al., *Calvin* | 2012 | 确定性事务 |
| Zhang et al., *TiDB* | — | 开源 Spanner |
| Taft et al., *CockroachDB* | 2020 | Parallel Commits |

---

## 9. 交叉引用

- [[03-时间与时钟]]:TrueTime / HLC / TSO
- [[05-共识-Paxos]]:Spanner 共识基础
- [[06-事务-Percolator与Spanner]]:Spanner 事务细节
- [[08-3-存储-Bigtable与HBase]]:Bigtable → Spanner 演进
- [[14-故障与容错]]:跨 Region 容错

---

## 10. TODO

- [ ] 补充 YugabyteDB 架构
- [ ] 补充 F1 Query 的分布式执行
- [ ] 增加 TiDB SQL Layer 查询优化器细节
- [ ] 补充 CockroachDB 多 Region 配置实战

---

## 11. 速查表 (Cheat Sheet)

```
NewSQL 三大方案:
  Spanner: TrueTime (硬件) + Paxos + 2PC
  TiDB:    TSO (PD) + Raft + Percolator
  CockroachDB: HLC + Raft + Parallel Commits

时钟方案对比:
  TrueTime: 全局单调,无单点,需硬件
  TSO:      中心化,简单,需 HA
  HLC:      去中心化,复杂,实现难

事务延迟:
  Spanner:        ~7ms (Commit Wait) + 网络
  TiDB:           ~5ms (TSO RPC) + 网络
  CockroachDB:    ~10ms (HLC 协调)

选型:
  私有云 + MySQL 协议 → TiDB
  私有云 + PG 协议    → CockroachDB / YugabyteDB
  云上全球分布       → Spanner (GCP)
  小规模            → 传统 RDBMS
```

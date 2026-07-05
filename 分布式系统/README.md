# 分布式系统 · 面试、原理、学术与工业笔记

> 定位：四层深度并重——
> 1. **面试速查层**（高频问答骨架）
> 2. **原理层**（算法、协议、形式化模型）
> 3. **学术层**（论文、定理、证明直觉、下界、开放问题）
> 4. **工业层**（真实系统内部、生产事故、调优、容量、多地域架构）
>
> 原则：Correctness > Completeness > Speed；每章按四层递进。
> 约定：⭐高频 🔥工程重点 📜论文 ⚠️易错点 🎓学术深度 🏭工业实战

---

## 0. 阅读指南

- 面试速查 → §18 + 每章「面试要点」
- 系统学习 → §1→§17 顺序，章间有依赖
- 学术深读 → 每章「学术深度」+ §19 论文清单 + 证明附录 §21
- 工业实战 → 每章「工业实战」+ §22 生产事故案例库
- 标记：⭐ 🔥 📜 ⚠️ 🎓 🏭

### 共享元数据文件

| 文件 | 用途 |
|------|------|
| [_章节模板.md](_章节模板.md) | 新建章节时复制的模板 |
| [_术语表.md](_术语表.md) | 中英对照术语表（持续更新） |
| [_符号约定.md](_符号约定.md) | 全仓数学符号统一 |
| [_标记规范.md](_标记规范.md) | ⭐🔥📜⚠️🎓🏭 使用边界 |
| [_Review清单.md](_Review清单.md) | 单章与全仓 review checklist |

---

## 0.1 快速导航表

| 章 | 文件 | 主题 | 关键词 | 状态 |
|----|------|------|--------|------|
| §1 | [01-分布式系统基础.md](01-分布式系统基础.md) | 基础与形式化模型 | 透明性 / 部分失败 / 故障模型 / Failure Detector | ✅ |
| §2 | [02-理论基础.md](02-理论基础.md) | CAP/PACELC/一致性谱系 | CAP / PACELC / BASE / Linearizability / FLP | ✅ |
| §3 | [03-时间与时钟.md](03-时间与时钟.md) | 时间与因果 | Lamport / Vector Clock / HLC / TrueTime | ✅ |
| §4 | [04-复制.md](04-复制.md) | 复制与 Quorum | Primary-Backup / Quorum / Read Repair / SMR | ✅ |
| §5 | 05-共识-*.md（6 文件） | 共识算法 | Paxos / Raft / ZAB / PBFT / HotStuff / FLP | ✅ |
| §6 | 06-事务-*.md（5 文件） | 分布式事务 | 2PC / 3PC / TCC / Saga / Percolator / Spanner | ✅ |
| §7 | [07-分片与路由.md](07-分片与路由.md) | 分片与路由 | 一致性哈希 / Maglev / Jump Hash / vnode | ✅ |
| §8 | 08-存储-*.md（5 文件） | 分布式存储 | GFS / Dynamo / Bigtable / Spanner / Cassandra / Redis | ✅ |
| §9 | 09-MQ-*.md（5 文件） | 消息队列与流 | Kafka / RocketMQ / Pulsar / Exactly-Once | ✅ |
| §10 | [10-协调服务.md](10-协调服务.md) | 协调与配置 | ZooKeeper / etcd / Consul / Nacos | ✅ |
| §11 | [11-分布式锁.md](11-分布式锁.md) | 分布式锁 | Redis / Redlock / ZK / etcd / Fencing | ✅ |
| §12 | [12-幂等性.md](12-幂等性.md) | 幂等与去重 | 幂等键 / Token / CAS / 状态机 | ✅ |
| §13 | 13-治理-*.md（3 文件） | 服务治理 | LB / 熔断 / 限流 / 可观测性 / Mesh / 混沌 | ✅ |
| §14 | [14-故障与容错.md](14-故障与容错.md) | 故障与容错 | 脑裂 / 多活 / 单元化 / RPO/RTO | ✅ |
| §15 | [15-协议与通信.md](15-协议与通信.md) | 协议与通信 | TCP/HTTP/QUIC/gRPC / Protobuf / Avro | ✅ |
| §16 | 16-计算-*.md（2 文件） | 分布式计算 | MapReduce / Spark / Flink / Chandy-Lamport | ✅ |
| §17 | [17-CRDT.md](17-CRDT.md) | CRDT | G-Counter / OR-Set / 协同编辑 | ✅ |
| §18 | [18-面试高频问题.md](18-面试高频问题.md) | 面试速查 | 30 高频问答骨架 | ✅ |
| §19 | [19-论文清单.md](19-论文清单.md) | 论文索引 | 40+ 经典论文 + Top10 必读 | ✅ |
| §20 | [20-附录.md](20-附录.md) | 附录 | 术语表 / 符号 / CAP-PACELC / 共识/MQ/存储速查 | ✅ |
| §21 | 21-学术附录-*.md（6 文件） | 学术附录 | FLP / CAP / 线性一致性 / Byzantine / 故障检测器 / 快照+CRDT | ✅ |
| §22 | 22-工业案例-*.md（10 文件） | 工业案例库 | Spanner / DynamoDB / 双11 / Netflix / Uber / Twitter / Kafka / TiDB / CockroachDB / Flink | ✅ |

**状态图例**：🚧 写作中 · ✅ 完成 · 📝 仅骨架 · 🔍 待 review

---

## 0.2 阅读顺序图

```
入门
  │
  ├─→ §1 基础 + 形式化模型
  │
  ├─→ §2 理论基础（CAP / PACELC / 一致性谱系）
  │
  ├─→ §3 时间与时钟（Lamport / Vector / HLC）
  │
  ├─→ §4 复制（Primary-Backup / Quorum）
  │
  └─→ §5 共识（Paxos / Raft / ZAB / BFT）── 面试核心
                                              │
事务与可靠性                                    ▼
  ├─→ §6 分布式事务（2PC / TCC / Saga / 消息事务）
  ├─→ §12 幂等性
  ├─→ §11 分布式锁
  ├─→ §10 协调服务
  ├─→ §7 分片与路由
  └─→ §14 故障与容错
                                              │
工业系统                                       ▼
  ├─→ §8 存储（GFS / Dynamo / Bigtable / Spanner）
  ├─→ §9 MQ（Kafka / RocketMQ / Pulsar）
  ├─→ §16 计算（MapReduce / Spark / Flink）
  ├─→ §13 治理（LB / 限流 / 可观测性 / Mesh）
  ├─→ §15 协议与通信（RPC / Gossip）
  └─→ §17 CRDT
                                              │
面试与深读                                     ▼
  ├─→ §18 面试高频
  ├─→ §19 论文清单
  ├─→ §21 学术附录（FLP / CAP 证明）
  └─→ §22 工业案例库
```

---

## 1. 分布式系统基础

### 1.1 定义与特征
- 透明性 / 可扩展性 / 容错性 / 并发性 / 开放性
- 分布式 vs 集群 vs 并行 vs 网格 vs 边缘

### 1.2 驱动力与代价
- 规模 / 地理 / 可用性 / 成本 / 合规
- 代价：复杂度、部分失败、调试困难、一致性弱化

### 1.3 核心挑战 ⭐
- 网络不可靠、节点不可靠、并发无序、部分失败、状态一致性

### 1.4 八大谬误（Fallacies）

### 1.5 设计目标三角与权衡
- C / A / P / Latency / Scalability / Cost 多维权衡

### 1.6 🎓 形式化模型
- 系统模型：同步 / 异步 / 部分同步
- 通信模型：消息传递 vs 共享内存
- 故障模型：Crash-stop / Crash-recovery / Omission / Byzantine
- Failure Detector 分类（Chandra-Toueg）：Perfect / Eventually Perfect / Strong / Weak

### 1.7 🏭 工业视角
- Google SRE 对分布式复杂度的工程化应对
- AWS Well-Architected Framework 的可靠性支柱

---

## 2. 理论基础 ⭐

### 2.1 CAP 定理
- Gilbert-Lynch 形式化定义 🎓
- 三选二的常见误解 ⚠️
- Partition 下的 C vs A

### 2.2 PACELC 定论 ⭐
- 无分区：Latency vs Consistency
- 各系统归类（CP/AP/EL/EC 矩阵）

### 2.3 BASE 理论

### 2.4 一致性模型谱系 ⭐🎓
- 线性一致性（Linearizability, Herlihy-Wing）🎓
- 顺序一致性（Lamport）
- 因果一致性
- PRAM / FIFO
- Read-Your-Writes / Monotonic Reads/Writes
- Eventual Consistency 的形式化定义
- 一致性层级图（由强到弱）

### 2.5 拜占庭将军问题 📜
- f < n/3 下界与证明直觉
- 口头消息 vs 签名消息

### 2.6 🎓 不可能性结果汇总
- FLP（异步系统共识不可能）
- CAP 不可回避
- Coordinated Attack（同步不可达）
- 分布式快照与因果一致性边界

### 2.7 🏭 工业取舍矩阵
- 主流系统在 PACELC 中的位置
- 同一系统不同配置下的切换（MongoDB R/W Concern、Cassandra consistency level）

---

## 3. 时间、时钟与因果 ⭐

### 3.1 物理时钟
- 时钟漂移、NTP 层级、闰秒、Smear Leap Second
- UTC/TAI/GPS

### 3.2 Lamport 逻辑时钟 📜
- happens-before → 偏序 → 全序
- ⚠️ 不能刻画并发

### 3.3 向量时钟 ⭐
- 并发事件检测
- 版本向量（version vector）区别

### 3.4 混合逻辑时钟（HLC）
- TrueTime（Spanner）⭐🔥
- HLC（Kulkarni-Rozanski）
- CockroachDB / YugabyteDB 的时间实现对比

### 3.5 全序广播与时钟

### 3.6 🎓 形式化
- 因果记忆（Causal Memory, Ahamad et al.）
- Interval Consistency
- 外部一致性（External Consistency）

### 3.7 🏭 工业实战
- Google TrueTime 的原子钟 + GPS 实现 + 误差预算 ⭐🔥
- Spanner 的 Commit Wait
- CockroachDB HLC 误差传播
- Hybrid Clock 在分布式数据库中的工程挑战

---

## 4. 一致性与复制 ⭐

### 4.1 复制架构
- Primary-Backup / Multi-Master / Quorum (Dynamo 风格)
- Chain Replication
- CRAQ（Chain Replication with Apportioned Queries）

### 4.2 同步 / 异步 / 半同步
- MySQL 半同步、PostgreSQL synchronous_commit 等级

### 4.3 Quorum 机制 ⭐
- W + R > N 推导
- 读写延迟与一致性权衡

### 4.4 Read Repair / Anti-Entropy / Hinted Handoff

### 4.5 Sloppy Quorum

### 4.6 🎓 复制理论
- Primary-Backup 与 Quorum 的等价性分析
- State Machine Replication（SMR）范式
- Virtual Synchrony

### 4.7 🏭 工业实战
- Cassandra 读写路径、Tombstone 处理
- MongoDB OpLog 与 Replica Set
- Kafka ISR 与 HW/LEO ⭐🔥
- etcd/etcd-raft 日志复制细节

---

## 5. 共识算法 ⭐🔥

### 5.1 共识问题定义
- 与一致性、原子广播关系
- FLP 不可能定理 📜 + 证明直觉 🎓

### 5.2 Paxos 📜
- Basic Paxos（Proposer/Acceptor/Learner）
- Multi-Paxos
- Fast Paxos / EPaxos 🎓
- 工程难点：日志空洞、成员变更、领导者选举

### 5.3 Raft ⭐🔥
- Leader Election（任期、随机超时）
- Log Replication（匹配、提交规则、Safety）
- 成员变更（Joint Consensus、单步成员变更）
- 日志压缩与快照
- ⚠️ 边界情况：网络分区恢复、日志回滚

### 5.4 ZAB（ZooKeeper Atomic Broadcast）
- 阶段：Discovery / Sync / Broadcast
- 与 Raft/Paxos 对比

### 5.5 拜占庭共识 🎓
- PBFT 📜（三阶段：pre-prepare/prepare/commit）
- HotStuff 📜（线性视图切换）
- Tendermint / DiemBFT

### 5.6 共识算法对比表

### 5.7 🎓 进阶学术
- Paxos 变体谱系图
- Generalized Consensus / Commutative cmds
- Consensus vs Atomic Broadcast 等价
- 乐观共识、Fast Path 条件

### 5.8 🏭 工业实战
- etcd-raft 实现细节（WAL、Snapshot、Lease Read、ReadIndex）
- TiKV Multi-Raft 与 PD 调度 🔥
- Chubby 的 Paxos 工程化（Paxos Made Live）📜
- ZooKeeper 在生产中的注意事项（羊群、Watch 风暴）
- Spanner Paxos Group 与跨 Paxos 事务

---

## 6. 分布式事务 ⭐🔥

### 6.1 ACID vs BASE

### 6.2 2PC ⭐
- 流程、阻塞、单点、不一致
- 协调者 HA、参与者超时策略

### 6.3 3PC
- 解决与未解决问题

### 6.4 TCC 🔥
- 空回滚、悬挂、幂等、资源预留

### 6.5 Saga 🔥
- 编排式 vs 协同式
- 补偿设计、隔离性问题

### 6.6 本地消息表 / 事务消息 🔥
- RocketMQ 事务消息、Outbox Pattern、CDC（Debezium）

### 6.7 最大努力通知

### 6.8 Seata 模式对比（AT/TCC/SAGA/XA）

### 6.9 🎓 进阶
- Percolator（Google）📜 —— 乐观锁 + 快照隔离
- Spanner 的 2PC + TrueTime 外部一致性
- Calvin / FaSST（确定性事务）
- MVSPO（多版本 + PO）
- 事务隔离级别与一致性模型关系

### 6.10 🏭 工业实战
- 阿里 Seata 在双 11 的规模与陷阱
- TiDB / OceanBase 的分布式事务路径
- 跨地域事务：Spanner / CockroachDB 的 Commutator
- 银行核心系统对账与补偿实践
- 事务消息在生产中的常见踩坑（消费幂等、回查）

### 6.11 选型决策树

---

## 7. 数据分片与路由 ⭐

### 7.1 分片策略
- Range / Hash / Consistent Hash ⭐
- 虚拟节点

### 7.2 路由与元数据
- 中心化 vs 去中心化

### 7.3 再平衡（Rebalancing）

### 7.4 分片键设计与热点

### 7.5 🎓 进阶
- Consistent Hashing 的 O(log N) 查找证明
- Rendezvous Hashing（HRW）
- Jump Hash、Maglev ⭐（Google）
- 动态分片与分裂/合并

### 7.6 🏭 工业实战
- Dynamo 的 token ring
- Cassandra vnodes
- Redis Cluster 的 16384 slot
- HBase Region split/merge 调优
- 热点打散：加盐、预分片、二级索引

---

## 8. 分布式存储经典系统 📜🔥

### 8.1 GFS / HDFS
- 架构、追加语义、一致性模型
- 小文件问题、NameNode HA

### 8.2 Dynamo
- 向量时钟、Sloppy Quorum、Hinted Handoff

### 8.3 Bigtable / HBase
- LSM-Tree、SSTable、Compaction（Size-tiered vs Leveled）
- RegionServer、WAL、MVCC

### 8.4 Megastore / Spanner 📜
- TrueTime、Paxos Group、2PC、外部一致性
- Spanner vs CockroachDB vs TiDB 对比

### 8.5 Cassandra
- Dynamo + Bigtable 混合
- 调优与一致性级别

### 8.6 Redis Cluster
- Gossip、Slot、主从、Cluster Bus

### 8.7 MongoDB Sharded Cluster
- Config Server、Mongos、Chunk 迁移

### 8.8 🎓 学术对照
- CAP 在各系统中的位置
- 一致性成本对比
- 新硬件影响（RDMA、PMEM、NVMe）

### 8.9 🏭 工业实战
- Google File System → Colossus 演进
- HBase 在 Facebook Messenger 的实践
- Spanner 跨地域部署与成本
- 各系统典型事故（HBase Split Storm、Cassandra Hinted Handoff 爆炸）

---

## 9. 消息队列与流系统 🔥

### 9.1 MQ 角色

### 9.2 消息模型
- Topic / Partition / Consumer Group / Offset

### 9.3 投递语义 ⭐

### 9.4 Kafka 深度 🔥
- 存储格式、Segment、Index
- ISR / HW / LEO / Leader Epoch
- 幂等生产者（PID + SeqNum）
- 事务（Transaction Coordinator、Two-Phase Commit 内部化）
- 顺序保证、分区分配、Rebalance（Cooperative vs Eager）
- KRaft（移除 ZooKeeper）🔥

### 9.5 RocketMQ
- CommitLog / ConsumeQueue / IndexFile
- 事务消息、延迟消息（特定 Level → 任意延迟）、顺序消息
- 高可用：Master-Slave 同步/异步刷盘

### 9.6 Pulsar
- 计算与存储分离（Broker + BookKeeper）
- 分层存储

### 9.7 🎓 学术
- Log-Structured Streams
- Exactly-Once 的形式化（Lin et al.）
- 流处理一致性语义

### 9.8 🏭 工业实战
- LinkedIn Kafka 规模化经验
- Uber / 滴滴 / 字节的 MQ 演进
- 消息积压、Rebalance 风暴、Producer 阻塞调优
- 跨机房复制与双活

### 9.9 MQ 对比表

---

## 10. 协调与配置服务 ⭐

### 10.1 ZooKeeper
- ZNode / Watcher / Ephemeral / ZAB
- 典型用法：选主、配置、锁、服务发现
- ⚠️ 羊群效应、Watch 风暴、脑裂

### 10.2 etcd
- Raft / Lease / Compact / MVCC
- Watch 的可靠实现（ revisions ）

### 10.3 Consul / Nacos / Eureka
- AP vs CP 取舍（Eureka AP、Consul/etcd CP、Nacos 双模）

### 10.4 🎓 进阶
- 协调服务的本质：线性一致 KV + Watch + Lease
- 服务发现的 CP/AP 取舍理论

### 10.5 🏭 工业实战
- ZooKeeper 集群规模上限与 GC 长暂停事故
- etcd 在 K8s 中的容量规划（key 数 / QPS）
- 配置中心灰度发布与回滚

---

## 11. 分布式锁 ⭐🔥

### 11.1 需求与挑战
- 互斥、可重入、容错、可见性、Fencing

### 11.2 实现方案
- 数据库唯一索引 / 行锁
- Redis：SETNX + EX + NX、Redisson 看门狗、Redlock ⭐
- ZooKeeper：临时节点 + Watch
- etcd：Lease + Txn

### 11.3 Redlock 争议 📜（Kleppmann vs antirez）
- 时钟漂移、GC 暂停、网络延迟对锁安全性的破坏

### 11.4 Fence Token 机制 ⭐

### 11.5 🎓 进阶
- 锁的形式化属性（Mutual Exclusion / Progress / Fairness）
- 容错锁下界（Fencing 必要性证明直觉）

### 11.6 🏭 工业实战
- Redisson 锁参数调优（watchdog、锁重入、读写锁）
- ZooKeeper 锁在生产中的羊群优化
- 跨数据中心锁的代价

### 11.7 对比与选型

---

## 12. 幂等性与去重 ⭐🔥

### 12.1 幂等的数学定义 f(f(x)) = f(x)

### 12.2 幂等键设计

### 12.3 实现方案
- 唯一索引、Token、状态机、乐观锁（版本号/CAS）

### 12.4 幂等与事务消息、去重表

### 12.5 🎓 进阶
- 幂等性与一致性模型关系
- 多次重试下的副作用边界

### 12.6 🏭 工业实战
- 支付/对账场景的幂等设计
- 接口幂等 vs 业务幂等
- Redis SETNX 幂等键的 TTL 边界问题

---

## 13. 服务治理与可靠性

### 13.1 负载均衡算法
- 轮询 / 加权 / 最少连接 / 一致性哈希 / P2C（Power of Two Choices）⭐

### 13.2 服务发现与注册
### 13.3 熔断、降级、限流 🔥
- 令牌桶 / 漏桶 / 滑动窗口 / 漏桶+令牌桶组合
- 自适应限流（BBR 思想）
- Sentinel / Hystrix / Resilience4j

### 13.4 超时与重试
- 超时预算（Timeout Budget）、退避（指数退避 + 抖动）、重试风暴 ⚠️

### 13.5 链路追踪与可观测性 🔥
- Trace / Span / Context Propagation
- OpenTelemetry
- Metrics（Prometheus）、Log（Loki/ELK）、Trace（Jaeger/Tempo）
- 三者关系与采样

### 13.6 灰度、蓝绿、金丝雀、A/B

### 13.7 🎓 进阶
- 故障注入与混沌工程（Chaos Engineering）
- 形式化验证服务网格

### 13.8 🏭 工业实战
- Netflix Chaos Monkey / 阿里 ChaosBlade
- 双 11 大促前的全链路压测
- Service Mesh（Istio / Linkerd）落地权衡 🔥
- eBPF 在可观测性中的新角色

---

## 14. 故障与容错 ⭐

### 14.1 故障模型回顾
### 14.2 故障检测
- 心跳、Phi Accrual Detector 🎓
### 14.3 脑裂 ⭐
- Quorum / Fencing / Witness / STONITH
### 14.4 副本一致性修复
### 14.5 HA：Active-Standby / Active-Active
### 14.6 容灾与多活 🔥
- 同城双活、异地多活、单元化（蚂蚁 LDC、字节）
- RPO / RTO / MTTR
### 14.7 🎓 进阶
- Byzantine Fault Detectors
- Self-Stabilizing Systems
- Crash-Recovery 中的日志恢复理论

### 14.8 🏭 工业实战
- AWS S3 / GitLab / Cloudflare 事故复盘 ⭐🔥
- 网络分区下的真实系统行为
- 长尾延迟（P99/P99.9）调优
- 容量规划与压测

---

## 15. 协议与通信

### 15.1 RPC 框架
- gRPC / Thrift / Dubbo / brpc
- 序列化（Protobuf / Thrift / FlatBuffers / JSON）
- 连接池、多路复用、超时、重试、拦截器

### 15.2 Gossip 协议 ⭐
- Push-Pull / Anti-Entropy / Rumor Mongering
- 收敛速度分析 🎓

### 15.3 网络分区与超时设计
### 15.4 背压（Backpressure）
### 15.5 🎓 进阶
- 网络拓扑感知（rack/DC awareness）
- 拥塞控制（BBR 在数据中心）
- RDMA / io_uring / kernel bypass

### 15.6 🏭 工业实战
- gRPC keepalive 调优陷阱
- 长连接 vs 短连接在 NAT 下的选择
- 跨地域 RPC 的延迟优化

---

## 16. 分布式计算

### 16.1 MapReduce 📜
- 编程模型、Shuffle、Combiner、Speculative Execution

### 16.2 Spark
- RDD / DAG / Stage / Shuffle
- 内存计算、Tungsten、AQE

### 16.3 Flink 🔥
- Event Time / Watermark / Window
- State Backend、Checkpoint（Chandy-Lamport）📜、Savepoint
- Exactly-Once（Two-Phase Commit + Kafka 事务）

### 16.4 流批一体与新一代引擎（DataFlow Model）

### 16.5 🎓 学术
- Chandy-Lamport 分布式快照算法 📜
- 数据流模型（Akidau et al.）
- 因果快照与一致性切点

### 16.6 🏭 工业实战
- 字节 / 阿里 / 美团的实时数仓架构
- Checkpoint 大状态调优（RocksDB、增量 Checkpoint）
- 反压与倾斜治理

---

## 17. CRDT 与最终一致编程 🎓🔥

### 17.1 CRDT 定义与分类
- State-based (CvRDT) / Operation-based (CmRDT)
- 半格（Semilattice）数学基础

### 17.2 典型 CRDT
- G-Counter / PN-Counter / LWW-Register / LWW-Set / OR-Set / MV-Register / Sequence CRDT（RGA, LSEQ）

### 17.3 应用场景
- 协同编辑（Yjs, Automerge）
- Riak 数据类型
- 离线优先应用

### 17.4 🎓 进阶
- CRDT 与因果一致性关系
- Merkle DAG + CRDT（IPFS / Automerge）
- 纯操作变换（OT）vs CRDT 对比

### 17.5 🏭 工业实战
- Figma / Notion 协同引擎选型
- Automerge 在本地优先软件中的实践

---

## 18. 面试高频问题索引 ⭐🔥

> 每题给出：答题骨架 + 关键点 + 加分项 + 常见追问

- CAP / PACELC 怎么理解？举例
- Raft 选举与日志复制完整流程
- Raft 与 Paxos 区别；为什么 Raft 更易理解
- 2PC / 3PC / TCC / Saga 对比与选型
- 分布式事务在金融场景怎么做
- 一致性哈希与虚拟节点
- Kafka 如何保证顺序、不丢、不重复（Exactly-Once 全链路）
- Redis 分布式锁的问题与 Redlock 争议
- ZooKeeper 临时节点与 Watcher 用途、羊群如何解决
- 脑裂如何避免
- 幂等如何设计
- 分布式 ID（雪花、号段、UUID、Leaf）
- 限流器设计（令牌桶、漏桶、滑动窗口、Sentinel）
- FLP 不可能定理
- Lamport 时钟与向量时钟
- GFS / Dynamo / Spanner 各自设计取舍
- 分布式系统监控指标（RED / USE / SLI/SLO）
- 设计一个支持千万 QPS 的 KV 系统（系统设计题）
- 设计一个分布式延迟队列
- 设计一个全局唯一 ID 生成器

---

## 19. 经典论文清单 📜

| 年份 | 论文 | 主题 |
|------|------|------|
| 1978 | Lamport, *Time, Clocks...* | 逻辑时钟 |
| 1982 | Lamport et al., *Byzantine Generals* | 拜占庭故障 |
| 1985 | Fischer-Lynch-Paterson, *FLP* | 共识不可能 |
| 1985 | Chandy-Lamport, *Distributed Snapshots* | 快照算法 |
| 1989 | Lamport, *Paxos* | 共识 |
| 1993 | Chandra-Toueg, *Failure Detectors* | 故障检测器 |
| 1996 | Herlihy-Wing, *Linearizability* | 线性一致性 |
| 1998 | Lynch, *Distributed Algorithms*（书）| 形式化教材 |
| 2003 | GFS | 分布式文件系统 |
| 2004 | MapReduce | 分布式计算 |
| 2006 | Bigtable | 列式存储 |
| 2007 | Dynamo | 最终一致 KV |
| 2007 | Percolator | 乐观分布式事务 |
| 2010 | ZooKeeper | 协调服务 |
| 2011 | Megastore | 跨地域事务 |
| 2012 | Spanner | 全球强一致 |
| 2012 | CRDT Survey (Shapiro et al.) | 最终一致结构 |
| 2014 | Raft | 可理解共识 |
| 2014 | Chubby / *Paxos Made Live* | 工程化 Paxos |
| 2015 | HotStuff | BFT 线性化 |
| 2017 | *Maglev* | 一致性哈希负载均衡 |
| 2015 | *Dataflow Model* | 流批一体 |

---

## 20. 附录

### 20.1 术语表
### 20.2 工程实现对照表
### 20.3 推荐资源
- DDIA ⭐
- van Steen & Tanenbaum, *Distributed Systems*
- MIT 6.824
- Martin Kleppmann's blog & book
- aphyr.com（Jepsen 报告）⭐🔥
- 《数据密集型应用系统设计》

### 20.4 笔记书写规范
- 每章统一结构：
  1. 定义与动机
  2. 原理与算法
  3. 🎓 学术深度（定理 / 证明直觉 / 下界）
  4. 🏭 工业实战（真实系统 / 事故 / 调优）
  5. 面试要点
  6. 论文与延伸阅读
- 公式配直觉；图示配数据流；对比统一用表格
- 代码示例仅用于关键算法（如 Raft 伪代码）

---

## 21. 学术深度附录 🎓

### 21.1 [FLP 不可能性证明](21-1-学术附录-FLP证明.md)
### 21.2 [CAP 形式化证明](21-2-学术附录-CAP形式化.md)
### 21.3 [线性一致性形式化](21-3-学术附录-线性一致性.md)
### 21.4 [Byzantine 下界证明](21-4-学术附录-Byzantine下界.md)
### 21.5 [故障检测器与等价证明](21-5-学术附录-故障检测器等价证明.md)
### 21.6 [分布式快照与 CRDT 收敛证明](21-6-学术附录-快照与CRDT收敛.md)

---

## 22. 工业实战案例库 🏭🔥

### 22.1 [Google Spanner 全球分布式数据库](22-1-工业案例-Google-Spanner.md)
### 22.2 [AWS DynamoDB 海量 KV](22-2-工业案例-AWS-DynamoDB.md)
### 22.3 [Alibaba 双 11 与单元化(LDC)](22-3-工业案例-Alibaba-双11与单元化.md)
### 22.4 [Netflix 微服务治理](22-4-工业案例-Netflix微服务治理.md)
### 22.5 [Uber 大规模实时计算](22-5-工业案例-Uber-大规模实时.md)
### 22.6 [Twitter / Cassandra 海量存储](22-6-工业案例-Twitter-Cassandra.md)
### 22.7 [Kafka at LinkedIn](22-7-工业案例-Kafka-at-LinkedIn.md)
### 22.8 [TiDB 开源 NewSQL](22-8-工业案例-TiDB.md)
### 22.9 [CockroachDB 地理分布式 SQL](22-9-工业案例-CockroachDB.md)
### 22.10 [Flink at Alibaba / Bilibili](22-10-工业案例-Flink-at-Alibaba.md)

---

## 23. 章节依赖关系

```
1 基础 + 1.6 形式化模型
   └→ 2 理论 ─→ 4 复制 ─→ 5 共识 ─→ 10 协调服务
        │           ↑          ↓
        │           └──────────┘
        ├→ 3 时钟 ─→ 4 / 5 / 8 / 16(快照)
        └→ 6 事务 ─→ 12 幂等 ─→ 9 MQ
                            │
                            └→ 11 锁 / 14 容错 / 13 治理
8 存储 / 9 MQ / 16 计算 —— 横向案例章节
17 CRDT —— 终极最终一致应用层
21 学术附录 —— 横向理论支撑
22 工业案例库 —— 横向实战对照
```

---

## 24. Review 检查清单（v2 增强）

### 覆盖度
- [ ] 面试高频：§18 是否足够
- [ ] 原理层：四层递进是否每章都有
- [ ] 学术层：§19 论文 + §21 证明附录是否完备
- [ ] 工业层：§22 事故库 + 每章工业实战是否充分

### 是否需补充章节
- [ ] 微服务架构 / Service Mesh 是否独立成章（目前在 §13）
- [ ] 云原生（K8s / Operator / 多云）是否独立
- [ ] 分布式 ID 是否独立成章
- [ ] 分布式缓存（Redis / Memcached / 多级缓存）是否独立
- [ ] 数据库相关（MVCC / 隔离级别）是否前置独立
- [ ] 边缘计算 / Serverless 是否纳入
- [ ] 安全（mTLS / 零信任 / 机密计算）是否独立

### 深度平衡
- [ ] 学术证明附录是否过深（FLP 完整证明是否需要）
- [ ] 工业案例是否过细（每篇事故独立成文 vs 简短要点）
- [ ] 是否需要专门章节对比 NewSQL（TiDB/OceanBase/CockroachDB/YugabyteDB）

### 章节粒度
- [ ] §13 是否过载（治理+可观测性+Mesh 混在一起）
- [ ] §8 是否拆分（文件/列存/KV/关系型 NewSQL）

### 待定项
- [ ] 是否需要配套代码仓库（Raft/CRDT 伪代码实现）
- [ ] 是否需要图示目录（架构图统一存放）
- [ ] 是否需要「面试刷题打卡表」

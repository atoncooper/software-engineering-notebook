# 消息队列 —— Kafka 深度

> 章号: §9.2
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业
> 前置: [[09-1-消息队列-基础与投递语义]] [[04-复制]] [[07-分片与路由]]

---

## 0. Kafka 是什么

Kafka (LinkedIn 2011,开源) 最初是"分布式提交日志",设计目标:

- **高吞吐**:百万级 QPS
- **持久化**:磁盘顺序写,长期保存
- **可扩展**:水平扩展 partition
- **容错**:多副本,自动 failover

后演化为"流处理平台"(Kafka Streams、KSQL),成为大数据生态核心。

---

## 1. 架构

```
              ┌────────────────────────────┐
              │  Producer                  │
              └────────────────────────────┘
                          ↓
              ┌────────────────────────────┐
              │  Kafka Cluster             │
              │  ┌──────────────────────┐  │
              │  │ Broker 1             │  │
              │  │  Topic-A: P0(L), P1  │  │
              │  │  Topic-B: P0, P1(L)  │  │
              │  └──────────────────────┘  │
              │  ┌──────────────────────┐  │
              │  │ Broker 2             │  │
              │  │  Topic-A: P0, P1(L)  │  │
              │  │  Topic-B: P0(L), P1  │  │
              │  └──────────────────────┘  │
              │  (ZK / KRaft 元数据)        │
              └────────────────────────────┘
                          ↓
              ┌────────────────────────────┐
              │  Consumer Group            │
              │  C1 ← P0  C2 ← P1          │
              └────────────────────────────┘
```

### 1.1 核心概念

| 概念 | 含义 |
|------|------|
| **Topic** | 逻辑分类,如 `orders` |
| **Partition** | Topic 的物理分片,有序追加日志 |
| **Replica** | Partition 的副本,Leader + Follower |
| **ISR** | In-Sync Replicas,与 Leader 同步的副本 |
| **OSR** | Out-of-Sync Replicas,落后副本 |
| **Broker** | Kafka 集群节点 |
| **Controller** | 管理 partition/replica 状态的 broker |
| **Consumer Group** | 协同消费的 consumer 集合 |
| **Offset** | consumer 在 partition 的消费位置 |

### 1.2 Partition 模型

每个 Partition 是一个**有序、不可变、追加**的日志:

```
Partition 0:
  offset: 0  1  2  3  4  5  6  7  8 ...
  msg:    A  B  C  D  E  F  G  H  I ...
        ↑                          ↑
    oldest                       newest
    (log retention 删除)         (新消息追加)
```

- Consumer 用 offset 跟踪消费位置
- 旧消息按 retention(时间或大小)删除
- offset 单调递增,不可重用

---

## 2. 存储引擎

### 2.1 日志结构

```
/var/lib/kafka/topics/orders-0/
  ├── 00000000000000000000.log     # 数据
  ├── 00000000000000000000.index   # offset 索引
  ├── 00000000000000000000.timeindex  # 时间戳索引
  ├── 00000000000000100000.log     # 下一个 segment
  └── ...
```

- Partition 由多个 **Segment** 组成
- 每个 Segment 默认 1GB 或 1 周
- Segment 文件名 = 起始 offset

### 2.2 顺序写

- 消息追加到 Segment 末尾(顺序写)
- 顺序写磁盘性能接近内存(几百 MB/s)
- 内核 Page Cache 加速读

### 2.3 索引

- **Offset Index**:offset → 物理位置(稀疏,每 4KB 一条)
- **Time Index**:timestamp → offset(按时间查询)
- **Index 文件本身也 mmap**

### 2.4 Zero-Copy

Kafka 用 Linux `sendfile` 实现 Zero-Copy:

```
传统 read+write:
  磁盘 → 内核 buffer → 用户 buffer → socket buffer → 网卡
  (4 次拷贝,2 次系统调用)

sendfile:
  磁盘 → 内核 buffer → 网卡
  (2 次拷贝,1 次系统调用)
```

吞吐提升数倍。

### 2.5 压缩

Kafka 支持批量压缩:snappy、gzip、lz4、zstd:

- Producer 端压缩(batch 内)
- Broker 直接存储压缩格式(不解压)
- Consumer 端解压

减少网络 + 存储开销。

---

## 3. 复制与一致性

### 3.1 Replica 角色

```
Partition 0:
  Leader:   Broker 1 (处理读写)
  ISR:      [Broker 1, Broker 2, Broker 3]
  OSR:      [Broker 4]  (落后太多)

写入:
  Producer → Leader (Broker 1) → 同步到 ISR 中的 Follower
  当 ISR 中所有副本确认 → Leader commit → 返回 Producer ACK
```

### 3.2 ISR 机制

- Follower 主动拉取 Leader 日志(`fetch` 请求)
- Follower 落后超过 `replica.lag.time.max.ms`(默认 10s)→ 移出 ISR
- 追上后重新加入 ISR

### 3.3 acks 配置

| acks | 含义 | 一致性 | 性能 |
|------|------|-------|------|
| 0 | 不等任何确认 | 弱 | 最快 |
| 1 | Leader 确认 | 中(Leader 挂可能丢) | 快 |
| all/-1 | 所有 ISR 确认 | 强 | 慢 |

```python
# Python Kafka Producer
producer = KafkaProducer(
    bootstrap_servers=['kafka:9092'],
    acks='all',
    enable_idempotence=True,
    retries=3,
    max_in_flight_requests_per_connection=5,
    compression_type='zstd',
)
```

### 3.4 Leader 选举

- Leader 故障 → Controller 从 ISR 选新 Leader
- `unclean.leader.election.enable=false`(默认):禁止 OSR 当 Leader,避免数据丢失
- `true`:允许 OSR 当 Leader,优先可用性(可能丢数据)

### 3.5 min.insync.replicas

```python
# Topic 配置
min.insync.replicas=2  # ISR 至少 2 个才允许写

# 与 acks=all 配合,保证写入至少 2 个副本
```

如果 ISR < min.insync.replicas,Producer 收到 `NotEnoughReplicasException`,写入失败(可用性换一致性)。

---

## 4. Consumer Group

### 4.1 模型

```
Topic orders (3 partitions):
  P0, P1, P2

Consumer Group A:
  C1 ← P0, P1
  C2 ← P2

Consumer Group B (独立消费进度):
  C1 ← P0
  C2 ← P1
  C3 ← P2
```

- 同 Group 内:partition 分配给 consumer,竞争消费
- 不同 Group:各自独立消费全部消息(Pub/Sub)

### 4.2 Rebalance

Consumer 加入/退出 → 触发 rebalance → 重新分配 partition。

#### 4.2.1 触发场景

- Consumer 加入/退出
- Consumer 心跳超时
- Topic 加 partition
- 订阅变化

#### 4.2.2 分配策略

- **Range**(默认):按 partition 范围分
- **RoundRobin**:轮询
- **Sticky**:尽量保持原分配,减少迁移
- **CooperativeSticky**(2.4+):增量 rebalance,不停顿

#### 4.2.3 Rebalance 问题

- Stop-the-world:rebalance 期间所有 consumer 停止消费
- 大集群几分钟不可用
- 解决:Cooperative Rebalance、Static Membership

### 4.3 Offset 管理

```java
// 提交 offset
consumer.subscribe(Collections.singletonList("orders"));
while (true) {
    ConsumerRecords<String, String> records = consumer.poll(Duration.ofMillis(100));
    for (ConsumerRecord<String, String> r : records) {
        process(r);
    }
    // 同步提交(失败抛异常)
    consumer.commitSync();
    // 或异步提交
    // consumer.commitAsync();
}
```

- **自动提交**:`enable.auto.commit=true`,周期提交(默认 5s)。简单但可能重复消费。
- **手动提交**:处理完后 `commitSync` / `commitAsync`。精确控制。

---

## 5. Exactly-Once 语义

### 5.1 Idempotent Producer (0.11+)

```java
props.put("enable.idempotence", true);
props.put("acks", "all");
props.put("retries", Integer.MAX_VALUE);
props.put("max.in_flight_requests_per_connection", 5);
```

- Producer 启动获得 `producer_id` + `epoch`
- 每条消息带 `sequence_number`
- Broker 按 `(partition, producer_id, sequence_number)` 去重

### 5.2 Transactional Producer (0.11+)

```java
props.put("transactional.id", "order-processor-1");
KafkaProducer<String, String> producer = new KafkaProducer<>(props);

producer.initTransactions();

try {
    producer.beginTransaction();
    producer.send(new ProducerRecord<>("orders", "key1", "value1"));
    producer.send(new ProducerRecord<>("orders", "key2", "value2"));

    // 同时提交消费 offset(读 kafka → 处理 → 写 kafka,端到端事务)
    producer.sendOffsetsToTransaction(offsets, "consumer-group-id");

    producer.commitTransaction();
} catch (Exception e) {
    producer.abortTransaction();
}
```

### 5.3 Consumer read_committed

```java
props.put("isolation.level", "read_committed");
// Consumer 只读已 commit 的消息,跳过 abort 的
```

### 5.4 端到端 Exactly-Once

```
Kafka Source → Process → Kafka Sink
   ↑                       ↓
   └─── Transaction ───────┘
```

`consume-process-produce` 全在一个 Kafka 事务内,实现端到端 exactly-once。

---

## 6. KRaft 模式 (2.8+)

### 6.1 摆脱 ZK

Kafka 早期依赖 ZK 存元数据,问题:

- ZK 元数据规模受限(集群 > 20w partition 时 ZK 成瓶颈)
- Controller 切换慢(需从 ZK 重新加载元数据)
- 运维复杂(两套系统)

### 6.2 KRaft 架构

```
KRaft 模式:
  - 几个 Broker 作为 Controller(Quorum,3 或 5 个)
  - 用 Raft 协议同步元数据
  - 元数据本身存在 Kafka 内部 topic (__cluster_metadata)
  - 普通 Broker 从 Controller 拉元数据
```

### 6.3 优势

- 元数据规模不受限(可以百万级 partition)
- Controller 切换快(Raft log 已持久化)
- 运维简单(单系统)

Kafka 4.0 完全移除 ZK。

---

## 7. 性能优化

### 7.1 Producer 优化

- **batch.size**:批量大小(默认 16KB),增大提升吞吐
- **linger.ms**:等待 batch 满的时间(默认 0),增大提升吞吐
- **compression.type**:snappy / lz4 / zstd
- **buffer.memory**:缓冲区大小(默认 32MB)

### 7.2 Broker 优化

- **num.io.threads**:IO 线程数(=磁盘数)
- **num.network.threads**:网络线程数(=CPU 核数)
- **log.flush.interval.messages**:每 N 条消息 fsync(慎用,影响性能)
- **log.segment.bytes**:Segment 大小(默认 1GB)

### 7.3 Consumer 优化

- **fetch.min.bytes**:最少拉取字节数(默认 1)
- **max.poll.records**:单次 poll 最大记录数(默认 500)
- **fetch.max.bytes**:最大拉取字节数(默认 50MB)

### 7.4 操作系统优化

- 文件系统:XFS > ext4
- `vm.dirty_ratio` / `vm.dirty_background_ratio`:Page Cache 控制
- `swappiness`:关闭 swap
- 网络:`tcp_max_syn_backlog`、`somaxconn` 调大

---

## 8. 工业应用

### 8.1 日志聚合

最经典场景:应用日志 → Kafka → ES / HDFS / S3。

### 8.2 流处理

Kafka Streams / Flink on Kafka:

```
Kafka Topic A → Flink (window / join / filter) → Kafka Topic B
```

### 8.3 事件驱动架构 (EDA)

订单创建 → Kafka → 库存扣减 / 通知 / 推荐 / 分析 各自消费。

### 8.4 CDC (Change Data Capture)

Debezium 监听 MySQL/PostgreSQL binlog → Kafka → 下游系统:

```
MySQL → Debezium → Kafka → Redis(缓存) / ES(搜索) / HDFS(归档)
```

### 8.5 Kafka 工业规模

- LinkedIn:7 万+ Broker,千万+ topic/partition,日处理 100PB+
- Uber:数万 Broker,支撑核心交易
- Netflix:全球多 Region Kafka,日志 + 事件

---

## 9. 面试要点

**Q1: Kafka 为什么快?**

> (1) 顺序写磁盘(几百 MB/s);(2) Zero-Copy(sendfile);(3) Page Cache 加速读;(4) 批量压缩(snappy/lz4/zstd);(5) Partition 并行。综合下来单 Broker 吞吐 100MB/s+,是 RabbitMQ 的 100 倍。

**Q2: Kafka 怎么保证消息不丢?**

> (1) Producer 端 `acks=all` + `retries` + `enable_idempotence`;(2) Broker 端 `min.insync.replicas=2` + 副本数 3;(3) Consumer 端处理完再 `commitSync`,关闭自动提交;(4) `unclean.leader.election.enable=false` 禁止 OSR 当 Leader。

**Q3: ISR 是什么?**

> In-Sync Replicas,与 Leader 同步的副本集合。Follower 主动拉取 Leader 日志,落后超过 `replica.lag.time.max.ms` 移出 ISR,追上后重新加入。Leader 故障时只从 ISR 选新 Leader(`unclean.leader.election.enable=false` 时),保证数据不丢。

**Q4: Kafka 怎么实现 exactly-once?**

> (1) Idempotent Producer:producer_id + sequence_number 去重;(2) Transactional Producer:事务跨多 partition 原子;(3) Consumer `read_committed` 只读已提交;(4) 端到端 consume-process-produce 在一个事务内。底层仍是 at-least-once + 幂等去重。

**Q5: Rebalance 为什么是问题?**

> Rebalance 期间所有 consumer 停止消费(stop-the-world),大集群几分钟不可用。触发场景:consumer 加入/退出、心跳超时。解决:Cooperative Rebalance(增量)、Static Membership(fixed membership)、合理 session timeout。

**Q6: KRaft 是什么?为什么取代 ZK?**

> KRaft 是 Kafka 2.8+ 内置的 Raft 元数据管理,取代 ZK。原因:ZK 元数据规模受限(>20w partition 时成瓶颈)、Controller 切换慢、运维复杂。KRaft 把元数据存在内部 topic,用 Raft 同步,4.0 完全移除 ZK。

**Q7: Kafka 的 partition 数怎么选?**

> (1) 吞吐:partition 数 ≥ 期望吞吐 / 单 partition 吞吐;(2) 并行:partition 数 ≥ consumer 数;(3) 不要过多:元数据开销、文件句柄、rebalance 时间。典型 12~72。生产建议:小集群 6~12,大集群 24~72。

**Q8: Kafka 和 RabbitMQ 的选型?**

> Kafka:高吞吐、持久化、流处理、日志、事件驱动,百万 QPS。RabbitMQ:复杂路由、低延迟、业务消息、AMQP 协议,万级 QPS。简单说:大数据用 Kafka,业务消息用 RabbitMQ。

---

## 10. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Kreps et al., *Kafka* | 2011 NetDB | 分布式提交日志 |
| Wang et al., *Exactly-Once Delivery* | 2017 KIP-98 | 事务与幂等 |
| Apache KRaft Proposal | 2020 KIP-500 | 去除 ZK |

---

## 11. 交叉引用

- [[09-1-消息队列-基础与投递语义]]:MQ 基础
- [[09-3-消息队列-RocketMQ]]:对比 RocketMQ
- [[09-4-消息队列-Pulsar]]:对比 Pulsar
- [[05-共识-Raft]]:KRaft 共识
- [[12-幂等性]]:消费幂等

---

## 12. 速查表

```
核心数字:
  Partition: 单 broker 上千,单集群上万
  Replica: 3 (生产标配)
  acks: all (生产标配)
  min.insync.replicas: 2
  Segment: 1GB / 1 周
  retention: 7 天 (默认)

性能:
  单 broker 吞吐: 100MB/s+
  单 partition 吞吐: 10MB/s
  Producer batch: 16KB / linger 5ms
  Consumer poll: max 500 records

不丢配置:
  acks=all
  min.insync.replicas=2
  replication.factor=3
  unclean.leader.election.enable=false
  enable.idempotence=true

Exactly-Once:
  Producer: enable.idempotence + transactional.id
  Consumer: isolation.level=read_committed
  端到端: consume-process-produce 在事务内
```

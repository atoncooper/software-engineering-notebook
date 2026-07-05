# 消息队列 —— Pulsar 与计算存储分离

> 章号: §9.4
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: 🔥工程 🏭工业 🎓学术
> 前置: [[09-1-消息队列-基础与投递语义]] [[09-2-消息队列-Kafka深度]] [[08-3-存储-Bigtable与HBase]]

---

## 0. Pulsar 的差异化

Apache Pulsar(Yahoo 2016,开源)是新一代 MQ,核心创新:**计算(Broker)与存储(BookKeeper)分离**。

Kafka / RocketMQ 的 Broker 既负责协议又负责存储,Pulsar 把两者拆开:

```
Kafka:    Broker = 协议 + 存储(耦合)
Pulsar:   Broker = 协议(无状态) + BookKeeper = 存储(独立)
```

这一架构带来:

- **弹性扩缩容**:Broker 无状态,秒级扩缩
- **存储独立扩展**:加 Bookie 即可扩容
- **跨 DC 复制原生**:Geo-Replication
- **多租户原生**:从设计之初支持

---

## 1. 架构

```
              ┌──────────────────┐
              │  Producer        │
              └────────┬─────────┘
                       ↓
        ┌──────────────────────────────┐
        │  Broker (无状态,可水平扩展)    │
        │  - 协议处理                    │
        │  - 路由                        │
        │  - 缓存                        │
        └──────┬───────────────────────┘
               ↓
        ┌──────────────────────────────┐
        │  BookKeeper (存储集群)         │
        │  ┌──────┐ ┌──────┐ ┌──────┐  │
        │  │Bookie│ │Bookie│ │Bookie│  │  ← 持久化 ledger
        │  └──────┘ └──────┘ └──────┘  │
        └──────────────────────────────┘
               ↓
        ┌──────────────────────────────┐
        │  ZooKeeper (元数据)            │
        └──────────────────────────────┘
```

### 1.1 Broker

- 无状态(状态在 ZK)
- 处理 Producer/Consumer 协议
- 不持久化数据,只缓存(hot 数据)
- 故障切换极快(秒级)

### 1.2 BookKeeper

- 分布式日志存储
- 每个 Topic Partition 对应一个 **Ledger**
- Ledger 由多个 **Bookie**(存储节点)上的 **Entry** 组成
- Quorum 写:`ensemble-size` / `write-quorum` / `ack-quorum`

```
Ledger 配置:
  ensemble-size = 5 (写入的 Bookie 数)
  write-quorum = 3 (副本数)
  ack-quorum = 2 (写入 ACK 数)
  → 每条 entry 写到 3 个 Bookie,等 2 个 ACK 即返回
```

### 1.3 ZK

- Broker 注册
- Ledger 元数据
- Topic 元数据
- Consumer 订阅状态

---

## 2. 数据模型

### 2.1 Topic

```
persistent://tenant/namespace/topic
         ↑         ↑         ↑
     持久/非持久  租户      命名空间   主题
```

- **persistent**:持久化到 BookKeeper
- **non-persistent**:内存队列,不持久化,极低延迟

### 2.2 Subscription

Pulsar 独有的灵活订阅模式:

#### 2.2.1 Exclusive

```
Topic → Subscription → Consumer (1 个)
```

独占消费,简单但无并行。

#### 2.2.2 Shared

```
Topic → Subscription → Consumer 1
                    → Consumer 2
                    → Consumer 3
```

负载均衡,但失去顺序。

#### 2.2.3 Failover

```
Topic → Subscription → Consumer 1 (Master)
                    → Consumer 2 (Standby,Master 挂自动接管)
```

主备模式,保证顺序。

#### 2.2.4 Key_Shared

```
Topic → Subscription → Consumer 1 (key=A, key=C)
                    → Consumer 2 (key=B, key=D)
```

**Pulsar 独有**:同 key 落同 consumer,既并行又保序(同 key 内有序)。

### 2.3 Schema Registry

Pulsar 原生支持 schema(Protobuf / Avro / JSON):

```java
Producer<User> producer = client.newProducer(Schema.AVRO(User.class))
    .topic("users")
    .create();
producer.send(new User("Alice", 30));
```

Schema 不兼容时自动拒绝,避免下游反序列化失败。

---

## 3. 计算存储分离的优势

### 3.1 弹性扩缩容

- Kafka 扩容:加 Broker 后需手动迁移 partition(耗时、有风险)
- Pulsar 扩容:加 Broker 立即生效(无状态),加 Bookie 立即生效(Bookie 自动接管新 ledger)

### 3.2 故障切换快

- Kafka:Broker 故障 → Controller 选新 Leader → Follower 同步 → 恢复(秒-分钟)
- Pulsar:Broker 故障 → 新 Broker 接管(无状态,无数据迁移)→ 立即恢复(秒级)

### 3.3 存储扩展独立

- 业务消息量增加:加 Broker(协议层)
- 历史数据堆积:加 Bookie(存储层)
- 两层独立扩展,成本更优

### 3.4 跨 DC 复制原生

Pulsar 内置 Geo-Replication:

```
DC1 (Pulsar Cluster)         DC2 (Pulsar Cluster)
  Topic "events"      ←→     Topic "events"
        ↓                       ↓
   本地 Consumer            本地 Consumer
```

- 配置即可,无需外部工具
- 支持同步/异步
- 跨 Region 多活基础

---

## 4. 投递语义

### 4.1 Producer

```java
Producer<String> producer = client.newProducer()
    .topic("events")
    .sendTimeout(0, TimeUnit.SECONDS)
    .enableBatching(true)
    .compressionType(CompressionType.LZ4)
    .messageRoutingMode(MessageRoutingMode.SinglePartition)
    .create();

// 同步发送
MessageId id = producer.send("hello");

// 异步发送
producer.sendAsync("hello").thenAccept(id -> { /* ... */ });
```

### 4.2 Consumer

```java
Consumer<String> consumer = client.newConsumer()
    .topic("events")
    .subscriptionName("sub-1")
    .subscriptionType(SubscriptionType.Shared)
    .subscribe();

while (true) {
    Message<String> msg = consumer.receive();
    try {
        process(msg);
        consumer.acknowledge(msg);  // ACK
    } catch (Exception e) {
        consumer.negativeAcknowledge(msg);  // NACK,稍后重投
    }
}
```

### 4.3 exactly-once

- Producer 端:幂等 producer(去重)
- Consumer 端:去重表 / 业务幂等
- 端到端:transaction coordinator(2.x+)

---

## 5. 函数计算 (Pulsar Functions)

Pulsar 内置轻量级流处理:

```java
public class WordCountFunction implements Function<String, Void> {
    @Override
    public Void process(String input, Context context) {
        for (String word : input.split(" ")) {
            context.incrCounter(word, 1);  // 状态计数
        }
        return null;
    }
}
```

部署:

```bash
pulsar-admin functions create \
  --jar wordcount.jar \
  --classname WordCountFunction \
  --inputs words \
  --output counts
```

类似 Kafka Streams / Flink,但更轻量(单函数级)。

---

## 6. IO 模型

### 6.1 BookKeeper 的 Quorum 写

```
Producer → Broker → Bookie 1 ──┐
                  → Bookie 2 ──┤ (Quorum=2 即 ACK)
                  → Bookie 3 ──┘
```

- 写:`E=3` ensemble,`W=3` 写副本,`A=2` ACK Quorum
- 读:任意 Bookie 读(失败换另一个)
- 容错:任意 1 个 Bookie 故障不影响

### 6.2 Ledger 切换

- 当前 Ledger 写满 → 关闭 → 开新 Ledger
- Ledger 关闭时元数据持久化到 ZK
- Broker 故障 → 新 Broker 读 ZK 找当前 Ledger → 继续写

---

## 7. 工业应用

### 7.1 Yahoo

- Pulsar 起源于 Yahoo 内部
- 支撑 Yahoo 内部多个高吞吐场景(用户行为、广告)

### 7.2 Twitter

- 部分替换 Kafka(主要用场景多租户)
- 千万级 QPS

### 7.3 Splunk / StreamNative

- 商业化 Pulsar 服务
- 提供企业级支持和托管

### 7.4 国内案例

- 滴滴:替换部分 Kafka,解决扩容问题
- 360:多租户场景
- 哔哩哔哩:弹幕、消息

---

## 8. 对比 Kafka

| 维度 | Kafka | Pulsar |
|------|-------|--------|
| 架构 | Broker 含存储 | 计算存储分离 |
| 扩容 | 需迁移 partition | 加 Broker 即可 |
| 故障切换 | 选主 + Follower 同步 | 无状态切换(秒级) |
| 多租户 | 弱 | 原生支持 |
| 订阅模式 | Consumer Group | 4 种(Exclusive/Shared/Failover/Key_Shared) |
| 跨 DC | MirrorMaker(外部) | 原生 Geo-Replication |
| Schema | 弱(外部 Confluent) | 原生 Schema Registry |
| 函数计算 | Kafka Streams(库) | Pulsar Functions(内置) |
| 存储 | 每 partition 文件 | BookKeeper ledger |
| 生态 | 极成熟 | 成长中 |
| 学习曲线 | 中 | 高 |
| 性能 | 极高 | 高(接近 Kafka) |

### 8.1 Kafka 更适合

- 大数据生态(HDFS/Spark/Flink 集成成熟)
- 极致吞吐(Kafka 单 broker 100MB/s,Pulsar 略低)
- 团队已有 Kafka 经验

### 8.2 Pulsar 更适合

- 多租户 SaaS 平台
- 需要弹性扩缩容
- 跨 DC 多活
- 复杂订阅模式(Key_Shared 等)
- 长期数据存储(Pulsar + Tiered Storage)

---

## 9. 面试要点

**Q1: Pulsar 的核心创新是什么?**

> 计算存储分离:Broker 无状态(只处理协议+缓存),BookKeeper 独立存储。带来弹性扩缩容、故障切换快、存储独立扩展、跨 DC 复制原生等优势。

**Q2: Pulsar 的订阅模式有哪些?**

> (1) Exclusive:独占消费;(2) Shared:负载均衡(失去顺序);(3) Failover:主备;(4) Key_Shared:同 key 同 consumer,既并行又保序(Pulsar 独有)。

**Q3: BookKeeper 的 Quorum 写怎么工作?**

> 每 ledger 配 ensemble(E)、write-quorum(W)、ack-quorum(A)。每条 entry 写到 W 个 Bookie,等 A 个 ACK 返回。容 W-A 个 Bookie 故障。读时任意 Bookie 读,失败换另一个。

**Q4: Pulsar 和 Kafka 在扩容上的区别?**

> Kafka:加 Broker 后需手动迁移 partition(reassignment),耗时且有风险。Pulsar:加 Broker 立即生效(无状态),加 Bookie 立即生效(新 ledger 自动分布到新 Bookie)。

**Q5: Pulsar 的 Key_Shared 模式解决什么?**

> Kafka 在 Shared 模式下失去顺序,在 Exclusive 模式下失去并行。Key_Shared 让同 key 消息落同 consumer,既并行(不同 key 可分散)又保序(同 key 内有序)。这是 Pulsar 独有创新。

**Q6: Pulsar 的 Tiered Storage 是什么?**

> 把冷数据从 BookKeeper 迁移到对象存储(S3/HDFS),降低长期数据存储成本。热数据在 BookKeeper(SSD),冷数据在 S3(HDD/对象存储)。对用户透明。

**Q7: Pulsar 为什么没取代 Kafka?**

> (1) Kafka 生态成熟(大数据集成);(2) Pulsar 学习曲线高(计算存储分离 + BookKeeper);(3) Kafka 性能仍领先;(4) 团队迁移成本。Pulsar 在多租户、弹性扩缩容场景有优势,但不是"silver bullet"。

---

## 10. 论文延伸

| 论文 | 年届 | 关键贡献 |
|------|------|---------|
| Yahoo, *Pulsar* | 2016 | 计算存储分离 MQ |
| Apache BookKeeper | 2008 | 分布式日志存储 |

---

## 11. 交叉引用

- [[09-1-消息队列-基础与投递语义]]:MQ 基础
- [[09-2-消息队列-Kafka深度]]:对比 Kafka
- [[09-3-消息队列-RocketMQ]]:对比 RocketMQ
- [[09-5-消息队列-工业实战与对比]]:工业选型
- [[08-3-存储-Bigtable与HBase]]:分层存储思想

---

## 12. 速查表

```
架构: Broker (无状态) + BookKeeper (存储) + ZK (元数据)

订阅模式:
  Exclusive: 独占
  Shared: 负载均衡(无序)
  Failover: 主备
  Key_Shared: 同 key 同 consumer(并行+有序)

Quorum 配置:
  ensemble (E): 写入 Bookie 数
  write-quorum (W): 副本数
  ack-quorum (A): ACK 数
  容 W-A 个 Bookie 故障

vs Kafka:
  扩容: 加 Broker 即可 vs 需迁移 partition
  切换: 秒级 vs 分钟级
  多租户: 原生 vs 弱
  跨 DC: 原生 vs MirrorMaker
  生态: 成长中 vs 极成熟
```

# 工业案例 —— Kafka at LinkedIn

> 章号: §22.7
> 层级: 工业 / 案例
> 标记: 🏭工业 📜论文 ⭐高频
> 前置: [[09-2-消息队列-Kafka深度]] [[09-1-消息队列-基础与投递语义]] [[16-2-计算-Flink与流处理]]
> 论文: Kreps et al., *Kafka: a Distributed Messaging System for Log Processing*, 2011

---

## 1. 背景

LinkedIn 2010 开发 Kafka,用于:

- 用户活动跟踪(浏览、点击、搜索)
- 监控指标
- 日志聚合

需求:

- 高吞吐(每秒百万级事件)
- 持久化(可重放)
- 多消费者(同一事件多种用途)

2011 开源 → Apache 顶级项目 → 行业标准。

---

## 2. 在 LinkedIn 的使用

### 2.1 关键场景

| 场景 | 用途 |
|------|------|
| 活动跟踪 | PageView / Click / Search 事件 |
| 监控指标 | Application metric |
| 日志聚合 | 应用日志 |
| 数据管道 | 入 Hadoop / 数据仓库 |
| 实时推荐 | Broker → Flink/Spark → 模型 |
| 数据库 CDC | Databus → Kafka → 数据湖 |

### 2.2 规模(2020 公开数据)

- **brokers**:数千
- **topics**:数万
- **partitions**:百万级
- **每天消息**:数万亿
- **数据量**:PB/天

---

## 3. LinkedIn 的 Kafka 演进

### 3.1 早期(2010-2014)

- 单 DC
- ZooKeeper 协调
- 高吞吐,但 Exactly-Once 缺失

### 3.2 跨 DC 复制(2014-2017)

- MirrorMaker(开源)
- Cluster 到 Cluster 异步复制
- 跨大洲延迟问题

### 3.3 Exactly-Once(2017,Kafka 0.11)

- Idempotent Producer
- Transactional Producer
- Consumer read_committed

详见 [[09-2-消息队列-Kafka深度]]。

### 3.4 KRaft 模式(2020+,Kafka 2.8+)

- 移除 ZooKeeper 依赖
- 自管理元数据(Raft 协议)
- 简化部署,扩展性更好

---

## 4. Brooklin(LinkedIn 自研替代 MirrorMaker)

### 4.1 痛点

- MirrorMaker 每节点消费 + 生产,延迟高
- 扩展性差
- 不支持 DC 间传输 Non-Kafka 数据

### 4.2 Brooklin(2017+)

- 通用数据流中间件
- 支持 Kafka → Kafka、Kafka → 其他
- 横向扩展
- LinkedIn 生产规模(每秒亿级消息)

---

## 5. 数据管道

### 5.1 Lambda 架构(Legacy)

```
Source → Kafka → Batch (Hadoop/Spark, hourly)
              → Speed (Storm, real-time)
              → Serving (Druid/Espresso)
```

问题:两套代码维护、数据不一致。

### 5.2 Kappa 架构(现代)

```
Source → Kafka → Stream Processing (Flink/Spark Streaming)
              → Serving (Pinot/Espresso)
              → Audit (HDFS backup)
```

- 全部流处理
- 重放 = 重新跑 Kafka topic
- LinkedIn 推荐

---

## 6. LinkedIn 的 Kafka 工程优化

### 6.1 Producer 端

- **linger.ms + batch.size**:批量发送
- **compression**:lz4 / zstd(节省 70% 网络)
- **acks=all**:可靠性
- **enable.idempotence=true**:幂等

```java
Properties props = new Properties();
props.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG, "broker:9092");
props.put(ProducerConfig.ACKS_CONFIG, "all");
props.put(ProducerConfig.ENABLE_IDEMPOTENCE_CONFIG, "true");
props.put(ProducerConfig.COMPRESSION_TYPE_CONFIG, "zstd");
props.put(ProducerConfig.LINGER_MS_CONFIG, "10");
props.put(ProducerConfig.BATCH_SIZE_CONFIG, "16384");
```

### 6.2 Consumer 端

- **fetch.min.bytes**:批量拉取
- **max.poll.records**:控制每批
- **手动提交 offset**:处理完再 commit

### 6.3 Broker 端

- **Page Cache**:依赖 OS 缓存
- **Sendfile Zero-Copy**:2 次拷贝(传统 4 次)
- **分区数控制**:单 broker 建议 < 4000

### 6.4 监控

- **UnderReplicatedPartitions**:副本落后
- **ISR shrink/expansion**:ISR 变化
- **Request latency**:延迟
- **Consumer lag**:消费滞后

LinkedIn 内部用 **Cruise Control**(开源)自动平衡集群。

---

## 7. 关键设计决策

### 7.1 顺序写磁盘

- 顺序写性能 ≫ 随机写
- 几百 MB/s
- 比内存随机写还快

### 7.2 Page Cache 而非应用层缓存

- 利用 OS 缓存
- 重启不丢失(从磁盘 mmap)
- 简化应用

### 7.3 Partition 而非 Topic 并行

- 单 Topic 多 Partition
- 消费者组:每 Partition 一个消费者

### 7.4 Pull 而非 Push

- 消费者 Pull(控制速率)
- 适应不同消费速率
- 简化 Broker(无需跟踪每个消费者)

---

## 8. 跨 DC 复制

### 8.1 模式

- **MirrorMaker**(开源):简单,但瓶颈
- **Brooklin**(LinkedIn):高性能
- **Cluster Linking**(Kafka 2.4+):原生,Topic 级联邦

### 8.2 LinkedIn 实践

- 主 DC(US West)+ 备 DC(US East)
- 主 → 备异步复制
- 备 DC 仅消费,不写
- 故障时手动切

### 8.3 跨 DC 一致性

- 默认:最终一致(秒-分钟延迟)
- 强一致:牺牲可用性,极少用

---

## 9. 与其他 MQ 对比(工业实践)

| 维度 | Kafka | RocketMQ | Pulsar |
|------|-------|----------|--------|
| 部署 | Broker + ZK/KRaft | NameServer + Broker | Broker + BookKeeper + ZK |
| 单 Broker 吞吐 | 100MB/s+ | 100MB/s+ | 100MB/s+ |
| 延迟 | ms 级 | ms 级 | ms 级 |
| Exactly-Once | 0.11+ | 原生 | 原生 |
| 事务消息 | 有(弱) | 强(2PC) | 弱 |
| 计算/存储分离 | 否 | 否 | 是 |
| 多租户 | 弱 | 中 | 强 |

详见 [[09-5-消息队列-工业实战与对比]]。

---

## 10. 教训

### 10.1 Partition 数选择

- 太少:并发不够
- 太多:Broker 元数据开销
- 单 broker 建议 < 4000 partitions

### 10.2 消费者组设计

- 一组消费者:每 Partition 一个消费者
- 消费者数 > Partition 数:多余的空闲
- 消费者数 < Partition 数:负载不均

### 10.3 重平衡(Rebalance)优化

- 早期:Stop-the-world(影响所有消费者)
- Kafka 2.4+:Incremental Cooperative Rebalance
- Kafka 2.6+:Sticky Consumer Group(保 partition 绑定)

### 10.4 监控 Consumer Lag

- 关键指标:lag = log_end_offset - committed_offset
- 持续 lag → 消费跟不上生产
- 应对:扩消费者、优化消费逻辑

---

## 10.5 完整配置文件示例

### 10.5.1 Broker 配置(`server.properties`,生产级)

```properties
# ============ 基础 ============
broker.id=1
listeners=PLAINTEXT://:9092,SSL://:9093,SASL_SSL://:9094
advertised.listeners=PLAINTEXT://broker1.linkedin.com:9092,SSL://broker1.linkedin.com:9093
inter.broker.listener.name=SSL
listener.security.protocol.map=PLAINTEXT:PLAINTEXT,SSL:SSL,SASL_SSL:SASL_SSL

# ============ 日志(存储) ============
log.dirs=/data/kafka/logs-1,/data/kafka/logs-2
num.recovery.threads.per.data.dir=4
log.segment.bytes=1073741824            # 1GB segment
log.retention.hours=168                 # 7 天
log.retention.bytes=-1                  # 按时间,不限大小
log.cleanup.policy=delete               # delete | compact
log.segment.ms=604800000                # 7 天强制滚动
log.flush.interval.messages=10000
log.flush.interval.ms=60000

# ============ Topic 默认 ============
num.partitions=12
default.replication.factor=3
min.insync.replicas=2
unclean.leader.election.enable=false     # 关键!防止数据丢失
auto.create.topics.enable=false          # 生产关闭自动创建

# ============ 网络 ============
num.network.threads=8
num.io.threads=16
socket.send.buffer.bytes=1048576
socket.receive.buffer.bytes=1048576
socket.request.max.bytes=104857600
queued.max.requests=500

# ============ 复制 ============
controller.socket.timeout.ms=30000
controller.message.queue.size=10
replica.fetch.max.bytes=1048576
replica.fetch.wait.max.ms=500
replica.fetch.min.bytes=1
replica.lag.time.max.ms=10000           # ISR 落后 10s 踢出
replica.socket.timeout.ms=60000
num.replica.fetchers=4

# ============ Group 协调 ============
group.initial.rebalance.delay.ms=3000
group.consumer.assignors=org.apache.kafka.clients.consumer.CooperativeStickyAssignor

# ============ 事务 ============
transaction.state.log.replication.factor=3
transaction.state.log.min.isr=2
transaction.max.timeout.ms=900000

# ============ Quota(多租户) ============
quota.producer.default=104857600        # 100MB/s
quota.consumer.default=104857600
quota.window.num=11
quota.window.size.seconds=1

# ============ ZK(KRaft 模式下不需要) ============
zookeeper.connect=zk1:2181,zk2:2181,zk3:2181/kafka
zookeeper.session.timeout.ms=18000
zookeeper.connection.timeout.ms=6000

# ============ JMX ============
metric.reporters=com.linkedin.kafka.monitor.KafkaMetricsReporter
metrics.num.samples=15
metrics.sample.window.ms=60000

# ============ LinkedIn 扩展 ============
authorizer.class.name=com.linkedin.kafka.security.acl.SimpleAclAuthorizer
super.users=User:CN=kafka-broker;User:CN=kafka-admin
```

### 10.5.2 KRaft 模式配置(Kafka 3.3+,无 ZK)

```properties
# ============ KRaft 角色 ============
process.roles=broker,controller
node.id=1
controller.quorum.voters=1@broker1:9093,2@broker2:9093,3@broker3:9093

listeners=PLAINTEXT://:9092,CONTROLLER://:9093
controller.listener.names=CONTROLLER
inter.broker.listener.name=PLAINTEXT

# ============ 元数据日志 ============
metadata.log.dir=/data/kafka/meta
metadata.log.max.snapshot.interval.ms=3600000
metadata.max.idle.ms=600000

# 其他与 ZK 模式相同
```

### 10.5.3 Producer 配置(生产级)

```java
Properties props = new Properties();
props.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG,
    "broker1:9092,broker2:9092,broker3:9092");

// === 可靠性 ===
props.put(ProducerConfig.ACKS_CONFIG, "all");              // 所有 ISR 确认
props.put(ProducerConfig.ENABLE_IDEMPOTENCE_CONFIG, true); // 幂等(0.11+)
props.put(ProducerConfig.RETRIES_CONFIG, Integer.MAX_VALUE);
props.put(ProducerConfig.MAX_IN_FLIGHT_REQUESTS_PER_CONNECTION, 5);

// === 批量 ===
props.put(ProducerConfig.BATCH_SIZE_CONFIG, 65536);        // 64KB
props.put(ProducerConfig.LINGER_MS_CONFIG, 10);             // 等 10ms 攒批
props.put(ProducerConfig.COMPRESSION_TYPE_CONFIG, "zstd");  // 压缩(snappy/lz4/zstd/gzip)
props.put(ProducerConfig.BUFFER_MEMORY_CONFIG, 67108864);   // 64MB 缓冲

// === 超时 ===
props.put(ProducerConfig.DELIVERY_TIMEOUT_MS_CONFIG, 120000); // 2min 总超时
props.put(ProducerConfig.REQUEST_TIMEOUT_MS_CONFIG, 30000);
props.put(ProducerConfig.METADATA_MAX_AGE_CONFIG, 300000);

// === 事务 ===
props.put(ProducerConfig.TRANSACTIONAL_ID_CONFIG, "order-tx-" + instanceId);
props.put(ProducerConfig.TRANSACTION_TIMEOUT_CONFIG, 60000);

// === 序列化 ===
props.put(ProducerConfig.KEY_SERIALIZER_CLASS_CONFIG,
    "org.apache.kafka.common.serialization.StringSerializer");
props.put(ProducerConfig.VALUE_SERIALIZER_CLASS_CONFIG,
    "io.confluent.kafka.serializers.KafkaAvroSerializer");
props.put("schema.registry.url", "http://schema-registry:8081");

KafkaProducer<String, Order> producer = new KafkaProducer<>(props);

// 事务发送
producer.initTransactions();
try {
    producer.beginTransaction();
    producer.send(new ProducerRecord<>("orders", order.getId(), order));
    producer.send(new ProducerRecord<>("order-events", event.getId(), event));
    producer.commitTransaction();
} catch (ProducerFencedException | OutOfOrderSequenceException | AuthorizationException e) {
    producer.close();
} catch (KafkaException e) {
    producer.abortTransaction();
}
```

### 10.5.4 Consumer 配置

```java
Properties props = new Properties();
props.put(ConsumerConfig.BOOTSTRAP_SERVERS_CONFIG, "broker1:9092,broker2:9092");
props.put(ConsumerConfig.GROUP_ID_CONFIG, "order-processor");

// === 反序列化 ===
props.put(ConsumerConfig.KEY_DESERIALIZER_CLASS_CONFIG,
    "org.apache.kafka.common.serialization.StringDeserializer");
props.put(ConsumerConfig.VALUE_DESERIALIZER_CLASS_CONFIG,
    "io.confluent.kafka.serializers.KafkaAvroDeserializer");
props.put("schema.registry.url", "http://schema-registry:8081");
props.put("specific.avro.reader", true);

// === Offset 管理 ===
props.put(ConsumerConfig.ENABLE_AUTO_COMMIT_CONFIG, false); // 手动提交
props.put(ConsumerConfig.AUTO_OFFSET_RESET_CONFIG, "earliest");

// === 事务隔离 ===
props.put(ConsumerConfig.ISOLATION_LEVEL_CONFIG, "read_committed");

// === 批量拉取 ===
props.put(ConsumerConfig.FETCH_MIN_BYTES_CONFIG, 1024);       // 1KB
props.put(ConsumerConfig.FETCH_MAX_BYTES_CONFIG, 52428800);   // 50MB
props.put(ConsumerConfig.MAX_POLL_RECORDS_CONFIG, 500);
props.put(ConsumerConfig.MAX_POLL_INTERVAL_MS_CONFIG, 300000); // 5min 处理时间

// === 心跳 ===
props.put(ConsumerConfig.SESSION_TIMEOUT_MS_CONFIG, 30000);
props.put(ConsumerConfig.HEARTBEAT_INTERVAL_MS_CONFIG, 10000);

// === Partition 分配 ===
props.put(ConsumerConfig.PARTITION_ASSIGNMENT_STRATEGY_CONFIG,
    "org.apache.kafka.clients.consumer.CooperativeStickyAssignor");

KafkaConsumer<String, Order> consumer = new KafkaConsumer<>(props);
consumer.subscribe(Arrays.asList("orders"));

while (running) {
    ConsumerRecords<String, Order> records = consumer.poll(Duration.ofMillis(500));
    for (ConsumerRecord<String, Order> record : records) {
        processOrder(record.value());
    }
    // 处理完再提交(Exactly-Once 关键)
    consumer.commitSync();
}
```

### 10.5.5 Topic 创建(CLI + JSON)

```bash
# 创建 topic
kafka-topics.sh --bootstrap-server broker1:9092 \
  --create --topic orders \
  --partitions 24 \
  --replication-factor 3 \
  --config min.insync.replicas=2 \
  --config cleanup.policy=compact,delete \
  --config segment.ms=604800000 \
  --config retention.ms=2592000000 \
  --config compression.type=zstd \
  --config max.message.bytes=1048576

# 查看 topic 详情
kafka-topics.sh --bootstrap-server broker1:9092 --describe --topic orders

# 查看 consumer group lag
kafka-consumer-groups.sh --bootstrap-server broker1:9092 \
  --describe --group order-processor
```

### 10.5.6 Cruise Control 配置(自动均衡)

```json
// cruise-control/config/cruisecontrol.properties
{
  "bootstrap.servers": "broker1:9092,broker2:9092,broker3:9092",
  "zookeeper.connect": "zk1:2181/kafka",
  "partition.metric.sample.store.topic": "__CruiseControlPartitionMetricSamples",
  "broker.metric.sample.store.topic": "__CruiseControlBrokerMetricSamples",
  "num.partition.metrics.samples": 50000,
  "num.broker.metrics.samples": 50000,
  "metric.sampling.interval.ms": 60000,
  "goals": "com.linkedin.kafka.cruisecontrol.analyzer.goals.RackAwareGoal,...",
  "default.goals": "com.linkedin.kafka.cruisecontrol.analyzer.goals.RackAwareGoal,...",
  "anomaly.detection.goals": "com.linkedin.kafka.cruisecontrol.analyzer.goals.RackAwareGoal",
  "self.healing.enabled": "true",
  "execution.progress.check.interval.ms": 10000
}
```

### 10.5.7 K8s 部署(Strimzi Operator)

```yaml
# kafka-cluster.yaml
apiVersion: kafka.strimzi.io/v1beta2
kind: Kafka
metadata:
  name: linkedin-kafka
spec:
  kafka:
    replicas: 6
    version: 3.5.0
    storage:
      type: jbod
      volumes:
        - id: 0
          type: persistent-claim
          size: 2Ti
          class: ssd-sc
          deleteClaim: false
    listeners:
      - name: plain
        port: 9092
        type: internal
        tls: false
      - name: tls
        port: 9093
        type: internal
        tls: true
      - name: external
        port: 9094
        type: loadbalancer
        tls: true
    config:
      offsets.topic.replication.factor: 3
      transaction.state.log.replication.factor: 3
      transaction.state.log.min.isr: 2
      default.replication.factor: 3
      min.insync.replicas: 2
      unclean.leader.election.enable: false
      compression.type: zstd
      log.message.format.version: "3.5"
    resources:
      requests:
        memory: 16Gi
        cpu: 4
      limits:
        memory: 32Gi
        cpu: 8
    rack:
      topologyKey: topology.kubernetes.io/zone
  zookeeper:
    replicas: 3
    storage:
      type: persistent-claim
      size: 100Gi
      class: ssd-sc
      deleteClaim: false
  entityOperator:
    topicOperator: {}
    userOperator: {}
```

---

## 11. 速查表

```
Kafka @ LinkedIn:
  场景: 活动跟踪、监控、日志、CDC、推荐
  
规模 (2020):
  数千 broker
  数万 topic
  百万级 partition
  PB/天数据
  
演进:
  2010-14: 单 DC + ZK
  2014-17: 跨 DC 复制 (MirrorMaker → Brooklin)
  2017:   Exactly-Once (0.11)
  2020+:   KRaft (去 ZK)

工程优化:
  Producer: linger + batch + zstd + idempotence
  Consumer: fetch.min + max.poll.records + 手动 commit
  Broker: Page Cache + Sendfile + 分区数控制
  Monitor: URP + ISR + Lag (Cruise Control)
  
架构:
  Kappa (流处理为主, 替代 Lambda)
  
教训:
  Partition 数控制 (<4000/broker)
  Consumer 数 = Partition 数
  Rebalance 优化 (Sticky)
  Consumer Lag 监控
```

---

## 12. 交叉引用

- [[09-2-消息队列-Kafka深度]]:Kafka 原理
- [[09-5-消息队列-工业实战与对比]]:MQ 对比
- [[16-2-计算-Flink与流处理]]:Flink + Kafka
- [[13-2-治理-可观测性与混沌工程]]:监控

---

## 13. 参考文献

- Kreps et al. *Kafka: a Distributed Messaging System for Log Processing*. NetDB 2011.
- Apache Kafka Documentation. https://kafka.apache.org/documentation
- Narkhede. *Kafka at LinkedIn*. QCon 2014.
- Goodhope et al. *Building LinkedIn's Real-time Activity Data Pipeline*. IEEE DE 2012.
- Apache Brooklin. https://brooklin.io
- LinkedIn Engineering Blog. https://engineering.linkedin.com
- Cruise Control. https://github.com/linkedin/cruise-control

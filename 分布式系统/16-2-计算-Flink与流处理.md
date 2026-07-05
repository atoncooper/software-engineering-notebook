# 计算 —— Flink 与流处理

> 章号: §16.2
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业 🎓学术
> 前置: [[16-1-计算-MapReduce与Spark]] [[09-2-消息队列-Kafka深度]] [[03-时间与时钟]]

---

## 0. 流处理 vs 批处理

- **批处理**:处理有界数据集(历史数据),延迟分钟-小时
- **流处理**:处理无界数据流(实时数据),延迟毫秒-秒

工业需求:

- 实时数仓(分钟级报表)
- 实时风控(秒级决策)
- 实时推荐(毫秒级响应)
- IoT 实时监控

Spark 用"微批"模拟流(秒级),Flink 是真流(毫秒级)。

---

## 1. Flink 架构

### 1.1 整体

```
              ┌─────────────────┐
              │  JobManager     │
              │  (调度、协调)    │
              └────┬────────────┘
                   ↓
       ┌───────────┴───────────┐
       ↓                       ↓
  ┌──────────┐            ┌──────────┐
  │TaskManager│           │TaskManager│  ← 执行 Task
  │  (TM1)   │            │  (TM2)   │
  └──────────┘            └──────────┘
       ↑                       ↑
       └───── 数据流(网络) ─────┘
```

- **JobManager**:调度、Checkpoint 协调、故障恢复
- **TaskManager**:实际执行 Task,有 slot 资源
- **Task**:算子实例(并行度决定实例数)

### 1.2 流处理模型

> Flink 把一切视为流:

- 流处理:无界数据流
- 批处理:有界数据流(流的一种特例)

```
Source → Map → KeyBy → Window → Reduce → Sink
                              ↑
                          触发计算
```

### 1.3 DataStream API

```java
StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

DataStream<Event> events = env
    .addSource(new FlinkKafkaConsumer<>("events", new EventSchema(), props))
    .name("kafka-source");

events
    .filter(e -> e.type == "click")
    .keyBy(e -> e.userId)
    .window(TumblingEventTimeWindows.of(Time.minutes(1)))
    .aggregate(new CountAgg())
    .addSink(new ClickHouseSink());
```

---

## 2. 时间语义

### 2.1 三种时间

- **Event Time**:事件发生时间(数据携带)
- **Ingestion Time**:数据进入 Flink 时间
- **Processing Time**:处理时间(机器时钟)

### 2.2 Watermark

> 📜 Flink 借鉴 Google MillWheel 的 Watermark 机制

Watermark 是"时间戳 T",表示"事件时间 ≤ T 的数据都已到达"。

```
事件流: (A, t=10), (B, t=12), (C, t=8), (D, t=15)
Watermark: T=12 (表示 t<=12 都到,但 C t=8 来迟了)

→ 窗口 [10, 20) 在 watermark >= 20 时触发
```

```java
env.setStreamTimeCharacteristic(TimeCharacteristic.EventTime);

events
    .assignTimestampsAndWatermarks(
        WatermarkStrategy.<Event>forBoundedOutOfOrderness(Duration.ofSeconds(5))
            .withTimestampAssigner((e, ts) -> e.timestamp))
    .keyBy(e -> e.userId)
    .window(TumblingEventTimeWindows.of(Time.minutes(1)))
    .aggregate(...);
```

### 2.3 迟到数据处理

- Watermark 触发窗口后,迟到数据默认丢弃
- `allowedLateness(Duration)`:允许延迟多久仍更新窗口
- `sideOutputLateData`:迟到数据送到旁路输出

```java
events
    .keyBy(e -> e.userId)
    .window(TumblingEventTimeWindows.of(Time.minutes(1)))
    .allowedLateness(Duration.ofSeconds(30))
    .sideOutputLateData(lateTag)
    .aggregate(...);
```

---

## 3. 窗口

### 3.1 窗口类型

| 类型 | 含义 | 例子 |
|------|------|------|
| **Tumbling** | 滚动,不重叠 | 每分钟 |
| **Sliding** | 滑动,可重叠 | 每 30 秒,窗口 1 分钟 |
| **Session** | 会话,按 gap 切分 | 用户会话 |
| **Global** | 全局(需自定义触发) | 全局 TopN |

### 3.2 触发器

- EventTimeTrigger:Watermark 到时触发
- ProcessingTimeTrigger:处理时间到时触发
- CountTrigger:元素数到时触发
- 自定义

### 3.3 驱逐器

- 时间过期驱逐
- 数量超限驱逐

---

## 4. State 管理

### 4.1 状态类型

- **Keyed State**:按 key 分组(只能在 KeyedStream 后用)
  - ValueState:单值
  - ListState:列表
  - MapState:Map
  - ReducingState / AggregatingState
- **Operator State**:算子级(如 Kafka source 的 offset)

### 4.2 状态后端

- **MemoryStateBackend**:内存,测试用
- **FsStateBackend**:内存 + Checkpoint 到 HDFS
- **RocksDBStateBackend**:RocksDB(磁盘),支持大状态

```java
env.setStateBackend(new EmbeddedRocksDBStateBackend());
env.getCheckpointConfig().setCheckpointStorage("hdfs:///flink/checkpoints");
```

### 4.3 状态 TTL

```java
StateTtlConfig ttl = StateTtlConfig.newBuilder(Time.hours(1))
    .setUpdateType(StateTtlConfig.UpdateType.OnCreateAndWrite)
    .setStateVisibility(StateTtlConfig.StateVisibility.NeverReturnExpired)
    .build();

ValueStateDescriptor<String> desc = new ValueStateDescriptor<>("lastSeen", String.class);
desc.enableTimeToLive(ttl);
```

---

## 5. Checkpoint 容错

### 5.1 Chandy-Lamport 算法

> 📜 Chandy & Lamport, 1985 — *Distributed Snapshots*

Flink Checkpoint 基于 Chandy-Lamport 分布式快照算法:

1. JobManager 向 Source 注入 **Checkpoint Barrier**
2. Barrier 随数据流向下传播
3. 算子收到 Barrier 后:
   - 对齐(等所有上游的 Barrier)
   - 把状态快照到持久化存储
   - 把 Barrier 转发给下游
4. 所有算子完成 → Checkpoint 成功

```
数据流:  [A B C |barrier| D E F]
                  ↑
              Checkpoint 点
```

### 5.2 Barrier 对齐

- 算子有多个上游时,需等所有上游的 Barrier
- 对齐期间缓冲数据(背压)
- Flink 1.11+ 支持 **Unaligned Checkpoint**:不对齐,直接转发,适合反压场景

### 5.3 Exactly-Once

- Source:可重放(Kafka offset 记录在 Checkpoint)
- 算子:状态快照
- Sink:两阶段提交(2PC)

```java
// Kafka Sink 两阶段提交
KafkaSink<String> sink = KafkaSink.<String>builder()
    .setBootstrapServers("kafka:9092")
    .setRecordSerializer(...)
    .setDeliveryGuarantee(DeliveryGuarantee.EXACTLY_ONCE)
    .setTransactionalIdPrefix("flink-tx-")
    .build();
```

### 5.4 故障恢复

- TaskManager 故障:从最近 Checkpoint 恢复
- JobManager 故障:HA(基于 ZK/etcd 选主)
- Source 重放(Kafka offset 回退)
- Sink 回滚(2PC abort 旧事务)

---

## 6. Flink SQL

```sql
-- 创建 Kafka source 表
CREATE TABLE events (
    user_id BIGINT,
    event_type STRING,
    event_time TIMESTAMP(3),
    WATERMARK FOR event_time AS event_time - INTERVAL '5' SECOND
) WITH (
    'connector' = 'kafka',
    'topic' = 'events',
    'properties.bootstrap.servers' = 'kafka:9092',
    'format' = 'json'
);

-- 实时窗口聚合
INSERT INTO clicks_per_minute
SELECT
    user_id,
    TUMBLE_START(event_time, INTERVAL '1' MINUTE) AS window_start,
    COUNT(*) AS cnt
FROM events
WHERE event_type = 'click'
GROUP BY user_id, TUMBLE(event_time, INTERVAL '1' MINUTE);
```

---

## 7. Flink vs Spark

| 维度 | Flink | Spark |
|------|-------|-------|
| 计算模型 | 真流(事件驱动) | 微批 |
| 延迟 | 毫秒级 | 秒级 |
| 容错 | Checkpoint(Chandy-Lamport) | RDD 血缘 |
| 状态 | 一等公民(原生) | 弱(Structured Streaming 才有) |
| Exactly-Once | 原生 | 需配置 |
| 时间语义 | Event Time + Watermark | 弱 |
| 批处理 | 也可(支持) | 强 |
| 生态 | 流处理领域第一 | 大数据全场景 |

---

## 8. 工业应用

### 8.1 实时数仓

```
Kafka → Flink (CDC / ETL / 聚合) → Kafka / ClickHouse / Hudi
                                 → BI 报表 / 实时大屏
```

### 8.2 实时风控

```
交易事件 → Flink (规则引擎 / 模型推理) → 风控决策(秒级)
```

### 8.3 实时推荐

```
用户行为 → Flink (特征计算) → 特征存储 → 推荐模型
```

### 8.4 IoT 监控

```
设备数据 → Flink (异常检测) → 告警
```

### 8.5 案例

- 阿里:双 11 实时大屏(成交量、GMV)
- 字节:实时推荐、广告
- Uber:实时 ETA、欺诈检测
- Netflix:实时观看指标

---

## 9. 面试要点

**Q1: 流处理和批处理的区别?**

> 批处理:有界数据集,延迟分钟-小时(MapReduce/Spark)。流处理:无界数据流,延迟毫秒-秒(Flink)。Flink 把批视为流的特例(有界流),统一计算模型。

**Q2: Event Time 和 Processing Time 的区别?**

> Event Time:事件实际发生时间(数据携带),不受处理延迟影响。Processing Time:处理时机器时钟。流处理推荐 Event Time,避免因处理延迟导致结果错误。Watermark 机制解决 Event Time 下"数据迟到"问题。

**Q3: Watermark 是什么?**

> 一个时间戳 T,表示"事件时间 ≤ T 的数据都已到达"。Watermark 由 Source 或算子定期生成,随数据流传播。窗口在 Watermark ≥ 窗口结束时间时触发。允许处理乱序数据(设 boundedOutOfOrderness)。

**Q4: Flink Checkpoint 怎么工作?**

> 基于 Chandy-Lamport 算法:JobManager 注入 Barrier,Barrier 随数据流传播;算子收到 Barrier 后对齐(等所有上游)、状态快照、转发 Barrier;所有算子完成 → Checkpoint 成功。故障时从最近 Checkpoint 恢复,Source 重放(Kafka offset),Sink 2PC。

**Q5: Flink 怎么实现 Exactly-Once?**

> Source:可重放(Kafka offset 在 Checkpoint);算子:状态快照;Sink:两阶段提交(2PC,事务写 Kafka/DB)。故障时 Source 重放 + Sink 回滚,达到端到端 Exactly-Once。

**Q6: Aligned vs Unaligned Checkpoint?**

> Aligned:算子收到 Barrier 后等所有上游(对齐),背压期间 Checkpoint 慢。Unaligned(Flink 1.11+):不对齐,直接转发 Barrier,缓冲中的数据作为状态保存。适合反压场景,代价是状态变大。

**Q7: Flink 和 Spark Streaming 的区别?**

> 计算模型:Flink 真流(毫秒),Spark 微批(秒级);状态:Flink 原生,Spark 弱;Exactly-Once:Flink 原生,Spark 需配置;Event Time + Watermark:Flink 强,Spark 弱。流处理首选 Flink。

**Q8: Flink 状态怎么管理?**

> Keyed State(按 key):ValueState/ListState/MapState 等。Operator State(算子级):如 Kafka offset。状态后端:Memory(测试)/ Fs(内存+HDFS)/ RocksDB(磁盘,大状态)。TTL 控制状态生命周期。Checkpoint 持久化状态。

---

## 10. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Chandy & Lamport, *Distributed Snapshots* | 1985 | Checkpoint 算法基础 |
| Carbone et al., *Flink* | 2015 | 流批一体 |
| Akidau et al., *MillWheel* | 2013 | Watermark 工业实践 |
| Akidau et al., *Dataflow Model* | 2015 | 流处理理论模型 |

---

## 11. 交叉引用

- [[16-1-计算-MapReduce与Spark]]:对比 Spark
- [[09-2-消息队列-Kafka深度]]:Flink Kafka Source/Sink
- [[03-时间与时钟]]:Event Time / Watermark
- [[06-事务-2PC与3PC]]:Flink Sink 2PC
- [[12-幂等性]]:Sink 幂等

---

## 12. 速查表

```
Flink 核心:
  流批一体 (批是流的特例)
  Event Time + Watermark (处理乱序)
  State + Checkpoint (容错)
  Exactly-Once (Source 重放 + Sink 2PC)

时间语义:
  Event Time: 事件实际时间 (推荐)
  Ingestion Time: 进入 Flink 时间
  Processing Time: 处理时间

Watermark: "≤T 都已到达"
  forBoundedOutOfOrderness(Duration)

窗口:
  Tumbling (滚动) / Sliding (滑动) / Session (会话) / Global

状态后端:
  Memory (测试) / Fs (内存+HDFS) / RocksDB (大状态)

Checkpoint:
  Chandy-Lamport 算法
  Barrier 随数据流传播
  Aligned (对齐) / Unaligned (反压场景)

vs Spark:
  真流 vs 微批
  毫秒 vs 秒
  状态原生 vs 弱
  Exactly-Once 原生 vs 需配置
```

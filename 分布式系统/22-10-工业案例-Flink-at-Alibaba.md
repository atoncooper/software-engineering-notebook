# 工业案例 —— Flink at Alibaba / Bilibili 流处理工业实战

> 章号: §22.10
> 层级: 工业 / 案例
> 标记: 🏭工业 📜论文 ⭐高频 🔥工程
> 前置: [[16-2-计算-Flink与流处理]] [[09-2-消息队列-Kafka深度]] [[13-2-治理-可观测性与混沌工程]]
> 来源: Alibaba / Bilibili Flink 团队公开分享 + Flink Forward China 历年演讲

---

## 1. 背景

阿里与 Bilibili 是中国 Flink 的两个最重投入方:

- **Alibaba**:自研 Blink(早期 Flink 分支),后回归社区;双 11 实时大屏、推荐
- **Bilibili**:实时数仓、弹幕、推荐、风控

特点:

- 海量数据(PB/天)
- 极低延迟(秒级)
- 大状态(TB 级)

---

## 2. Alibaba Flink 工业实战

### 2.1 演进

```
2015: 自研 Galaxy (基于 Storm)
2016: 转 JStorm (Storm 改进)
2017: 引入 Flink, 自研 Blink (Flink 分支)
2019: Blink 回归社区 (合并到 Flink 1.9+)
2020+: Flink 主力 + Flink SQL 主流
```

### 2.2 双 11 实时大屏

**场景**:实时展示成交量、GMV、品类分布等。

**架构**:

```
Binlog → DTS → Kafka → Flink (聚合)
                    ↓
                Hologres (实时数仓) → 大屏
                    ↓
                HBase / ClickHouse (明细)
```

**关键技术**:

- **大状态**:TB 级(用 RocksDB)
- **Unaligned Checkpoint**:反压场景
- **Flink SQL**:业务方低门槛开发
- **CEP**:实时风控规则

### 2.3 Blink(Flink 商业版)关键改进

#### 2.3.1 Flink SQL 增强

- 流批统一 SQL
- DDL/Catalog(类 Hive)
- Python UDF
- Connectors(面向数据湖:Hudi/Iceberg)

```sql
-- 创建源表
CREATE TABLE binlog_orders (
    order_id BIGINT,
    user_id BIGINT,
    amount DECIMAL(10, 2),
    ts TIMESTAMP(3),
    WATERMARK FOR ts AS ts - INTERVAL '5' SECOND
) WITH (
    'connector' = 'kafka',
    'topic' = 'orders',
    ...
);

-- 实时聚合
INSERT INTO gmv_per_minute
SELECT
    TUMBLE_START(ts, INTERVAL '1' MINUTE) AS win,
    SUM(amount) AS gmv
FROM binlog_orders
GROUP BY TUMBLE(ts, INTERVAL '1' MINUTE);
```

#### 2.3.2 Streaming Warehouse(流式数仓)

- Flink + Hudi/Iceberg
- 实时入湖
- 增量查询

### 2.4 阿里 Flink 规模

- 万级 TaskManager
- 单 Job 状态 TB 级
- 百万级 QPS
- 每天 PB 级数据

---

## 3. Bilibili Flink 工业实战

### 3.1 场景

| 场景 | 用途 |
|------|------|
| 实时数仓 | 报表、监控 |
| 弹幕推荐 | 实时特征 |
| UP 主分析 | 数据洞察 |
| 风控 | 异常行为检测 |
| 广告 | 实时竞价特征 |

### 3.2 架构

```
业务 DB → Canal (CDC) → Kafka → Flink → ClickHouse / Redis / HBase
                                  ↓
                              实时大屏 / 推荐系统
```

### 3.3 关键技术

#### 3.3.1 Flink on YARN / K8s

- YARN(早期)
- K8s(现代,弹性伸缩)

#### 3.3.2 Flink SQL 主导

- 80% 业务用 Flink SQL
- DataStream 仅用于复杂场景

#### 3.3.3 Catalog 集成

- 接入内部元数据系统(类 Hive Metastore)
- 跨 Job 共享 schema

### 3.4 Bilibili 规模

- 千级 Flink Job
- 万级 Kafka topic
- 单 Job 状态 GB-TB

---

## 4. 工业级 Flink 优化

### 4.1 大状态管理

- **RocksDB State Backend**(默认)
- **增量 Checkpoint**:只传增量,大幅减少 IO
- **状态 TTL**:控制增长

```java
env.setStateBackend(new EmbeddedRocksDBStateBackend());
env.getCheckpointConfig().setCheckpointStorage("hdfs:///flink/ckpt");
env.getCheckpointConfig().enableIncrementalCheckpointing();
env.getCheckpointConfig().setMinPauseBetweenCheckpoints(30000);
env.getCheckpointConfig().setCheckpointTimeout(600000);
```

### 4.2 反压处理

- **定位**:Flink Web UI / Metrics
- **原因**:数据倾斜、慢算子、外部依赖
- **解决**:
  - Unaligned Checkpoint(避免超时)
  - 增加并行度
  - Salt 解决倾斜

### 4.3 Checkpoint 优化

- **间隔**:30s-5min(根据 SLA)
- **超时**:合理设置(默认 10min)
- **存储**:HDFS / S3
- **并发**:限制(防资源争抢)

### 4.4 Flink SQL 优化

- **Mini-batch**:微批聚合,减少状态访问
- **Local-Global Aggregation**:本地预聚合 → 全局聚合
- **去重**:防止数据倾斜

```sql
-- Mini-batch
SET 'table.exec.mini-batch.enabled' = 'true';
SET 'table.exec.mini-batch.allow-latency' = '1s';
SET 'table.exec.mini-batch.size' = '1000';

-- Local-Global
SET 'table.optimizer.agg-phase-strategy' = 'TWO_PHASE';
```

---

## 5. 流式数仓(Streaming Warehouse)

### 5.1 传统 Lambda 痛点

- 批流两套代码
- 数据不一致
- 维护成本高

### 5.2 现代 Kappa + 流式数仓

```
Binlog → Flink (CDC / ETL) → Hudi/Iceberg (湖)
                          → ClickHouse (实时 OLAP)
                          → Kafka (下游流)
```

- 一套 Flink 代码
- 数据湖(Hudi/Iceberg)支持更新
- 实时入湖 + 增量查询

### 5.3 阿里 Hologres

- 自研实时数仓
- 与 Flink 深度集成
- 支持实时写入 + 实时查询

---

## 6. 阿里 Ververica Platform(商业版)

- 原 Flink 创始团队创立
- 阿里 2018 收购 data Artisans
- 商业版 Flink 平台(管理 + 监控)

---

## 7. 教训

### 7.1 状态膨胀

- **问题**:状态持续增长,Checkpoint 慢
- **解决**:
  - TTL 过期
  - 业务设计:只存必要状态
  - 增量 Checkpoint

### 7.2 数据倾斜

- **问题**:某 key 数据量过大,某 TaskManager 跑不动
- **解决**:
  - Salt 打散 key
  - Local-Global 两阶段聚合
  - 自定义 Partitioner

### 7.3 Checkpoint 失败

- **原因**:状态太大 / 反压 / 存储慢
- **解决**:
  - Unaligned Checkpoint
  - 减少状态
  - 升级存储(HDFS → SSD)

### 7.4 Flink SQL 业务化

- **优势**:低门槛,业务方自服务
- **代价**:复杂场景仍需 DataStream
- **混合**:SQL 80% + DataStream 20%

---

## 8. 性能与规模

| 维度 | Alibaba | Bilibili |
|------|---------|----------|
| Job 数 | 万级 | 千级 |
| 单 Job 状态 | TB 级 | GB-TB |
| 并行度 | 万级 | 千级 |
| Kafka topic | 万级 | 万级 |
| 数据量 | PB/天 | TB/天 |

---

## 9. 与其他公司对比

| 维度 | Alibaba | Bilibili | Uber | Netflix |
|------|---------|----------|------|---------|
| 流处理主力 | Flink | Flink | Flink | Flink + Spark |
| 商业平台 | Ververica | 自建 | 自建 | 自建 |
| 大状态 | TB | TB | GB | GB |
| 流式数仓 | Hudi + Hologres | Hudi | Hudi | Iceberg |
| SQL 主导 | 是 | 是 | 部分 | 部分 |

---

## 10. 速查表

```
Alibaba Flink:
  双 11 实时大屏 (GMV / 大屏)
  Blink → 回归社区 (Flink 1.9+)
  Hologres + Hudi (流式数仓)
  
Bilibili Flink:
  实时数仓 / 弹幕 / 推荐 / 风控
  80% Flink SQL
  
工业优化:
  RocksDB + 增量 Checkpoint
  Unaligned (反压)
  Flink SQL (Mini-batch + Local-Global)
  Salt (倾斜)
  
流式数仓:
  Flink + Hudi/Iceberg (Kappa)
  替代 Lambda (一套代码)
  
教训:
  状态 TTL + 增量 Checkpoint
  数据倾斜: Salt + Local-Global
  SQL 主导 + DataStream 复杂场景
```

---

## 10.5 完整配置文件示例

### 10.5.1 `flink-conf.yaml`(生产级)

```yaml
# ============ 基础 ============
jobmanager.rpc.address: flink-jm-1
jobmanager.rpc.port: 6123
jobmanager.bind-host: 0.0.0.0
jobmanager.memory.process.size: 8192m
jobmanager.memory.heap.size: 4096m
jobmanager.memory.jvm-overhead.size: 1024m

taskmanager.bind-host: 0.0.0.0
taskmanager.host: flink-tm-1
taskmanager.numberOfTaskSlots: 4                     # 每 TM 4 slot(=CPU 核数)
taskmanager.memory.process.size: 16384m
taskmanager.memory.task.heap.size: 6144m
taskmanager.memory.task.off-heap.size: 1024m
taskmanager.memory.managed.size: 4096m
taskmanager.memory.jvm-overhead.size: 1024m

# ============ 并行度 ============
parallelism.default: 128
jobmanager.execution.failover-strategy: region

# ============ Web UI ============
rest.address: 0.0.0.0
rest.port: 8081

# ============ Checkpoint ============
execution.checkpointing.interval: 60000
execution.checkpointing.timeout: 600000
execution.checkpointing.min-pause: 30000
execution.checkpointing.max-concurrent-checkpoints: 1
execution.checkpointing.tolerable-failed-checkpoints: 3
execution.checkpointing.mode: EXACTLY_ONCE
execution.checkpointing.unaligned: false             # 反压时设 true
execution.checkpointing.externalized-checkpoint-retention: RETAIN_ON_CANCELLATION

# ============ State Backend(RocksDB) ============
state.backend: rocksdb
state.backend.rocksdb.localdir: /data/flink/rocksdb
state.backend.rocksdb.memory.managed: true
state.backend.rocksdb.memory.fixed-memory-per-slot: 2048m
state.backend.incremental: true
state.backend.local-recovery: true
state.checkpoints.dir: hdfs:///flink/checkpoints
state.checkpoints.num-retained: 3
state.savepoints.dir: hdfs:///flink/savepoints

# ============ Restart ============
restart-strategy: exponential-delay
restart-strategy.exponential-delay.initial-backoff: 1s
restart-strategy.exponential-delay.max-backoff: 5min
restart-strategy.exponential-delay.backoff-multiplier: 2.0
restart-strategy.exponential-delay.reset-backoff-threshold: 30min

# ============ HA(基于 ZK) ============
high-availability: zookeeper
high-availability.zookeeper.quorum: zk-1:2181,zk-2:2181,zk-3:2181
high-availability.zookeeper.path.root: /flink
high-availability.storageDir: hdfs:///flink/ha/
high-availability.cluster-id: /flink-prod-cluster

# ============ 网络 ============
taskmanager.network.memory.fraction: 0.15
taskmanager.network.memory.max: '1gb'

# ============ Heartbeat ============
heartbeat.timeout: 30000
heartbeat.interval: 10000

# ============ JVM ============
env.java.opts.all: -XX:+UseG1GC -XX:MaxGCPauseMillis=200 -XX:InitiatingHeapOccupancyPercent=35
env.java.opts.jobmanager: -Xms4g -Xmx4g -Dcom.sun.management.jmxremote
env.java.opts.taskmanager: -Xms10g -Xmx10g -Dcom.sun.management.jmxremote

# ============ Metrics(Prometheus) ============
metrics.reporter.prom.class: org.apache.flink.metrics.prometheus.PrometheusReporter
metrics.reporter.prom.host: 0.0.0.0
metrics.reporter.prom.port: 9999
metrics.reporter.prom.interval: 30 SECONDS

# ============ Flink SQL ============
table.exec.mini-batch.enabled: true
table.exec.mini-batch.allow-latency: 1s
table.exec.mini-batch.size: 1000
table.optimizer.agg-phase-strategy: TWO_PHASE
table.exec.async-lookup.enable: true
table.exec.async-lookup.timeout: 5s
```

### 10.5.2 masters / workers 文件

```
# masters
flink-jm-1:8081
flink-jm-2:8081
flink-jm-3:8081

# workers
flink-tm-1
flink-tm-2
...
flink-tm-8
```

### 10.5.3 Flink SQL Job(实时 GMV 聚合)

```sql
-- ============ 源表:订单 binlog ============
CREATE TABLE orders_binlog (
    order_id BIGINT,
    user_id BIGINT,
    amount DECIMAL(18, 2),
    region STRING,
    create_time TIMESTAMP(3),
    op STRING METADATA FROM 'op' VIRTUAL,
    proc_time AS PROCTIME(),
    WATERMARK FOR create_time AS create_time - INTERVAL '5' SECOND
) WITH (
    'connector' = 'mysql-cdc',
    'hostname' = 'mysql-master',
    'port' = '3306',
    'username' = 'flink',
    'password' = '***',
    'database-name' = 'orders',
    'table-name' = 'orders',
    'scan.startup.mode' = 'latest-offset',
    'scan.incremental.snapshot.enabled' = 'true'
);

-- ============ 维表:用户 ============
CREATE TABLE users (
    user_id BIGINT,
    user_name STRING,
    level INT,
    PRIMARY KEY (user_id) NOT ENFORCED
) WITH (
    'connector' = 'jdbc',
    'url' = 'jdbc:mysql://mysql-slave:3306/users',
    'username' = 'flink',
    'password' = '***',
    'table-name' = 'users',
    'lookup.cache.max-rows' = '10000',
    'lookup.cache.ttl' = '5min'
);

-- ============ sink:ClickHouse ============
CREATE TABLE gmv_per_minute (
    window_start TIMESTAMP(3),
    region STRING,
    gmv DECIMAL(18, 2),
    order_cnt BIGINT,
    PRIMARY KEY (window_start, region) NOT ENFORCED
) WITH (
    'connector' = 'clickhouse',
    'url' = 'clickhouse://clickhouse-1:8123,clickhouse-2:8123',
    'database-name' = 'realtime',
    'table-name' = 'gmv_per_minute',
    'sink.batch-size' = '1000',
    'sink.flush-interval' = '10s',
    'sink.max-retries' = '3'
);

-- ============ sink:Hudi(数据湖) ============
CREATE TABLE orders_hudi (
    order_id BIGINT PRIMARY KEY NOT ENFORCED,
    user_id BIGINT,
    amount DECIMAL(18, 2),
    region STRING,
    create_time TIMESTAMP(3),
    op STRING
) WITH (
    'connector' = 'hudi',
    'path' = 'hdfs:///warehouse/orders_hudi',
    'table.type' = 'MERGE_ON_READ',
    'write.operation' = 'upsert',
    'write.precombine.field' = 'create_time',
    'write.recordkey.field' = 'order_id',
    'write.partitionpath.field' = 'region',
    'compaction.async.enabled' = 'true',
    'compaction.delta_commits' = '5'
);

-- ============ 实时聚合:每分钟每区域 GMV ============
INSERT INTO gmv_per_minute
SELECT
    TUMBLE_START(create_time, INTERVAL '1' MINUTE) AS window_start,
    region,
    SUM(amount) AS gmv,
    COUNT(*) AS order_cnt
FROM orders_binlog
WHERE op IN ('c', 'u')
GROUP BY 
    TUMBLE(create_time, INTERVAL '1' MINUTE),
    region;

-- ============ 双流 JOIN(订单 + 用户) ============
CREATE TABLE orders_enriched (
    order_id BIGINT,
    user_id BIGINT,
    user_name STRING,
    amount DECIMAL(18, 2),
    region STRING,
    create_time TIMESTAMP(3),
    PRIMARY KEY (order_id) NOT ENFORCED
) WITH (
    'connector' = 'kafka',
    'topic' = 'orders_enriched',
    'properties.bootstrap.servers' = 'kafka:9092',
    'format' = 'debezium-json'
);

INSERT INTO orders_enriched
SELECT 
    o.order_id, o.user_id, u.user_name, o.amount, o.region, o.create_time
FROM orders_binlog o
LEFT JOIN users FOR SYSTEM_TIME AS OF o.proc_time AS u
    ON o.user_id = u.user_id;

-- ============ 写入 Hudi ============
INSERT INTO orders_hudi
SELECT order_id, user_id, amount, region, create_time, op
FROM orders_binlog;

-- ============ CEP(实时风控:同用户 1 分钟内 5 笔订单) ============
CREATE TABLE risk_alerts (
    user_id BIGINT,
    alert_time TIMESTAMP(3),
    order_count BIGINT,
    total_amount DECIMAL(18, 2),
    PRIMARY KEY (user_id, alert_time) NOT ENFORCED
) WITH (
    'connector' = 'kafka',
    'topic' = 'risk_alerts',
    'properties.bootstrap.servers' = 'kafka:9092',
    'format' = 'json'
);

INSERT INTO risk_alerts
SELECT user_id, alert_time, order_count, total_amount
FROM orders_binlog
    MATCH_RECOGNIZE (
        PARTITION BY user_id
        ORDER BY create_time
        MEASURES
            LAST(create_time) AS alert_time,
            COUNT(*) AS order_count,
            SUM(amount) AS total_amount
        ONE ROW PER MATCH
        AFTER MATCH SKIP TO LAST A
        PATTERN (A B C D E) WITHIN INTERVAL '1' MINUTE
        DEFINE
            B AS B.create_time <= A.create_time + INTERVAL '1' MINUTE,
            C AS C.create_time <= A.create_time + INTERVAL '1' MINUTE,
            D AS D.create_time <= A.create_time + INTERVAL '1' MINUTE,
            E AS E.create_time <= A.create_time + INTERVAL '1' MINUTE
    );
```

### 10.5.4 DataStream Job(Java,实时风控)

```java
public class RealtimeRiskDetection {
    
    public static void main(String[] args) throws Exception {
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();
        
        env.setParallelism(128);
        env.enableCheckpointing(60000, CheckpointingMode.EXACTLY_ONCE);
        env.getCheckpointConfig().setMinPauseBetweenCheckpoints(30000);
        env.getCheckpointConfig().setCheckpointTimeout(600000);
        env.getCheckpointConfig().setMaxConcurrentCheckpoints(1);
        env.getCheckpointConfig().setExternalizedCheckpointCleanup(
            ExternalizedCheckpointCleanup.RETAIN_ON_CANCELLATION);
        
        env.setStateBackend(new EmbeddedRocksDBStateBackend());
        env.getCheckpointConfig().setCheckpointStorage("hdfs:///flink/checkpoints/risk");
        
        // Kafka Source(Exactly-Once)
        KafkaSource<Order> source = KafkaSource.<Order>builder()
            .setBootstrapServers("kafka:9092")
            .setTopics("orders")
            .setGroupId("risk-detection")
            .setStartingOffsets(OffsetsInitializer.committedOffsets(OffsetResetStrategy.EARLIEST))
            .setValueOnlyDeserializer(new JsonDeserializationSchema<>(Order.class))
            .build();
        
        DataStream<Order> orders = env.fromSource(
            source,
            WatermarkStrategy.<Order>forBoundedOutOfOrderness(Duration.ofSeconds(5))
                .withTimestampAssigner((o, ts) -> o.getCreateTime().getTime()),
            "orders-source"
        );
        
        // 实时特征:用户每分钟订单数 + 金额
        DataStream<UserFeature> userFeatures = orders
            .keyBy(Order::getUserId)
            .window(TumblingEventTimeWindows.of(Time.minutes(1)))
            .aggregate(new UserFeatureAggregator());
        
        // 风控规则
        DataStream<RiskAlert> alerts = userFeatures
            .filter(f -> f.getOrderCount() > 5 
                      || f.getTotalAmount().compareTo(BigDecimal.valueOf(10000)) > 0)
            .map(f -> new RiskAlert(f.getUserId(), System.currentTimeMillis(), 
                f.getOrderCount(), f.getTotalAmount()));
        
        // Kafka Sink(Exactly-Once)
        KafkaSink<RiskAlert> sink = KafkaSink.<RiskAlert>builder()
            .setBootstrapServers("kafka:9092")
            .setRecordSerializer(new RiskAlertSerializer("risk_alerts"))
            .setDeliveryGuarantee(DeliveryGuarantee.EXACTLY_ONCE)
            .setTransactionalIdPrefix("risk-tx-")
            .setProperty("transaction.timeout.ms", "900000")
            .build();
        
        alerts.sinkTo(sink).name("risk-sink");
        
        env.execute("Realtime Risk Detection");
    }
    
    public static class UserFeatureAggregator 
            implements AggregateFunction<Order, UserFeature, UserFeature> {
        @Override public UserFeature createAccumulator() { return new UserFeature(); }
        @Override public UserFeature add(Order o, UserFeature acc) {
            acc.setUserId(o.getUserId());
            acc.setOrderCount(acc.getOrderCount() + 1);
            acc.setTotalAmount(acc.getTotalAmount().add(o.getAmount()));
            return acc;
        }
        @Override public UserFeature getResult(UserFeature acc) { return acc; }
        @Override public UserFeature merge(UserFeature a, UserFeature b) {
            a.setOrderCount(a.getOrderCount() + b.getOrderCount());
            a.setTotalAmount(a.getTotalAmount().add(b.getTotalAmount()));
            return a;
        }
    }
}
```

### 10.5.5 K8s 部署(Flink Operator)

```yaml
# flink-session-job.yaml
apiVersion: flink.apache.org/v1beta1
kind: FlinkSessionJob
metadata:
  name: gmv-dashboard
  namespace: flink
spec:
  deploymentName: gmv-dashboard-deployment
  flinkConfiguration:
    taskmanager.numberOfTaskSlots: "4"
    taskmanager.memory.process.size: "16g"
    state.backend: rocksdb
    state.backend.incremental: "true"
    execution.checkpointing.interval: "60s"
    execution.checkpointing.mode: EXACTLY_ONCE
    table.exec.mini-batch.enabled: "true"
    table.exec.mini-batch.allow-latency: "1s"
    high-availability: kubernetes
    high-availability.kubernetes.cluster-id: flink-prod
  jobManagerResource:
    memory: "4g"
    cpu: 1
  taskManagerResource:
    memory: "16g"
    cpu: 4
  jarURI: local:///opt/flink/usrlib/gmv-dashboard.jar
  entryClass: com.alibaba.flink.GMVDashboard
  args: ["--env", "prod", "--region", "cn-hangzhou"]
  parallelism: 128
---
apiVersion: flink.apache.org/v1beta1
kind: FlinkDeployment
metadata:
  name: gmv-dashboard-deployment
  namespace: flink
spec:
  image: flink:1.17.1-scala_2.12-java11
  flinkVersion: v1_17
  flinkConfiguration:
    taskmanager.numberOfTaskSlots: "4"
    state.backend: rocksdb
    state.backend.incremental: "true"
    execution.checkpointing.interval: "60s"
    high-availability: kubernetes
    high-availability.kubernetes.cluster-id: flink-prod
  serviceAccount: flink
  podTemplate:
    apiVersion: v1
    kind: Pod
    spec:
      containers:
        - name: flink-main-container
          resources:
            requests: { memory: "8Gi", cpu: "2" }
            limits:   { memory: "16Gi", cpu: "4" }
          volumeMounts:
            - name: rocksdb
              mountPath: /data/flink/rocksdb
      volumes:
        - name: rocksdb
          hostPath:
            path: /data/flink/rocksdb
            type: DirectoryOrCreate
  jobManager:
    replicas: 1
    resource: { memory: "2g", cpu: 1 }
  taskManager:
    replicas: 32
    resource: { memory: "16g", cpu: 4 }
```

### 10.5.6 运维命令

```bash
# ============ 提交作业 ============
flink run -d -p 128 \
    -c com.alibaba.flink.GMVDashboard \
    gmv-dashboard.jar --env prod

# ============ SQL 客户端 ============
sql-client.sh embedded -e sql-defaults.yaml -f gmv.sql

# ============ Checkpoint / Savepoint ============
flink cancel -s hdfs:///flink/savepoints/gmv-$(date +%s) <job-id>
flink run -s hdfs:///flink/savepoints/gmv-2024031510000 -d gmv-dashboard.jar

# ============ 监控 ============
flink list -r
flink cancel <job-id>
flink stop <job-id>
flink savepoint <job-id> hdfs:///flink/sp/
```

---

## 11. 交叉引用

- [[16-2-计算-Flink与流处理]]:Flink 原理
- [[09-2-消息队列-Kafka深度]]:Kafka 与 Flink 集成
- [[22-3-工业案例-Alibaba-双11与单元化]]:双 11 整体架构
- [[22-5-工业案例-Uber-大规模实时]]:对比 Uber
- [[13-2-治理-可观测性与混沌工程]]:Flink 监控

---

## 12. 参考文献

- Alibaba Flink 团队. *Flink 在阿里的应用与实践*. Flink Forward China 历年.
- Bilibili 技术团队. *B 站实时计算平台建设*. 各技术分享.
- Carbone et al. *Apache Flink: Stream and Batch Processing in a Single Engine*. IEEE Data Eng. Bull. 2015.
- Apache Flink Documentation. https://flink.apache.org
- Hudi / Iceberg 文档.
- Ververica Platform. https://www.ververica.com

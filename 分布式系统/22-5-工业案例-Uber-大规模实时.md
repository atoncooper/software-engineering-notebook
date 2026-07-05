# 工业案例 —— Uber 大规模实时计算

> 章号: §22.5
> 层级: 工业 / 案例
> 标记: 🏭工业 🔥工程
> 前置: [[16-2-计算-Flink与流处理]] [[09-2-消息队列-Kafka深度]] [[13-2-治理-可观测性与混沌工程]]
> 来源: Uber Engineering Blog 公开分享

---

## 1. 背景

Uber 全球运营:

- 数千万司机 + 数亿用户
- 每秒数十万事件
- ETA、定价、风控、欺诈检测 → 都需实时

挑战:

- 跨地理区域
- 实时 + 海量 + 多样数据源
- 低延迟(秒级决策)

---

## 2. 实时栈

### 2.1 整体

```
Sources → Kafka → Flink/Spark Streaming → Pinot / ClickHouse
                                     → Alerting / Dashboard
                                     → ML (Michelangelo)
```

### 2.2 关键组件

| 组件 | 用途 |
|------|------|
| Kafka | 消息总线 |
| Flink | 流处理主力 |
| Spark Streaming | 微批(早期) |
| Pinot | OLAP(实时分析) |
| ClickHouse | OLAP(轻量级) |
| Michelangelo | ML 平台 |
| M3 | 监控(指标) |
| Jaeger | 分布式追踪 |

---

## 3. Kafka 在 Uber

### 3.1 规模

- 数千 broker
- 每秒 TB 级数据
- 跨 Region 复制

### 3.2 uReplicator

- Uber 自研 Kafka 跨 Cluster 复制
- 解决原 MirrorMaker 性能瓶颈
- 基于 Kafka Consumer + Producer

### 3.3 Schema Registry

- Avro schema 管理
- 兼容性检查(backward/forward)
- 跨服务数据契约

---

## 4. Flink 在 Uber

### 4.1 演进

- 2016-2018:Spark Streaming 微批(秒级)
- 2018+:Flink 真流(毫秒级)
- 2020:Flink 成为主力

### 4.2 关键场景

#### 4.2.1 实时 ETA

- 司机位置 → Kafka → Flink
- Flink 计算位置 + 路况 → ETA 估算
- ML 模型推理 → 实时更新

#### 4.2.2 欺诈检测

- 交易事件 → Flink 规则引擎
- 实时风控决策(秒级)
- 异常告警

#### 4.2.3 动态定价

- 区域需求/供给 → Flink 聚合
- Surge 定价模型
- 实时调整价格

#### 4.2.4 Hudi + Flink

- 流处理 + 数据湖
- Hudi 提供 ACID + 增量更新
- 实时入湖

### 4.3 工程实践

- **Flink on Kubernetes**:容器化部署
- **Checkpoint RocksDB**:大状态(GB 级)
- **Unaligned Checkpoint**:反压场景
- **状态 TTL**:控制状态增长

---

## 5. Pinot(实时 OLAP)

### 5.1 用途

- 实时 dashboard
- 业务指标(订单量、完成率)
- 用户行为分析

### 5.2 架构

```
Kafka → Pinot Realtime Segment → Pinot Server → Query Broker → Dashboard
                                ↓
                            Segment Store (S3)
```

### 5.3 特性

- **Hybrid**:实时 + 历史数据
- **Index**:倒排 + Range + Star Tree
- **Push-down**:谓词下推,减少扫描

### 5.4 性能

- P99 查询 < 1s
- 单表 TB 级数据

---

## 6. Michelangelo(ML 平台)

### 6.1 整体

- **训练**:Spark + Horovod
- **推理**:在线 + 离线
- **特征存储**:在线(快速)+ 离线(训练)

### 6.2 实时特征

- Flink 实时计算特征
- 写入特征存储(Redis/Aerospike)
- 模型在线推理时取特征

```
Kafka → Flink (特征计算) → Redis (特征存储) ← Michelangelo 推理服务
```

---

## 7. 多 Region 部署

### 7.1 多活架构

- 主要 Region:US East / US West / EU
- Kafka 跨 Region 复制(uReplicator)
- Flink 备份(Standby)

### 7.2 容灾

- 单 Region 故障 → 流量切换
- 数据丢失:Kafka 跨 Region 异步复制,秒级丢失

---

## 8. 可观测性

### 8.1 M3(Metrics)

- 自研时间序列数据库
- 基于 Prometheus 兼容
- 亿级 metric

### 8.2 Jaeger(Tracing)

- 开源分布式追踪
- Uber 主导
- 跨服务调用链

### 8.3 日志

- ELK
- 关键业务日志实时分析

---

## 9. 性能与规模

- 每天 PB 级数据
- 数千 Flink job
- 数万 Kafka topic
- 全球 70+ 国家运营

---

## 10. 教训与最佳实践

### 10.1 从 Spark Streaming 迁 Flink

- 微批 vs 真流:延迟差 10-100x
- 状态管理:Flink 原生,Spark 弱
- 迁移成本:Job 重写

### 10.2 大状态管理

- RocksDB 后端
- Unaligned Checkpoint 处理反压
- 状态 TTL 控制

### 10.3 Schema 演进

- Schema Registry 必备
- Backward/Forward 兼容性
- 滚动升级

### 10.4 监控与告警

- 流处理延迟监控
- Checkpoint 成功率
- 消费 lag(Kafka offset)

---

## 11. 与其他公司对比

| 维度 | Uber | Netflix | Bytedance |
|------|------|---------|-----------|
| 流处理 | Flink 主力 | Flink + Spark | Flink 主力 |
| OLAP | Pinot(自研) | Redshift | ClickHouse + 内部 |
| ML | Michelangelo | 自研 | 内部 |
| 监控 | M3 | Atlas | 内部 |
| Tracing | Jaeger(开源) | Zipkin | 内部 |

---

## 11.5 完整配置文件示例

### 11.5.1 Kafka Connect(CDC → Kafka)

```json
{
  "name": "mysql-orders-cdc",
  "config": {
    "connector.class": "io.debezium.connector.mysql.MySqlConnector",
    "database.hostname": "mysql-orders.uber.internal",
    "database.port": "3306",
    "database.user": "debezium",
    "database.password": "***",
    "database.server.id": "184054",
    "database.server.name": "orders",
    "database.include.list": "orders",
    "table.include.list": "orders.orders,orders.order_items",
    "database.history.kafka.bootstrap.servers": "kafka:9092",
    "database.history.kafka.topic": "schema-changes.orders",
    "snapshot.mode": "schema_only_recovery",
    "snapshot.locking.mode": "minimal",
    "tombstones.on.delete": "true",
    "key.converter": "io.confluent.connect.avro.AvroConverter",
    "value.converter": "io.confluent.connect.avro.AvroConverter",
    "key.converter.schema.registry.url": "http://schema-registry:8081",
    "value.converter.schema.registry.url": "http://schema-registry:8081",
    "transforms": "unwrap",
    "transforms.unwrap.type": "io.debezium.transforms.UnwrapFromEnvelope",
    "transforms.unwrap.drop.tombstones": "false",
    "transforms.unwrap.delete.handling.mode": "rewrite",
    "producer.override.acks": "all",
    "producer.override.enable.idempotence": "true",
    "producer.override.compression.type": "zstd",
    "producer.override.max.in.flight.requests.per.connection": 5,
    "consumer.override.auto.offset.reset": "earliest",
    "errors.tolerance": "all",
    "errors.deadletterqueue.topic.name": "orders-dlq",
    "errors.deadletterqueue.context.headers.enable": "true"
  }
}
```

### 11.5.2 Flink Job(实时 ETA 估算)

```java
public class RealtimeETAEstimator {
    
    public static void main(String[] args) throws Exception {
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();
        env.setParallelism(256);
        env.enableCheckpointing(60000, CheckpointingMode.EXACTLY_ONCE);
        env.setStateBackend(new EmbeddedRocksDBStateBackend());
        env.getCheckpointConfig().setCheckpointStorage("s3://uber-flink/ckpt/eta");
        
        // 司机位置流
        KafkaSource<DriverLocation> driverStream = KafkaSource.<DriverLocation>builder()
            .setBootstrapServers("kafka:9092")
            .setTopics("driver-locations")
            .setGroupId("eta-estimator")
            .setValueOnlyDeserializer(new JsonDeserializationSchema<>(DriverLocation.class))
            .build();
        
        DataStream<DriverLocation> drivers = env.fromSource(
            driverStream,
            WatermarkStrategy.<DriverLocation>forBoundedOutOfOrderness(Duration.ofSeconds(3))
                .withTimestampAssigner((d, t) -> d.getTimestamp()),
            "driver-locations"
        );
        
        // 订单事件流
        KafkaSource<TripEvent> tripStream = KafkaSource.<TripEvent>builder()
            .setBootstrapServers("kafka:9092")
            .setTopics("trip-events")
            .setGroupId("eta-estimator")
            .setValueOnlyDeserializer(new JsonDeserializationSchema<>(TripEvent.class))
            .build();
        
        DataStream<TripEvent> trips = env.fromSource(
            tripStream,
            WatermarkStrategy.<TripEvent>forBoundedOutOfOrderness(Duration.ofSeconds(5))
                .withTimestampAssigner((e, t) -> e.getTimestamp()),
            "trip-events"
        );
        
        // 状态:每个司机最近 N 个位置(用于路线)
        DataStream<TripETA> etas = trips
            .keyBy(TripEvent::getTripId)
            .connect(drivers.keyBy(DriverLocation::getDriverId))
            .process(new ETAEstimateFunction())
            .name("eta-estimate");
        
        // 写入 Redis 特征存储(供 Michelangelo 推理)
        etas.addSink(new RedisSink<>(new FlinkJedisPoolConfig.Builder()
            .setHost("redis-cluster.uber.internal").setPort(6379).build(),
            new ETARedisMapper()))
            .name("eta-redis-sink");
        
        // 写入 Pinot(实时分析)
        etas.addSink(PinotSinkUtils.buildPinotSink("etastream", "eta-realtime"))
            .name("eta-pinot-sink");
        
        // 写入 Kafka(下游消费)
        KafkaSink<TripETA> kafkaSink = KafkaSink.<TripETA>builder()
            .setBootstrapServers("kafka:9092")
            .setRecordSerializer(new ETASerializer("etastream"))
            .setDeliveryGuarantee(DeliveryGuarantee.EXACTLY_ONCE)
            .setTransactionalIdPrefix("eta-tx-")
            .build();
        
        etas.sinkTo(kafkaSink).name("eta-kafka-sink");
        
        env.execute("Realtime ETA Estimator");
    }
    
    // CoProcessFunction:订单 + 司机位置
    public static class ETAEstimateFunction 
            extends CoProcessFunction<TripEvent, DriverLocation, TripETA> {
        
        private transient MapState<String, List<DriverLocation>> driverLocations;
        private transient ValueState<TripEvent> currentTrip;
        
        @Override
        public void open(Configuration params) {
            driverLocations = getRuntimeContext().getMapState(
                new MapStateDescriptor<>("driver-loc", 
                    TypeInformation.of(String.class),
                    TypeInformation.of(new TypeHint<List<DriverLocation>>(){}))
            );
            currentTrip = getRuntimeContext().getState(
                new ValueStateDescriptor<>("current-trip", TripEvent.class)
            );
        }
        
        @Override
        public void processElement1(TripEvent trip, Context ctx, Collector<TripETA> out) 
                throws Exception {
            currentTrip.update(trip);
            // 触发 ETA 计算
            TripEvent t = currentTrip.value();
            List<DriverLocation> locs = driverLocations.get(t.getDriverId());
            if (locs != null && !locs.isEmpty()) {
                double eta = estimateETA(t, locs);
                out.collect(new TripETA(t.getTripId(), eta, System.currentTimeMillis()));
            }
        }
        
        @Override
        public void processElement2(DriverLocation loc, Context ctx, Collector<TripETA> out) 
                throws Exception {
            TripEvent t = currentTrip.value();
            if (t != null && t.getDriverId().equals(loc.getDriverId())) {
                List<DriverLocation> locs = driverLocations.get(loc.getDriverId());
                if (locs == null) locs = new ArrayList<>();
                locs.add(loc);
                if (locs.size() > 100) locs.remove(0);  // 滑窗 100 个位置
                driverLocations.put(loc.getDriverId(), locs);
            }
        }
        
        private double estimateETA(TripEvent trip, List<DriverLocation> locs) {
            // 简化:平均速度 + 剩余距离
            return 0;
        }
    }
}
```

### 11.5.3 Pinot Table Schema

```json
{
  "schemaName": "etastream",
  "dimensionFieldSpecs": [
    {"name": "trip_id", "dataType": "LONG"},
    {"name": "driver_id", "dataType": "LONG"},
    {"name": "rider_id", "dataType": "LONG"},
    {"name": "city", "dataType": "STRING"},
    {"name": "region", "dataType": "STRING"}
  ],
  "metricFieldSpecs": [
    {"name": "eta_minutes", "dataType": "DOUBLE"},
    {"name": "distance_km", "dataType": "DOUBLE"},
    {"name": "surge_multiplier", "dataType": "DOUBLE"}
  ],
  "dateTimeFieldSpecs": [
    {
      "name": "event_time",
      "dataType": "TIMESTAMP",
      "format": "1:MILLISECONDS:EPOCH",
      "granularity": "1:MILLISECONDS"
    }
  ]
}
```

```json
{
  "tableName": "etastream",
  "tableType": "REALTIME",
  "segmentsConfig": {
    "schemaName": "etastream",
    "timeColumnName": "event_time",
    "timeType": "MILLISECONDS",
    "retentionTimeUnit": "DAYS",
    "retentionTimeValue": "30",
    "segmentPushType": "APPEND",
    "segmentAssignmentStrategy": "ReplicaGroupSegmentAssignmentStrategy",
    "replication": "3",
    "schemaName": "etastream"
  },
  "tenants": {
    "broker": "defaultTenant",
    "server": "defaultTenant"
  },
  "tableIndexConfig": {
    "loadMode": "MMAP",
    "streamConfigs": {
      "streamType": "kafka",
      "stream.kafka.consumer.type": "lowLevel",
      "stream.kafka.topic.name": "etastream",
      "stream.kafka.decoder.class.name": "org.apache.pinot.plugin.stream.kafka.KafkaJSONMessageDecoder",
      "stream.kafka.broker.list": "kafka:9092",
      "stream.kafka.consumer.prop.auto.offset.reset": "smallest",
      "realtime.segment.flush.threshold.rows": "1000000",
      "realtime.segment.flush.threshold.time": "1h"
    },
    "invertedIndexColumns": ["city", "region"],
    "rangeIndexColumns": ["eta_minutes", "distance_km"],
    "starTreeIndexConfigs": [
      {
        "dimensionsSplitOrder": ["city", "region"],
        "functionColumnPairs": ["SUM__eta_minutes", "COUNT__*"],
        "maxLeafRecords": 10000
      }
    ]
  },
  "metadata": {"customConfigs": {}}
}
```

### 11.5.4 Pinot SQL 查询(实时大屏)

```sql
-- 实时 ETA P50/P90/P99(按城市)
SELECT 
    city,
    PERCENTILE(eta_minutes, 50) AS p50,
    PERCENTILE(eta_minutes, 90) AS p90,
    PERCENTILE(eta_minutes, 99) AS p99,
    COUNT(*) AS cnt
FROM etastream
WHERE event_time > ago('PT5M')
GROUP BY city
ORDER BY cnt DESC
LIMIT 100;

-- 实时订单量(每分钟)
SELECT 
    DATETRUNC('minute', event_time) AS minute,
    city,
    COUNT(*) AS orders,
    SUM(distance_km) AS total_distance
FROM etastream
WHERE event_time > ago('PT1H')
GROUP BY minute, city
ORDER BY minute DESC, orders DESC;
```

### 11.5.5 M3 Metrics 配置

```yaml
# m3query.yaml
listenAddress: 0.0.0.0:7201
metrics:
  enabled: true
  samplingRate: 1.0
  scope:
    prefix: "m3query"

clusters:
  - name: m3db-cluster
    type: m3db
    namespace: metrics
    zone: embedded
    client:
      config:
        service:
          env: prod
          zone: embedded
          service: m3db
        etcdClusters:
          - zone: embedded
            endpoints:
              - etcd-1:2379
              - etcd-2:2379
              - etcd-3:2379
      writeConsistencyLevel: majority
      readConsistencyLevel: unstrict_majority
      writeTimeout: 10s
      fetchTimeout: 30s
      writeOpTimeout: 10s
      fetchOpTimeout: 30s
      backgroundHealthCheckFailLimit: 1
```

### 11.5.6 Jaeger 分布式追踪

```yaml
# jaeger-collector.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: jaeger-collector
spec:
  replicas: 5
  selector:
    matchLabels: { app: jaeger-collector }
  template:
    metadata:
      labels: { app: jaeger-collector }
    spec:
      containers:
        - name: jaeger-collector
          image: jaegertracing/all-in-one:1.35
          ports:
            - { containerPort: 14250, name: grpc }
            - { containerPort: 14268, name: http }
            - { containerPort: 9411,  name: zipkin }
          env:
            - { name: COLLECTOR_OTLP_ENABLED, value: "true" }
            - { name: SPAN_STORAGE_TYPE, value: "elasticsearch" }
            - { name: ES_SERVER_URLS, value: "http://es-1:9200,http://es-2:9200" }
            - { name: ES_TAGS_AS_FIELDS_ALL, value: "true" }
            - { name: SAMPLING_STRATEGIES_FILE, value: "/etc/jaeger/strategies.json" }
          resources:
            requests: { cpu: 1, memory: 2Gi }
            limits:   { cpu: 2, memory: 4Gi }
          volumeMounts:
            - { name: strategies, mountPath: /etc/jaeger }
      volumes:
        - name: strategies
          configMap:
            name: jaeger-strategies
---
# 采样策略(头采 + 尾采)
{
  "default_strategy": {
    "type": "probabilistic",
    "param": 0.01
  },
  "service_strategies": [
    {
      "service": "user-service",
      "type": "probabilistic",
      "param": 0.1
    },
    {
      "service": "trip-service",
      "operation_strategies": [
        {
          "operation": "POST /trips",
          "type": "probabilistic",
          "param": 0.5
        },
        {
          "operation": "GET /trips/:id",
          "type": "ratelimiting",
          "param": 100.0
        }
      ]
    }
  ]
}
```

### 11.5.7 OpenTelemetry 接入(Go)

```go
package main

import (
    "context"
    "go.opentelemetry.io/otel"
    "go.opentelemetry.io/otel/attribute"
    "go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc"
    "go.opentelemetry.io/otel/propagation"
    "go.opentelemetry.io/otel/sdk/resource"
    sdktrace "go.opentelemetry.io/otel/sdk/trace"
    semconv "go.opentelemetry.io/otel/semconv/v1.4.0"
    "go.opentelemetry.io/otel/trace"
)

func initTracer() (*sdktrace.TracerProvider, error) {
    exporter, err := otlptracegrpc.New(context.Background(),
        otlptracegrpc.WithEndpoint("jaeger-collector:4317"),
        otlptracegrpc.WithInsecure(),
    )
    if err != nil {
        return nil, err
    }
    
    tp := sdktrace.NewTracerProvider(
        sdktrace.WithBatcher(exporter,
            sdktrace.WithBatchTimeout(5*time.Second),
            sdktrace.WithMaxExportBatchSize(512),
        ),
        sdktrace.WithResource(resource.NewWithAttributes(
            semconv.SchemaURL,
            semconv.ServiceNameKey.String("trip-service"),
            semconv.ServiceVersionKey.String("1.2.3"),
            semconv.DeploymentEnvironmentKey.String("production"),
        )),
        sdktrace.WithSampler(sdktrace.TraceIDRatioBased(0.1)),
    )
    otel.SetTracerProvider(tp)
    otel.SetTextMapPropagator(propagation.TraceContext{})
    return tp, nil
}

func estimateETA(ctx context.Context, tripID int64) (float64, error) {
    tracer := otel.Tracer("trip-service")
    ctx, span := tracer.Start(ctx, "estimate-eta",
        trace.WithAttributes(
            attribute.Int64("trip.id", tripID),
        ),
    )
    defer span.End()
    
    // 子 span:取司机位置
    _, span2 := tracer.Start(ctx, "fetch-driver-location")
    driver, err := fetchDriver(ctx, tripID)
    span2.RecordError(err)
    span2.End()
    
    if err != nil {
        return 0, err
    }
    
    // 子 span:模型推理
    _, span3 := tracer.Start(ctx, "ml-inference")
    eta := mlModel.Predict(driver)
    span3.SetAttributes(attribute.Float64("eta.minutes", eta))
    span3.End()
    
    return eta, nil
}
```

---

## 12. 速查表

```
Uber 实时栈:
  Kafka (消息总线)
  Flink (流处理主力)
  Pinot (实时 OLAP)
  Michelangelo (ML)
  Hudi (数据湖)

关键场景:
  ETA 估算
  欺诈检测
  动态定价
  实时大屏

规模:
  数千 Flink job
  数万 Kafka topic
  PB/天 数据

工程:
  Flink on K8s
  RocksDB 状态
  Unaligned Checkpoint
  Schema Registry
  状态 TTL
  uReplicator (Kafka 跨 DC)

教训:
  Spark Streaming → Flink (延迟 10-100x)
  大状态用 RocksDB
  Schema 兼容性必备
  监控 checkpoint + lag
```

---

## 13. 交叉引用

- [[16-2-计算-Flink与流处理]]:Flink 原理
- [[09-2-消息队列-Kafka深度]]:Kafka 深度
- [[13-2-治理-可观测性与混沌工程]]:可观测性
- [[16-1-计算-MapReduce与Spark]]:Spark 对比

---

## 14. 参考文献

- Uber Engineering Blog. https://eng.uber.com
- Kats et al. *Aster: A Unified Streaming Platform*. 2019.
- Sidhu. *Uber's Real-time Push Platform*. 2018.
- Apache Pinot by Uber & LinkedIn. https://pinot.apache.org
- Schulze et al. *Michelangelo: Uber's ML Platform*. 2017.
- Apache Hudi. https://hudi.apache.org
- uReplicator. https://github.com/uber/uReplicator

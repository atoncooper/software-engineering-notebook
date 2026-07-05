# 工业案例 —— Twitter / Cassandra 海量存储

> 章号: §22.6
> 层级: 工业 / 案例
> 标记: 🏭工业 📜论文 ⭐高频
> 前置: [[08-5-存储-Cassandra与Redis]] [[08-2-存储-Dynamo]] [[07-分片与路由]]
> 论文: Lakshman & Malik, *Cassandra: A Decentralized Structured Storage System*, 2009; Lakshman et al., VLDB 2010

---

## 1. 背景

Facebook 2008 推出 Cassandra,用于 Inbox Search(收件箱搜索)。

- 海量写(每秒百万级消息)
- 弱一致可接受
- 跨 DC 部署

2010 Facebook 把 Cassandra 开源 → Apache 项目。Twitter、Netflix、Apple、Instagram 大量采用。

---

## 2. Cassandra 设计要点

### 2.1 Dynamo + Bigtable

| 来源 | 借鉴 |
|------|------|
| Dynamo | 一致性哈希、Quorum、Gossip、Vector Clock(早期) |
| Bigtable | LSM-Tree、Column Family、SSTable |

### 2.2 数据模型

```sql
CREATE TABLE tweets (
    user_id UUID,
    tweet_id TIMEUUID,
    text TEXT,
    created_at TIMESTAMP,
    PRIMARY KEY (user_id, tweet_id)
) WITH CLUSTERING ORDER BY (tweet_id DESC);
```

- **Partition Key**(`user_id`):决定 partition
- **Clustering Key**(`tweet_id`):partition 内排序
- **Column**:动态

### 2.3 一致性

```sql
-- 写
INSERT INTO tweets ... USING CONSISTENCY QUORUM;

-- 读
SELECT * FROM tweets WHERE user_id = ? USING CONSISTENCY LOCAL_QUORUM;
```

级别:

- ONE:单副本
- QUORUM:多数派
- LOCAL_QUORUM:同 DC 多数派
- EACH_QUORUM:每 DC 多数派
- ALL:所有副本
- ANY:任意(即使 hinted handoff)

---

## 3. Twitter 使用场景

### 3.1 时间线

- 用户发推 → 写 Cassandra
- Follower 时间线 → fan-out 写到各 Follower 的 timeline 表
- 单 tweet 备份:Cassandra

### 3.2 2012 重建架构

- 之前:Redis + Memcached
- 之后:Cassandra 主力
- 原因:海量写 + 跨 DC

### 3.3 Twitter 的 Cassandra 优化

- **TwCS(Tiered Compaction Strategy)**:Twitter 自研 Compaction
- **Custom Replication**:跨 DC 策略
- **Monitor**:Twitter ServerStats + Observability

---

## 4. Cassandra 工业级演进

### 4.1 Compaction 策略

| 策略 | 适用 | 缺点 |
|------|------|------|
| **SizeTiered**(默认) | 写多 | 旧数据多时读放大 |
| **Leveled** | 读多 | 写放大 |
| **TimeWindow**(TWCS) | 时间序列 | 仅按时间 |

```sql
CREATE TABLE metrics (
    metric_id UUID,
    ts TIMESTAMP,
    value DOUBLE,
    PRIMARY KEY (metric_id, ts)
) WITH compaction = {
    'class': 'TimeWindowCompactionStrategy',
    'compaction_window_unit': 'DAYS',
    'compaction_window_size': '1'
};
```

### 4.2 节点拓扑

- **DataCenter**:逻辑(物理 DC)
- **Rack**:逻辑机架
- **Snitch**:决定 DC/Rack 归属
  - SimpleSnitch:单 DC
  - GossipingPropertyFileSnitch:多 DC(推荐)
  - Ec2Snitch / Ec2MultiRegionSnitch:AWS

### 4.3 Replication

```sql
CREATE KEYSPACE twitter WITH replication = {
    'class': 'NetworkTopologyStrategy',
    'us_east': 3,
    'us_west': 3,
    'eu_west': 2
};
```

- 每 DC 独立副本数
- 跨 DC 异步(默认)
- 单 DC 内强同步(可配置)

---

## 5. 关键机制

### 5.1 Gossip

- 节点间 P2P 传播状态
- 每秒与 1-3 个随机节点交换信息
- 检测故障 + 拓扑变更

### 5.2 Hinted Handoff

- 写时副本不可达 → 写到"hint"
- 副本恢复后回放
- 避免读修复

### 5.3 Read Repair

- 读时发现不一致 → 异步修复旧副本
- 适合读多写少

### 5.4 Anti-Entropy(Repair)

- 定期全量同步
- Merkle Tree 比对
- `nodetool repair`

---

## 6. 性能数据

- 单节点写:5-20w ops/秒(取决于硬件)
- 单节点读:1-5w ops/秒(命中缓存高)
- 跨 DC 复制延迟:秒级

### 6.1 Twitter 规模

- 数百节点集群
- PB 级数据
- 每天万亿级写

---

## 7. 工程实战

### 7.1 数据建模原则

- **Query-Driven**:按查询建模
- **避免二级索引**:跨 partition 慢
- **Wide Row**:同 partition 大量行(利于范围查询)

### 7.2 反模式

- **热点**:时间序列 + 单 partition(用 TWCS + 分桶)
- **大 partition**:单 partition GB 级会拖慢
- **频繁删除**:tombstone 堆积

### 7.3 监控

- `nodetool status` / `tpstats`
- Compaction 队列
- Read/Write latency
- Disk usage

---

## 8. 跨 DC 演进

### 8.1 早期(2.0 之前)

- 跨 DC 同步差
- 网络抖动放大

### 8.2 现代(3.0+)

- **Native Async Replication**
- **CDC**(Change Data Capture)
- **Materialized Views**:最终一致视图

### 8.3 LWT(Lightweight Transaction)

- Paxos-based CAS
- 强一致但慢
- 不建议大量用

```sql
INSERT INTO users (id, name) VALUES (?, ?)
IF NOT EXISTS;
```

---

## 9. 与其他 NoSQL 对比

| 维度 | Cassandra | HBase | MongoDB |
|------|-----------|-------|---------|
| 模型 | 宽列 | 宽列 | 文档 |
| 一致性 | 可调 Quorum | 强(CP) | 强(副本集) |
| 共识 | Gossip(无) | ZK + Master | Raft |
| 跨 DC | 原生支持 | 手动 | Replica Set |
| 适用 | 海量写 + 跨 DC | 海量写 + 范围 | 灵活 schema |

详见 [[08-3-存储-Bigtable与HBase]] 和 [[08-5-存储-Cassandra与Redis]]。

---

## 10. 教训

### 10.1 Schema 设计是核心

- Cassandra 不是关系 DB
- Query-Driven,先想查询再建表
- 反规范化(冗余存储)

### 10.2 Compaction 影响

- 选错策略 → 读放大 / 写放大
- 监控 Compaction 队列

### 10.3 跨 DC 延迟

- 默认异步,秒级延迟
- 强一致需 EACH_QUORUM(代价高)
- 业务容忍最终一致

### 10.4 运维复杂度

- Gossip 模式运维难度高
- 节点加入/退出需小心
- 大集群(>500 节点)运维挑战

---

## 10.5 完整配置文件示例

### 10.5.1 `cassandra.yaml`(生产级)

```yaml
# ============ 集群 ============
cluster_name: 'Twitter Cassandra Cluster'
num_tokens: 16                      # vnodes 数(3.0+ 推荐 16)
allocate_tokens_for_local_replication_factor: 3

# ============ 种子节点 ============
seed_provider:
  - class_name: org.apache.cassandra.locator.SimpleSeedProvider
    parameters:
      - seeds: "10.0.1.1,10.0.2.1,10.0.3.1"

# ============ 网络 ============
listen_address: 10.0.1.10
broadcast_address: 10.0.1.10
broadcast_rpc_address: 10.0.1.10
storage_port: 7000
ssl_storage_port: 7001
native_transport_port: 9042
rpc_address: 0.0.0.0

# ============ Snitch(拓扑感知) ============
endpoint_snitch: GossipingPropertyFileSnitch

# ============ 数据目录 ============
data_file_directories:
  - /data/cassandra/data-1
  - /data/cassandra/data-2
commitlog_directory: /data/cassandra/commitlog
saved_caches_directory: /data/cassandra/saved_caches
hints_directory: /data/cassandra/hints

# ============ Disk ============
disk_optimization_strategy: ssd
commitlog_sync: periodic
commitlog_sync_period_in_ms: 10000
commitlog_segment_size_in_mb: 32
commitlog_total_space_in_mb: 4096

# ============ Memtable ============
memtable_allocation_type: heap_buffers
memtable_flush_writers: 8
memtable_heap_space_in_mb: 2048
memtable_offheap_space_in_mb: 2048

# ============ Compaction ============
concurrent_compactors: 4
compaction_throughput_mb_per_sec: 64
stream_throughput_outbound_megabits_per_sec: 200

# ============ Cache ============
key_cache_size_in_mb: 1024
key_cache_save_period: 14400
row_cache_size_in_mb: 0              # 行缓存一般关
row_cache_save_period: 0
counter_cache_size_in_mb: 512

# ============ 并发 ============
concurrent_reads: 32
concurrent_writes: 32
concurrent_counter_writes: 32

# ============ 网络 ============
inter_dc_tcp_nodelay: false          # 跨 DC 关闭(节省带宽)
trickle_fsync: true
trickle_fsync_interval_in_kb: 10240

# ============ GC ============
gc_warn_threshold_in_ms: 1000

# ============ 复制 ============
hints_enabled: true
max_hint_window_in_ms: 10800000      # 3h
hints_flush_period_in_ms: 10000
max_hints_file_size_in_mb: 128

# ============ CQL ============
batch_size_warn_threshold_in_kb: 5
batch_size_fail_threshold_in_kb: 50
unlogged_batch_across_partitions_warn_threshold: 10

# ============ 加密(可选) ============
server_encryption_options:
  internode_encryption: all
  keystore: conf/.keystore
  keystore_password: cassandra
  truststore: conf/.truststore
  truststore_password: cassandra
client_encryption_options:
  enabled: true
  optional: false
  keystore: conf/.keystore
  keystore_password: cassandra

# ============ 认证 ============
authenticator: PasswordAuthenticator
authorizer: CassandraAuthorizer
roles_validity_in_ms: 2000
permissions_validity_in_ms: 2000
```

### 10.5.2 `cassandra-rackdc.properties`(GossipingPropertyFileSnitch)

```properties
# 同 DC 多 Rack 部署
dc=us_east_1
rack=1a
```

### 10.5.3 JVM 配置(`cassandra-env.sh`)

```bash
# ============ Heap ============
MAX_HEAP_SIZE="16G"
HEAP_NEWSIZE="4G"

# ============ GC(CMS) ============
JVM_OPTS="$JVM_OPTS -XX:+UseConcMarkSweepGC"
JVM_OPTS="$JVM_OPTS -XX:+CMSParallelRemarkEnabled"
JVM_OPTS="$JVM_OPTS -XX:SurvivorRatio=8"
JVM_OPTS="$JVM_OPTS -XX:MaxTenuringThreshold=1"
JVM_OPTS="$JVM_OPTS -XX:CMSInitiatingOccupancyFraction=75"
JVM_OPTS="$JVM_OPTS -XX:+UseCMSInitiatingOccupancyOnly"
JVM_OPTS="$JVM_OPTS -XX:+UseTLAB"
JVM_OPTS="$JVM_OPTS -XX:+ResizeTLAB"
JVM_OPTS="$JVM_OPTS -XX:TLABSize=128k"
JVM_OPTS="$JVM_OPTS -XX:+PerfDisableSharedMem"
JVM_OPTS="$JVM_OPTS -XX:+AlwaysPreTouch"

# ============ GC(G1,可选) ============
# JVM_OPTS="$JVM_OPTS -XX:+UseG1GC"
# JVM_OPTS="$JVM_OPTS -XX:MaxGCPauseMillis=200"
# JVM_OPTS="$JVM_OPTS -XX:InitiatingHeapOccupancyPercent=35"

# ============ JMX ============
JVM_OPTS="$JVM_OPTS -Dcom.sun.management.jmxremote.port=7199"
JVM_OPTS="$JVM_OPTS -Dcom.sun.management.jmxremote.ssl=false"
JVM_OPTS="$JVM_OPTS -Dcom.sun.management.jmxremote.authenticate=false"

# ============ OOM 处理 ============
JVM_OPTS="$JVM_OPTS -XX:+HeapDumpOnOutOfMemoryError"
JVM_OPTS="$JVM_OPTS -XX:HeapDumpPath=/var/log/cassandra/"
JVM_OPTS="$JVM_OPTS -XX:OnOutOfMemoryError=kill -9 %p"
```

### 10.5.4 Keyspace 与 Table 创建

```sql
-- Keyspace:跨 DC 3 副本
CREATE KEYSPACE twitter
WITH replication = {
    'class': 'NetworkTopologyStrategy',
    'us_east_1': 3,
    'us_west_2': 3,
    'eu_west_1': 2
}
AND durable_writes = true;

-- 用户时间线(Wide Row + TWCS)
CREATE TABLE twitter.user_timeline (
    user_id UUID,
    tweet_id TIMEUUID,
    text TEXT,
    created_at TIMESTAMP,
    favorites INT,
    retweets INT,
    PRIMARY KEY (user_id, tweet_id)
) WITH CLUSTERING ORDER BY (tweet_id DESC)
  AND compaction = {
      'class': 'TimeWindowCompactionStrategy',
      'compaction_window_unit': 'DAYS',
      'compaction_window_size': '1'
  }
  AND gc_grace_seconds = 86400
  AND bloom_filter_fp_chance = 0.01
  AND caching = {
      'keys': 'ALL',
      'rows_per_partition': '100'
  };

-- 推文明细(单 partition)
CREATE TABLE twitter.tweets (
    tweet_id TIMEUUID PRIMARY KEY,
    user_id UUID,
    text TEXT,
    created_at TIMESTAMP,
    favorites INT,
    retweets INT
) WITH compaction = {
    'class': 'LeveledCompactionStrategy',
    'sstable_size_in_mb': 160
};

-- 二级索引(谨慎用,跨 partition)
CREATE INDEX idx_tweets_user ON twitter.tweets (user_id);
```

### 10.5.5 Java 客户端(DataStax Java Driver)

```java
// CqlSession 初始化
CqlSession session = CqlSession.builder()
    .addContactPoint(new InetSocketAddress("cassandra-1", 9042))
    .addContactPoint(new InetSocketAddress("cassandra-2", 9092))
    .addContactPoint(new InetSocketAddress("cassandra-3", 9042))
    .withLocalDatacenter("us_east_1")
    .withKeyspace(CqlIdentifier.fromCql("twitter"))
    .withAuthCredentials("cassandra", "cassandra")
    .withSSL(ContextUtil.createSSLContext())
    .build();

// Prepared Statement(防注入 + 性能)
PreparedStatement prepared = session.prepare(
    "INSERT INTO twitter.user_timeline (user_id, tweet_id, text, created_at) " +
    "VALUES (?, now(), ?, ?) USING TTL 86400");

// 批量写入(Fan-out 到 Follower)
BatchStatement batch = BatchStatement.builder(DefaultBatchType.UNLOGGED)
    .add(prepared.bind(userId1, "Hello", Instant.now()))
    .add(prepared.bind(userId2, "Hello", Instant.now()))
    .add(prepared.bind(userId3, "Hello", Instant.now()))
    .build();
session.execute(batch);

// 异步查询
ResultSetFuture future = session.executeAsync(
    prepared.bind(userId, "Hello", Instant.now()));

// 一致性级别
SimpleStatement stmt = SimpleStatement.builder(
    "SELECT * FROM twitter.user_timeline WHERE user_id = ?")
    .addPositionalValue(userId)
    .setConsistencyLevel(ConsistencyLevel.LOCAL_QUORUM)
    .setPageSize(100)
    .build();
ResultSet rs = session.execute(stmt);
```

### 10.5.6 Python 客户端(cassandra-driver)

```python
from cassandra.cluster import Cluster, ExecutionProfile, EXEC_PROFILE_DEFAULT
from cassandra.policies import DCAwareRoundRobinPolicy, TokenAwarePolicy
from cassandra import ConsistencyLevel
from cassandra.query import BatchStatement, BatchType
from cassandra.concurrent import execute_concurrent

cluster = Cluster(
    contact_points=['cassandra-1', 'cassandra-2', 'cassandra-3'],
    load_balancing_policy=TokenAwarePolicy(
        DCAwareRoundRobinPolicy(local_dc='us_east_1')
    ),
    port=9042,
    auth_provider={'username': 'cassandra', 'password': 'cassandra'},
    ssl_options={'ca_certs': '/etc/cassandra/ca.crt'},
)

profile = ExecutionProfile(
    consistency_level=ConsistencyLevel.LOCAL_QUORUM,
    request_timeout=10,
)
cluster.add_execution_profile('quorum', profile)

session = cluster.connect('twitter')

# 异步批量写入
insert_stmt = session.prepare(
    "INSERT INTO user_timeline (user_id, tweet_id, text) VALUES (?, now(), ?)"
)
batch = BatchStatement(batch_type=BatchType.UNLOGGED)
for uid in follower_ids:
    batch.add(insert_stmt, (uid, tweet_text))
session.execute(batch)

# 并发查询
results = execute_concurrent(
    session,
    [(insert_stmt, (uid,)) for uid in user_ids],
    concurrency=100,
    results_generator=True,
)
```

### 10.5.7 `nodetool` 运维命令

```bash
# ============ 集群状态 ============
nodetool status
nodetool describecluster
nodetool ring

# ============ 性能 ============
nodetool tpstats                   # 线程池状态
nodetool netstats                  # 网络流
nodetool tablestats twitter.user_timeline  # 表统计
nodetool cfstats twitter.user_timeline

# ============ Compaction ============
nodetool compactionstats           # Compaction 进度
nodetool enableautocompaction twitter.user_timeline
nodetool compact twitter user_timeline  # 手动 Major Compaction

# ============ Repair ============
nodetool repair -pr twitter        # 主范围 Repair(避免全表)
nodetool repair -par twitter       # 并行 Repair
nodetool repair -seq twitter       # 顺序 Repair

# ============ 清理 ============
nodetool cleanup twitter           # 节点退出后清理多余数据
nodetool garbagecollect twitter

# ============ 节点操作 ============
nodetool decommission              # 安全下线
nodetool bootstrap                 # 加入集群
nodetool rebuild                   # 重建(从其他 DC)
nodetool move <new_token>          # 移动 token
```

### 10.5.8 监控(Prometheus + JMX Exporter)

```yaml
# /etc/cassandra/jmx_exporter.yml
hostPort: localhost:7199
ssl: false
lowercaseOutputName: true
lowercaseOutputLabelNames: true
whitelistObjectNames:
  - org.apache.cassandra.metrics:*
  - java.lang:type=GarbageCollector
rules:
  - pattern: 'org.apache.cassandra.metrics<type=(\w+), name=(\w+)><>Value'
    name: cassandra_$1_$2
  - pattern: 'org.apache.cassandra.metrics<type=Table, keyspace=(\w+), table=(\w+), name=(\w+)><>Value'
    name: cassandra_table_$3
    labels:
      keyspace: $1
      table: $2
```

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'cassandra'
    static_configs:
      - targets:
        - 'cassandra-1:7070'
        - 'cassandra-2:7070'
        - 'cassandra-3:7070'
    scrape_interval: 15s
```

---

## 11. 速查表

```
Cassandra 核心:
  Dynamo (一致性哈希 + Quorum + Gossip)
  + Bigtable (LSM-Tree + Column Family)
  
数据模型:
  Partition Key (HASH) + Clustering Key (排序)
  Wide Row (同 partition 大量行)
  
一致性级别:
  ONE / QUORUM / LOCAL_QUORUM / EACH_QUORUM / ALL
  
Compaction:
  SizeTiered (写多)
  Leveled (读多)
  TimeWindow (时间序列)

机制:
  Gossip (P2P 状态传播)
  Hinted Handoff (写时副本不可达 → 缓存)
  Read Repair (读时修复)
  Anti-Entropy Repair (定期 Merkle Tree)

规模:
  单节点 5-20w 写/秒
  跨 DC 异步秒级

教训:
  Query-Driven 建模
  避免 LWT 大量用
  Compaction 监控
  跨 DC 容忍最终一致
```

---

## 12. 交叉引用

- [[08-5-存储-Cassandra与Redis]]:Cassandra 原理
- [[08-2-存储-Dynamo]]:Dynamo 基础
- [[08-3-存储-Bigtable与HBase]]:Bigtable 模型
- [[07-分片与路由]]:一致性哈希
- [[14-故障与容错]]:Gossip 与故障检测

---

## 13. 参考文献

- Lakshman, Malik. *Cassandra: A Decentralized Structured Storage System*. SIGMOD 2009 (Workshop).
- Lakshman et al. *Cassandra: A Decentralized Structured Storage System*. VLDB 2010 (Industrial Track).
- Apache Cassandra Documentation. https://cassandra.apache.org/doc
- Ellis. *Cassandra and Twitter*. 2012.
- Hewitt. *Cassandra: The Definitive Guide*. O'Reilly, 2nd ed 2016.
- Twitter Engineering Blog. https://blog.twitter.com/engineering

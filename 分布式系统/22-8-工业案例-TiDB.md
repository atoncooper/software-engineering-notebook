# 工业案例 —— TiDB 开源 NewSQL

> 章号: §22.8
> 层级: 工业 / 案例
> 标记: 🏭工业 ⭐高频 🔥工程
> 前置: [[08-4-存储-Spanner与NewSQL]] [[05-共识-Raft]] [[06-事务-Percolator与Spanner]]
> 来源: PingCAP 工程博客 + TiDB Tech Talks

---

## 1. 背景

PingCAP 2015 创立,目标:开源 NewSQL,对标 Spanner。

- 兼容 MySQL 协议
- 水平扩展
- 强一致 + 高可用
- HTAP(OLTP + OLAP 同库)

TiDB 在 GitHub 12k+ stars(2024),被国内众多公司使用(知乎、Bilibili、美团、平安等)。

---

## 2. 架构

### 2.1 整体

```
        ┌─────────────────────────┐
        │      TiDB Server        │  ← SQL 层
        │   (无状态, 计算)         │
        └─────┬───────────────────┘
              ↓
        ┌─────────────────────────┐
        │       PD Server          │  ← Placement Driver
        │  (元数据 + TSO + 调度)    │
        └─────┬───────────────────┘
              ↓
        ┌─────────────────────────┐
        │      TiKV Server         │  ← 存储 (Multi-Raft)
        │   (Region: 96MB)         │
        └─────┬───────────────────┘
              ↓
        ┌─────────────────────────┐
        │      TiFlash             │  ← 列存 (HTAP)
        └─────────────────────────┘
```

### 2.2 三大组件

- **TiDB**:SQL 层,无状态,可水平扩展
- **PD(Placement Driver)**:元数据 + 全局时间戳(TSO)+ 调度
- **TiKV**:KV 存储,Multi-Raft,Region(96MB)为分片单位
- **TiFlash**:列存副本,与 TiKV 实时同步(Raft Learner),支持 OLAP

---

## 3. TiKV(Multi-Raft)

### 3.1 数据模型

- Key-Value 存储(有序)
- 单 Region 96MB
- 每 Region 一个 Raft Group(3 副本)

### 3.2 Region 分裂与合并

- 写入超过阈值 → 自动分裂
- 长期空 Region → 合并
- PD 调度,负载均衡

### 3.3 Raft 优化

- **Batch + Pipeline**:多 log entry 批量发送
- **Lease Read**:Leader 租约内本地读
- **Follower Read**:从 Follower 读(降低 Leader 压力)

### 3.4 RocksDB

- 每 Region 一个 RocksDB instance(或共享)
- LSM-Tree 存储
- WAL + MemTable + SSTable

---

## 4. TiDB SQL 层

### 4.1 MySQL 兼容

- 协议层兼容 MySQL
- 客户端用 MySQL Driver 直接连
- SQL 语法兼容(MySQL 大部分)

### 4.2 SQL 编译

```
SQL → Parse (AST) → Logical Plan → Physical Plan → Execution
       ↑                                ↑
       yacc parser                      Catalyst-like optimizer
```

### 4.3 执行

- 算子下推(Push-down):过滤、聚合下推到 TiKV
- Coprocessor:TiKV 上执行 SQL 算子
- 分布式执行:并行扫多个 Region

### 4.4 事务

- 乐观事务(默认):Percolator 风格
- 悲观事务:加锁(类似 2PC)

详见 [[06-事务-Percolator与Spanner]]。

---

## 5. PD(Placement Driver)

### 5.1 职责

- **元数据**:Region 范围、位置
- **TSO**:全局时间戳(单点)
- **调度**:Region 迁移、副本均衡
- **DDL**:Schema 变更协调

### 5.2 TSO(Timestamp Oracle)

- 单 PD 节点分配时间戳
- 类似 Percolator 的 timestamp oracle
- 物理时间(48 位)+ 逻辑号(16 位)

```go
type TSO struct {
    Physical time.Time  // 48 bit ms
    Logical  uint64     // 16 bit
}
```

- 单 PD 处理 TSO 请求,每秒百万级
- PD 通过 Raft HA(3 副本)

### 5.3 调度策略

- **副本均衡**:Region 副本分布均衡
- **Leader 均衡**:Leader 跨节点均衡
- **热 点调度**:热点 Region 分散
- **Label 调度**:跨 DC/机架

---

## 6. TiFlash(HTAP)

### 6.1 列存

- TiFlash = TiKV 的列存副本
- 通过 Raft Learner 异步同步
- Raft 日志写入 TiFlash,转为列存格式

### 6.2 HTAP 查询

- OLTP → TiKV(行存)
- OLAP → TiFlash(列存)
- 优化器自动选择(基于代价)
- 同一份数据,强一致

### 6.3 性能

- 列存扫描比行存快 10-100x
- 不影响 OLTP(异步复制)

---

## 7. 事务模型(Percolator 风格)

### 7.1 乐观事务

```
1. Prewrite (Phase 1)
   - 客户端选 primary key
   - 对所有 key 写入"prewrite" (锁 + 数据)
   - 冲突检测(检查版本)
   
2. Commit (Phase 2)
   - 写 primary key 的 commit 记录
   - 异步清理 secondary key 的锁
```

### 7.2 悲观事务

- 在 Prewrite 前加锁
- DML 直接加锁,事务结束时统一 commit
- 类似 MySQL 的 InnoDB

### 7.3 性能数据

- 单事务延迟:10-30ms(网络主导)
- 单 PD TSO:百万 QPS
- 单 TiKV 节点:几万 TPS

---

## 8. 关键设计选择

### 8.1 为什么不用 TrueTime

- TrueTime 需 GPS + 原子钟,部署成本高
- TSO 单点(但 PD HA)够用
- 牺牲一点延迟换简化部署

### 8.2 为什么用 Region(96MB)

- 太小:Raft Group 多,元数据开销
- 太大:迁移慢,分裂不灵活
- 96MB 是工程平衡点

### 8.3 为什么 Percolator 而非 Spanner 2PC

- Percolator 简单,基于 Bigtable 模型
- Spanner 需要 TrueTime + 跨 Group 2PC
- TiDB 优先简单 + 可维护

### 8.4 为什么列存 + 行存

- 行存:OLTP 高吞吐
- 列存:OLAP 高扫描
- Raft 同步:数据强一致
- 代价:存储 2x

---

## 9. 工业部署案例

### 9.1 知乎

- 替代 MySQL + ES
- 单 TiDB 集群支持亿级帖子
- 实时分析 + 事务同库

### 9.2 Bilibili

- 业务核心库
- 多 DC 部署
- 弹性扩展

### 9.3 平安 / 银行

- 金融场景
- 悲观事务 + 强一致
- 异地多活

### 9.4 海外(Zendesk, Square)

- 替代 Aurora / MySQL
- 全球多区域

---

## 10. 与其他 NewSQL 对比

| 维度 | TiDB | CockroachDB | Spanner |
|------|------|-------------|---------|
| 时钟 | TSO(中心化) | HLC | TrueTime |
| 共识 | Raft | Raft | Paxos |
| 事务 | Percolator | Parallel Commits | 2PC |
| 协议 | MySQL | PostgreSQL | 专有 |
| 开源 | 是 | 是(核心) | 否 |
| 部署 | 简单 | 简单 | 复杂(需 GPS) |

详见 [[08-4-存储-Spanner与NewSQL]]。

---

## 11. 教训

### 11.1 Region 大小选择

- 96MB 是平衡点
- 太小:Raft 元数据开销
- 太大:迁移、分裂代价

### 11.2 热点问题

- 单调递增主键 → 热点(写集中到一个 Region)
- 解决:`AUTO_RANDOM`(随机化)+ `SHARD_ROW_ID_BITS`

### 11.3 PD 单点风险

- TSO 单 PD 处理
- PD HA 通过 Raft
- 监控 PD 延迟

### 11.4 事务模式选择

- OLTP 高并发:乐观(冲突少时)
- 强冲突场景:悲观(避免重试)

---

## 11.5 完整配置文件示例

### 11.5.1 TiDB Server 配置(`tidb.toml`)

```toml
# ============ 基础 ============
host = "0.0.0.0"
port = 4000                            # MySQL 协议端口
advertise-address = "tidb-1.internal"
store = "tikv"
path = "tidb-1:2379,tidb-2:2379,tidb-3:2379"  # PD 地址

# ============ 日志 ============
[log]
level = "info"
format = "text"
file = { filename = "/var/log/tidb/tidb.log", max-size = 300, max-days = 7, max-backups = 10 }

# ============ 性能 ============
[performance]
max-procs = 16                          # CPU 核数
max-memory = 0                          # 0 表示自动
tcp-keep-alive = true
cross-join = false                       # 允许 CROSS JOIN
stats-lease = "3s"
bind-info-lease = "3s"

[tikv-client]
grpc-connection-count = 16
grpc-keepalive-time = 10
grpc-keepalive-timeout = 3
commit-timeout = "41s"
max-batch-size = 128
batch-wait-size = 8
overload-threshold = 200
max-batch-wait-time = "2ms"
region-cache-ttl = 600

[proxy-protocol]
networks = ""                           # PROXY 协议信任 IP

# ============ 兼容性 ============
[isolation-read]
engines = ["tikv", "tiflash", "tidb"]   # 优先 TiKV,有 TiFlash 时也用

# ============ 计划缓存 ============
[prepared-plan-cache]
enabled = true
capacity = 100
memory-guard-ratio = 0.1

[optimistic-txn]
retry-limit = 10
retry-window = "2s"

[pessimistic-txn]
max-retry-count = 256
ttl = "40s"
```

### 11.5.2 PD Server 配置(`pd.toml`)

```toml
# ============ 基础 ============
client-urls = "http://0.0.0.0:2379"
peer-urls = "http://0.0.0.0:2380"
advertise-client-urls = "http://pd-1.internal:2379"
advertise-peer-urls = "http://pd-1.internal:2380"
name = "pd-1"
data-dir = "/data/pd"
initial-cluster = "pd-1=http://pd-1.internal:2380,pd-2=http://pd-2.internal:2380,pd-3=http://pd-3.internal:2380"
initial-cluster-state = "new"

# ============ 日志 ============
[log]
level = "info"
[log.file]
filename = "/var/log/pd/pd.log"
max-size = 300
max-days = 7
max-backups = 10

# ============ 调度 ============
[schedule]
max-snapshot-count = 3
max-pending-peer-count = 16
max-merge-region-size = 20              # MB,Region 合并阈值
max-merge-region-keys = 200000
split-merge-interval = "1h"
patrol-region-interval = "10ms"
max-store-down-time = "30m"
leader-schedule-limit = 4                # Leader 迁移并发
region-schedule-limit = 2048             # Region 迁移并发
replica-schedule-limit = 64
merge-schedule-limit = 8
hot-region-schedule-limit = 4
hot-region-cache-hits-threshold = 3

# ============ 复制 ============
[replication]
max-replicas = 3                         # 副本数
location-labels = ["region", "zone", "host"]
strictly-match-label = false

# ============ Label 调度 ============
[store]
store-balance-algorithm = "size-based"

# ============ TSO ============
[tso]
save-interval = "3s"
update-physical-interval = "50ms"

# ============ Dashboard ============
[dashboard]
tidb-cacert-path = ""
tidb-key-path = ""
public-path-prefix = "/dashboard"
internal-proxy = false
enable-telemetry = false
```

### 11.5.3 TiKV Server 配置(`tikv.toml`)

```toml
# ============ 基础 ============
[server]
addr = "0.0.0.0:20160"
advertise-addr = "tikv-1.internal:20160"
status-addr = "0.0.0.0:20180"
status-thread-pool-size = 1
grpc-concurrency = 5
grpc-raft-conn-num = 10
grpc-memory-pool-size = 128

[storage]
data-dir = "/data/tikv"
gc-ratio-threshold = 1.1
max-key-size = 8192
scheduler-concurrency = 2048000
scheduler-worker-pool-size = 4
scheduler-pending-write-threshold = "100MB"

[storage.block-cache]
shared = true
capacity = "8GB"

[storage.flow-control]
enabled = true
memtables-threshold = 5
l0-files-threshold = 20
soft-pending-compaction-bytes-limit = "192GB"
hard-pending-compaction-bytes-limit = "1024GB"

# ============ Raft ============
[raftstore]
sync-log = true
prevote = true
raft-base-tick-interval = "1s"
raft-heartbeat-ticks = 2
raft-election-timeout-ticks = 10
raft-entry-max-size = "8MB"
raft-log-gc-threshold = 50
raft-log-gc-count-limit = 72000
raft-log-gc-size-limit = "64MB"
raft-entry-cache-life-time = "30s"
raft-reject-transfer-leader-duration = "3s"
split-region-check-tick-interval = "10s"
region-split-size = "96MB"
region-max-size = "144MB"
region-split-keys = 960000
region-max-keys = 1440000
consistency-check-interval = "10s"
clean-stale-peer-threshold = 10000
apply-pool-size = 2
store-pool-size = 2

[coprocessor]
split-region-on-table = false
batch-split-limit = 10
region-max-size = "144MB"
region-split-size = "96MB"
region-max-keys = 1440000
region-split-keys = 960000

# ============ RocksDB ============
[rocksdb]
wal-recovery-mode = "point-in-time"
max-open-files = 40960
max-background-jobs = 12
max-sub-compactions = 3
titan = false

[rocksdb.defaultcf]
compression = "zstd"
block-size = "64KB"
block-cache-size = "8GB"
disable-block-cache = false
cache-index-and-filter-blocks = true
pin-l0-filter-and-index-blocks = true
bloom-filter-bits-per-key = 10
baseline-bits-per-key = 0
write-buffer-size = "128MB"
max-write-buffer-number = 5
min-write-buffer-number-to-merge = 1
level0-file-num-compaction-trigger = 4
level0-slowdown-writes-trigger = 20
level0-stop-writes-trigger = 36
target-file-size-base = "8MB"
max-bytes-for-level-base = "512MB"

[rocksdb.writecf]
compression = "zstd"
block-cache-size = "8GB"
write-buffer-size = "128MB"
max-write-buffer-number = 5

[rocksdb.lockcf]
compression = "no"
block-cache-size = "1GB"
write-buffer-size = "32MB"
max-write-buffer-number = 5

[raftdb]
max-open-files = 40960
max-background-jobs = 4

[raftdb.defaultcf]
compression = "no"
block-cache-size = "1GB"

# ============ Security ============
[security]
ca-path = "/etc/tikv/ca.crt"
cert-path = "/etc/tikv/tikv.crt"
key-path = "/etc/tikv/tikv.key"
cert-allowed-cn = ["tikv"]

# ============ Import/Export ============
[import]
num-threads = 8
stream-channel-window = 5
```

### 11.5.4 TiFlash 配置(`tiflash.toml`)

```toml
[server]
tcp_port = 9000
http_port = 8123
flash_service_addr = "tiflash-1.internal:3930"
flash_proxy_addr = "0.0.0.0:20170"
advertise_addr = "tiflash-1.internal:3930"

[storage]
path = "/data/tiflash/data"
capacity = "1TB"
[storage.main]
dir = ["/data/tiflash/data"]
[storage.raft]
pd_addr = "pd-1.internal:2379,pd-2.internal:2379"

[logger]
log = "/var/log/tiflash/tiflash.log"
level = "info"
[rafts3]
endpoint = "tiflash-1.internal:20170"

[profiles.default]
max_memory_usage = 0
max_threads = 16
use_uncompressed_cache = 0
```

### 11.5.5 K8s 部署(TiDB Operator)

```yaml
# tidb-cluster.yaml
apiVersion: pingcap.com/v1alpha1
kind: TidbCluster
metadata:
  name: tidb-prod
  namespace: tidb
spec:
  version: v7.5.0
  timezone: Asia/Shanghai
  pvReclaimPolicy: Retain
  enableDynamicConfiguration: true
  configUpdateStrategy: RollingUpdate

  pd:
    replicas: 3
    requests:
      storage: 100Gi
    config:
      schedule:
        max-merge-region-size: 20
      replication:
        max-replicas: 3
        location-labels:
          - region
          - zone
          - host

  tikv:
    replicas: 5
    requests:
      storage: 2Ti
    config:
      storage:
        block-cache:
          shared: true
          capacity: "16GB"
      raftstore:
        region-split-size: "96MB"
    nodeSelector:
      disktype: nvme-ssd

  tidb:
    replicas: 3
    requests:
      cpu: 4
      memory: 8Gi
    service:
      type: LoadBalancer
      externalTrafficPolicy: Local
    config:
      performance:
        max-procs: 8

  tiflash:
    replicas: 2
    requests:
      storage: 1Ti
    config:
      logger:
        level: "info"
    nodeSelector:
      disktype: nvme-ssd

  helper:
    image: busybox:1.34
```

### 11.5.6 SQL 客户端示例(Go)

```go
package main

import (
    "database/sql"
    "fmt"
    _ "github.com/go-sql-driver/mysql"
)

func main() {
    dsn := "root:@tcp(tidb-prod.internal:4000)/orders?charset=utf8mb4" +
        "&parseTime=true&loc=Local" +
        "&readTimeout=30s&writeTimeout=30s&timeout=5s"

    db, err := sql.Open("mysql", dsn)
    if err != nil {
        panic(err)
    }
    defer db.Close()

    db.SetMaxOpenConns(100)
    db.SetMaxIdleConns(20)
    db.SetConnMaxLifetime(5 * time.Minute)

    // 悲观事务
    tx, _ := db.Begin()
    defer tx.Rollback()

    // 转账(悲观锁,自动加)
    _, err = tx.Exec(
        "UPDATE accounts SET balance = balance - ? WHERE id = ?",
        100, 1)
    if err != nil { panic(err) }

    _, err = tx.Exec(
        "UPDATE accounts SET balance = balance + ? WHERE id = ?",
        100, 2)
    if err != nil { panic(err) }

    tx.Commit()
}

// AUTO_RANDOM 避免热点
// CREATE TABLE orders (
//     id BIGINT NOT NULL AUTO_RANDOM PRIMARY KEY,
//     user_id BIGINT,
//     amount DECIMAL(10,2),
//     INDEX idx_user (user_id)
// ) SHARD_ROW_ID_BITS = 4 PRE_SPLIT_REGIONS = 4;
```

### 11.5.7 Python 客户端(SQLAlchemy)

```python
from sqlalchemy import create_engine, text
from sqlalchemy.pool import QueuePool

engine = create_engine(
    "mysql+pymysql://root:@tidb-prod.internal:4000/orders",
    poolclass=QueuePool,
    pool_size=20,
    max_overflow=10,
    pool_pre_ping=True,
    pool_recycle=1800,
    connect_args={
        "charset": "utf8mb4",
        "read_timeout": 30,
        "write_timeout": 30,
    },
)

with engine.connect() as conn:
    # 乐观事务(默认)
    with conn.begin():
        result = conn.execute(text(
            "SELECT * FROM orders WHERE user_id = :uid"
        ), {"uid": 123})
        for row in result:
            print(row)
```

### 11.5.8 BR 备份恢复

```bash
# 备份到 S3
tiup br backup full \
    --pd "pd-1.internal:2379" \
    --storage "s3://backup/tidb-$(date +%Y%m%d)" \
    --s3.endpoint "https://s3.amazonaws.com" \
    --concurrency 4 \
    --log-file /var/log/br/backup.log

# PITR(Point-in-Time Recovery,需要日志备份)
tiup br log start \
    --task-name "continuous-backup" \
    --pd "pd-1.internal:2379" \
    --storage "s3://backup/tidb-log"

# 恢复到指定时间点
tiup br restore point \
    --pd "pd-1.internal:2379" \
    --storage "s3://backup/tidb-$(date +%Y%m%d)" \
    --restored-ts '2024-03-15 10:00:00+08:00'
```

---

## 12. 速查表

```
TiDB 架构:
  TiDB (SQL, 无状态)
  PD (元数据 + TSO + 调度, Raft HA)
  TiKV (Multi-Raft, 96MB Region)
  TiFlash (列存, Raft Learner)

事务:
  乐观: Percolator (Prewrite + Commit)
  悲观: 加锁 + Percolator
  
TSO: 物理(48b) + 逻辑(16b)
  PD 单点处理, Raft HA
  百万 QPS

关键设计:
  Region 96MB (平衡点)
  TSO 替代 TrueTime (简化部署)
  Percolator (简单可维护)
  列存 + 行存 (HTAP)

性能:
  事务延迟 10-30ms
  单 TiKV 数万 TPS

部署案例:
  知乎 / Bilibili / 平安 / 海外

教训:
  AUTO_RANDOM 避免热点
  PD 监控
  事务模式按冲突选择
```

---

## 13. 交叉引用

- [[08-4-存储-Spanner与NewSQL]]:NewSQL 对比
- [[05-共识-Raft]]:Raft 共识
- [[06-事务-Percolator与Spanner]]:Percolator 事务
- [[22-1-工业案例-Google-Spanner]]:对比 Spanner
- [[22-9-工业案例-CockroachDB]]:对比 CockroachDB

---

## 14. 参考文献

- TiDB Documentation. https://docs.pingcap.com/tidb
- Huang et al. *TiDB: A Raft-based HTAP Database*. VLDB 2021 (Industry).
- Peng, Dabek. *Large-scale Incremental Processing Using Distributed Transactions and Notifications*. OSDI 2010. (Percolator)
- Ongaro, Ousterhout. *In Search of an Understandable Consensus Algorithm*. ATC 2014. (Raft)
- PingCAP Engineering Blog. https://pingcap.com/blog
- TiDB Tech Talks. https://pingcap.com/community/tidb-tech-talks

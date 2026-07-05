# 工业案例 —— CockroachDB 地理分布式 SQL

> 章号: §22.9
> 层级: 工业 / 案例
> 标记: 🏭工业 ⭐高频 🔥工程
> 前置: [[08-4-存储-Spanner与NewSQL]] [[05-共识-Raft]] [[03-时间与时钟]]
> 来源: Cockroach Labs 工程博客 + 论文

---

## 1. 背景

Cockroach Labs 2015 创立(由 Google 工程师 Spencer Kimball 等创立),目标:开源、地理分布式、强一致 SQL DB。

设计目标:

- **Survive failures**(故障幸存):disk/machine/DC 故障不停服
- **Strong consistency**:跨 DC 强一致
- **Geo-distribution**:数据按地理分布,本地访问
- **PostgreSQL 兼容**:协议 + 语法

---

## 2. 架构

### 2.1 整体(对称节点)

```
All nodes are symmetric:
  SQL Layer (Pgwire protocol)
       ↓
  Transaction Layer (HLC + Parallel Commits)
       ↓
  Distribution Layer (Range = 64MB)
       ↓
  Replication Layer (Raft, 3 副本)
       ↓
  Storage Layer (RocksDB / Pebble)
```

与 TiDB 不同,CockroachDB **无中心节点**(无 PD)。

### 2.2 关键抽象

- **Range**:数据分片(默认 64MB),对应一个 Raft Group
- **Leaseholder**:Range 的"租约持有者",处理读写
- **Gossip**:节点间元数据传播(类似 Cassandra)

---

## 3. 数据模型与 SQL

### 3.1 PostgreSQL 兼容

- 协议层兼容 PostgreSQL
- 客户端用 `psql` / `pgx` 直连
- 语法大部分兼容

```sql
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT,
    region STRING,
    created_at TIMESTAMPTZ DEFAULT now()
);

-- 地理分区
ALTER TABLE users PARTITION BY LIST (region);
```

### 3.2 Geo-Partitioning

```sql
CREATE PARTITION us_east VALUES IN ('us-east')
    AT LOCALITY 'region=us-east-1';
CREATE PARTITION us_west VALUES IN ('us-west')
    AT LOCALITY 'region=us-west-1';
```

- 数据按 region 分区
- 副本就近放置
- 本地读,跨 region 写

---

## 4. 一致性: HLC

### 4.1 HLC(Hybrid Logical Clock)

- 物理 + 逻辑组合
- 物理部分接近 NTP 时间
- 逻辑部分解决同物理时间冲突
- 详见 [[03-时间与时钟]]

### 4.2 vs TrueTime

| 维度 | TrueTime | HLC |
|------|----------|-----|
| 时钟源 | GPS + 原子钟 | NTP |
| 误差 | <7ms | 100-250ms(NTP 依赖) |
| Commit Wait | 必须 | 不需要 |
| 部署 | 复杂(需特殊硬件) | 简单(任何 NTP) |

HLC 牺牲一点延迟精度,换部署简单。

### 4.3 与 Spanner External Consistency 对比

- Spanner:用 TrueTime + Commit Wait → External Consistency
- CockroachDB:HLC + 重读机制 → 接近但非严格 External
- 实际工程够用(99.999% 场景)

---

## 5. 事务: Parallel Commits

### 5.1 传统 2PC

```
1. Prewrite (Phase 1): 所有 key 加锁
2. Commit (Phase 2): 写 commit 记录
```

问题:两阶段,延迟翻倍。

### 5.2 Parallel Commits(CockroachDB 自研)

```
1. Prewrite + Staging(标记"待 commit")
   - 同时发到所有 key
   - 数据已写,但状态待定
   
2. 客户端可立即返回(乐观)
   - 因为"待 commit"等价于已 commit
   
3. 异步 Resolve Txn Record
   - 清理 staging 状态
```

**优势**:事务延迟从 2 RTT 降到 1 RTT。

### 5.3 读事务

- Snapshot Read:HLC 时戳读对应版本
- 不加锁,只读已提交版本

---

## 6. Raft 实现

### 6.1 Multi-Raft

- 每 Range 一个 Raft Group
- 单节点可有数千 Range
- Batch + Pipeline 优化

### 6.2 Leaseholder

- Range 的 Leader = Leaseholder
- 处理所有读写(本地读)
- 跨 Range 事务通过 2PC

### 6.3 Range 分裂与合并

- 64MB 自动分裂
- 空 Range 合并
- Gossip 传播元数据

---

## 7. Geo-Distribution 设计

### 7.1 Topology

- 节点配置:`--locality=region=us-east,zone=us-east-1a`
- 数据按 locality 优化放置

### 7.2 Replication

- 默认 3 副本,跨 region/rack
- **Region Survival**:单 region 故障仍可用
- **Zone Survival**:单 zone 故障仍可用

### 7.3 Follower Reads

- 从 Follower 读(本地读)
- 牺牲一点一致性,换低延迟
- 适合"读多写少"场景

```sql
SET enable_follower_reads_for_insert = true;
SELECT * FROM users AS OF SYSTEM TIME '-5s';
```

### 7.4 Performance

- 单 region 延迟:< 10ms
- 跨 region:50-100ms
- 跨大洲:100-300ms

---

## 8. 与 Spanner / TiDB 对比

| 维度 | Spanner | TiDB | CockroachDB |
|------|---------|------|-------------|
| 时钟 | TrueTime | TSO | HLC |
| 共识 | Paxos | Raft | Raft |
| 事务 | 2PC + Commit Wait | Percolator | Parallel Commits |
| SQL | 私有 | MySQL | PostgreSQL |
| 中心节点 | 无(universe) | PD | 无 |
| Geo | 原生 | 有限 | 原生 |
| 开源 | 否 | 是 | 是(核心) |

详见 [[08-4-存储-Spanner与NewSQL]] 和 [[22-8-工业案例-TiDB]]。

---

## 9. 工业部署案例

### 9.1 DoorDash

- 替代 PostgreSQL 单机
- 多 region 强一致
- 处理订单 + 支付

### 9.2 Bose

- IoT + 电商
- 跨大洲部署
- 本地读 + 强一致

### 9.3 Netflix(部分场景)

- 替代 Cassandra(部分)
- 强一致需求场景

### 9.4 国内

- 部分公司用 CockroachDB Core(开源版)
- 国产化替代场景

---

## 10. 关键设计选择

### 10.1 为什么不用 TrueTime

- TrueTime 需 GPS + 原子钟
- CockroachDB 主打"任何云都能跑"
- HLC 是工程权衡

### 10.2 为什么无中心节点

- 简化部署:任何节点平等
- Gossip 传播元数据
- 代价:元数据一致性弱于 PD 模式

### 10.3 为什么 PostgreSQL 兼容

- PG 生态丰富(工具、ORM)
- 客户端迁移成本低
- 比 MySQL 兼容更有优势(数据类型 + JSON 支持)

### 10.4 为什么默认 Serializable 隔离

- CockroachDB 默认 Serializable
- 简化正确性推理
- 代价:并发性能略低

---

## 11. 性能与规模

- 单节点:数千 TPS
- 集群(10+ 节点):万级 TPS
- 单 Range:64MB
- 总数据:TB 级(生产案例)

### 11.1 延迟

- 本地读:< 5ms
- 本地写:< 20ms(Quorum)
- 跨 region:50-100ms
- 跨大洲:100-300ms

---

## 12. 教训

### 12.1 HLC 与 NTP 依赖

- NTP 漂移大会影响 HLC
- 必须配 NTP(chrony)
- 监控时钟偏移

### 12.2 Range 大小(64MB)

- 比 TiDB(96MB)小,迁移更快
- 元数据开销略大
- 工程平衡

### 12.3 Geo 设计需业务配合

- Partition + Locality 设计
- 跨 region 写要避免(慢)
- 业务建模需考虑数据局部性

### 12.4 Follower Reads 谨慎用

- 牺牲一致性
- 仅对"读多写少 + 容忍 stale" 场景

---

## 12.5 完整配置与部署示例

### 12.5.1 启动命令(单节点)

```bash
cockroach start \
    --advertise-addr=cockroach-1.internal:26257 \
    --listen-addr=0.0.0.0:26257 \
    --http-addr=0.0.0.0:8080 \
    --locality=region=us-east-1,zone=us-east-1a,dc=us-east-1 \
    --store=path=/data/cockroach,size=80% \
    --cache=8GiB \
    --max-sql-memory=4GiB \
    --max-disk-temp-storage=4GiB \
    --join=cockroach-1.internal:26257,cockroach-2.internal:26257,cockroach-3.internal:26257 \
    --certs-dir=/etc/cockroach/certs \
    --background
```

### 12.5.2 集群初始化

```bash
# 在第一个节点执行(只执行一次)
cockroach init --certs-dir=/etc/cockroach/certs \
    --host=cockroach-1.internal:26257

# 添加节点(任意节点)
cockroach node --certs-dir=/etc/cockroach/certs \
    --host=cockroach-1.internal:26257 \
    ls
```

### 12.5.3 Systemd 部署

```ini
# /etc/systemd/system/cockroach.service
[Unit]
Description=Cockroach Database cluster node
Requires=network.target
After=network.target

[Service]
Type=notify
WorkingDirectory=/var/lib/cockroach
ExecStart=/usr/local/bin/cockroach start \
    --certs-dir=/etc/cockroach/certs \
    --advertise-addr=${NODE_IP}:26257 \
    --join=cockroach-1.internal:26257,cockroach-2.internal:26257,cockroach-3.internal:26257 \
    --cache=8GiB \
    --max-sql-memory=4GiB \
    --locality=region=us-east-1,zone=us-east-1a
ExecStop=/usr/local/bin/cockroach quit --certs-dir=/etc/cockroach/certs \
    --host=${NODE_IP}:26257
LimitNOFILE=35000
LimitNPROC=12288
User=cockroach
Group=cockroach
Restart=always
RestartSec=10
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=cockroach

[Install]
WantedBy=default.target
```

### 12.5.4 K8s 部署(StatefulSet)

```yaml
# cockroachdb-statefulset.yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: cockroachdb
  namespace: cockroach
spec:
  serviceName: cockroachdb
  replicas: 5
  podManagementPolicy: Parallel
  selector:
    matchLabels:
      app: cockroachdb
  template:
    metadata:
      labels:
        app: cockroachdb
    spec:
      affinity:
        podAntiAffinity:
          preferredDuringSchedulingIgnoredDuringExecution:
            - weight: 100
              podAffinityTerm:
                labelSelector:
                  matchExpressions:
                    - key: app
                      operator: In
                      values:
                        - cockroachdb
                topologyKey: kubernetes.io/hostname
      containers:
        - name: cockroachdb
          image: cockroachdb/cockroach:v23.1.0
          imagePullPolicy: IfNotPresent
          ports:
            - containerPort: 26257
              name: grpc
            - containerPort: 8080
              name: http
          resources:
            requests:
              cpu: 4
              memory: 16Gi
            limits:
              cpu: 8
              memory: 32Gi
          env:
            - name: COCKROACH_CHANNEL
              value: kubernetes-insecure
          command:
            - /bin/bash
            - -c
            - |
              exec /cockroach/cockroach start \
                --logtostderr \
                --advertise-host $(hostname -f) \
                --http-addr 0.0.0.0:8080 \
                --cache 25% \
                --max-sql-memory 25% \
                --join cockroachdb-0.cockroachdb,cockroachdb-1.cockroachdb,cockroachdb-2.cockroachdb,cockroachdb-3.cockroachdb,cockroachdb-4.cockroachdb \
                --locality region=us-east-1,zone=$ZONE
          volumeMounts:
            - name: datadir
              mountPath: /cockroach/cockroach-data
      volumes:
        - name: datadir
          persistentVolumeClaim:
            claimName: datadir
  volumeClaimTemplates:
    - metadata:
        name: datadir
      spec:
        accessModes:
          - ReadWriteOnce
        resources:
          requests:
            storage: 1Ti
        storageClassName: ssd-sc
```

### 12.5.5 集群配置(SQL 级)

```sql
-- 集群级配置
SET CLUSTER SETTING cluster.organization = 'DoorDash Inc.';
SET CLUSTER SETTING cluster.license = 'xxxx-xxxx-xxxx';

-- 时钟监控
SET CLUSTER SETTING server.clock.forward_jump_check_enabled = true;
SET CLUSTER SETTING server.clock.persist_upper_bound_interval = '24h';

-- Range 分裂
SET CLUSTER SETTING kv.range_split.load_based_splitting = true;
SET CLUSTER SETTING kv.range_split.by_load_merge_delay = '5m';

-- RocksDB cache
SET CLUSTER SETTING rocksdb.encrypted_env = false;

-- SQL 优化
SET CLUSTER SETTING sql.stats.automatic_collection.enabled = true;
SET CLUSTER SETTING sql.stats.histogram_collection.enabled = true;

-- Zone Config(数据分布)
ALTER RANGE default CONFIGURE ZONE USING
    num_replicas = 3,
    constraints = '[-region=us-east]',
    gc.ttlseconds = 90000;

-- 数据库级 Zone Config
ALTER DATABASE orders CONFIGURE ZONE USING
    num_replicas = 5,
    constraints = '{+region=us-east: 2, +region=us-west: 2, +region=eu-west: 1}',
    lease_preferences = '[[+region=us-east]]';

-- 表级 Partition + Locality
ALTER TABLE orders PARTITION BY LIST (region) (
    PARTITION us_east VALUES IN ('us-east')
        CONFIGURE ZONE USING
            constraints = '[+region=us-east]',
            lease_preferences = '[[+region=us-east]]',
            num_replicas = 3,
    PARTITION us_west VALUES IN ('us-west')
        CONFIGURE ZONE USING
            constraints = '[+region=us-west]',
            lease_preferences = '[[+region=us-west]]',
            num_replicas = 3,
    PARTITION eu_west VALUES IN ('eu-west')
        CONFIGURE ZONE USING
            constraints = '[+region=eu-west]',
            lease_preferences = '[[+region=eu-west]]',
            num_replicas = 3
);
```

### 12.5.6 Python 客户端(asyncpg + CockroachDB)

```python
import asyncio
import asyncpg
from contextlib import asynccontextmanager

# CockroachDB PG 兼容,可直接用 asyncpg
class CockroachDB:
    def __init__(self, dsn: str, pool_size: int = 10):
        self.dsn = dsn
        self.pool_size = pool_size
        self.pool = None

    async def connect(self):
        self.pool = await asyncpg.create_pool(
            dsn=self.dsn,
            min_size=5,
            max_size=self.pool_size,
            max_queries=50000,
            max_inactive_connection_lifetime=300,
            command_timeout=30,
            server_settings={
                "application_name": "orders-service",
                "default_transaction_isolation": "serializable",
                "jit": "off",  # JIT 在 OLTP 下不一定有用,可关
            },
        )

    @asynccontextmanager
    async def transaction(self):
        async with self.pool.acquire() as conn:
            async with conn.transaction():
                yield conn

    async def transfer(self, from_id: int, to_id: int, amount: float):
        async with self.transaction() as conn:
            # 悲观事务(显式 SELECT FOR UPDATE)
            row = await conn.fetchrow(
                "SELECT balance FROM accounts WHERE id = $1 FOR UPDATE",
                from_id
            )
            if row["balance"] < amount:
                raise ValueError("Insufficient balance")

            await conn.execute(
                "UPDATE accounts SET balance = balance - $1 WHERE id = $2",
                amount, from_id
            )
            await conn.execute(
                "UPDATE accounts SET balance = balance + $1 WHERE id = $2",
                amount, to_id
            )

# 使用
db = CockroachDB(
    "postgresql://root@cockroachdb-0.cockroachdb:26257/orders?sslmode=verify-full"
    "&sslrootcert=/etc/cockroach/certs/ca.crt"
    "&sslcert=/etc/cockroach/certs/client.root.crt"
    "&sslkey=/etc/cockroach/certs/client.root.key"
)
await db.connect()
await db.transfer(1, 2, 100.0)
```

### 12.5.7 Java 客户端(HikariCP + JDBC)

```java
// application.yml
spring:
  datasource:
    url: jdbc:postgresql://cockroachdb-0:26257,cockroachdb-1:26257,cockroachdb-2:26257/orders
      ?sslmode=verify-full
      &sslrootcert=/etc/cockroach/certs/ca.crt
      &sslcert=/etc/cockroach/certs/client.root.crt
      &sslkey=/etc/cockroach/certs/client.root.key.pk8
      &loadBalanceHosts=true
      &hostRecheckSeconds=10
      &targetServerType=primary
    driver-class-name: org.postgresql.Driver
    username: root
    hikari:
      maximum-pool-size: 50
      minimum-idle: 10
      idle-timeout: 600000
      max-lifetime: 1800000
      connection-timeout: 30000
      connection-test-query: SELECT 1
      transaction-isolation: TRANSACTION_SERIALIZABLE
```

```java
// Spring Boot + JPA
@Service
public class OrderService {
    @Autowired
    private OrderRepository orderRepo;
    
    @Autowired
    private AccountRepository accountRepo;
    
    @Transactional(isolation = Isolation.SERIALIZABLE)
    public void createOrder(Long userId, BigDecimal amount) {
        Account account = accountRepo.findById(userId)
            .orElseThrow(() -> new RuntimeException("Account not found"));
        
        if (account.getBalance().compareTo(amount) < 0) {
            throw new RuntimeException("Insufficient balance");
        }
        
        account.setBalance(account.getBalance().subtract(amount));
        accountRepo.save(account);
        
        Order order = new Order(userId, amount);
        orderRepo.save(order);
    }
    
    // 重试(面对 40001 serialization conflict)
    @Retryable(
        value = {org.springframework.dao.OptimisticLockingFailureException.class,
                 org.postgresql.util.PSQLException.class},
        maxAttempts = 5,
        backoff = @Backoff(delay = 100, multiplier = 2, maxDelay = 1000)
    )
    public void createOrderWithRetry(Long userId, BigDecimal amount) {
        createOrder(userId, amount);
    }
}
```

### 12.5.8 备份与导入

```bash
# 备份到 S3
cockroach sql --insecure --execute "
BACKUP DATABASE orders
TO 's3://backup/cockroach/orders-$(date +%Y%m%d)?AWS_ACCESS_KEY_ID=xxx&AWS_SECRET_ACCESS_KEY=xxx'
AS OF SYSTEM TIME '-10s';
"

# 全集群备份
cockroach sql --insecure --execute "
BACKUP TO 's3://backup/cockroach/cluster-$(date +%Y%m%d)?AWS_ACCESS_KEY_ID=xxx&AWS_SECRET_ACCESS_KEY=xxx';
"

# 恢复
cockroach sql --insecure --execute "
RESTORE DATABASE orders
FROM 's3://backup/cockroach/orders-20240315?AWS_ACCESS_KEY_ID=xxx&AWS_SECRET_ACCESS_KEY=xxx';
"

# 导入 CSV
cockroach sql --insecure --execute "
IMPORT INTO orders (id, user_id, amount, created_at)
CSV DATA (
    's3://data/orders.csv?AWS_ACCESS_KEY_ID=xxx&AWS_SECRET_ACCESS_KEY=xxx'
) WITH skip = '1';
"
```

### 12.5.9 监控(Prometheus)

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'cockroachdb'
    static_configs:
      - targets:
        - 'cockroachdb-0:8080'
        - 'cockroachdb-1:8080'
        - 'cockroachdb-2:8080'
    metrics_path: '/_status/vars'
    scrape_interval: 15s
```

```promql
# 关键告警
# 1. 节点宕机
count(up{job="cockroachdb"} == 0) > 0

# 2. Range 不可用
sum(rate(coderaft_range_unavailable[5m])) > 0

# 3. 慢查询
histogram_quantile(0.99, rate(sql_latency_seconds_bucket{op="exec"}[5m])) > 1

# 4. 时钟漂移
abs(hlc_physical_time - scrape_samples_scraped) > 500000000  # 500ms
```

---

## 13. 速查表

```
CockroachDB 核心:
  对称节点 (无 PD)
  Range (64MB) = Raft Group
  HLC (NTP + 逻辑)
  Parallel Commits (1 RTT)
  PostgreSQL 兼容

Geo 分布:
  --locality=region=X,zone=Y
  Partition + Locality
  Follower Reads (本地读, stale)
  Region/Zone Survival

事务:
  Serializable (默认)
  Parallel Commits (1 RTT)
  Snapshot Read (HLC)

vs Spanner:
  TrueTime → HLC (简化)
  2PC → Parallel Commits (1 RTT)
  
vs TiDB:
  TSO → HLC (无中心)
  Percolator → Parallel Commits
  MySQL → PostgreSQL

部署案例:
  DoorDash / Bose / Netflix (部分)

教训:
  NTP 监控
  Geo 设计需业务配合
  Follower Reads 谨慎
```

---

## 14. 交叉引用

- [[08-4-存储-Spanner与NewSQL]]:NewSQL 对比
- [[05-共识-Raft]]:Raft 共识
- [[03-时间与时钟]]:HLC / TrueTime
- [[22-1-工业案例-Google-Spanner]]:Spanner 对比
- [[22-8-工业案例-TiDB]]:TiDB 对比

---

## 15. 参考文献

- Taft et al. *CockroachDB: The Resilient Geo-Distributed SQL Database*. SIGMOD 2020.
- CockroachDB Documentation. https://www.cockroachlabs.com/docs
- Kulkarni et al. *Serializable, Snapshot Isolation, and the Search for a Middle Ground*. 2019.
- Cockroach Labs Engineering Blog. https://www.cockroachlabs.com/community/
- Spitzer, Kimball. *CockroachDB Design*. 2014 (Initial Design Doc).
- Parallel Commits RFC. https://github.com/cockroachdb/cockroach

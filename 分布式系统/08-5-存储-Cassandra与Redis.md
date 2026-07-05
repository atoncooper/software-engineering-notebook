# 存储系统 —— Cassandra / Redis / MongoDB

> 章号: §8.5
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 🔥工程 🏭工业
> 前置: [[08-2-存储-Dynamo]] [[08-3-存储-Bigtable与HBase]] [[07-分片与路由]]

---

## 0. 三种不同的工业存储

本章聚焦三个工业级存储,代表三种不同设计哲学:

| 存储 | 设计哲学 | 适用 |
|------|---------|------|
| **Cassandra** | Dynamo (AP) + Bigtable (LSM) | 海量写、最终一致、跨 DC |
| **Redis** | 内存优先 + 单线程 + 主从 | 低延迟缓存、会话、计数 |
| **MongoDB** | 文档模型 + B-Tree + Sharding | 半结构化数据、Web 应用 |

三者覆盖了"NoSQL 时代"的三大典型场景,本章分析其架构与选型。

---

## 1. Cassandra:Dynamo + Bigtable

### 1.1 定位

Cassandra (Facebook 2008,开源 2009) 是 Dynamo + Bigtable 的混合:

- **从 Dynamo**:一致性哈希 + vnode、Quorum (W+R>N)、反熵、Gossip、Sloppy Quorum、Hinted Handoff
- **从 Bigtable**:LSM-Tree、SSTable、Compaction、Column Family

> 📜 Lakshman & Malik, 2009 — *Cassandra: A Decentralized Structured Storage System*

### 1.2 数据模型

CQL (Cassandra Query Language):

```sql
CREATE TABLE users (
    user_id UUID PRIMARY KEY,
    name text,
    email text,
    age int
);

CREATE TABLE events (
    user_id UUID,
    event_time timestamp,
    event_type text,
    data text,
    PRIMARY KEY (user_id, event_time)
) WITH CLUSTERING ORDER BY (event_time DESC);
```

- **Partition Key** (`user_id`):决定数据分布(一致性哈希)
- **Clustering Key** (`event_time`):决定分区内排序
- 同一 Partition 内数据物理相邻,支持高效范围扫描
- 不同 Partition 数据随机分布

### 1.3 架构

```
                  ┌──────────────┐
                  │   Client     │
                  └──────┬───────┘
                         ↓
              ┌─────────────────────────┐
              │  Any Cassandra Node     │  ← 对等,任意节点可作协调者
              │  (Coordinator)          │
              └────┬────────────────────┘
                   │ Gossip 同步集群状态
       ┌───────────┼───────────┐
       ↓           ↓           ↓
   [Node A]   [Node B]    [Node C]    ← 每节点承载若干 vnode
       ↓           ↓           ↓
   [MemTable] [MemTable] [MemTable]
   [SSTable]  [SSTable]  [SSTable]
   [CommitLog][CommitLog][CommitLog]
```

#### 1.3.1 节点角色对等

- 无 Master,所有节点平等
- 任意节点可作为 Coordinator 接收客户端请求
- Coordinator 转发到对应 vnode 所在节点

#### 1.3.2 存储引擎

- **CommitLog**:WAL,顺序写,故障恢复
- **MemTable**:内存中的 LSM-Tree 第一层
- **SSTable**:不可变的磁盘文件,带索引和 Bloom Filter
- **Compaction**:Size-Tiered(默认)/ Leveled / Time-Windowed

### 1.4 读写路径

#### 1.4.1 写路径

```
1. Client → Coordinator(任意节点)
2. Coordinator 用一致性哈希找 N 个 vnode 节点
3. Coordinator 转发写请求到 N 个节点
4. 每个节点:
   - 写 CommitLog(顺序写)
   - 写 MemTable(内存)
   - 返回 ACK
5. Coordinator 等待 W 个 ACK,返回 Client
6. MemTable 满后 flush 成 SSTable,异步 Compaction
```

#### 1.4.2 读路径

```
1. Client → Coordinator
2. Coordinator 找 N 个 vnode 节点
3. Coordinator 同时发读请求到 R 个节点(可能更多)
4. 各节点查 MemTable + SSTable + Row Cache + Key Cache
5. 返回数据(带 timestamp)
6. Coordinator 收到 R 个响应,选 timestamp 最大的返回
7. 若发现有不一致,触发 Read Repair
```

### 1.5 Consistency Level

Cassandra 的可调一致性(每次操作可指定):

| Level | 含义 |
|-------|------|
| ONE | 等待 1 个副本响应 |
| TWO | 等待 2 个副本响应 |
| THREE | 等待 3 个副本响应 |
| QUORUM | 等待 $\lceil N/2 \rceil + 1$ 个副本响应 |
| LOCAL_QUORUM | 同 DC 的 Quorum |
| EACH_QUORUM | 每个 DC 各 Quorum |
| ALL | 所有副本响应 |
| LOCAL_ONE | 同 DC 的 1 个 |
| ANY | 任意一个节点(包括 hinted) |

```sql
-- 写:强一致
INSERT INTO users (...) VALUES (...) USING CONSISTENCY QUORUM;

-- 读:强一致
SELECT * FROM users WHERE user_id = ? USING CONSISTENCY QUORUM;

-- 写:可用性优先
INSERT INTO events (...) VALUES (...) USING CONSISTENCY ONE;
```

### 1.6 反熵与修复

- **Read Repair**:读时发现不一致,异步修复旧副本
- **Hinted Handoff**:节点故障时暂存 hint,恢复后回传
- **Anti-Entropy Repair**:管理员手动触发 `nodetool repair`,用 Merkle Tree 对比并修复
- **Self-Healing**:定时 Anti-Entropy Repair(如每周)

### 1.7 多 DC 架构

Cassandra 原生支持多 DC:

```
DC1 (US East):
  Rack 1: [N1, N2]
  Rack 2: [N3, N4]

DC2 (EU West):
  Rack 1: [N5, N6]
  Rack 2: [N7, N8]

复制策略:
  NetworkTopologyStrategy:
    DC1: 3 副本
    DC2: 3 副本
```

客户端可指定 LOCAL_QUORUM(只在本 DC 内 Quorum),避免跨 DC 延迟。

### 1.8 适用场景

- 海量写(日志、监控、IoT)
- 跨 DC 多活
- 最终一致可接受
- 时间序列数据(Cassandra 在时序领域比 InfluxDB 更扩展)

不适用:

- 强一致多行事务
- 复杂 JOIN
- ad-hoc 查询(索引有限)

---

## 2. Redis:内存优先 + 单线程

### 2.1 定位

Redis (Salvatore Sanfilippo, 2009) 是内存优先的 KV 存储,设计哲学:

- **内存即数据库**:所有数据在内存,磁盘仅持久化
- **单线程**:命令串行执行,无锁
- **丰富数据结构**:String/List/Hash/Set/ZSet/Stream/Bitmap/HLL/Geo
- **主从复制 + 哨兵 + 集群**:高可用 + 横向扩展

### 2.2 单线程模型

Redis 6.0 之前,核心命令处理单线程:

```
[网络 IO] → [命令解析] → [命令执行] → [响应]
                ↑
        单线程,串行
```

为什么单线程快?

- 内存操作本身极快(< 1μs)
- 无锁、无上下文切换
- IO 多路复用 (epoll)
- 瓶颈在网络 IO 而非 CPU

Redis 6.0+ 引入多线程 IO(网络读写多线程,命令执行仍单线程):

```
[IO Thread 1] ─┐
[IO Thread 2] ─┼→ [命令执行(单线程)] ─┼→ [IO Thread 3] (回包)
[IO Thread N] ─┘                       │
                                      ↓
                              [IO Thread M]
```

### 2.3 数据结构

| 类型 | 用途 | 底层 |
|------|------|------|
| String | 缓存、计数 | SDS (简单动态字符串) |
| List | 队列、消息 | ziplist / quicklist |
| Hash | 对象 | ziplist / hashtable |
| Set | 集合 | intset / hashtable |
| ZSet | 排行榜 | ziplist / skiplist + hashtable |
| Stream | 消息流(5.0+) | radix tree |
| Bitmap | 位图 | String |
| HyperLogLog | 基数估算 | String |
| Geo | 地理位置 | ZSet |

### 2.4 持久化

#### 2.4.1 RDB (Redis Database)

- 全量快照,二进制压缩
- 触发:定时(bgsave)/ 手动 / 主从同步
- 优势:恢复快、文件小
- 劣势:数据丢失窗口(两次 RDB 之间的写)

#### 2.4.2 AOF (Append-Only File)

- 追加记录每个写命令
- 触发:always(每次写)/ everysec(默认,每秒)/ no(由 OS)
- 优势:数据丢失少(everysec 最多丢 1 秒)
- 劣势:文件大、恢复慢

#### 2.4.3 混合持久化 (4.0+)

- AOF 文件 = RDB 全量 + 增量 AOF
- 启动恢复快(先加载 RDB,再 replay AOF)

```redis
# redis.conf
save 900 1                # RDB: 900s 内 1 个修改触发
save 300 10
save 60 10000
appendonly yes            # AOF 开启
appendfsync everysec      # AOF 每秒刷盘
auto-aof-rewrite-percentage 100  # AOF 自动重写
```

### 2.5 主从复制

```
Master ──(全量+增量)──→ Slave1
       ──(全量+增量)──→ Slave2
```

- **全量同步**:Master 执行 BGSAVE 生成 RDB,发给 Slave;Slave 加载 RDB 后,Master 发送增量命令
- **增量同步**:Master 把写命令实时同步给 Slave(基于 offset)
- **PSYNC**:Slave 断线重连时,用 PSYNC 协议判断能否增量(基于 replication ID + offset)

### 2.6 哨兵 (Sentinel)

Sentinel 集群监控 Master/Slave,自动故障切换:

```
[Sentinel 1] [Sentinel 2] [Sentinel 3]
       ↓            ↓            ↓
   监控 Master / Slave 状态
       ↓
   Master 故障 → 选举 Leader Sentinel → 选新 Master → 通知客户端
```

详见 [[14-故障与容错]]。

### 2.7 Redis Cluster

#### 2.7.1 16384 Slot

详见 [[07-分片与路由]]。

- 集群有 16384 个 slot
- `slot = CRC16(key) mod 16384`
- 每节点负责一部分 slot

#### 2.7.2 Gossip

节点间通过 Gossip 协议同步集群状态:

- 节点角色(主/从)
- slot 分配
- 节点健康

#### 2.7.3 故障检测与切换

- 节点 PING/PONG 互检
- 标记 PFAIL(主观下线)
- Quorum 标记 FAIL(客观下线)
- 该 slot 的 Slave 升级为新 Master

### 2.8 适用场景

- 缓存(最常见)
- 会话存储
- 计数器(原子 incr)
- 排行榜(ZSet)
- 消息队列(Stream / List)
- 分布式锁(详见 [[11-分布式锁]])

不适用:

- 数据量超内存(成本极高)
- 强一致多行事务
- 复杂查询

---

## 3. MongoDB:文档 + Sharding

### 3.1 定位

MongoDB (2009) 是文档型数据库:

- **文档模型**:JSON/BSON,无 schema(灵活)
- **B-Tree 索引**:类似 RDBMS
- **水平扩展**:Sharding
- **聚合管道**:类 SQL 的复杂查询

### 3.2 数据模型

```javascript
// 插入文档(灵活 schema)
db.users.insertOne({
    _id: ObjectId("..."),
    name: "Alice",
    age: 30,
    address: {
        city: "Shanghai",
        zip: "200000"
    },
    tags: ["vip", "active"],
    orders: [
        { id: 1, amount: 100 },
        { id: 2, amount: 200 }
    ]
});

// 查询
db.users.find({ "address.city": "Shanghai", age: { $gte: 18 } })
        .sort({ age: -1 })
        .limit(10);

// 索引
db.users.createIndex({ "address.city": 1, age: -1 });
```

### 3.3 架构

```
                ┌──────────────────┐
                │  mongos (Router) │  ← 无状态,可水平扩展
                └────────┬─────────┘
                         ↓
                ┌──────────────────┐
                │  Config Server   │  ← 元数据(3 节点副本集)
                └────────┬─────────┘
                         ↓
       ┌─────────────────┼─────────────────┐
       ↓                 ↓                 ↓
  [Shard 1]         [Shard 2]         [Shard 3]
   Replica Set      Replica Set      Replica Set
   (Primary +       (Primary +       (Primary +
    2 Secondary)    2 Secondary)     2 Secondary)
```

#### 3.3.1 mongos

- 客户端入口,无状态
- 从 Config Server 拉路由表
- 转发请求到对应 Shard

#### 3.3.2 Config Server

- 元数据:Shard 列表、chunk 分布、数据库/集合配置
- 副本集部署(3 节点)

#### 3.3.3 Shard

- 实际数据节点
- 副本集部署(典型 1 Primary + 2 Secondary)
- Primary 读写,Secondary 同步

### 3.4 Sharding

#### 3.4.1 Shard Key

```javascript
// 启用分片
sh.enableSharding("mydb");
sh.shardCollection("mydb.users", { user_id: "hashed" });  // 哈希分片
sh.shardCollection("mydb.events", { timestamp: 1 });       // 范围分片
```

- **Range Sharding**:按 shard key 范围划分,支持范围查询,但易热点(单调 key)
- **Hash Sharding**:hash(shard key) 后范围划分,均匀但失去范围查询

#### 3.4.2 Chunk

- MongoDB 把数据切成 chunk(默认 64MB)
- chunk 是 shard 内的迁移单位
- chunk 数量自动平衡(balancer 周期性迁移)

### 3.5 索引

- **单字段索引**:`{ field: 1 }`
- **复合索引**:`{ a: 1, b: -1 }`
- **多键索引**:数组字段
- **文本索引**:全文搜索
- **地理索引**:2dsphere
- **TTL 索引**:自动过期文档

```javascript
db.events.createIndex({ createdAt: 1 }, { expireAfterSeconds: 86400 });
// 24 小时后自动删除
```

### 3.6 聚合管道

```javascript
db.orders.aggregate([
    { $match: { status: "paid" } },                  // 过滤
    { $group: { _id: "$user_id", total: { $sum: "$amount" } } },  // 分组聚合
    { $sort: { total: -1 } },                        // 排序
    { $limit: 10 }                                   // 限制
]);
```

聚合操作在 shard 上并行执行,mongos 合并结果。

### 3.7 事务

MongoDB 4.0+ 支持副本集事务,4.2+ 支持分片事务:

```javascript
const session = db.getMongo().startSession();
session.startTransaction();
try {
    db.accounts.updateOne({ _id: 1 }, { $inc: { balance: -100 } }, { session });
    db.accounts.updateOne({ _id: 2 }, { $inc: { balance: 100 } }, { session });
    session.commitTransaction();
} catch (e) {
    session.abortTransaction();
}
```

分片事务用 2PC,延迟较高,慎用。

### 3.8 适用场景

- 半结构化数据(JSON 灵活 schema)
- Web 应用、内容管理
- 物联网设备数据
- 实时分析(聚合管道)

不适用:

- 强关系数据(用 RDBMS)
- 极高并发写(用 Cassandra)
- 极低延迟(用 Redis)

---

## 4. 三者对比

| 维度 | Cassandra | Redis | MongoDB |
|------|-----------|-------|---------|
| 模型 | 宽列 | KV + 数据结构 | 文档 |
| 存储 | 磁盘 (LSM) | 内存 (主) + 磁盘 | 磁盘 (B-Tree) |
| 一致性 | 可调 (AP 倾向) | 强(单机) + 异步复制 | 强(副本集) + 最终(分片) |
| 事务 | 轻量级(batch) | 单命令原子 | 4.0+ 多文档 |
| 协议 | CQL | RESP | Wire (BSON) |
| 横向扩展 | 原生 | Cluster (16384 slot) | Sharding |
| 跨 DC | 原生 | 需要额外工具 | Zone Sharding |
| 性能 | 10w QPS/节点 | 100w QPS/节点 | 10w QPS/节点 |
| 数据量 | PB | GB-TB(内存限制) | TB |

---

## 5. 🎓 学术深度

### 5.1 Cassandra 的演化

- **0.x**:Facebook 内部用,基本 Dynamo 模型
- **1.x**:SQL-like CQL,Leveled Compaction
- **2.x**:Materialized View、User-Defined Type、SASI 索引
- **3.x**:存储引擎重写(CQL-native),性能 2x
- **4.x**:异步、Compaction 改进、Repaired State Tracking
- **5.x**:NASA 算法改进、tWCS 增强

### 5.2 Redis 的演化

- **2.x**:主从复制、AOF
- **3.x**:Cluster(16384 slot)
- **4.x**:模块系统、混合持久化
- **5.x**:Stream 数据类型
- **6.x**:多线程 IO、ACL、客户端缓存
- **7.x**:Function(替代 Lua)、sharded Pub/Sub

### 5.3 MongoDB 的演化

- **2.x**:Sharding 改进、聚合管道
- **3.x**:WiredTiger 存储引擎、副本集选举改进
- **4.x**:副本集事务
- **5.x**:分片事务、Time Series Collection
- **6.x**:Queryable Encryption、Cluster-to-Cluster Sync

### 5.4 CAP 视角

| 存储 | CAP 选型 |
|------|---------|
| Cassandra | AP (默认 ONE) / CP (ALL) |
| Redis | CP(单机强一致)/ AP(主从异步) |
| MongoDB | CP(副本集强一致)/ AP(分片最终一致) |

### 5.5 一致性 vs 可用性的工程权衡

- Cassandra 默认 AP(ONE),可调到 CP(ALL),但 ALL 失去可用性
- Redis 主从异步复制,故障切换可能丢数据,严格 CP 需要同步复制(WAIT 命令)
- MongoDB 副本集默认多数派写确认(w:majority),平衡一致性和可用性

---

## 6. 🏭 工业实战

### 6.1 选型决策树

```
数据量 + QPS:
  < 1TB + < 10w QPS → RDBMS / MongoDB
  1TB~10TB + 中等 QPS → MongoDB / Cassandra
  > 10TB + 高 QPS → Cassandra / HBase
  极低延迟 + 小数据 → Redis

数据模型:
  关系 → RDBMS
  文档 → MongoDB
  宽列/时序 → Cassandra / HBase
  KV → Redis

一致性:
  强一致 → RDBMS / MongoDB
  最终一致 → Cassandra
  缓存 → Redis
```

### 6.2 工业案例

- **Instagram**: Cassandra(用户图、feed)、Redis(缓存)
- **Netflix**: Cassandra(观看历史)、Redis(会话)、MySQL(账单)
- **Twitter**: Manhattan(自研 KV,类 Cassandra)
- **Apple**: Cassandra(数亿用户数据)
- **GitHub**: MySQL(主) + Redis(缓存) + Spokes(搜索)

### 6.3 Redis 生产陷阱

1. **Big Key**:单 key > 10MB,删除会阻塞主线程 → 拆分或异步删除(UNLINK)
2. **Hot Key**:单 key QPS 过高 → 多副本读、本地缓存
3. **缓存穿透**:查询不存在的 key → Bloom Filter / 空值缓存
4. **缓存击穿**:热 key 过期 → 互斥锁 / 永不过期 + 异步刷新
5. **缓存雪崩**:大量 key 同时过期 → 随机 TTL
6. **持久化阻塞**:RDB/AOF 重写期间 fork 慢 → 用 SSD、调大 fork 系数

### 6.4 MongoDB 生产陷阱

1. **Shard Key 选择**:不可变,选错难改 → 选高基数 + 均匀分布
2. **索引膨胀**:每个索引占空间 + 写放大 → 定期审查
3. **Chunk Migration**:迁移期间性能抖动 → 在低峰期平衡
4. **Working Set 超内存**:WiredTiger 缓存不够 → 加内存或分片

---

## 7. 面试要点

### 7.1 高频问答

**Q1: Cassandra 的数据模型?**

> Partition Key(决定分布)+ Clustering Key(决定分区内排序)。同一 Partition 内数据物理相邻,支持高效范围扫描。CQL 表面像 SQL,但语义不同(无 JOIN、有限事务)。

**Q2: Cassandra 和 HBase 的区别?**

> 一致性:Cassandra 默认 AP(可调),HBase 强一致(CP)。架构:Cassandra 去中心化(无 Master),HBase 中心化(HMaster + RegionServer)。复制:Cassandra 跨 DC 原生,HBase 单 DC(跨 DC 用 HBase Replication)。模型:Cassandra 宽列,HBase 稀疏有序映射。

**Q3: Redis 为什么单线程还快?**

> (1) 内存操作本身极快(<1μs);(2) 单线程无锁、无上下文切换;(3) IO 多路复用(epoll)高效;(4) 瓶颈在网络 IO 而非 CPU。Redis 6.0 引入多线程 IO(网络读写多线程,命令执行仍单线程)。

**Q4: Redis RDB 和 AOF 怎么选?**

> RDB:全量快照,恢复快、丢失多。AOF:增量日志,恢复慢、丢失少(everysec 最多丢 1 秒)。生产用混合持久化(4.0+):AOF = RDB + 增量,兼具两者优势。

**Q5: Redis Cluster 怎么分片?**

> 16384 个 slot,CRC16(key) mod 16384 决定 key 所属 slot。每节点负责一部分 slot。客户端缓存 slot → node 映射,变更时通过 MOVED/ASK 重定向。Hash Tag `{tag}` 让多 key 落同 slot 支持多键操作。

**Q6: MongoDB 的 Sharding 怎么工作?**

> 选 Shard Key 分片(范围或哈希)。数据切成 chunk(64MB),分布在 shard 上。Config Server 记录 chunk 分布,mongos 转发请求。Balancer 周期性平衡 chunk。Shard Key 不可变,选择关键(高基数 + 均匀)。

**Q7: MongoDB 事务支持到什么程度?**

> 4.0+ 支持副本集事务(单 shard 多文档),4.2+ 支持分片事务(跨 shard)。分片事务用 2PC,延迟较高。生产慎用大规模事务,优先用单文档原子操作。

**Q8: Cassandra、Redis、MongoDB 选型?**

> Cassandra:海量写 + 跨 DC + 最终一致(日志、IoT、时序);Redis:低延迟缓存 + 会话 + 计数(内存数据);MongoDB:文档模型 + 中等规模 + Web 应用(灵活 schema)。三者覆盖不同场景,常组合使用(如 Redis 缓存 + MongoDB 主存 + Cassandra 日志)。

### 7.2 易错点 ⚠️

1. **"Redis 是数据库"** — 严格说是内存 KV 存储,持久化是辅助。数据量超内存不适合。
2. **"Cassandra 强一致"** — 默认 AP(ONE)。强一致需 QUORUM/ALL,有性能代价。
3. **"MongoDB 无事务"** — 4.0+ 支持事务,只是早期版本不支持。
4. **"Redis Cluster 自动 failover"** — 是,但只针对 Master 故障。Slave 升级期间数据可能丢失(异步复制)。
5. **"MongoDB Sharding 解决一切"** — 不。Sharding 有开销(路由、跨 shard 查询),小数据用副本集即可。

---

## 8. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Lakshman & Malik, *Cassandra* | 2009 | Dynamo + Bigtable |
| Sanfilippo, *Redis* | 2009 | 内存 KV |
| Chodorow, *MongoDB: The Definitive Guide* | 2010 | 文档模型 |
| Carlson, *Redis in Action* | 2013 | Redis 工程实践 |

---

## 9. 交叉引用

- [[08-2-存储-Dynamo]]:Cassandra 的 Dynamo 血统
- [[08-3-存储-Bigtable与HBase]]:Cassandra 的 Bigtable 引擎
- [[07-分片与路由]]:Redis Cluster slot / MongoDB Sharding
- [[11-分布式锁]]:Redis 锁实现
- [[14-故障与容错]]:Redis Sentinel / MongoDB Replica Set 故障切换

---

## 10. TODO

- [ ] 补充 Cassandra Materialized View 实现细节
- [ ] 补充 Redis Stream 替代 MQ 的实践
- [ ] 增加 MongoDB Change Streams 应用场景
- [ ] 补充 Redis 7.0 sharded Pub/Sub 改进

---

## 11. 速查表 (Cheat Sheet)

```
Cassandra:
  Partition Key → 一致性哈希分布
  Clustering Key → 分区内排序
  Consistency Level: ONE / QUORUM / LOCAL_QUORUM / ALL
  Compaction: STCS / LCS / TWCS
  Gossip + vnode + 反熵 + Hinted Handoff

Redis:
  单线程 + IO 多路复用
  6.0+ 多线程 IO
  持久化: RDB (快) + AOF (全) + 混合 (推荐)
  Cluster: 16384 slot + Gossip
  Sentinel: 主从故障切换
  Big Key / Hot Key / 缓存三大问题(穿透/击穿/雪崩)

MongoDB:
  文档 (BSON) + 灵活 schema
  mongos + Config Server + Shard (Replica Set)
  Sharding: Range / Hash
  Chunk: 64MB,Balancer 自动平衡
  4.0+ 副本集事务, 4.2+ 分片事务
  聚合管道在 shard 上并行

选型:
  低延迟缓存 → Redis
  文档灵活 + Web → MongoDB
  海量写 + 跨 DC → Cassandra
```

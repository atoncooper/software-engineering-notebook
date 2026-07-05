# 存储系统 —— Bigtable 与 HBase (LSM-Tree)

> 章号: §8.3
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业
> 前置: [[08-1-存储-GFS与HDFS]] [[07-分片与路由]] [[10-协调服务]]

---

## 0. 为什么需要 Bigtable

GFS/HDFS 解决了大文件存储,但很多场景需要:

- 海量数据(10亿+ 行)的随机读写
- 高吞吐写入(日志、监控、爬虫数据)
- 稀疏数据(列多但每行只用少数列)
- 范围扫描(按 key 区间查询)

传统 RDBMS 不能扩展到这个规模。Google 2006 年发表 **Bigtable** 论文,提出"稀疏、分布式、持久化、有序映射"模型,核心创新是:

1. **LSM-Tree 存储**(替代 B-Tree,写入吞吐提升 10x+)
2. **行列稀疏模型**(适合半结构化数据)
3. **Region 分片**(自动分裂/合并,横向扩展)

HBase 是 Bigtable 的开源实现,运行在 HDFS 上,成为 Hadoop 生态的"数据库"。

本章分析 Bigtable/HBase 的数据模型、LSM-Tree 原理、架构、Compaction、Coprocessor,以及 NewSQL 演进。

---

## 1. 定义与动机

### 1.1 数据模型

> Bigtable = (row_key, column_family:column_qualifier, timestamp) → value

形式化:$\text{Bigtable}: \text{RowKey} \times \text{Column} \times \text{Timestamp} \to \text{Value}$

- **RowKey**:有序字符串,所有数据按 RowKey 字典序排列。范围扫描天然支持。
- **Column Family**:列族,创建表时定义,数量少(典型 < 10)。访问控制和存储压缩单位。
- **Column Qualifier**:列限定符,动态创建,数量可多。`family:qualifier` 形式。
- **Timestamp**:版本号,默认服务端时间戳,可指定。多版本数据。

```
表:WebPage
RowKey      ColumnFamily:contents      ColumnFamily:anchor
com.cnn.www  contents:t3 = "<html>..."   anchor:cnnsi.com = "CNN"
             contents:t2 = "<html>..."   anchor:my.look.ca = "CNN.com"
             contents:t1 = "<html>..."
```

### 1.2 与 RDBMS 的对比

| 维度 | RDBMS | Bigtable/HBase |
|------|-------|---------------|
| 模型 | 关系表 | 稀疏有序映射 |
| Schema | 严格 | 仅列族,列动态 |
| 事务 | ACID | 单行原子 |
| 索引 | B-Tree 多索引 | 仅 RowKey |
| Join | 支持 | 不支持(应用层) |
| SQL | 支持 | 不支持(过滤器 API) |
| 扩展性 | 难(分库分表) | 自动 Region 分裂 |
| 写吞吐 | 中(万 QPS) | 高(百万 QPS) |

### 1.3 适用场景

- 高吞吐写入(日志、监控、爬虫)
- 稀疏数据(用户画像、特征库)
- 范围扫描(按 user_id 区间查询)
- 多版本(数据历史)

不适用:

- 复杂 JOIN、聚合(SQL on Hadoop 替代)
- 强一致多行事务(NewSQL 替代)
- 小数据量(单机 RDBMS 即可)

---

## 2. 原理与架构

### 2.1 LSM-Tree (Log-Structured Merge-Tree)

> 📜 O'Neil et al., 1996 — *The Log-Structured Merge-Tree*

LSM-Tree 是 Bigtable/HBase/Cassandra/RocksDB/LevelDB 的存储引擎核心。

#### 2.1.1 与 B-Tree 的对比

| 维度 | B-Tree | LSM-Tree |
|------|--------|----------|
| 写入 | 就地更新(随机写) | 追加(顺序写) |
| 读 | $O(\log N)$,1 次 IO | $O(\log N)$,可能多次 IO |
| 写吞吐 | 中 | 高(10x+) |
| 空间放大 | 1x(原地) | >1x(多版本 + tombstone) |
| 读放大 | 1x | >1x(多 Level 查找) |
| 适合 | 读多写少 | 写多读少 |

#### 2.1.2 LSM-Tree 结构

```
                  MemTable (内存,跳表/红黑树)
                      ↓ (满了 flush)
                  SSTable 0 (Level 0,磁盘)
                      ↓ (Compaction)
                  SSTable 1, 2, ... (Level 0)
                      ↓
                  SSTable (Level 1, 排序合并)
                      ↓
                  SSTable (Level 2, ...)
                      ↓
                  ... (Level N,容量指数增长)
```

#### 2.1.3 写路径

```
1. 写 WAL(预写日志,顺序写磁盘,保证持久性)
2. 写 MemTable(内存)
3. 返回成功(无需等待 flush)

MemTable 满(默认 128MB):
4. flush 成 Level 0 SSTable(顺序写磁盘)
5. 异步 Compaction 合并到更高 Level
```

#### 2.1.4 读路径

```
1. 查 MemTable(最新数据)
2. 查 Level 0 SSTable(可能有多个,需全部查)
3. 查 Level 1, 2, ... SSTable(每 Level 至多 1 个,二分查找)
4. 合并结果,返回最新版本

Bloom Filter 加速:每个 SSTable 带 Bloom Filter,先查 Bloom 排除不存在的 key
```

#### 2.1.5 Compaction

Level 0 SSTable 之间 key 范围重叠,读时需全部查。Compaction 把多个 SSTable 合并成更大的、key 范围不重叠的 SSTable,提升读性能。

**Size-Tiered Compaction**(Cassandra 默认):

- 同等大小 SSTable 合并
- 写放大低,读放大高(同 Level 多 SSTable)
- 适合写多场景

**Leveled Compaction**(LevelDB/RocksDB 默认):

- Level $i+1$ 容量是 Level $i$ 的 10 倍
- Level $i+1$ 内 SSTable 间 key 不重叠
- 读放大低,写放大高
- 适合读多场景

**Time-Windowed Compaction**(Cassandra 时序):

- 按时间窗口分 SSTable
- 老窗口不再合并
- 适合时序数据

```java
// HBase Compaction 简化流程
public class CompactionManager {
    public void compact(Region region, boolean major) {
        List<HFile> files = region.getStoreFiles();
        if (major) {
            // Major Compaction: 全部 HFile 合并成一个,清理 tombstone
            HFile merged = mergeAll(files);
            region.replaceFiles(files, merged);
        } else {
            // Minor Compaction: 选若干小 HFile 合并
            List<HFile> smallFiles = selectSmall(files);
            HFile merged = merge(smallFiles);
            region.replaceFiles(smallFiles, merged);
        }
    }
}
```

### 2.2 HBase 架构

```
                ┌──────────────┐
                │   Client     │
                └──────┬───────┘
                       │
        ┌──────────────┼──────────────┐
        ↓              ↓              ↓
  ┌──────────┐  ┌──────────┐   ┌──────────┐
  │HMaster   │  │RegionServer│   │RegionServer│
  │(元数据)   │  │  (RS1)   │   │  (RS2)   │
  └────┬─────┘  └────┬─────┘   └────┬─────┘
       │              │              │
       │      Region  Region         │
       │      ↓       ↓              │
       │      ┌─────────────┐        │
       │      │  HFile       │←──────┘
       │      │  (HDFS)      │
       │      └─────────────┘
       │
       ↓
  ┌──────────┐
  │ ZooKeeper│ (Master 选举、RegionServer 注册、Meta 路由)
  └──────────┘
```

#### 2.2.1 HMaster

- 管理 Region 分配(把 Region 分给哪个 RS)
- 负载均衡(Region 在 RS 间迁移)
- DDL 操作(建表、改 schema)
- 故障恢复(RS 挂了,把它的 Region 重新分配)
- 不参与数据读写(避免单点)

#### 2.2.2 RegionServer

- 实际承载 Region(每个 RS 典型 100~1000 Region)
- 处理客户端读写
- MemTable + WAL + HFile 管理
- Compaction、Split 触发

#### 2.2.3 Region

- 表的水平分片,按 RowKey 范围 $[start, end)$ 划分
- 默认 10GB,达到阈值自动 Split 成两个
- 每个 Region 在一个 RS 上(主),可选其他 RS 上(副本,实验特性)

#### 2.2.4 HFile

- HBase 的 SSTable 格式,存在 HDFS
- 内部按 RowKey 排序,带索引和 Bloom Filter
- 不可变(写完后只能 Compaction 合并)

### 2.3 读写路径

#### 2.3.1 Meta 路由

HBase 三层路由:

```
1. ZK → /hbase/meta-region-server → 找到 Meta Region 所在 RS
2. Meta Region 表 (hbase:meta) → 找到 user Region 所在 RS
3. 直接访问 user Region
```

客户端缓存 Meta 信息(默认 30s),减少 ZK/Meta 访问。

#### 2.3.2 写路径

```
1. Client → RegionServer: put(rowkey, family:col, value)
2. RS 检查 WAL 是否需要 roll(避免单 WAL 过大)
3. 写 WAL(HDFS,顺序追加,3 副本)
4. 写 MemTable(内存)
5. ACK 客户端

MemTable 满:
6. flush 成 HFile(Level 0),清空 MemTable
7. 异步触发 Minor Compaction(合并小 HFile)
```

#### 2.3.3 读路径

```
1. Client → RegionServer: get(rowkey)
2. 查 BlockCache (LRU 缓存,内存)
3. 查 MemTable (最新未 flush 的)
4. 查 HFile (按时间戳从新到旧)
   - 用 Bloom Filter 过滤
   - 用 Block Index 二分查找
5. 合并所有结果,返回最新版本
```

#### 2.3.4 HBase 写放大

每次写:
- WAL:1 次写(3 副本 = 3 次网络)
- MemTable:1 次内存写
- flush:1 次磁盘写(HDFS 3 副本)
- Compaction:多次重写(Level N 的数据被 Compaction 多次)

实测写放大:5~10x(典型 LSM-Tree 的代价)。

### 2.4 Region Split / Merge

#### 2.4.1 Split

Region 太大(默认 10GB)自动 Split:

```
Region [a, z) (12GB)
   ↓ Split
Region [a, m) (6GB) + Region [m, z) (6GB)
```

Split 点由 policy 决定:

- `ConstantSizeRegionSplitPolicy`:固定大小(默认,旧)
- `IncreasingToUpperBoundRegionSplitPolicy`:按 Region 数动态(默认,新)
- `KeyPrefixRegionSplitPolicy`:按 key 前缀
- `DelimiterRegionsplitPolicy`:按分隔符

Split 流程:

1. RegionServer 触发 Split
2. 创建两个新 Region(offline 状态)
3. 把原 Region 的 HFile 拆分(实际是引用,不复制数据)
4. 两个新 Region 上线,原 Region 下线
5. 更新 Meta
6. HMaster 调度 Compaction 把引用 HFile 实化为独立 HFile

#### 2.4.2 Merge

两个相邻小 Region 合并:

```
Region [a, m) (1GB) + Region [m, z) (1GB)
   ↓ Merge
Region [a, z) (2GB)
```

用于:Region 数过多、Region 太小(写入分散)。

### 2.5 Coprocessor (协处理器)

HBase 0.92+ 引入,类似 RDBMS 触发器/存储过程:

#### 2.5.1 Observer

钩子在 RegionServer 的操作上:

- `RegionObserver`:preGet/postGet、prePut/postPut、preScan/postScan
- `MasterObserver`:preCreateTable/postCreateTable
- `WALObserver`:preWALWrite/postWALWrite

```java
public class AuditObserver extends BaseRegionObserver {
    @Override
    public void prePut(ObserverContext<RegionCoprocessorEnvironment> e,
                       Put put, WALEdit edit, Durability durability) {
        // 在 put 前记录审计日志
        auditLog.write(put.getRow());
    }
}
```

#### 2.5.2 Endpoint

类似 RPC,在 RegionServer 上执行计算:

```java
public class SumEndpoint extends BaseEndpointImplementation {
    public long sum(byte[] family, byte[] qualifier) {
        long total = 0;
        InternalScanner scanner = env.getRegion().getScanner(...);
        // 在 RS 本地扫描,聚合后返回
        return total;
    }
}

// 客户端:并行调用所有 Region,汇总结果
long total = table.coprocessorService(SumService.class)
    .execSum(family, qualifier);
```

Coprocessor 把计算下推到数据所在节点,减少网络传输。HBase Phoenix (SQL on HBase) 大量用 Endpoint 实现聚合。

---

## 3. 🎓 学术深度

### 3.1 Bigtable 论文核心贡献

> 📜 Chang et al., 2006 OSDI — *Bigtable: A Distributed Storage System for Structured Data*

1. **稀疏有序映射**:打破 RDBMS 的关系模型,简化分布式
2. **LSM-Tree 工业化**:把学术 LSM-Tree 推到 PB 级
3. **SSTable 格式**:不可变、有序、带索引的数据块
4. **Chubby + Tablet**:中心化元数据 + 动态分片

### 3.2 LSM-Tree 的写放大分析

> 📜 Andersen et al., 2009 — *BitRot*

LSM-Tree Level $i$ 容量 $L_i = L_0 \cdot T^i$(T 是 size ratio,典型 10)。

写放大 = 一条数据被重写的次数。Leveled Compaction:

$$\text{WA} \approx \sum_{i=0}^{N} 1 = N = \log_T (L_{\max} / L_0)$$

典型值:$T=10$,$N=4$,WA $\approx 4 \sim 10$。B-Tree 的 WA $\approx 1$(原地更新),但随机写吞吐低。

> 🎓 学术权衡:LSM-Tree 用"写放大 + 读放大"换"写吞吐",在写多场景显著优于 B-Tree。

### 3.3 SSTable 的不可变性

SSTable 一旦写入不可修改,带来:

- ✓ 缓存友好(无锁,直接 mmap)
- ✓ 并发读简单(无 MVCC 复杂性)
- ✓ Compaction 简单(合并多文件,无冲突)
- ✗ 删除靠 tombstone(增加空间和读放大)
- ✗ 更新靠追加新版本(空间膨胀)

### 3.4 Bloom Filter 在 LSM-Tree 中的作用

每个 SSTable 带 Bloom Filter(典型 10 bits/key,1% 误判率):

- 读 key 时先查 Bloom,排除"肯定不存在"
- 减少不必要的 SSTable 扫描
- Level 0 多文件 + Bloom = 接近"全文件查找"

```python
class BloomFilter:
    def __init__(self, n, p=0.01):
        self.m = int(-n * math.log(p) / (math.log(2) ** 2))  # bits
        self.k = int(self.m / n * math.log(2))                # hash functions
        self.bits = [0] * self.m

    def add(self, key):
        for i in range(self.k):
            self.bits[self.hash(key, i) % self.m] = 1

    def contains(self, key):
        return all(self.bits[self.hash(key, i) % self.m] for i in range(self.k))
```

### 3.5 Bigtable → Spanner/NewSQL 演进

Bigtable 缺陷:

- 仅单行事务
- 无强一致多副本(只依赖 GFS 复制)
- 无 SQL

Google 演进:

- **Megastore**:在 Bigtable 上加跨行事务(实体组)
- **Spanner**:用 Paxos Group + TrueTime 替代 GFS,全球分布
- **F1**:在 Spanner 上加 SQL

详见 [[08-4-存储-Spanner与NewSQL]]。

### 3.6 HBase vs Bigtable 的工程差异

| 维度 | Bigtable | HBase |
|------|----------|-------|
| 底层存储 | GFS / Colossus | HDFS |
| 元数据 | Chubby | ZooKeeper |
| Tablet | Master 调度 | RegionServer + HMaster |
| Compaction | 自定义 | Size-tiered / Leveled |
| 事务 | 单行 | 单行 + batch(0.94+) |
| 协处理器 | 无 | Coprocessor |

---

## 4. 🏭 工业实战

### 4.1 HBase 调优关键参数

| 参数 | 默认 | 含义 | 调优建议 |
|------|------|------|---------|
| `hbase.hregion.max.filesize` | 10GB | Region Split 阈值 | 写多场景调小(5GB) |
| `hbase.hregion.memstore.flush.size` | 128MB | MemTable flush 阈值 | 内存大可调大 |
| `hbase.hstore.compactionThreshold` | 3 | Minor Compaction 触发文件数 | 写多调大 |
| `hbase.hstore.blockingStoreFiles` | 16 | 阻塞写时的文件数 | 调大防阻塞 |
| `hbase.regionserver.global.memstore.size` | 0.4 | RegionServer MemTable 总内存占比 | 内存大调大 |
| `hfile.block.cache.size` | 0.4 | BlockCache 占比 | 读多调大 |

### 4.2 RowKey 设计原则

RowKey 决定数据分布和查询性能:

1. **散列性**:避免热点(自增 key 会导致写热点)
2. **范围性**:支持范围扫描(时间序列用 `userId_reverse + timestamp`)
3. **长度**:短(典型 < 50 字节,避免元数据膨胀)
4. **类型**:字节(避免 String 编码开销)

热点处理(详见 [[07-分片与路由]]):

```java
// 反转 key:把"顺序写"打散
String rowkey = new StringBuilder(userId).reverse().toString()
    + "|" + Long.toString(Long.MAX_VALUE - timestamp);

// Salt:加随机前缀
byte[] rowkey = Bytes.add(
    Bytes.toBytes(Math.abs(userId.hashCode() % 16)),
    Bytes.toBytes("|"),
    Bytes.toBytes(userId),
    Bytes.toBytes("|"),
    Bytes.toBytes(timestamp)
);

// Hash + Pre-Split
admin.createTable(desc, getHexSplits(0, 16));  // 16 个预分裂 Region
```

### 4.3 HBase 二级索引

HBase 原生只有 RowKey 索引,二级索引需自建:

| 方案 | 实现 | 一致性 |
|------|------|-------|
| **MapReduce 同步** | 定时 MR 把数据写入索引表 | 最终 |
| **Coprocessor 同步** | Observer 在 Put 时同步写索引表 | 强(同事务) |
| **Phoenix** | SQL on HBase,自动维护二级索引 | 强 |
| **ElasticSearch** | 外部 ES 做索引 | 最终 |
| **Lily HBase Indexer** | WAL 订阅 + 异步索引 | 最终 |

```java
// Coprocessor 实现二级索引
public class IndexObserver extends BaseRegionObserver {
    @Override
    public void postPut(ObserverContext<RegionCoprocessorEnvironment> e,
                        Put put, WALEdit edit, Durability durability) {
        // 主表 put 完成后,同步写索引表
        byte[] value = put.getValue(CF, COL);
        Put indexPut = new Put(value);  // 索引表的 rowkey = 原 value
        indexPut.addColumn(IDX_CF, IDX_COL, put.getRow());  // 索引表 value = 原 rowkey
        indexTable.put(indexPut);
    }
}
```

### 4.4 HBase + Phoenix (SQL)

Phoenix 是 SQL on HBase 引擎,把 SQL 转为 HBase API:

```sql
-- Phoenix DDL
CREATE TABLE users (
    user_id BIGINT PRIMARY KEY,
    name VARCHAR,
    age INTEGER,
    email VARCHAR
);

-- 二级索引
CREATE INDEX idx_email ON users(email);

-- 查询
SELECT * FROM users WHERE email = 'a@b.com';  -- 走 idx_email
```

Phoenix 内部用 Coprocessor 维护索引,支持 JOIN、聚合、UDF。

### 4.5 HBase 工业案例

- **淘宝/支付宝**:历史订单、消息中心、用户画像
- **Facebook Messages**:用户消息存储(已迁移到 Cassandra)
- **Netflix**:观看历史、推荐特征
- **Apple**: iCloud 部分数据

### 4.6 HBase 读写性能数据(参考)

- 单 RegionServer 写 QPS:5w~20w(取决于 rowkey 散列)
- 单 RegionServer 读 QPS:10w~50w(BlockCache 命中)
- 范围扫描:1w~5w 行/s
- 写延迟:P99 < 50ms
- 读延迟:P99 < 20ms(BlockCache 命中)/ < 100ms(磁盘)

---

## 5. 面试要点

### 5.1 高频问答

**Q1: LSM-Tree 和 B-Tree 的区别?**

> B-Tree 就地更新(随机写),读快写慢;LSM-Tree 追加写(顺序写),写快读慢。LSM-Tree 写吞吐是 B-Tree 的 10x+,但需 Compaction 和 Bloom Filter 优化读。LSM-Tree 适合写多读少(日志、监控),B-Tree 适合读多写少(OLTP)。

**Q2: HBase 写数据的流程?**

> (1) 写 WAL(HDFS,3 副本,顺序追加,保证持久性);(2) 写 MemTable(内存);(3) ACK 客户端。MemTable 满后异步 flush 成 HFile(磁盘)。HFile 多了触发 Compaction 合并。读时按 MemTable → BlockCache → HFile(Level 0 → Level N)顺序查。

**Q3: HBase 为什么快?**

> 写快:顺序写 WAL + MemTable(内存)。读快:BlockCache 缓存 + Bloom Filter 过滤 + Block Index 二分。海量数据:Region 分片 + HDFS 多节点并行。

**Q4: HBase Region Split 的过程?**

> Region 达到阈值(10GB)触发 Split:创建两个新 Region(offline)→ 拆分原 HFile(实际是引用,不复制)→ 新 Region 上线、原 Region 下线 → 更新 Meta → HMaster 调度 Compaction 实化 HFile。Split 期间 Region 短暂不可用(几秒)。

**Q5: RowKey 设计的原则?**

> (1) 散列性:避免热点,可加 salt 或 hash 前缀;(2) 范围性:支持范围扫描,如 `userId + timestamp`;(3) 短:减少元数据膨胀;(4) 类型一致:避免 String/byte 转换开销。热点是大敌,自增 RowKey 会导致所有写打到最后一个 Region。

**Q6: HBase 怎么做二级索引?**

> HBase 原生只有 RowKey 索引。二级索引方案:(1) Coprocessor 在 Put 时同步写索引表(强一致);(2) MapReduce 定时同步(最终一致);(3) Phoenix 自动维护索引;(4) 外部 ElasticSearch。Coprocessor 最常用,代价是写放大 2x。

**Q7: Compaction 的策略有哪些?**

> (1) Size-Tiered(同大小合并):写放大低,读放大高,适合写多;(2) Leveled(分层合并,Level i+1 是 Level i 的 10 倍):读放大低,写放大高,适合读多;(3) Time-Windowed(时序):按时间窗口分,老窗口不合并,适合时序数据。HBase 默认类似 Size-Tiered。

**Q8: HBase 和 RDBMS 的选型?**

> HBase:海量数据(PB)+ 高吞吐写 + 稀疏数据 + 范围扫描 + 单行事务可接受。RDBMS:复杂 SQL + JOIN + 多行事务 + 中等数据量。混合用:RDBMS 存核心交易,HBase 存历史数据/画像/日志。

### 5.2 易错点 ⚠️

1. **"HBase 支持事务"** — 仅单行原子。多行事务需 Phoenix 或外部协调。
2. **"HBase 删除立即生效"** — 不。删除写 tombstone,Compaction 时才真正清理。
3. **"HBase 适合小数据量"** — 错。Region/RS 启动开销大,小数据用 RDBMS 或 Redis 即可。
4. **"MemTable 越大越好"** — 不一定。太大 flush 时长,故障恢复慢。
5. **"HBase 不需要 RowKey 设计"** — 错。RowKey 决定性能,设计不当会热点。

---

## 6. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Chang et al., *Bigtable* | 2006 OSDI | LSM-Tree 工业化、稀疏有序映射 |
| O'Neil et al., *The LSM-Tree* | 1996 | LSM-Tree 原始论文 |
| George et al., *HBase Architecture* | 2008 | Bigtable 开源实现 |
| Dong et al., *RocksDB* | 2017 | 嵌入式 LSM 引擎 |
| Apache Phoenix | — | SQL on HBase |

---

## 7. 交叉引用

- [[08-1-存储-GFS与HDFS]]:HBase 底层存储
- [[08-2-存储-Dynamo]]:对比 Dynamo 的 AP 模型
- [[08-4-存储-Spanner与NewSQL]]:Bigtable → Spanner 演进
- [[07-分片与路由]]:Region Split 与热点
- [[10-协调服务]]:ZK 在 HBase 中的作用
- [[14-故障与容错]]:HMaster HA / RS 故障切换

---

## 8. TODO

- [ ] 补充 HBase MOB(Medium Object)存储大对象机制
- [ ] 补充 HBase Async API(异步客户端)
- [ ] 增加 rocksdb/LevelDB 对比
- [ ] 补充 HBase Region Replica(读副本)实现

---

## 9. 速查表 (Cheat Sheet)

```
HBase 核心数字:
  Region size: 默认 10GB
  MemTable flush: 128MB
  BlockCache: 0.4 * Heap
  WAL roll: 60s 或 64MB
  Default replicas: 3 (HDFS)

LSM-Tree 层级:
  MemTable (内存) → Level 0 (磁盘,重叠) → Level 1 (10x) → Level 2 (10x) → ...

Compaction:
  Size-Tiered: 同大小合并 (Cassandra)
  Leveled:     分层 (RocksDB)
  TWCS:        时序 (Cassandra)

写路径: WAL + MemTable → flush → HFile → Compaction
读路径: BlockCache → MemTable → HFile (Bloom + Index)

HBase vs RDBMS:
  海量 + 高吞吐写 + 稀疏 + 范围 + 单行事务 → HBase
  复杂 SQL + 多行事务 + 中等数据 → RDBMS
```

# 存储系统 —— GFS 与 HDFS

> 章号: §8.1
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业
> 前置: [[04-复制]] [[07-分片与路由]] [[14-故障与容错]]

---

## 0. 为什么需要分布式文件系统

单机文件系统(ext4、XFS、NTFS)受限于:

- 单磁盘容量(典型 < 20TB)
- 单机 IO 带宽(典型 < 1GB/s)
- 单点故障(磁盘坏、机器挂)

大数据场景(AI 训练、日志归档、数据仓库)需要:

- PB 级容量(单机不可能)
- 多 GB/s 聚合带宽(单机不够)
- 容错(几百节点挂几个不影响)

Google 2003 年发表 **GFS (Google File System)**,首次系统化解决"廉价服务器集群上的大文件存储"。Hadoop 2006 年开源实现 **HDFS (Hadoop Distributed File System)**,基本是 GFS 的开源克隆,成为大数据生态基石。

本章分析 GFS/HDFS 的设计哲学、架构、读写路径、容错机制,以及学术上的反思与工业演进。

---

## 1. 定义与动机

### 1.1 设计假设 (GFS 论文)

> 📜 Ghemawat, Gobioff, Leung, 2003 SOSP — *The Google File System*

GFS 论文开篇列出的工作负载假设:

1. **组件故障是常态**:廉价服务器集群,节点随时挂。系统必须持续监控 + 自动恢复。
2. **文件巨大**:GB 级是常态,TB 级不稀奇。小文件不是优化目标。
3. **追加为主,随机写少**:文件写一次,读多次(append-only)。支持追加,不支持任意位置修改。
4. **顺序读为主**:大文件批量读取,关心吞吐而非 latency。
5. **客户端语义**:应用知道文件格式,可以容忍不一致(如重读、checkpoint)。

### 1.2 GFS 与传统 FS 的根本差异

| 维度 | 传统 FS | GFS/HDFS |
|------|---------|----------|
| 文件大小 | KB ~ GB | GB ~ TB |
| 访问模式 | 随机读写 | 顺序追加 + 批量读 |
| 一致性 | 强一致 | "一致 + 已定义" 弱一致 |
| 元数据 | 单机 inode | 中心化 Master |
| 副本 | RAID | 多机复制(默认 3) |
| 故障 | 磁盘坏 | 节点挂 |
| POSIX | 兼容 | 不兼容 |

### 1.3 核心抽象

> 文件 = 有序的 **Chunk** 列表,每个 Chunk 固定大小(GFS: 64MB;HDFS: 128MB 默认,可配 256MB),每个 Chunk 有 3 个副本分布在不同节点。

```
File: /logs/2024/access.log
  ↓
  Chunk 0: [ChunkID=abc, size=64MB, replicas=[N1, N2, N3]]
  Chunk 1: [ChunkID=def, size=64MB, replicas=[N2, N3, N4]]
  Chunk 2: [ChunkID=ghi, size=30MB, replicas=[N3, N4, N5]]
```

为什么 Chunk 这么大?

- 减少 Master 元数据量(64MB chunk → 一个 64 字节元数据条目,1PB 文件只需 ~16MB 元数据)
- 减少 client-master 交互(一次拿到 chunk 列表后顺序读)
- 提高顺序吞吐(TCP 长连接有效利用)

---

## 2. 原理与架构

### 2.1 GFS 架构

```
                 ┌──────────────────┐
                 │  Single Master   │
                 │  (元数据)         │
                 └────────┬─────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ↓                 ↓                 ↓
  ┌─────────┐       ┌─────────┐       ┌─────────┐
  │ChunkServer│     │ChunkServer│     │ChunkServer│
  │  (N1)    │     │  (N2)    │     │  (N3)    │
  └─────────┘       └─────────┘       └─────────┘
        ↑                 ↑                 ↑
        └─────────────────┼─────────────────┘
                          │
                     ┌────┴────┐
                     │ Client  │
                     └─────────┘
```

#### 2.1.1 Master 职责

- 文件命名空间(目录树 + 文件)
- 文件 → Chunk ID 映射
- Chunk ID → ChunkServer 列表映射
- Chunk 租约管理(谁是 Primary)
- 垃圾回收(孤儿 chunk)
- 副本复制决策(节点负载、磁盘空间)

#### 2.1.2 ChunkServer 职责

- 实际存储 chunk 数据(Linux 文件,如 `/gfs/chunk_abc`)
- 响应 client 读写请求
- 周期性心跳上报 chunk 列表

#### 2.1.3 Client 职责

- 缓存 chunk 位置(短期,如 60s)
- 直接与 ChunkServer 交互(数据流不经过 Master)

### 2.2 读写路径

#### 2.2.1 读路径

```
1. Client 把 (filename, offset) 转 (filename, chunk_index)
2. Client → Master: GetChunks(filename, chunk_index)
3. Master 返回 chunk_id + replica 列表 [N1, N2, N3]
4. Client 缓存,直接连 N1(或就近节点)
5. N1 返回 chunk 数据
```

> 🎓 关键设计:数据流不经过 Master,Master 只处理元数据(几十 KB),支持百万级文件。

#### 2.2.2 写路径 (Record Append)

GFS 的"原子追加"是核心创新:

```
1. Client 询问 Master: chunk_id + primary + secondary 副本
2. Master 返回 [Primary=N1, Secondaries=[N2, N3]]
3. Client 把数据 push 到所有副本(流水线:N1→N2→N3)
4. 数据落盘后,Client 发"append"请求给 Primary
5. Primary 决定 offset(本地记录顺序),写入本地
6. Primary 转发"append at offset X"给 Secondaries
7. 所有副本 ACK → Primary ACK 给 Client → 成功
   任一失败 → Primary 通知 Client 重试(可能造成重复)
```

**GFS 的弱一致语义**:

- **一致 (Consistent)**:所有客户端看到相同数据(可能不是最新)
- **已定义 (Defined)**:一致 + 客户端能看到追加的内容(无重复)

正常情况下追加是"已定义"的;失败重试时可能产生"一致但未定义"的重复数据,客户端需自行处理(用 checksum、唯一 ID 去重)。

### 2.3 HDFS 架构

HDFS 是 GFS 的开源实现,基本一致,术语略变:

| GFS | HDFS |
|-----|------|
| Master | NameNode |
| ChunkServer | DataNode |
| Chunk (64MB) | Block (128MB 默认) |
| Chunk ID | Block ID |
| Primary | 不是用租约,而是 NameNode 直接协调 |

#### 2.3.1 NameNode

- 内存中维护文件系统元数据(目录树 + 文件 → block 列表)
- EditLog(预写日志)+ FsImage(快照)持久化
- 接收 DataNode 心跳 + Block Report

#### 2.3.2 DataNode

- 实际存储 block(本地 Linux 文件,典型 `/data/hdfs/dn/current/BP_xx`)
- 周期性心跳(默认 3s)上报状态
- 周期性 Block Report(默认 6h)上报 block 列表
- 接收 client 读写请求

#### 2.3.3 HDFS 写路径

```
1. Client → NameNode: create file
2. NameNode 检查权限、命名空间,生成文件元数据
3. Client 写数据到内部缓冲(64KB packet)
4. Client 询问 NameNode: addBlock(filename)
5. NameNode 选 3 个 DataNode(机架感知),返回 pipeline [DN1, DN2, DN3]
6. Client 建立 pipeline:Client → DN1 → DN2 → DN3
7. 数据流式传输,每个 packet ACK 链路返回
8. Block 写满(128MB)或文件 close:Client 通知 NameNode
```

```java
// HDFS 客户端写文件(简化)
Configuration conf = new Configuration();
FileSystem fs = FileSystem.get(URI.create("hdfs://namenode:8020"), conf);
try (FSDataOutputStream out = fs.create(new Path("/data/log.txt"),
        (short) 3,  // 3 副本
        128 * 1024 * 1024,  // buffer 128MB
        (progress) -> System.out.println("Writing..."))) {
    out.write("hello".getBytes());
}
```

### 2.4 副本放置策略

HDFS 默认 3 副本放置(机架感知):

```
副本 1: 本机架的某节点(本地优先,减少跨机架带宽)
副本 2: 不同机架的某节点(机架容错)
副本 3: 与副本 2 同机架的另一节点(减少跨机架带宽)
```

```
Rack 1: [DN1, DN2, DN3]
Rack 2: [DN4, DN5, DN6]

Client 写 block-1:
  副本 1 → DN1 (Rack 1)
  副本 2 → DN4 (Rack 2)
  副本 3 → DN5 (Rack 2)
```

> 🎓 这一策略平衡了"机架容错"和"读带宽":副本 2 和 3 在同机架,读时本机架有 2 个副本可用,减少跨机架流量;但 Rack 1 整挂,仍有 Rack 2 的副本。

### 2.5 容错与恢复

#### 2.5.1 DataNode 故障

- DataNode 60s 内未心跳 → NameNode 标记为 dead
- 该节点所有 block 标记为"副本不足"
- NameNode 调度其他 DataNode 复制这些 block,恢复到 3 副本

#### 2.5.2 Block 损坏

- DataNode 周期性校验 block 的 MD5/CRC32
- 发现不匹配 → 通知 NameNode → 标记 block 损坏 → 从其他副本复制

#### 2.5.3 NameNode 故障

- **单点风险**:NameNode 是 HDFS 的单点
- 解决方案:
  - **HA (High Availability)**:Active + Standby NameNode,共享 EditLog(JournalNode 集群)
  - **Federation**:多个 NameNode 各管一部分命名空间(扩展元数据)
  - **Backup Node**:实时同步元数据的备用节点

```
HA 架构:
  Active NameNode ←──EditLog──→ Standby NameNode
       ↓                              ↑
  JournalNode 集群 (3/5 节点, Quorum)
       ↓
  DataNodes 同时上报到 Active 和 Standby
```

#### 2.5.4 小文件问题

HDFS 的元数据全在 NameNode 内存,每个文件 ~150 字节:

- 1 亿个小文件 = 15GB 内存,NameNode 持续压力
- 解决:HAR (Hadoop Archive)、Sequence File、HDFS Federation

---

## 3. 🎓 学术深度

### 3.1 GFS 的弱一致性争议

GFS 论文承认"一致但未定义"的存在,但 Google 内部应用容忍(应用自己用唯一 ID 去重)。学术批评:

- **未定义语义难以推理**:开发者需要知道何时可能重复,增加心智负担
- **跨语言/平台移植困难**:其他文件系统假设强一致,移植应用需重写

后续演进:

- **Colossus (GFS 二代)**:Google 内部替换 GFS,强化一致性,支持小文件
- **HDFS 强一致追加**:HDFS 2.x 后的 `append` 操作提供更强保证

### 3.2 Chunk 大小的权衡

> 🎓 GFS 论文 Section 4: 64MB chunk 是多次权衡的结果

| 大 | 小 |
|----|----|
| ✓ 减少 Master 元数据 | ✗ 小文件浪费空间 |
| ✓ 减少 client-master 交互 | ✗ 随机访问粒度粗 |
| ✓ 提高 TCP 吞吐 | ✗ 客户端缓存压力大 |
| ✗ 小文件少,chunk 多 → Master 压力大 | |

GFS 论文承认"小文件场景未优化":如果一个文件只有几 KB,仍占一个 chunk,且 Master 元数据压力相对更大。

### 3.3 单 Master 的瓶颈与解决

GFS 原始设计是单 Master,论文论证"单 Master 不是瓶颈":

- 元数据小(64B/chunk → 1PB 文件只需 16MB)
- 数据流不经过 Master(只走控制流)
- Client 缓存 chunk 位置 60s

但实际生产中,单 Master 确实成为瓶颈:

- 文件数大时元数据压力
- Master 故障期间集群不可用

演进:

- **GFS → Colossus**:分布式 Master 元数据
- **HDFS → Federation**:多个 NameNode
- **CephFS**:分布式 Metadata Server 集群

### 3.4 GFS 论文的影响

GFS 论文是分布式存储领域的奠基性工作,催生了:

- **HDFS**:开源 GFS(Hadoop 生态基石)
- **Bigtable**:基于 GFS 的 KV 存储
- **MapReduce**:基于 GFS 的计算框架
- **Colossus / Spanner / Megastore**:Google 内部演进
- **对象存储**:S3、OSS、COS(简化版的 GFS 思路)

> 🎓 **学术延伸**:GFS 论文的"敢于放弃 POSIX 兼容性、为特定工作负载优化"是分布式系统设计的经典案例,体现了"workload-aware design"思想。

---

## 4. 🏭 工业实战

### 4.1 Hadoop 生态中的 HDFS

```
Hive / Spark / Flink / MapReduce
              ↓
            HDFS
              ↓
       DataNode (Linux 文件)
```

HDFS 是大数据生态的"底层存储",所有框架(MapReduce/Spark/Flink/Hive/HBase)都直接读写 HDFS。

### 4.2 HBase on HDFS

HBase 把 HDFS 当"块设备"用,每个 Region 的 HFile 存在 HDFS:

- HDFS 提供"原子追加 + 多副本",HBase 在此上构建 LSM-Tree
- HBase 短期数据写 MemTable(内存),刷盘成 HFile 落到 HDFS
- Compaction 把多个 HFile 合并成大 HFile

> 🎓 HBase 不修改 HFile(只追加),HDFS 的 append-only 模型完美匹配。

### 4.3 HDFS Tiered Storage

HDFS 2.x+ 支持分层存储:

| 类型 | 介质 | 用途 |
|------|------|------|
| ARCHIVE | 磁带/归档 | 冷数据 |
| COLD | HDD | 历史数据 |
| WARM | HDD | 较少访问 |
| HOT | SSD | 频繁访问 |
| RAM_DISK | 内存 | 极热数据 |

策略:数据按访问频率自动迁移,降低成本。

### 4.4 HDFS Erasure Coding

HDFS 3.0+ 引入 EC 替代 3 副本:

- 3 副本:存储开销 3x
- EC (Reed-Solomon 6+3):存储开销 1.5x,容 3 个节点故障
- EC (RS 10+4):存储开销 1.4x,容 4 个节点故障

适用:冷数据归档(降低存储成本);不适合热数据(EC 计算开销大、随机读慢)。

### 4.5 对象存储与 HDFS

| 维度 | HDFS | S3/OSS |
|------|------|--------|
| 接口 | POSIX-like | HTTP REST |
| 一致性 | 强一致(写后读) | Read-after-write strong (单 key) |
| 元数据 | NameNode 集中 | 分布式索引 |
| 大文件 | 优秀 | 优秀 |
| 小文件 | 差 | 差(每对象有 metadata 开销) |
| 性能 | 局域网内极快 | 受网络限制 |
| 弹性 | 加节点 | 云原生弹性 |

趋势:云上大数据用 S3/OSS 替代 HDFS(Spark on S3、Presto on OSS),HDFS 多用于私有云。

### 4.6 工业案例:阿里巴巴 HDFS

- 规模:10w+ DataNode,PB 级存储
- 优化:
  - 异构存储(SSD + HDD 分层)
  - Erasure Coding 降低成本
  - Federation 拆分 NameNode
  - 定制 Short-Circuit Read(本地 DataNode 直接读,绕过 TCP)

---

## 5. 面试要点

### 5.1 高频问答

**Q1: GFS/HDFS 为什么用大 chunk(64MB/128MB)?**

> (1) 减少 Master 元数据量(64MB chunk → 64B 元数据,1PB 文件只需 16MB);(2) 减少 client-master 交互(一次拿到 chunk 列表后顺序读);(3) 提高 TCP 长连接吞吐;(4) 减少心跳/Block Report 频率。代价:小文件场景浪费空间且 Master 压力大。

**Q2: HDFS 写数据的流程?**

> (1) Client 向 NameNode 申请创建文件;(2) Client 向 NameNode 申请 block,NameNode 选 3 个 DataNode 返回 pipeline;(3) Client 建立 pipeline,数据流式写(packet 64KB);(4) pipeline ACK 链路返回;(5) block 写满或文件 close,Client 通知 NameNode。数据流不经过 NameNode,只走控制流。

**Q3: HDFS 副本放置策略?**

> 默认 3 副本:副本 1 在本机架(本地优先),副本 2 在另一机架(机架容错),副本 3 与副本 2 同机架(降低跨机架带宽)。平衡机架容错和读带宽。

**Q4: NameNode 单点怎么解决?**

> (1) HA:Active + Standby NameNode,共享 EditLog(JournalNode 集群 Quorum);(2) Federation:多个 NameNode 各管一部分命名空间,扩展元数据;(3) 备份 FsImage + EditLog 到远程存储。

**Q5: HDFS 怎么处理 DataNode 故障?**

> DataNode 60s 内未心跳 → NameNode 标记为 dead → 该节点所有 block 标记"副本不足" → NameNode 调度其他 DataNode 复制 block 恢复到 3 副本。整个过程自动,对用户透明。

**Q6: HDFS 小文件为什么是问题?**

> (1) NameNode 内存压力:每个文件 ~150B 元数据,1 亿文件 = 15GB;(2) 读写效率低:每个文件单独一个 block,频繁 seek;(3) MapReduce 启动开销大:每文件一个 task。解决:HAR 归档、SequenceFile、Federation、合并小文件。

**Q7: GFS 的"一致但未定义"是什么?**

> 并发追加失败重试时,某些副本可能写了重复数据,所有副本最终一致(都包含重复),但客户端看到的是"一致但未定义"的状态(不知道哪段是重复)。应用需用唯一 ID 去重。

**Q8: HDFS 和对象存储(S3)的选型?**

> HDFS:适合私有云、需要极快本地 IO、POSIX-like 接口、生态集成(HBase on HDFS)。S3:适合云上、弹性扩展、低成本归档、跨区域复制。趋势:云上用 S3 替代 HDFS,私有云仍用 HDFS。

### 5.2 易错点 ⚠️

1. **"HDFS 是强一致"** — 不完全。单个 chunk 内是强一致(写后读),但 GFS 论文承认弱一致场景。
2. **"HDFS 文件可以随机修改"** — 不能。HDFS 只支持 append,不支持任意位置修改。
3. **"HDFS 副本越多越好"** — 错。3 副本是工业标准,更多副本浪费存储且写入更慢。
4. **"NameNode 故障立即切换"** — 错。需要 Standby 同步完 EditLog 才能切换,RTO 几十秒到几分钟。
5. **"HDFS 适合所有大数据场景"** — 错。低 latency 随机访问不适合(用 HBase/Cassandra);小文件场景不适合。

---

## 6. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Ghemawat et al., *GFS* | 2003 SOSP | 大文件分布式 FS 奠基 |
| Shvachko, *HDFS* | 2010 MSST | 开源 GFS 实现详解 |
| Borthakur, *HDFS Architecture* | 2008 | Hadoop 内部文档 |
| McKusick, *Colossus* | 2010 | GFS 二代演进 |
| Maltzahn et al., *Ceph FS* | 2007 | 分布式 Metadata Server |

---

## 7. 交叉引用

- [[04-复制]]:副本策略基础
- [[07-分片与路由]]:Block 分布
- [[08-2-存储-Dynamo]]:对比 Dynamo 的无 Master 设计
- [[08-3-存储-Bigtable与HBase]]:基于 HDFS 的 KV
- [[14-故障与容错]]:NameNode HA / DataNode 容错

---

## 8. TODO

- [ ] 补充 HDFS EC 的 Reed-Solomon 编码细节
- [ ] 补充 HDFS Federation 的命名空间划分策略
- [ ] 增加 Short-Circuit Read 性能数据
- [ ] 补充 Colossus 相对 GFS 的具体改进

---

## 9. 速查表 (Cheat Sheet)

```
GFS/HDFS 核心数字:
  Chunk/Block size: 64MB (GFS) / 128MB (HDFS 默认)
  Replica: 3 (默认)
  Heartbeat: 3s (HDFS DataNode)
  Block Report: 6h
  NameNode 内存: ~150B/file

写路径关键:
  数据流不经 Master,只走控制流
  Pipeline: Client → DN1 → DN2 → DN3
  Packet: 64KB

读路径关键:
  Client 缓存 block 位置 60s
  就近选择 DataNode(本机架优先)

故障容忍:
  DataNode 60s 未心跳 → dead
  Block 损坏 → MD5/CRC 校验 → 重新复制
  NameNode → HA + Federation
```

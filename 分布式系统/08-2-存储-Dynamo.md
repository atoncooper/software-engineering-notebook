# 存储系统 —— Dynamo 与 AP 一致性

> 章号: §8.2
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业
> 前置: [[04-复制]] [[07-分片与路由]] [[02-理论基础]]

---

## 0. 为什么需要 Dynamo

GFS/HDFS 是"大文件、顺序访问"场景的存储,但电商系统(如 Amazon)的负载完全不同:

- 访问模式:KV 读写(购物车、用户偏好、会话)
- 一致性需求:最终一致可接受,可用性优先
- 规模:成千上万节点,故障常态
- 延迟:严格 SLA(99.9% < 10ms)

Amazon 2007 年发表 **Dynamo** 论文,首次系统化阐述"去中心化、AP、最终一致"的 KV 存储,开创了 NoSQL 浪潮。Cassandra、Riak、Voldemort 都是其开源后裔。

本章分析 Dynamo 的设计哲学、关键技术(一致性哈希、Quorum、Vector Clock、Sloppy Quorum、Hinted Handoff、Merkle Tree 反熵),以及工业落地。

---

## 1. 定义与动机

### 1.1 设计哲学 (SOSP 2007)

> 📜 DeCandia et al., 2007 SOSP — *Dynamo: Amazon's Highly Available Key-value Store*

Dynamo 论文开篇就鲜明立场:

> "Customers must be able to view and add items to their shopping cart at all times. ... For Amazon, a customer-facing service is the system of record. ... an unreliable system can quickly wipe out any gains."

核心理念:

1. **Availability First**:可用性 > 强一致(CAP 选 AP)
2. **Always Writeable**:写永不失败(不返回"unavailable"给客户)
3. **No Single Point of Failure**:去中心化,无 Master
4. **Incremental Scalability**:加节点即可扩容,无需重启
5. **Tunable Consistency**:每次操作可调 W/R/N

### 1.2 与 GFS/HDFS 的对比

| 维度 | GFS/HDFS | Dynamo |
|------|----------|--------|
| 一致性 | 强一致(单 chunk) | 最终一致、可调 |
| Master | 单 Master/NameNode | 无 Master |
| 写入 | 追加为主 | 任意 KV 读写 |
| 适用 | 大文件顺序 | 高并发 KV |
| 可用性 | Master 单点 | 高可用(去中心化) |
| 故障容忍 | NameNode HA | 节点故障自动接管 |

### 1.3 Dynamo 的核心抽象

> Dynamo = 一致性哈希 + Quorum 复制 + Vector Clock + 反熵 + Hinted Handoff + Sloppy Quorum + Merkle Tree

这些技术组合,实现了"在任意网络分区下都能写入、最终一致收敛"的目标。

---

## 2. 原理与算法

### 2.1 一致性哈希 + 虚拟节点

详见 [[07-分片与路由]]。Dynamo 把 key 通过 MD5 映射到 128 位环上,每个节点负责环上一段范围。

关键点:**虚拟节点 (vnode)**。Dynamo 默认每节点 $V = N$ 个虚节点(后期 Cassandra 用 $V=256$),使负载均匀。

```
Ring (简化):
   A1 --- A2 --- B1 --- B2 --- C1 --- C2 --- A1
   |                                             |
key k1: hash(k1) 落在 B1 - B2 之间 → 路由到 B1
key k2: hash(k2) 落在 C2 - A1 之间 → 路由到 A1
```

### 2.2 Quorum 复制 (W + R > N)

#### 2.2.1 N、W、R

- $N$:每个 key 的副本数(典型 3)
- $W$:写需要的最少确认数(写 Quorum)
- $R$:读需要的最少返回数(读 Quorum)

**强一致条件**:$W + R > N$,且 $W > \frac{N}{2}$。

证明:读和写的 Quorum 必有交集,交集节点持有最新数据,读时通过版本号拿到最新。

#### 2.2.2 一致性 vs 可用性权衡

| 配置 | 含义 | 一致性 | 性能 |
|------|------|-------|------|
| $W=N, R=1$ | 全部副本确认才写,读一个就够 | 强一致 | 写慢,读快 |
| $W=1, R=N$ | 写一个就返回,读全部 | 强一致 | 写快,读慢 |
| $W=R=\lceil N/2 \rceil+1$ | 多数派 | 强一致 | 平衡 |
| $W=1, R=1$ | 任一副本 | 最终一致 | 最快 |
| $W=N, R=N$ | 所有副本 | 强一致 + 容错差 | 最慢 |

Dynamo 默认 $N=3, W=2, R=2$,平衡一致性与可用性。

```java
public class DynamoQuorum {
    private final int N, W, R;
    private final List<Node> nodes;

    public WriteResult put(String key, byte[] value) {
        List<Node> replicas = routing.topN(key, N);
        CountDownLatch latch = new CountDownLatch(W);
        AtomicReference<WriteResult> firstSuccess = new AtomicReference<>();

        for (Node n : replicas) {
            CompletableFuture.runAsync(() -> {
                try {
                    n.put(key, value, clock);
                    WriteResult r = new WriteResult(n, true);
                    if (firstSuccess.compareAndSet(null, r)) {
                        latch.countDown();
                    }
                } catch (Exception e) {
                    // 失败,等待其他副本
                }
            });
        }

        try {
            latch.await(timeout, TimeUnit.MILLISECONDS);
            return firstSuccess.get();
        } catch (InterruptedException e) {
            throw new WriteTimeoutException();
        }
    }

    public ReadResult get(String key) {
        List<Node> replicas = routing.topN(key, N);
        List<Future<ReadResult>> futures = new ArrayList<>();
        for (Node n : replicas) {
            futures.add(CompletableFuture.supplyAsync(() -> n.get(key)));
        }
        // 等待 R 个返回,选版本号最大的
        List<ReadResult> results = new ArrayList<>();
        long deadline = System.currentTimeMillis() + readTimeoutMs;
        while (results.size() < R && System.currentTimeMillis() < deadline) {
            for (Future<ReadResult> f : futures) {
                try {
                    ReadResult r = f.get(10, TimeUnit.MILLISECONDS);
                    if (r != null && !results.contains(r)) {
                        results.add(r);
                        if (results.size() >= R) break;
                    }
                } catch (Exception e) { /* ignore */ }
            }
        }
        return reconcile(results);  // 用 vector clock 合并
    }
}
```

### 2.3 Vector Clock (向量时钟)

详见 [[03-时间与时钟]]。Dynamo 用 vector clock 检测并发写冲突:

```
T1: client A put(k, v1, clock=[A:1])
T2: client B (从 A 读到 v1) put(k, v2, clock=[A:1, B:1])
T3: client C (从 A 读到 v1) put(k, v3, clock=[A:1, C:1])
                ↓
v2 clock=[A:1, B:1], v3 clock=[A:1, C:1]
→ 并发!无法定序,保留两个版本(Sibling)
```

读时返回所有 sibling,客户端用业务逻辑合并(如购物车合并:取并集)。

```java
public class VectorClock {
    private final Map<String, Long> clocks = new HashMap<>();

    public void increment(String nodeId) {
        clocks.merge(nodeId, 1L, Long::sum);
    }

    public VectorClock merge(VectorClock other) {
        VectorClock result = new VectorClock();
        Set<String> allKeys = new HashSet<>(clocks.keySet());
        allKeys.addAll(other.clocks.keySet());
        for (String k : allKeys) {
            result.clocks.put(k, Math.max(clocks.getOrDefault(k, 0L),
                                          other.clocks.getOrDefault(k, 0L)));
        }
        return result;
    }

    public int compare(VectorClock other) {
        boolean ge = true, le = true;
        Set<String> allKeys = new HashSet<>(clocks.keySet());
        allKeys.addAll(other.clocks.keySet());
        for (String k : allKeys) {
            long a = clocks.getOrDefault(k, 0L);
            long b = other.clocks.getOrDefault(k, 0L);
            if (a < b) ge = false;
            if (a > b) le = false;
        }
        if (ge && le) return 0;  // 相等
        if (ge) return 1;         // this 后于 other
        if (le) return -1;        // this 先于 other
        return Integer.MIN_VALUE; // 并发
    }
}
```

### 2.4 Sloppy Quorum + Hinted Handoff

#### 2.4.1 问题

Quorum 假设副本节点可达。如果某副本暂时故障,Quorum 直接降级可用性。

#### 2.4.2 Sloppy Quorum

> 写时若首选副本不可达,顺时针找到下一个可达节点暂存,标记 "hinted"(提示节点)。

```
N=3, key=k 的首选副本 [A, B, C]
写时 A 故障:
  顺时针找下一个 D
  D 暂存数据,标记 hinted_for=A
返回成功(实际副本数还是 3,只是临时多在 D)

A 恢复:
  D 把数据回传给 A
  D 删除 hinted 副本
```

```python
def sloppy_quorum_put(key, value, n=3, w=2):
    preferred = ring.top_n(key, n)
    success = 0
    hinted = []
    candidates = preferred + ring.next_after(preferred[-1], limit=10)
    for node in candidates:
        if node.is_alive():
            if node in preferred:
                node.put(key, value)
            else:
                node.put_hinted(key, value, hint_for=preferred[success])
                hinted.append((node, preferred[success]))
            success += 1
            if success >= w:
                break
    if success < w:
        raise WriteFailed()
    return hinted
```

#### 2.4.3 Hinted Handoff

节点 A 恢复后,D 主动把 hinted 数据回传给 A:

```python
def hinted_handoff_loop(self):
    while True:
        for hinted_node in self.get_hinted_data():
            target = hinted_node.hint_for
            if target.is_alive():
                try:
                    target.put(hinted_node.key, hinted_node.value)
                    self.delete_hinted(hinted_node)
                except Exception:
                    pass  # 等下次重试
        time.sleep(60)
```

> 🎓 关键洞察:Sloppy Quorum 突破"Quorum 必须严格"的限制,让 Dynamo 真正实现"always writeable"。代价是数据可能短暂不一致,但通过 Hinted Handoff 最终收敛。

### 2.5 Read Repair

读时如果发现副本数据不一致(版本号不同),触发修复:

```
读 k,返回 3 个副本:
  A: v1, clock=[A:1]
  B: v1, clock=[A:1]
  C: v0, clock=[]  (旧数据)

→ 客户端读到最新 v1
→ 顺便把 v1 写回 C(Read Repair)
```

```python
def read_with_repair(key, n=3, r=2):
    results = quorum_read(key, n, r)
    latest = max(results, key=lambda x: x.clock)
    stale = [r for r in results if r.clock != latest.clock]
    for s in stale:
        s.node.async_put(key, latest.value, latest.clock)
    return latest.value
```

### 2.6 反熵 (Anti-Entropy) 与 Merkle Tree

节点长期故障后恢复,可能错过大量更新。需要快速对比节点间数据差异:

#### 2.6.1 Merkle Tree

> 把节点的 keys 分组(按 key range),每组计算 hash,组合成 Merkle Tree。两节点对比时只需对比根 hash,不同则递归下钻。

```
           Root Hash
          /         \
       H_AB        H_CD
      /    \      /    \
    H_A   H_B  H_C   H_D
     |     |    |     |
   keys  keys  keys  keys
   [a-c] [d-f] [g-i] [j-l]
```

对比流程:

1. A 和 B 交换 Root Hash
2. 不同 → 交换 H_AB/H_CD
3. 不同 → 交换 H_A/H_B/...
4. 找到差异的 key range → 只同步该 range

复杂度:$O(\log N)$,远优于全量对比 $O(N)$。

#### 2.6.2 Anti-Entropy 流程

```
节点定期(如 1h)随机选另一个节点:
  1. 对比 Merkle Tree
  2. 找到差异 range
  3. 用 Read Repair 同步
```

Dynamo 默认每小时一次反熵,确保长期故障节点恢复后能追上数据。

### 2.7 Membership 与 Gossip

#### 2.7.1 节点成员管理

Dynamo 有两种成员:

- **Permanent**:显式加入/移除(管理员操作)
- **Temporary**:节点临时故障(Membership 协议自动检测)

#### 2.7.2 Gossip 协议

节点定期(如 1s)随机选另一个节点交换状态:

- 自己的状态(活着、最近活跃时间)
- 已知的其他节点状态
- 已知的 token range 归属

经过 $O(\log N)$ 轮,集群所有节点达到一致状态。

```python
def gossip_loop(self):
    while True:
        peer = random.choice(self.known_peers)
        my_state = self.get_state()
        peer_state = peer.exchange_state(my_state)
        self.merge_state(peer_state)
        time.sleep(1)
```

> 🎓 Gossip 的优势:去中心化、可扩展($O(\log N)$ 收敛)、容错(节点故障不影响协议)。

---

## 3. 🎓 学术深度

### 3.1 Dynamo 论文的核心贡献

1. **AP 优先的工程证明**:Amazon 实际生产系统证明"放弃强一致换可用性"在电商场景可行
2. **可调一致性 (Tunable Consistency)**:$N/W/R$ 让开发者按操作权衡
3. **去中心化架构**:无 Master,所有节点对等,无单点
4. **技术组合拳**:一致性哈希 + Quorum + Vector Clock + Hinted Handoff + Merkle Tree + Gossip 的工程集成

### 3.2 与 CAP 的关系

Dynamo 在 CAP 中选 AP:

- **网络分区时**:Sloppy Quorum + Hinted Handoff 保证可写,牺牲强一致
- **无分区时**:Quorum $W+R>N$ 可达到强一致

> 🎓 Dynamo 论文不是"放弃一致性",而是"让开发者按场景选择"。这是 CAP 的工程化诠释。

### 3.3 Sibling 的处理责任

Dynamo 把冲突解决的"皮球"踢给应用:

- 购物车:并集合并(两个版本的物品都保留)
- 计数器:CRDT(详见 [[17-CRDT]])
- 用户偏好:Last-Write-Wins(用 timestamp)

学术批评:

- 应用开发者需理解 vector clock,心智负担重
- 不同应用语义不同,通用合并难

后续演进:

- **CRDT (Conflict-free Replicated Data Type)**:数据结构内置合并规则,无需应用介入
- **Riak**:Dynamo 后裔,提供 CRDT 支持
- **Cassandra**:用 Last-Write-Wins 替代 sibling,简化语义但可能丢数据

### 3.4 Quorum 的不一致窗口

即使 $W+R>N$,也存在"不一致窗口":

- 写完成 ($W$ 个副本有最新值) 到 读完成 ($R$ 个副本返回) 之间,副本状态可能变化
- 严格的线性一致还需读修复 + 同步复制

> 🎓 学术观点:Quorum 是"概率性强一致",不是"绝对强一致"。需要更复杂的协议(如 Paxos/MVCC)才能达到严格线性一致。

### 3.5 反熵协议的对比

| 协议 | 复杂度 | 触发 | 适用 |
|------|-------|------|------|
| Read Repair | $O(1)$ | 读时 | 热点 key 频繁修复 |
| Hinted Handoff | $O(1)$ | 故障恢复 | 短期故障 |
| Merkle Tree Anti-Entropy | $O(\log N)$ | 定期 | 长期差异 |
| Full Sync | $O(N)$ | 手动 | 灾难恢复 |

Dynamo 三者并用,覆盖不同场景。

---

## 4. 🏭 工业实战

### 4.1 Amazon DynamoDB

Dynamo 论文是 DynamoDB 的前身。DynamoDB 相对 Dynamo 的演进:

| 维度 | Dynamo | DynamoDB |
|------|--------|----------|
| 部署 | 单租户内部 | 多租户云服务 |
| 路由 | Gossip 同步 | 中心化 Coordinator |
| 一致性 | 最终一致 | 强一致(可选) + 最终一致 |
| 索引 | 仅主键 | LSI / GSI |
| 事务 | 无 | 单分区事务 |
| 计费 | 内部 | 容量单位(RCU/WCU) |

DynamoDB 仍保留 Dynamo 的去中心化思想,但为了运维可控,引入了中心化元数据。

### 4.2 Cassandra

Cassandra 是 Dynamo + Bigtable 的混合:

- Dynamo:一致性哈希、Quorum、反熵、Gossip
- Bigtable:LSM-Tree 存储、SSTable、Compaction

详细见 [[08-5-存储-Cassandra与Redis]]。

### 4.3 Riak

Basho 公司的 Dynamo 开源实现,特色:

- 原生 CRDT 支持(Counter、Set、Map、Register)
- Strong Consistency 可选(Riak 2.0+)
- MapReduce 计算
- 已停止维护(2017)

### 4.4 Voldemort (LinkedIn)

LinkedIn 内部 KV 存储,设计哲学与 Dynamo 类似:

- 中心化路由(不 Gossip)
- Read Repair + Quorum
- 用于 LinkedIn 社交图谱、推荐特征

### 4.5 工业级 Dynamo 实现要点

1. **可调一致性**:按业务设 $W/R$(如购物车 $W=2, R=1$;订单 $W=3, R=2$)
2. **监控 Quorum 超时**:写超时不能算成功,需要客户端重试 + 幂等
3. **Sibling 处理**:应用必须实现 merge 逻辑(或选 CRDT)
4. **反熵节奏**:1h 一次 Merkle 对比,确保长期收敛
5. **容量规划**:vnode 数量决定负载均匀度,Cassandra 默认 256/节点

---

## 5. 面试要点

### 5.1 高频问答

**Q1: Dynamo 和 HDFS 的核心区别?**

> Dynamo:AP、去中心化、最终一致、KV 模型、适合高并发读写;HDFS:CP、中心化 Master、强一致、文件模型、适合大文件顺序访问。前者是 NoSQL 鼻祖,后者是大数据存储基石。

**Q2: Quorum 的 W+R>N 怎么保证一致性?**

> 读和写的 Quorum 必有交集(至少一个节点既在 W 中又在 R 中),该节点持有最新版本。读时通过版本号比较,返回最新值。严格证明:集合论,$W + R > N \Rightarrow W \cap R \ne \emptyset$。

**Q3: Vector Clock 在 Dynamo 中怎么用?**

> 每次写附带 vector clock(节点 → 计数的 map)。读时返回多个版本(siblings)如果 vector clock 并发。客户端用业务逻辑合并(如购物车取并集)。新写入的 vector clock 是合并后的 + 自己节点计数 +1。

**Q4: Sloppy Quorum 和 Hinted Handoff 解决什么问题?**

> 解决"Quorum 副本故障导致写失败"的问题。Sloppy Quorum 在首选副本故障时找下一个节点暂存(hinted),保证 always writeable;Hinted Handoff 在原节点恢复后回传数据,实现最终一致。

**Q5: Dynamo 怎么处理节点长期故障?**

> Hinted Handoff 解决短期故障(秒-分钟),Merkle Tree Anti-Entropy 解决长期故障(小时-天)。节点恢复后,通过 Merkle Tree $O(\log N)$ 对比找到差异 range,只同步差异部分。

**Q6: Dynamo 为什么用 Gossip?**

> 去中心化:无 Master,Gossip 让所有节点最终知道集群状态;可扩展:Gossip 收敛 $O(\log N)$,节点数到几千都可行;容错:节点故障不影响协议。代价是状态收敛有延迟(秒级)。

**Q7: Cassandra 和 Dynamo 的关系?**

> Cassandra 是 Dynamo + Bigtable 的混合:从 Dynamo 借鉴一致性哈希、Quorum、反熵、Gossip;从 Bigtable 借鉴 LSM-Tree、SSTable、Compaction。Cassandra 用 Last-Write-Wins 替代 sibling,简化了 Dynamo 的复杂度。

**Q8: Dynamo 的 Sibling 怎么解决?**

> (1) 应用层合并:购物车取并集、计数器累加;(2) Last-Write-Wins:用 timestamp 比较,丢旧的;(3) CRDT:数据结构内置合并规则;(4) 应用避免并发写同一 key(用 partition key 隔离)。

### 5.2 易错点 ⚠️

1. **"Dynamo 是强一致"** — 错。Dynamo 是 AP,最终一致;只有 $W=N, R=N$ 才强一致,但代价极大。
2. **"Quorum 永远保证强一致"** — 错。Quorum $W+R>N$ 保证"读到最新写",但不是线性一致(读写交叉场景可能有异常)。
3. **"Vector Clock 是 Lamport Clock"** — 不是。Vector Clock 是 Lamport Clock 的多节点扩展,可检测并发;Lamport 只能给事件全序。
4. **"Gossip 实时一致"** — 错。Gossip 是最终一致,秒级延迟。
5. **"Dynamo = DynamoDB"** — 不是。Dynamo 是 2007 内部系统,DynamoDB 是 2012 商业云服务,架构有演进(中心化元数据)。

---

## 6. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| DeCandia et al., *Dynamo* | 2007 SOSP | AP KV 存储奠基 |
| Lakshman & Malik, *Cassandra* | 2009 | Dynamo + Bigtable |
| Shapiro et al., *CRDTs* | 2011 | 冲突自由数据结构 |
| Lakshman et al., *Cassandra VLDB* | 2010 | 工业级 Dynamo 演进 |
| Decandia et al., *DynamoDB* | 2012 | Dynamo 商业化演进 |

---

## 7. 交叉引用

- [[02-理论基础]]:CAP / BASE / 最终一致
- [[03-时间与时钟]]:Vector Clock
- [[04-复制]]:Quorum / Read Repair / Anti-Entropy
- [[07-分片与路由]]:一致性哈希 + vnode
- [[08-1-存储-GFS与HDFS]]:对比 GFS 的 CP 设计
- [[08-5-存储-Cassandra与Redis]]:Dynamo 工业后裔
- [[17-CRDT]]:Dynamo Sibling 的最终解决方案

---

## 8. TODO

- [ ] 补充 Riak CRDT 实现细节
- [ ] 补充 Voldemort 的中心化路由设计
- [ ] 增加 DynamoDB 多租户隔离机制
- [ ] 补充 Gossip 协议的概率分析($O(\log N)$ 收敛证明)

---

## 9. 速查表 (Cheat Sheet)

```
Dynamo 核心数字:
  N=3, W=2, R=2 (默认)
  vnode=256 (Cassandra)
  Gossip: 1s 周期
  Anti-Entropy: 1h 周期
  Hinted Handoff: 节点恢复后立即触发

技术组合:
  一致性哈希 + vnode       → 路由
  Quorum (W+R>N)           → 一致性
  Vector Clock             → 冲突检测
  Sibling                  → 冲突保留
  Sloppy Quorum            → 可用性
  Hinted Handoff           → 临时副本回流
  Read Repair              → 读时修复
  Merkle Tree              → 反熵
  Gossip                   → 成员管理

一致性档位:
  强一致:   W=N, R=1 (写慢读快)
  强一致:   W=1, R=N (写快读慢)
  强一致:   W=R=⌈N/2⌉+1 (平衡)
  最终一致: W=1, R=1 (最快,可能读到旧)
```

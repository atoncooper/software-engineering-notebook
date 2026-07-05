# CRDT —— 冲突自由复制数据类型

> 章号: §17
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: 🎓学术 🔥工程 🏭工业
> 前置: [[03-时间与时钟]] [[08-2-存储-Dynamo]] [[02-理论基础]]

---

## 0. CRDT 是什么

> 📜 Shapiro et al., 2011 — *Conflict-Free Replicated Data Types*

**CRDT (Conflict-Free Replicated Data Type)**:一种数据结构,多副本并发修改时,自动收敛到一致状态,无需协调(无需共识)。

形式化:CRDT 操作满足 **交换律 + 结合律 + 幂等律**,任意顺序执行结果相同。

```
传统复制:
  Node A: x = 1     Node B: x = 2
       ↓ 网络同步 ↓
  冲突!谁是正确值?

CRDT:
  Node A: GCounter[1,0,0]   Node B: GCounter[0,1,0]
       ↓ 网络同步 ↓
  两边都变成 [1,1,0] → 一致
```

CRDT 解决 Dynamo 类系统的"sibling 冲突"问题(详见 [[08-2-存储-Dynamo]]),无需应用层合并逻辑。

---

## 1. 应用场景

- **协同编辑**:Google Docs、Figma、Notion
- **离线优先应用**:移动端断网后修改,联网后同步
- **分布式数据库**:Riak、Redis CRDT、Cosmos DB
- **P2P 应用**:IPFS、Secure Scuttlebutt

---

## 2. 数学基础

### 2.1 半格 (Semilattice)

CRDT 的状态空间是**半格**:有偏序关系 $\leq$ 和 join 操作 $\vee$,满足:

- 交换律:$a \vee b = b \vee a$
- 结合律:$(a \vee b) \vee c = a \vee (b \vee c)$
- 幂等律:$a \vee a = a$

任意两个状态 join 后得到"最小上界",即自动收敛。

### 2.2 两类 CRDT

- **CvRDT (State-based)**:基于状态,副本间同步状态,合并用 join
- **CmRDT (Operation-based)**:基于操作,副本间同步操作,操作需满足交换律

---

## 3. 经典 CRDT

### 3.1 G-Counter (Grow-only Counter)

只增计数器。每个副本维护自己的计数,合并时取 max。

```python
class GCounter:
    def __init__(self, node_id):
        self.node_id = node_id
        self.counts = {}  # {node_id: count}

    def increment(self):
        self.counts[self.node_id] = self.counts.get(self.node_id, 0) + 1

    def value(self):
        return sum(self.counts.values())

    def merge(self, other):
        for nid, c in other.counts.items():
            self.counts[nid] = max(self.counts.get(nid, 0), c)
```

```
Node A: {A:3, B:0, C:0}
Node B: {A:0, B:2, C:0}
Node C: {A:0, B:0, C:5}

merge 后所有节点: {A:3, B:2, C:5} → 总值 10
```

### 3.2 PN-Counter (Positive-Negative Counter)

支持增减。两个 G-Counter:一个增计数,一个减计数,值为差。

```python
class PNCounter:
    def __init__(self, node_id):
        self.p = GCounter(node_id)  # positive
        self.n = GCounter(node_id)  # negative

    def increment(self):
        self.p.increment()

    def decrement(self):
        self.n.increment()

    def value(self):
        return self.p.value() - self.n.value()

    def merge(self, other):
        self.p.merge(other.p)
        self.n.merge(other.n)
```

### 3.3 G-Set (Grow-only Set)

只增集合。合并用并集。

```python
class GSet:
    def __init__(self):
        self.elements = set()

    def add(self, e):
        self.elements.add(e)

    def merge(self, other):
        self.elements |= other.elements
```

### 3.4 2P-Set (Two-Phase Set)

支持删除:用两个 G-Set,一个 add,一个 remove。元素在 add 且不在 remove 时存在。

```python
class TwoPSet:
    def __init__(self):
        self.adds = GSet()
        self.removes = GSet()

    def add(self, e):
        self.adds.add(e)

    def remove(self, e):
        if e in self.adds.elements:
            self.removes.add(e)

    def contains(self, e):
        return e in self.adds.elements and e not in self.removes.elements

    def merge(self, other):
        self.adds.merge(other.adds)
        self.removes.merge(other.removes)
```

问题:**删除是永久的**,删除后无法重新添加(tombstone)。

### 3.5 LWW-Set (Last-Write-Wins Set)

每个元素带时间戳,合并时取时间戳大的状态。

```python
class LWWSet:
    def __init__(self):
        self.adds = {}  # {element: timestamp}
        self.removes = {}

    def add(self, e, ts):
        self.adds[e] = max(self.adds.get(e, 0), ts)

    def remove(self, e, ts):
        self.removes[e] = max(self.removes.get(e, 0), ts)

    def contains(self, e):
        add_ts = self.adds.get(e, 0)
        rem_ts = self.removes.get(e, 0)
        return add_ts > rem_ts

    def merge(self, other):
        for e, ts in other.adds.items():
            self.adds[e] = max(self.adds.get(e, 0), ts)
        for e, ts in other.removes.items():
            self.removes[e] = max(self.removes.get(e, 0), ts)
```

依赖时钟同步(详见 [[03-时间与时钟]])。

### 3.6 OR-Set (Observed-Remove Set)

解决 2P-Set 的"删除永久"问题:每次 add 加唯一 tag,remove 只删已观察到的 tag。

```python
class ORSet:
    def __init__(self):
        self.elements = {}  # {element: set of tags}
        self.tombstones = set()  # removed tags

    def add(self, e):
        tag = uuid.uuid4()
        self.elements.setdefault(e, set()).add(tag)

    def remove(self, e):
        if e in self.elements:
            self.tombstones |= self.elements[e]
            del self.elements[e]

    def contains(self, e):
        return e in self.elements and len(self.elements[e]) > 0

    def merge(self, other):
        for e, tags in other.elements.items():
            self.elements.setdefault(e, set()).update(tags - self.tombstones)
        self.tombstones |= other.tombstones
        # 清理已删除的 tag
        for e in list(self.elements.keys()):
            self.elements[e] -= self.tombstones
            if not self.elements[e]:
                del self.elements[e]
```

允许"删除后重新添加"。

### 3.7 LWW-Register

单值寄存器,最后写胜出。

```python
class LWWRegister:
    def __init__(self):
        self.value = None
        self.timestamp = 0

    def set(self, value, ts):
        if ts > self.timestamp:
            self.value = value
            self.timestamp = ts

    def merge(self, other):
        if other.timestamp > self.timestamp:
            self.value = other.value
            self.timestamp = other.timestamp
```

### 3.8 MV-Register

多值寄存器,保留并发版本(sibling)。

```python
class MVRegister:
    def __init__(self):
        self.values = {}  # {value: vector_clock}

    def set(self, value, clock):
        self.values = {value: clock}  # 覆盖

    def merge(self, other):
        new_values = {}
        for v, c in self.values.items():
            dominated = any(other_dominates(c, oc) for oc in other.values.values())
            if not dominated:
                new_values[v] = c
        for v, c in other.values.items():
            dominated = any(self_dominates(c, oc) for oc in self.values.values())
            if not dominated:
                new_values[v] = c
        self.values = new_values
```

类似 Dynamo sibling,客户端需选择。

### 3.9 RGA (Replicated Growable Array)

支持有序列表,协同编辑基础。每个元素带唯一 ID 和前置元素 ID,合并时按 ID 排序。

---

## 4. 协同编辑

### 4.1 OT (Operational Transformation)

> 📜 Ellis & Gibbs, 1989

早期协同编辑算法:操作变换。两个并发操作 $O_1, O_2$,客户端应用 $O_1$ 时,把 $O_2$ 变换为 $O_2'$,使结果一致。

问题:变换函数复杂,易错。Google Docs 用 OT。

### 4.2 CRDT 协同编辑

Yjs、Automerge 用 CRDT:

- 文本元素带唯一 ID
- 操作满足交换律
- 自动收敛

```javascript
// Yjs 示例
import * as Y from 'yjs';

const doc = new Y.Doc();
const text = doc.getText('content');

text.insert(0, 'Hello');
text.insert(5, ' World');

// 多客户端同步,自动收敛
```

---

## 5. CRDT 数据库

### 5.1 Riak

- 原生支持 CRDT:Counter / Set / Map / Register
- 用 CRDT 替代应用层 sibling 合并

```python
# Riak CRDT
bucket = client.bucket_type('counters').bucket('visits')
counter = bucket.new('user_123')
counter.increment()
counter.store()
```

### 5.2 Redis CRDT (Redis Enterprise)

- Redis Enterprise 多活副本用 CRDT
- 计数器、集合、Hash 等自动收敛

### 5.3 Cosmos DB

- 多区域复制
- 部分类型用 CRDT 思想

### 5.4 Automerge

- JavaScript CRDT 库
- 离线优先应用

---

## 6. 学术深度

### 6.1 CAP 视角

CRDT 在 CAP 中选 AP:

- 分区时各副本可独立修改
- 无需协调(无共识开销)
- 合并时最终一致

代价:

- 中间状态可能不一致
- 某些场景需应用层选择(MV-Register)
- 依赖时钟(LWW 类)

### 6.2 与共识的关系

| 维度 | CRDT | 共识(Paxos/Raft) |
|------|------|------------------|
| 一致性 | 最终一致 | 强一致 |
| 协调 | 无需 | 需 Quorum |
| 性能 | 高(无协调) | 中(共识开销) |
| 适用 | 协同编辑、离线、计数器 | 金融、配置、Leader 选举 |

### 6.3 收敛性证明

CRDT 收敛性来自半格性质:

- 状态空间是半格(偏序 + join)
- 操作单调递增(只向"上"走)
- 任意两状态的 join 是唯一最小上界
- 因此多副本最终收敛到同一状态

### 6.4 Operation-based CRDT

CmRDT 要求操作满足交换律,但更高效(只传操作,不传状态):

- Op1, Op2 并发 → Op1 ∘ Op2 = Op2 ∘ Op1
- 需要可靠因果广播(Reliable Causal Broadcast)

---

## 7. 工业实践

### 7.1 何时用 CRDT

适合:

- 协同编辑(文档、白板)
- 离线优先(移动端)
- 多活最终一致(计数器、状态)
- 无需强一致

不适合:

- 金融(强一致必需)
- 唯一性约束(需共识)
- 复杂事务

### 7.2 工业案例

- **Google Docs**:OT(非 CRDT,但思想类似)
- **Figma**:CRDT + 自定义
- **Notion**:CRDT
- **Yjs**:开源协同编辑库
- **Automerge**:JSON-like CRDT

### 7.3 性能考虑

- 状态膨胀:tombstone 不删,长期增长
- 解决:定期 GC(需协调)、压缩
- 网络:状态 vs 操作 trade-off

---

## 8. 面试要点

**Q1: CRDT 是什么?**

> Conflict-Free Replicated Data Type,冲突自由复制数据类型。多副本并发修改时自动收敛,无需协调(无共识)。操作满足交换律 + 结合律 + 幂等律。

**Q2: CRDT 解决什么问题?**

> Dynamo 类系统的 sibling 冲突(并发写需应用层合并)。CRDT 把合并逻辑内置到数据结构,无需应用介入。适合协同编辑、离线优先、多活最终一致。

**Q3: G-Counter 怎么工作?**

> 每副本维护自己的计数(避免覆盖)。合并时取 max(每副本的计数取最大)。总值 = 所有副本计数之和。支持并发增,自动收敛。

**Q4: PN-Counter 怎么实现减?**

> 两个 G-Counter:一个增计数(P),一个减计数(N)。值 = P - N。增调用 P.increment,减调用 N.increment,合并时两个 G-Counter 各自合并。

**Q5: 2P-Set 的问题?**

> 删除是永久的:remove 后无法重新 add(tombstone 已记)。OR-Set 解决:每次 add 带唯一 tag,remove 只删观察到的 tag,新 add 用新 tag 可生效。

**Q6: CRDT 在 CAP 中属于什么?**

> AP。分区时各副本独立修改(可用性),合并时最终一致(不保证强一致)。代价:中间状态可能不一致,某些场景需应用选择(MV-Register)。

**Q7: CRDT 和 OT 的区别?**

> OT(操作变换):并发操作互相变换,实现一致。复杂易错,Google Docs 用。CRDT:数据结构内置合并规则,自动收敛。理论更优雅,Yjs/Automerge 用。

**Q8: CRDT 何时不适合?**

> (1) 金融(强一致必需,用共识);(2) 唯一性约束(用户名唯一等,需共识);(3) 复杂事务(多对象原子)。CRDT 适合"最终一致可接受"的场景。

---

## 9. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Shapiro et al., *CRDTs* | 2011 | CRDT 理论 |
| Ellis & Gibbs, *OT* | 1989 | 协同编辑早期 |
| Kleppmann, *Automerge* | 2019 | JSON CRDT |
| Nicolas et al., *Yjs* | 2017 | 高性能协同编辑 |

---

## 10. 交叉引用

- [[03-时间与时钟]]:Vector Clock / LWW 时钟依赖
- [[08-2-存储-Dynamo]]:Dynamo sibling 与 CRDT
- [[02-理论基础]]:CAP / 最终一致
- [[05-共识-Raft]]:对比共识方案

---

## 11. 速查表

```
CRDT 类型:
  G-Counter: 只增计数器,merge 取 max
  PN-Counter: 增减计数器,两个 G-Counter
  G-Set: 只增集合,merge 并集
  2P-Set: 增 + 删集合,删除永久
  OR-Set: 带 tag 的集合,可重新 add
  LWW-Register: 最后写胜出
  MV-Register: 多值寄存器 (保留 sibling)
  RGA: 有序列表 (协同编辑基础)

数学性质: 交换 + 结合 + 幂等 (半格)

vs 共识:
  CRDT: AP,最终一致,无协调,高并发
  共识: CP,强一致,Quorum,金融场景

应用:
  协同编辑 (Yjs/Automerge)
  离线优先应用
  多活最终一致 (Riak/Redis CRDT)
```

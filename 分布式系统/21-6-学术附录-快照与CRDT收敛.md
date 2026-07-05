# 学术附录 —— 分布式快照与 CRDT 收敛证明

> 章号: §21.6
> 层级: 学术 / 证明
> 标记: 🎓学术 📜论文 ⚠️易错
> 前置: [[17-CRDT]] [[16-2-计算-Flink与流处理]] [[03-时间与时钟]]
> 论文: Chandy-Lamport, *Distributed Snapshots: Determining Global States of Distributed Systems*, 1985; Shapiro et al., *Conflict-Free Replicated Data Types*, 2011

---

## 0. 概述

两个相关但不同的"收敛性"证明:

1. **分布式快照(Chandy-Lamport)**:在无故障下,确定性地捕获一致全局状态
2. **CRDT 收敛**:在任意延迟/乱序/重发下,多副本最终收敛到同一状态

两者共享"半格/偏序"的数学结构。

---

## 1. Chandy-Lamport 分布式快照

### 1.1 问题

在异步分布式系统中,如何"拍照"全局状态 $G = (s_1, ..., s_n, M)$(节点状态 + 信道消息)?

- 无全局时钟
- 节点不能暂停
- 消息在飞行中

### 1.2 算法

假设:**FIFO 信道**。

每个节点 $p$:

1. **触发快照**:$p$ 记录本地状态 $s_p$,然后向所有出边发 marker
2. **接收 marker**(首次):记录本地状态,把"从该信道收到 marker 之前"的消息记录为该信道状态,向其他出边转发 marker
3. **接收 marker**(后续):该信道的状态 = 已记录的"飞行消息"

### 1.3 伪代码

```python
def chandy_lamport():
    # 触发快照的节点
    if initiator:
        local_state = capture_state()
        channel_states = {c: [] for c in incoming_channels}
        markers_received = set()
        for c in outgoing_channels:
            send(c, MARKER)
    
def on_receive_message(channel, msg):
    if msg == MARKER:
        if channel not in markers_received:
            if not snapshot_in_progress:
                local_state = capture_state()
                channel_states = {c: [] for c in incoming_channels}
                for c in outgoing_channels:
                    send(c, MARKER)
            markers_received.add(channel)
            channel_states[channel] = pending_messages[channel]
            if len(markers_received) == len(incoming_channels):
                finish_snapshot()
        else:
            channel_states[channel] = pending_messages[channel]
            if len(markers_received) == len(incoming_channels):
                finish_snapshot()
    else:
        if snapshot_in_progress and channel not in markers_received:
            channel_states[channel].append(msg)
        deliver(msg)
```

### 1.4 正确性

**Theorem**(Chandy-Lamport 1985):快照 $S$ 满足:

- **Consistent**(一致性):$S$ 是某个可达全局状态(可能未实际发生,但符合因果)
- **Reachable**:从初始状态可达 $S$,且从 $S$ 可达终止状态

证明思路:

- 节点状态:本节点在记录时刻的状态
- 信道状态:发方记录前发的消息,但收方未在记录前收到 → 这些消息"在飞行中"
- FIFO 保证 marker 之前消息都已记录,之后消息未记录

### 1.5 应用

- **Flink Checkpoint**:Barrier 即 marker,详见 [[16-2-计算-Flink与流处理]]
- **Termination Detection**:检测分布式计算是否结束
- **Deadlock Detection**:检查等待图是否有环
- **Global State Debugging**:分布式调试

---

## 2. CRDT 收敛证明

### 2.1 半格(Semilattice)基础

**Join-Semilattice** $(S, \vee, \leq)$:

- 偏序 $\leq$:$a \leq b$ 表示"$a$ 的信息少于 $b$"
- Join $\vee$:$a \vee b$ = 最小上界(LUB)
- 性质:
  - 交换:$a \vee b = b \vee a$
  - 结合:$(a \vee b) \vee c = a \vee (b \vee c)$
  - 幂等:$a \vee a = a$

### 2.2 CvRDT(状态-based CRDT)

CvRDT 的状态空间是半格。操作:

- `query`:本地查询,不影响状态
- `update`:本地"递增"状态($s \to s', s \leq s'$)
- `merge(remote)`:本地 = 本地 $\vee$ 远程(取 LUB)

### 2.3 收敛定理

**Theorem**(Shapiro et al. 2011):

> 若 CvRDT 的状态空间是半格,且 `update` 单调递增,则任意副本经过有限次 merge 后收敛到同一状态。

### 2.4 证明

设初始状态 $s_0$。每个副本 $i$ 经过一系列 update 和 merge:

$$
s_0 \to s_1^{(i)} \to s_2^{(i)} \to \cdots
$$

由于:

1. **单调性**:$s_k^{(i)} \leq s_{k+1}^{(i)}$(update 单调)
2. **merge 是 join**:$s^{(i)} \vee s^{(j)} \geq s^{(i)}, s^{(j)}$
3. **半格性质**:任意有限子集有唯一 LUB

**最终状态** $s^* = \bigvee_i s_\infty^{(i)}$(所有副本最终状态的 join)。

由 LUB 唯一性,所有副本 converge 到 $s^*$。$\square$

### 2.5 CmRDT(操作-based CRDT)

CmRDT 不用状态 join,而用**操作广播**:

- 操作 $f$ 必须满足**交换律**:$f \circ g = g \circ f$(对并发操作)
- 副本收到操作后 apply

需**可靠因果广播**(Reliable Causal Broadcast, RCB):

- 不丢消息(可靠)
- 因果保序(若 send $m_1 \prec m_2$,则 deliver $m_1$ before $m_2$)

**收敛定理**(CmRDT):若操作满足交换律 + RCB,则收敛。

证明:并发操作任意顺序 apply,结果相同(交换律)。因果顺序保留 → 等价于某个全局顺序。

---

## 3. 经典 CRDT 半格实例

### 3.1 G-Counter

状态:$\mathbb{N}^n$ 向量(每副本一个分量)

- update:自己分量 $+1$
- merge:逐分量 max
- 偏序:$a \leq b \iff \forall i, a_i \leq b_i$
- join:$a \vee b = (\max(a_1, b_1), ..., \max(a_n, b_n))$

验证半格性质:

- 交换:$\max(a_i, b_i) = \max(b_i, a_i)$ ✓
- 结合:$\max(\max(a_i, b_i), c_i) = \max(a_i, \max(b_i, c_i))$ ✓
- 幂等:$\max(a_i, a_i) = a_i$ ✓

### 3.2 G-Set

状态:$\mathcal{P}(U)$(U 的子集)

- update:add $e$
- merge:并集
- 偏序:$\subseteq$
- join:$\cup$

### 3.3 LWW-Register

状态:$V \times T$(值 + 时间戳)

- update:set $(v, t)$ 当 $t > $ 当前时间戳
- merge:取 timestamp 大的
- 偏序:$(v_1, t_1) \leq (v_2, t_2) \iff t_1 \leq t_2$
- join:取较大 timestamp 的对

注意:依赖时钟同步,详见 [[03-时间与时钟]]。

### 3.4 OR-Set

状态:$(E, T, R)$,元素集合 $E$(带 tag)、tombstones $T$

- update:add:生成新 tag $t$,加入 $E$
- merge:union 元素(去掉 tombstone 中的),union tombstones
- 半格:tag 集合的并 + tombstone 的并

---

## 4. CRDT 与 Chandy-Lamport 的联系

| 维度 | Chandy-Lamport | CRDT |
|------|---------------|------|
| 模型 | 同步假设(FIFO)+ 无故障 | 异步 + 故障容忍 |
| 收敛 | 单次快照一致 | 持续收敛 |
| 数学结构 | 因果序 | 半格 |
| 适用 | 流处理 checkpoint | 协同编辑、AP 存储 |

**共同点**:都是用"偏序/半格"描述一致状态。

---

## 5. 限制与扩展

### 5.1 Chandy-Lamport 的限制

- 假设 FIFO 信道 → 非 FIFO 需更复杂算法(Lai-Yang 算法)
- 假设无故障 → 故障下需重新触发
- 单次快照 → 多次需版本号

### 5.2 CRDT 的限制

- **状态膨胀**:tombstone 不删 → 长期增长
- **GC 困难**:需协调(违反 AP)
- **依赖时钟**:LWW 类需时钟同步

### 5.3 解决

- **CRDT GC**:用 causal context 删除"所有副本都已 merge 的 tombstone"
- **Lai-Yang**:非 FIFO 信道的快照算法
- **Stable Snapshot**:稳定快照(故障后仍有效)

---

## 6. 工业实现

### 6.1 Flink Checkpoint(Chandy-Lamport 应用)

- Barrier = marker
- 对齐(Aligned)= FIFO 假设
- Unaligned = 处理非 FIFO(缓冲作为状态)

详见 [[16-2-计算-Flink与流处理]]。

### 6.2 Yjs / Automerge(CRDT 应用)

- Yjs:RGA-based 文本 CRDT,高性能
- Automerge:JSON-like CRDT

详见 [[17-CRDT]]。

### 6.3 Riak / Redis Enterprise

- Riak:原生 CRDT(Counter/Set/Map/Register)
- Redis Enterprise:Active-Active CRDT

---

## 7. 速查表

```
Chandy-Lamport:
  假设: FIFO 信道, 无故障
  算法: marker 广播, 首次记录本地 + 信道, 后续标记信道完成
  性质: 一致 (可达) + 可达 (从初始到终止)
  应用: Flink Checkpoint, 死锁检测, 全局状态调试

CRDT 收敛:
  数学: 半格 (偏序 + join, 满足交换/结合/幂等)
  CvRDT: 状态 join, update 单调 → 收敛到 LUB
  CmRDT: 操作交换 + RCB → 收敛
  实例:
    G-Counter (向量 max)
    G-Set (并集)
    LWW-Register (timestamp 大的)
    OR-Set (tag 集合)

共同点: 偏序/半格描述一致状态

限制:
  CL: FIFO, 无故障
  CRDT: 状态膨胀, GC 难
```

---

## 8. 交叉引用

- [[17-CRDT]]:CRDT 实例与工业应用
- [[16-2-计算-Flink与流处理]]:Chandy-Lamport 在 Flink 中的应用
- [[03-时间与时钟]]:Vector Clock / Causal Order
- [[08-2-存储-Dynamo]]:Vector Clock 与 sibling

---

## 9. 参考文献

- Chandy, Lamport. *Distributed Snapshots: Determining Global States of Distributed Systems*. ACM TOCS 1985.
- Shapiro, Preguiça, Baquero, Zawirski. *Conflict-Free Replicated Data Types*. SSS 2011.
- Lai, Yang. *On Distributed Snapshots*. 1987.
- Baquero, Moura. *A Study of CRDTs*. 2013.
- Kleppmann, Howard. *Automerge: A New Foundation for Collaboration Software*. 2019.
- Nicolas et al. *Yjs: A High-Performance CRDT for Real-Time Collaboration*. 2017.

# 学术附录 —— 拜占庭容错下界

> 章号: §21.4
> 层级: 学术 / 证明
> 标记: 🎓学术 📜论文 ⚠️易错
> 前置: [[05-共识-拜占庭共识]] [[05-共识-问题与FLP]]
> 论文: Lamport, Shostak, Pease, *The Byzantine Generals Problem*, 1982; Dolev, Strong, 1983

---

## 0. 定理陈述

**拜占庭容错下界**:在同步网络中,容忍 $f$ 个拜占庭节点的共识算法,至少需要 $n \ge 3f+1$ 个节点。

等价表述:若 $n \le 3f$,则无解。

---

## 1. 模型

### 1.1 同步网络

- 消息延迟有上界(否则 FLP 阻止共识)
- 节点知道超时 → 可区分"故障"和"慢"

### 1.2 拜占庭故障

节点可任意行为:

- 不发消息
- 发错消息
- 发不同消息给不同节点(叛变)
- 串通多个拜占庭节点

### 1.3 共识要求

- **Agreement**:所有诚实节点输出相同值
- **Validity**:若所有诚实节点输入相同 $v$,则输出 $v$
- **Termination**:所有诚实节点最终决策

---

## 2. 直觉:3 个节点 1 个拜占庭不可解

### 2.1 场景

3 个节点 $A, B, C$,其中 $C$ 是拜占庭。$A$ 输入 1,$B$ 输入 0。

### 2.2 试图达成共识

**Round 1**:每节点广播自己的值。

- $A$ 收到:$A$ 自己 = 1,$B$ = 0,$C$ = ?
- $B$ 收到:$A$ = 1,$B$ 自己 = 0,$C$ = ?

$C$ 拜占庭,可能发给 $A$ 的是 0,发给 $B$ 的是 1。

### 2.3 $A$ 的视角

$A$ 看到三个值 $\{1, 0, 0\}$(自己 + $B$ + $C$ 给的 0)。$A$ 无法判断:是 $B$ 输入 0 且 $C$ 说实话,还是 $B$ 输入 1 且 $C$ 撒谎(实际 $C$ 拜占庭)?

### 2.4 $B$ 的视角

$B$ 看到三个值 $\{1, 0, 1\}$。$B$ 无法判断对称情况。

### 2.5 不可达共识

$A$ 和 $B$ 看到的视图"对称",无法区分彼此的角色。若算法对 $A$ 决策 0,则对称地 $B$ 决策 1,违反 Agreement。

---

## 3. 形式化证明(n = 3, f = 1)

### 3.1 反证

假设存在算法 $\mathcal{A}$ 在 $n = 3, f = 1$ 下达成共识。

### 3.2 构造三个实例

考虑三个独立运行实例:

- **$E_1$**:$A$ 输入 1,$B$ 输入 1,$C$ 拜占庭
- **$E_2$**:$A$ 输入 0,$B$ 输入 0,$C$ 拜占庭
- **$E_3$**:$A$ 输入 1,$B$ 输入 0,$C$ 拜占庭(混合)

由 Validity:

- $E_1$:诚实节点都输入 1,必须决策 1
- $E_2$:诚实节点都输入 0,必须决策 0

### 3.3 $C$ 的"伪装"

$C$ 在 $E_3$ 中可以这样行为:

- 给 $A$ 模拟 $E_2$ 的 $B$(假装 $B$ 输入 0)
- 给 $B$ 模拟 $E_1$ 的 $A$(假装 $A$ 输入 1)

但 $A$ 自己输入 1,$B$ 自己输入 0,所以 $A$ 看到的视图 = $E_2$ 中 $A$ 看到的视图(自己 = 1,其他节点说 0),... 这个等价性需要更精细论证。

### 3.4 严格归纳

经典证明用归纳:

- 假设 $n$ 节点 $f$ 拜占庭可解,$3f \ge n$
- 通过"分裂"节点构造 $3f' = n', f' = f$ 的反例
- 详细见 Lamport-Shostak-Pease 1982

**核心思想**:诚实节点不能"分辨"消息来自诚实还是拜占庭,拜占庭节点可通过发不同消息"分裂"诚实节点视图。

---

## 4. 通用证明:n ≤ 3f 不可解

### 4.1 拆分(Scaling)

通过反证 + 拆分:

假设存在算法 $\mathcal{A}$ 在 $n$ 节点容忍 $f$ 拜占庭,其中 $n \le 3f$。

把 $n$ 个节点分成 3 组 $G_1, G_2, G_3$,每组至多 $f$ 个节点(因 $n \le 3f$)。

### 4.2 模拟 3 节点

把每组"模拟"为一个超级节点,每组至多 1 个"拜占庭超级节点"(整组拜占庭 = 1 个拜占庭超级节点)。

3 个超级节点,1 个拜占庭 → 由 §2,不可解。

### 4.3 矛盾

但 $\mathcal{A}$ 假设可解,矛盾。故 $n \le 3f$ 不可解。$\square$

### 4.4 严格化

详细见 Fischer 1983 / Dolev-Strong 1983 的归约证明:

- 把"3 节点 1 拜占庭不可解"作为基础
- 通过组模拟归纳到 $n$ 节点 $f$ 拜占庭

---

## 5. $n \ge 3f+1$ 可解(PBFT, HotStuff)

### 5.1 Quorum 交集

$n \ge 3f+1$ 时,$2f+1$ Quorum 之间至少有 $f+1$ 交集。

- 拜占庭最多 $f$ 个,所以交集至少 1 个诚实节点
- 诚实节点"传递"信息,保证一致

### 5.2 PBFT 三阶段

- **Pre-prepare**:Primary 提议
- **Prepare**:副本广播,等 $2f+1$ prepare → prepared
- **Commit**:广播 commit,等 $2f+1$ commit → committed

每阶段 Quorum 交集保证一致性。详见 [[05-共识-拜占庭共识]]。

### 5.3 数字签名 vs MAC

- 数字签名(公钥):$n \ge 3f+1$(标准下界)
- MAC(认证码):$n \ge 3f+1$(仍需,但消息复杂度不同)
- 信息论安全(无签名):$n \ge 3f+1$ 不够,需 $n > 3f$(甚至更多,见 Pease 1980)

---

## 6. 与 Crash 故障对比

| 故障模型 | 节点下界 | Quorum | 典型算法 |
|---------|---------|--------|---------|
| Crash | $n \ge 2f+1$ | $f+1$ | Paxos, Raft |
| Byzantine | $n \ge 3f+1$ | $2f+1$ | PBFT, HotStuff |

**为何差 1 倍**:

- Crash:故障节点"沉默",Quorum 交集只需保证有 1 个最新诚实节点 → $2f+1$ 足够
- Byzantine:故障节点"撒谎",Quorum 交集需保证有 1 个诚实节点(盖过 $f$ 拜占庭)→ $2f+1$ 中至少 $f+1$ 诚实 → $n \ge 3f+1$

---

## 7. 弱化下界

### 7.1 同步 + 数字签名

数字签名让"叛变"节点无法伪造他人消息。下界仍 $n \ge 3f+1$(可防止拜占庭节点撒谎,但不能阻止"沉默")。

### 7.2 异步 + Byzantine

异步拜占庭共识需更强条件:

- **Bracha-Toueg 1985**:异步 BFT 需 $n \ge 3f+1$
- 加上随机化可期望终止(Ben-Or)

### 7.3 拜占庭 + Crash 不区分

实际系统中"慢"和"拜占庭"难区分 → 工程上往往用超时(假设同步),达到 $3f+1$。

---

## 8. 工业实践

### 8.1 联盟链

- Hyperledger Fabric PBFT:$n \ge 3f+1$
- 4 节点容忍 1 拜占庭,7 节点容忍 2

### 8.2 公链

- PoW/PoS:替代 BFT,$n$ 可无界
- 公链牺牲 finality(概率最终一致)
- 详见 [[05-共识-拜占庭共识]]

### 8.3 工程折衷

- 多数工程系统假设"Crash + 慢节点"而非纯 Byzantine
- 用 $2f+1$ + 故障检测器,简化部署
- 风险:有恶意节点时失效(如内部威胁)

---

## 9. 速查表

```
Byzantine 下界:
  同步网络 + 拜占庭 f:
    n >= 3f + 1 (必要性)
    n >= 3f + 1 (充分性, PBFT/HotStuff)

证明思路:
  基础: n=3, f=1 不可解 (诚实节点无法分辨 C 拜占庭)
  归纳: n<=3f 不可解 (拆组 + 模拟 3 节点)

vs Crash:
  Crash: n >= 2f+1, Quorum f+1 (Paxos/Raft)
  Byzantine: n >= 3f+1, Quorum 2f+1 (PBFT)
  差异: 拜占庭"撒谎"需 Quorum 交集有诚实节点

工业:
  联盟链: 4 容 1, 7 容 2 (PBFT/HotStuff)
  公链: PoW/PoS 替代 BFT
  假设同步: 超时检测故障
```

---

## 10. 交叉引用

- [[05-共识-拜占庭共识]]:PBFT/HotStuff 算法
- [[05-共识-问题与FLP]]:共识问题与故障模型
- [[21-1-学术附录-FLP证明]]:FLP 在异步下的限制
- [[22-工业案例库]]:联盟链案例

---

## 11. 参考文献

- Lamport, Shostak, Pease. *The Byzantine Generals Problem*. ACM TOPLAS 1982.
- Pease, Shostak, Lamport. *Reaching Agreement in the Presence of Faults*. JACM 1980.
- Dolev, Strong. *Authenticated Algorithms for Byzantine Agreement*. SICOMP 1983.
- Fischer. *The Consensus Problem in Unreliable Distributed Systems*. 1983.
- Castro, Liskov. *Practical Byzantine Fault Tolerance*. OSDI 1999. (PBFT)
- Yin et al. *HotStuff: BFT Consensus with Linearity and Responsiveness*. PODC 2019.
- Bracha, Toueg. *Asynchronous Consensus and Broadcast Protocols*. JACM 1985.

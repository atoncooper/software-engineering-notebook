# 第 6 章 分布式事务（二）—— TCC 与 Saga

> **本章导读**
> 2PC 的同步阻塞使其无法支撑高并发互联网场景。TCC（Try-Confirm-Cancel）通过业务侵入——把一个操作拆成「资源预留 → 确认 → 取消」三步——实现最终一致 + 高性能，是支付宝、微信支付的核心方案。Saga 把长事务拆成多个本地事务 + 补偿事务，用「正向 + 反向」回滚序列处理失败。两者都牺牲了隔离性，换来高吞吐。本章用 Java/Go 代码展示完整实现。
>
> **学完能回答**：
> 1. TCC 三阶段各做什么？为什么需要业务侵入？
> 2. TCC 的三大陷阱（空回滚、悬挂、幂等）各是什么？怎么解决？
> 3. Saga 的编排式 vs 协同式区别？
> 4. Saga 的补偿事务设计原则？
> 5. TCC 和 Saga 怎么选？
>
> **前置**：[06-事务-2PC与3PC](./06-事务-2PC与3PC.md) · **预计时长**：4-5 小时 · **标记**：⭐🔥

---

## 6.0 章节地图（本文件）

```
              TCC 与 Saga
                  │
        ┌─────────┴─────────┐
        │                   │
      TCC                Saga
        │                   │
   ┌────┼────┐        ┌────┴────┐
  Try Confirm Cancel  编排式    协同式
        │                   │
   ┌────┼────┐         补偿事务设计
  空回滚 悬挂 幂等        隔离性
```

- 6.1 TCC 概述
- 6.2 TCC 三阶段
- 6.3 TCC 三大陷阱 ⭐
- 6.4 TCC 代码实现
- 6.5 Saga 概述
- 6.6 Saga 两种实现模式
- 6.7 Saga 补偿设计
- 6.8 Saga 代码实现
- 6.9 🏭 工业实战
- 6.10 面试要点
- 6.11 论文与延伸阅读

---

## 6.1 TCC 概述

📜 [Pat Helland 2007] *Life beyond Distributed Transactions*

### 6.1.1 动机

2PC 的根本问题：

- **同步阻塞**：持锁等待
- **业务无感知**：DB 层 2PC，但应用层逻辑无法介入

TCC 的思路：

> **把「提交」这件事交给业务**——业务把一个操作拆成三步：Try（预留资源）、Confirm（确认提交）、Cancel（取消预留）。

### 6.1.2 与 2PC 的本质区别

| 维度 | 2PC | TCC |
|------|-----|-----|
| 隔离层面 | DB 层（锁） | 业务层（预留） |
| 业务侵入 | 无 | 有（必须实现 Try/Confirm/Cancel） |
| 阻塞 | 持锁阻塞 | 无锁（业务层预留） |
| 性能 | 差 | 好 |
| 一致性 | 强一致 | 最终一致 |
| 隔离性 | 强 | 弱（业务需自行保证） |

### 6.1.3 适用场景

- 高并发互联网业务（支付、订单、库存）
- 跨服务事务（微服务架构）
- 业务可拆分为「预留 + 确认/取消」

---

## 6.2 TCC 三阶段 ⭐

### 6.2.1 三阶段定义

| 阶段 | 作用 | 失败处理 |
|------|------|---------|
| **Try** | 资源预留（冻结余额、预占库存） | 失败则全局 Cancel |
| **Confirm** | 确认提交（扣减冻结、扣减库存） | 失败重试（最终必成功） |
| **Cancel** | 取消预留（解冻余额、释放库存） | 失败重试（最终必成功） |

### 6.2.2 示例：转账

**业务**：A 向 B 转 100 元

| 阶段 | A 账户 | B 账户 |
|------|--------|--------|
| **Try** | 余额 -100，冻结 +100 | 校验存在（不操作） |
| **Confirm** | 冻结 -100 | 余额 +100 |
| **Cancel** | 余额 +100，冻结 -100 | 无操作 |

### 6.2.3 状态机

```
              Try 成功
   INIT ──────────────► CONFIRMING ────► CONFIRMED
     │                                      ▲
     │ Try 失败                              │
     ▼                                      │
  CANCELLING ─────────► CANCELLED           │
                                              │
   全局决策：全 Try 成功 → Confirm            │
            任一 Try 失败 → Cancel ───────────┘
```

### 6.2.4 关键约束

1. **Try 必须先于 Confirm/Cancel**
2. **Confirm 和 Cancel 互斥**（同一事务只执行一个）
3. **Confirm/Cancel 必须幂等**（重试安全）
4. **Try 可失败**，Confirm/Cancel 必须最终成功

---

## 6.3 TCC 三大陷阱 ⭐🔥

### 6.3.1 空回滚（Empty Rollback）

> **问题**：Try 未执行（如网络丢包），但 Cancel 被调用。

**场景**：

```
Coordinator ──Try──► Service A（成功）
Coordinator ──Try──► Service B（网络丢包，未到达）
Coordinator: Try B 超时 → 全局 Cancel
Coordinator ──Cancel──► Service A（正常回滚）
Coordinator ──Cancel──► Service B（空回滚！B 从未 Try）
```

**解决**：Cancel 前检查 Try 是否执行过。

```java
public void cancel(String txId) {
    // 检查是否有 Try 记录
    TccTransaction tcc = tccRepo.findByTxId(txId);
    if (tcc == null) {
        // 空回滚：插入「已 Cancel」记录，标记 Try 未发生
        tccRepo.insert(new TccTransaction(txId, State.CANCELLED, "empty_rollback"));
        return;
    }
    if (tcc.getState() == State.CONFIRMED) {
        return;  // 已 Confirm，不能 Cancel
    }
    // 正常 Cancel
    doCancel(tcc);
    tccRepo.updateState(txId, State.CANCELLED);
}
```

### 6.3.2 悬挂（Suspend）

> **问题**：Cancel 先于 Try 到达。

**场景**：

```
Coordinator ──Try──► Service B（网络延迟，迟迟未到）
Coordinator: Try B 超时 → 全局 Cancel
Coordinator ──Cancel──► Service B（先到达，空回滚）
Coordinator ──Try──► Service B（延迟到达，但事务已 Cancel）
→ Try 资源永久悬挂
```

**解决**：Try 前检查是否已 Cancel。

```java
public void try(String txId, Resource resource) {
    TccTransaction tcc = tccRepo.findByTxId(txId);
    if (tcc != null && tcc.getState() == State.CANCELLED) {
        // 已 Cancel，Try 拒绝（避免悬挂）
        return;
    }
    // 正常 Try
    doTry(resource);
    tccRepo.insert(new TccTransaction(txId, State.TRYING));
}
```

### 6.3.3 幂等（Idempotency）

> **问题**：Confirm/Cancel 因网络重试，可能被多次调用。

**解决**：用事务 ID + 状态机保证幂等。

```java
public void confirm(String txId) {
    TccTransaction tcc = tccRepo.findByTxId(txId);
    if (tcc == null) {
        throw new IllegalStateException("Try not executed");
    }
    if (tcc.getState() == State.CONFIRMED) {
        return;  // 幂等返回
    }
    if (tcc.getState() == State.CANCELLED) {
        throw new IllegalStateException("Already cancelled");
    }
    // 正常 Confirm
    doConfirm(tcc);
    tccRepo.updateState(txId, State.CONFIRMED);  // CAS 更新，防并发
}
```

### 6.3.3 三大陷阱对照表

| 陷阱 | 触发条件 | 解决方案 |
|------|---------|---------|
| **空回滚** | Try 未执行，Cancel 被调用 | Cancel 前检查 Try 记录 |
| **悬挂** | Cancel 先于 Try 到达 | Try 前检查 Cancel 记录 |
| **幂等** | Confirm/Cancel 重试 | 事务 ID + 状态机 |

🔥 **生产必备**：TCC 实现必须同时解决三大陷阱，否则数据会错乱。

---

## 6.4 TCC 代码实现

### 6.4.1 接口定义（Java）

```java
public interface TccAction<T> {
    /** Try：资源预留 */
    boolean tryAction(String txId, T param);
    
    /** Confirm：确认提交 */
    boolean confirmAction(String txId);
    
    /** Cancel：取消预留 */
    boolean cancelAction(String txId);
}

// 转账业务的扣款方
@Component
public class DeductAccountAction implements TccAction<TransferRequest> {
    @Autowired
    private AccountMapper accountMapper;
    
    @Autowired
    private TccTransactionMapper tccMapper;
    
    @Override
    public boolean tryAction(String txId, TransferRequest req) {
        // 1. 防悬挂：检查是否已 Cancel
        TccTransaction tcc = tccMapper.findByTxId(txId);
        if (tcc != null && tcc.getState() == State.CANCELLED) {
            return false;  // 已 Cancel，拒绝 Try
        }
        
        // 2. 业务检查
        Account account = accountMapper.selectById(req.getFromId());
        if (account.getBalance() < req.getAmount()) {
            return false;  // 余额不足
        }
        
        // 3. 资源预留：冻结余额
        accountMapper.freeze(req.getFromId(), req.getAmount());
        
        // 4. 记录 TCC 状态（持久化）
        tccMapper.insert(new TccTransaction(
            txId, State.TRYING, req.getFromId(), req.getAmount()
        ));
        
        return true;
    }
    
    @Override
    public boolean confirmAction(String txId) {
        // 幂等检查
        TccTransaction tcc = tccMapper.findByTxId(txId);
        if (tcc == null) return false;
        if (tcc.getState() == State.CONFIRMED) return true;  // 幂等
        if (tcc.getState() == State.CANCELLED) return false;  // 已 Cancel
        
        // 扣减冻结
        accountMapper.deductFrozen(tcc.getAccountId(), tcc.getAmount());
        
        // CAS 更新状态（防并发）
        int updated = tccMapper.casUpdateState(txId, State.TRYING, State.CONFIRMED);
        return updated > 0;
    }
    
    @Override
    public boolean cancelAction(String txId) {
        TccTransaction tcc = tccMapper.findByTxId(txId);
        
        // 空回滚：Try 未执行
        if (tcc == null) {
            tccMapper.insert(new TccTransaction(txId, State.CANCELLED, "empty_rollback"));
            return true;
        }
        
        // 幂等
        if (tcc.getState() == State.CANCELLED) return true;
        if (tcc.getState() == State.CONFIRMED) return false;
        
        // 解冻
        accountMapper.unfreeze(tcc.getAccountId(), tcc.getAmount());
        
        // CAS 更新状态
        int updated = tccMapper.casUpdateState(txId, State.TRYING, State.CANCELLED);
        return updated > 0;
    }
}
```

### 6.4.2 协调者（Coordinator）

```java
public class TccCoordinator {
    @Autowired
    private List<TccAction<?>> actions;
    
    @Autowired
    private GlobalTransactionMapper globalTxMapper;
    
    public GlobalTxResult execute(GlobalTransaction globalTx) {
        String txId = globalTx.getId();
        
        // Phase 1: Try
        List<TccAction<?>> succeeded = new ArrayList<>();
        boolean allTrySuccess = true;
        
        for (TccAction<?> action : actions) {
            try {
                boolean ok = action.tryAction(txId, globalTx.getParam(action));
                if (!ok) {
                    allTrySuccess = false;
                    break;
                }
                succeeded.add(action);
            } catch (Exception e) {
                allTrySuccess = false;
                break;
            }
        }
        
        // Phase 2: Confirm 或 Cancel
        if (allTrySuccess) {
            globalTxMapper.persistDecision(txId, Decision.CONFIRM);
            return confirmAll(txId);
        } else {
            globalTxMapper.persistDecision(txId, Decision.CANCEL);
            return cancelAll(txId, succeeded);
        }
    }
    
    private GlobalTxResult confirmAll(String txId) {
        // 重试直到全部成功
        for (TccAction<?> action : actions) {
            while (true) {
                try {
                    if (action.confirmAction(txId)) break;
                } catch (Exception e) {
                    log.warn("Confirm failed, retry", e);
                }
                sleep(retryInterval);
            }
        }
        return GlobalTxResult.success();
    }
    
    private GlobalTxResult cancelAll(String txId, List<TccAction<?>> toCancel) {
        for (TccAction<?> action : toCancel) {
            while (true) {
                try {
                    if (action.cancelAction(txId)) break;
                } catch (Exception e) {
                    log.warn("Cancel failed, retry", e);
                }
                sleep(retryInterval);
            }
        }
        return GlobalTxResult.cancelled();
    }
}
```

### 6.4.3 数据库表设计

```sql
-- 全局事务表
CREATE TABLE global_transaction (
    tx_id VARCHAR(64) PRIMARY KEY,
    status VARCHAR(20),  -- TRYING / CONFIRMING / CANCELLING / DONE
    created_at TIMESTAMP,
    decision VARCHAR(10)  -- CONFIRM / CANCEL
);

-- 分支事务表（每个 TCC Action 一条）
CREATE TABLE tcc_transaction (
    tx_id VARCHAR(64),
    branch_id VARCHAR(64),
    action_name VARCHAR(100),
    state VARCHAR(20),  -- TRYING / CONFIRMED / CANCELLED
    business_key VARCHAR(100),
    amount DECIMAL(20, 2),
    created_at TIMESTAMP,
    PRIMARY KEY (tx_id, branch_id),
    INDEX idx_state (state)
);
```

### 6.4.4 Go 风格实现

```go
// TCC Action 接口
type TccAction interface {
    Try(txID string, param interface{}) error
    Confirm(txID string) error
    Cancel(txID string) error
}

// 账户扣款 Action
type DeductAction struct {
    db *sql.DB
}

func (a *DeductAction) Try(txID string, param interface{}) error {
    req := param.(*TransferRequest)
    
    // 防悬挂
    var state string
    err := a.db.QueryRow("SELECT state FROM tcc_tx WHERE tx_id=?", txID).Scan(&state)
    if err == nil && state == "CANCELLED" {
        return errors.New("already cancelled (suspend)")
    }
    
    // 资源预留
    _, err = a.db.Exec(
        "UPDATE account SET balance=balance-?, frozen=frozen+? WHERE id=? AND balance>=?",
        req.Amount, req.Amount, req.FromID, req.Amount,
    )
    if err != nil {
        return err
    }
    
    // 记录 TCC 状态
    _, err = a.db.Exec(
        "INSERT INTO tcc_tx (tx_id, state, account_id, amount) VALUES (?, 'TRYING', ?, ?)",
        txID, req.FromID, req.Amount,
    )
    return err
}

func (a *DeductAction) Confirm(txID string) error {
    // 幂等检查
    var state string
    err := a.db.QueryRow("SELECT state FROM tcc_tx WHERE tx_id=?", txID).Scan(&state)
    if err != nil {
        return err
    }
    if state == "CONFIRMED" {
        return nil  // 幂等
    }
    
    // 扣减冻结
    _, err = a.db.Exec(
        "UPDATE account SET frozen=frozen-? WHERE id=(SELECT account_id FROM tcc_tx WHERE tx_id=?)",
        /* amount */, txID,
    )
    if err != nil {
        return err
    }
    
    // CAS 更新状态
    _, err = a.db.Exec(
        "UPDATE tcc_tx SET state='CONFIRMED' WHERE tx_id=? AND state='TRYING'",
        txID,
    )
    return err
}
```

---

## 6.5 Saga 概述

📜 [Hector Garcia-Molina, Salem 1987] *Sagas*

### 6.5.1 动机

> 长事务（持续数分钟到数小时）不适合 2PC（持锁太久）。Saga 把长事务拆成多个**本地事务** $T_1, T_2, \dots, T_n$，每个 $T_i$ 有对应的**补偿事务** $C_i$，失败时反向执行补偿。

### 6.5.2 定义

> **Saga**：序列 $T_1, T_2, \dots, T_n$，其中 $T_i$ 是本地事务，$C_i$ 是 $T_i$ 的补偿事务。
> - 成功：$T_1 \to T_2 \to \dots \to T_n$
> - 失败（$T_k$ 失败）：$T_1 \to \dots \to T_{k-1} \to C_{k-1} \to \dots \to C_1$

### 6.5.3 与 TCC 的区别

| 维度 | TCC | Saga |
|------|-----|------|
| 阶段 | Try/Confirm/Cancel | 一串本地事务 + 补偿 |
| 隔离性 | 业务层预留（较强） | 无预留（弱） |
| 业务侵入 | 三接口 | 两接口（执行 + 补偿） |
| 适合 | 短事务、强隔离 | 长事务、跨服务编排 |
| 性能 | 中 | 高 |

### 6.5.4 示例：电商下单

```
T1: 创建订单
T2: 扣减库存
T3: 扣减余额
T4: 发货

若 T3 失败：
  C2: 恢复库存
  C1: 取消订单
  （不执行 T4）
```

---

## 6.6 Saga 两种实现模式 ⭐

### 6.6.1 编排式（Orchestration）

> 中央协调器（Orchestrator）控制流程，各服务被动响应。

```
              Orchestrator
                  │
        ┌─────────┼─────────┐
        │         │         │
       T1        T2        T3
     (Order)  (Stock)   (Pay)
```

**优点**：

- 流程清晰，集中管理
- 易监控、易调试
- 状态机显式

**缺点**：

- Orchestrator 单点（需 HA）
- 业务逻辑集中在 Orchestrator

**适用**：复杂流程、强一致需求

### 6.6.2 协同式（Choreography）

> 各服务订阅事件，自行决策。无中央协调器。

```
   Order Service ──OrderCreated──► Stock Service
                                       │
                                       ▼
                              ──StockReserved──► Pay Service
                                                       │
                                                       ▼
                                              ──PaySuccess──► Ship Service
```

**优点**：

- 去中心化，无单点
- 服务自治
- 适合事件驱动架构

**缺点**：

- 流程隐式，难追踪
- 循环依赖风险
- 调试困难

**适用**：简单流程、事件驱动架构

### 6.6.3 对比

| 维度 | 编排式 | 协同式 |
|------|--------|--------|
| 协调器 | 有（中央） | 无 |
| 流程可见性 | 强 | 弱 |
| 单点风险 | 有 | 无 |
| 调试 | 易 | 难 |
| 业务耦合 | 集中 | 分散 |
| 典型框架 | Seata Saga、Camunda | Eventuate Tram |

---

## 6.7 Saga 补偿设计 ⭐

### 6.7.1 补偿事务的原则

1. **语义补偿**：不是物理回滚，而是「反向业务操作」
2. **必须成功**：补偿失败需重试，最终必成功
3. **幂等**：补偿可能重试
4. **不可补偿的操作**：如发邮件、发短信，需特殊处理

### 6.7.2 补偿的边界

| 操作 | 可补偿？ | 说明 |
|------|---------|------|
| 插入记录 | ✅ | 删除记录 |
| 更新字段 | ✅ | 恢复原值（需记录原值） |
| 扣减余额 | ✅ | 增加余额 |
| 发送邮件 | ❌ | 无法撤回，需「补偿邮件」 |
| 调用外部 API | ❌ | 取决于外部 API |
| 创建订单 | ✅ | 标记为「已取消」 |

### 6.7.3 不可补偿操作的处理

- **延迟执行**：先记录意图，最后执行
- **可逆化设计**：发邮件 → 发「撤回邮件」
- **人工介入**：标记异常，人工处理

### 6.7.4 补偿顺序

> 反向执行：$C_{k-1}, C_{k-2}, \dots, C_1$

```java
public void compensate(List<SagaStep> executedSteps) {
    // 反向遍历
    for (int i = executedSteps.size() - 1; i >= 0; i--) {
        SagaStep step = executedSteps.get(i);
        while (true) {
            try {
                step.compensate();
                break;
            } catch (Exception e) {
                log.warn("Compensate failed, retry", e);
                sleep(retryInterval);
            }
        }
    }
}
```

---

## 6.8 Saga 代码实现

### 6.8.1 编排式（Java）

```java
// Saga 步骤定义
public class SagaStep {
    private String name;
    private Runnable action;
    private Runnable compensation;
    
    public SagaStep(String name, Runnable action, Runnable compensation) {
        this.name = name;
        this.action = action;
        this.compensation = compensation;
    }
    
    public void execute() { action.run(); }
    public void compensate() { compensation.run(); }
}

// Saga 协调器
public class SagaOrchestrator {
    private List<SagaStep> steps = new ArrayList<>();
    private List<SagaStep> executed = new ArrayList<>();
    
    public SagaOrchestrator step(String name, Runnable action, Runnable comp) {
        steps.add(new SagaStep(name, action, comp));
        return this;
    }
    
    public void execute() {
        try {
            for (SagaStep step : steps) {
                step.execute();
                executed.add(step);
            }
        } catch (Exception e) {
            log.error("Saga failed at step, compensating", e);
            compensate();
            throw e;
        }
    }
    
    private void compensate() {
        // 反向补偿
        for (int i = executed.size() - 1; i >= 0; i--) {
            SagaStep step = executed.get(i);
            try {
                step.compensate();
            } catch (Exception e) {
                log.error("Compensate failed for step: " + step.name, e);
                // 重试或人工介入
            }
        }
    }
}

// 使用示例
public class OrderService {
    public void createOrder(Order order) {
        new SagaOrchestrator()
            .step("create_order",
                () -> orderRepo.insert(order),
                () -> orderRepo.updateStatus(order.getId(), "CANCELLED"))
            .step("deduct_stock",
                () -> stockService.deduct(order.getSkus()),
                () -> stockService.restore(order.getSkus()))
            .step("deduct_balance",
                () -> accountService.deduct(order.getUserId(), order.getAmount()),
                () -> accountService.credit(order.getUserId(), order.getAmount()))
            .step("ship",
                () -> shipService.ship(order),
                () -> shipService.cancel(order.getId()))
            .execute();
    }
}
```

### 6.8.2 协同式（事件驱动，Go）

```go
// 订单服务
func (s *OrderService) CreateOrder(req *CreateOrderRequest) error {
    order := NewOrder(req)
    if err := s.db.Save(order); err != nil {
        return err
    }
    // 发布事件，不直接调用其他服务
    return s.eventBus.Publish("order.created", order)
}

// 库存服务订阅 order.created
func (s *StockService) OnOrderCreated(order *Order) {
    if err := s.deduct(order.SKUs); err != nil {
        s.eventBus.Publish("stock.deduct_failed", order.ID)
        return;
    }
    s.eventBus.Publish("stock.reserved", order.ID)
}

// 支付服务订阅 stock.reserved
func (s *PayService) OnStockReserved(orderID string) {
    if err := s.pay(orderID); err != nil {
        s.eventBus.Publish("pay.failed", orderID)
        return;
    }
    s.eventBus.Publish("pay.success", orderID)
}

// 失败时反向补偿
// 库存服务订阅 pay.failed → 恢复库存 + 发布 stock.restored
// 订单服务订阅 stock.restored → 取消订单
```

---

## 6.9 🏭 工业实战

### 6.9.1 支付宝 TCC

- 早期支付核心方案
- Try：冻结余额
- Confirm：扣减冻结
- Cancel：解冻
- 三大陷阱全部解决

### 6.9.2 Seata TCC 模式

```java
@LocalTCC
public interface AccountTccAction {
    @TwoPhaseBusinessAction(name = "deductAccount",
        commitMethod = "confirm",
        rollbackMethod = "cancel")
    boolean tryDeduct(
        @BusinessActionContextParameter(paramName = "userId") Long userId,
        @BusinessActionContextParameter(paramName = "amount") BigDecimal amount
    );
    
    boolean confirm(BusinessActionContext ctx);
    boolean cancel(BusinessActionContext ctx);
}
```

### 6.9.3 Seata Saga 模式

- 基于 State Machine（状态机）
- JSON 定义流程
- 可视化编排

```json
{
  "Name": "transferMoney",
  "States": {
    "Reduce": {
      "Type": "ServiceTask",
      "ServiceName": "accountService",
      "ServiceMethod": "reduce",
      "CompensateState": "CompensateReduce"
    },
    "CompensateReduce": {
      "Type": "ServiceTask",
      "ServiceName": "accountService",
      "ServiceMethod": "compensateReduce"
    }
  }
}
```

### 6.9.4 ⚠️ 工业陷阱

- ⚠️ **TCC 业务侵入大**：每个接口需实现三方法
- ⚠️ **Saga 隔离性弱**：中间状态可见，需业务容忍
- ⚠️ **补偿失败**：需重试 + 告警 + 人工介入
- ⚠️ **不可补偿操作**：发邮件、发短信需特殊设计
- 🔥 **生产建议**：

  - 短事务、强一致 → TCC
  - 长事务、跨服务 → Saga
  - 配合幂等性（详见 [§12](./12-幂等性.md)）
  - 监控补偿成功率

---

## 6.10 面试要点

### ⭐ Q1：TCC 三阶段各做什么？

**骨架**：

1. **Try**：资源预留（冻结余额、预占库存）
2. **Confirm**：确认提交（扣减冻结）
3. **Cancel**：取消预留（解冻）

**关键点**：业务层预留 + 确认/取消

**加分项**：提到业务侵入是 TCC 的核心特征

### ⭐ Q2：TCC 的三大陷阱是什么？

**骨架**：

1. **空回滚**：Try 未执行，Cancel 被调用 → Cancel 前检查 Try 记录
2. **悬挂**：Cancel 先于 Try → Try 前检查 Cancel 记录
3. **幂等**：Confirm/Cancel 重试 → 事务 ID + 状态机

**关键点**：三个陷阱 + 解决方案

**加分项**：举例说明每个陷阱的触发场景

### ⭐ Q3：TCC 和 2PC 的本质区别？

**骨架**：

1. 2PC 在 DB 层（锁），TCC 在业务层（预留）
2. 2PC 强一致强隔离，TCC 最终一致弱隔离
3. 2PC 同步阻塞，TCC 无锁高性能
4. 2PC 业务无侵入，TCC 业务侵入

**关键点**：业务层 vs DB 层

**加分项**：提到 TCC 适合高并发互联网场景

### ⭐ Q4：Saga 是什么？

**骨架**：

1. 长事务拆成多个本地事务 + 补偿
2. 成功：$T_1 \to T_2 \to \dots \to T_n$
3. 失败：反向补偿 $C_{k-1} \to \dots \to C_1$
4. 适合长事务、跨服务

**关键点**：本地事务 + 补偿

**加分项**：提到与 TCC 的区别

### ⭐ Q5：Saga 编排式 vs 协同式？

**骨架**：

1. **编排式**：中央协调器，流程清晰，单点风险
2. **协同式**：事件驱动，去中心化，难调试
3. 复杂流程选编排，简单事件驱动选协同

**关键点**：中央 vs 去中心

**加分项**：举例框架（Seata Saga 编排、Eventuate 协同）

### ⭐ Q6：Saga 的补偿事务怎么设计？

**骨架**：

1. 语义补偿（反向业务操作），非物理回滚
2. 必须最终成功
3. 幂等
4. 不可补偿操作特殊处理

**关键点**：语义补偿 + 必须成功

**加分项**：举例发邮件如何「补偿」

### ⭐ Q7：TCC 和 Saga 怎么选？

**骨架**：

1. 短事务、强隔离 → TCC
2. 长事务、跨服务 → Saga
3. 高并发互联网 → TCC
4. 复杂业务流程 → Saga

**关键点**：事务长度 + 隔离需求

**加分项**：提到 Seata 支持 XA/TCC/Saga/AT 四种模式

### ⭐ Q8：TCC 的 Confirm/Cancel 为什么要幂等？

**骨架**：

1. 网络重试可能导致多次调用
2. 多次 Confirm 不能重复扣减
3. 多次 Cancel 不能重复解冻
4. 用事务 ID + 状态机保证

**关键点**：网络重试 + 状态机

**加分项**：提到 CAS 更新防并发

### ⭐ Q9：Saga 中如何处理「不可补偿」操作？

**骨架**：

1. 延迟执行：先记录意图，最后执行
2. 可逆化：发「撤回邮件」
3. 人工介入：标记异常

**关键点**：延迟 + 可逆化 + 人工

**加分项**：举例发短信、外部 API 调用

### ⭐ Q10：TCC 业务侵入大，有没有更轻量方案？

**骨架**：

1. AT 模式（Seata）：自动生成反向 SQL，业务无侵入
2. 但 AT 仅支持关系型 DB
3. 跨服务仍需 TCC/Saga

**关键点**：AT 模式作为折衷

**加分项**：提到 AT 的原理（解析 SQL 生成 undo log）

---

## 6.11 论文与延伸阅读

### 📜 经典论文

- 📜 [Garcia-Molina, Salem 1987] *Sagas* —— Saga 原始论文
- 📜 [Pat Helland 2007] *Life beyond Distributed Transactions* —— TCC 思想来源

### 🔗 教材与资料

- 📖 DDIA 第 11 章「流处理」（事件驱动 Saga）
- 🔗 [Seata TCC 文档](https://seata.io/zh-cn/docs/user-manual/tcc-mode.html)（访问于 2026-06-23）
- 🔗 [Seata Saga 文档](https://seata.io/zh-cn/docs/user-manual/saga-mode.html)（访问于 2026-06-23）

---

## 本章 TODO

- [ ] 补充图 6-4：TCC 状态机
- [ ] 补充图 6-5：Saga 编排式 vs 协同式对比
- [ ] 补充图 6-6：TCC 三大陷阱时序图
- [ ] 核对 Seata TCC 注解最新写法
- [ ] 交叉引用 [§12 幂等性](./12-幂等性.md)

## 交叉引用

- **前置**：[06-事务-2PC与3PC](./06-事务-2PC与3PC.md)
- **后续**：[06-事务-消息事务与Outbox](./06-事务-消息事务与Outbox.md)
- **后续**：[06-事务-Percolator与Spanner](./06-事务-Percolator与Spanner.md)
- **后续**：[06-事务-工业实战与选型](./06-事务-工业实战与选型.md)
- **横向**：[§12 幂等性](./12-幂等性.md)
- **横向**：[§9 MQ](./09-MQ-基础与投递语义.md)（事件驱动 Saga）

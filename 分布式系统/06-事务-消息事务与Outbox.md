# 第 6 章 分布式事务（三）—— 消息事务与 Outbox Pattern

> **本章导读**
> 跨服务事务最常见的场景是「写 DB + 发 MQ」——既要保证数据落库，又要保证消息可靠投递。直接「先写 DB 后发 MQ」会有消息丢失问题，「先发 MQ 后写 DB」会有消息多发问题。本地消息表、RocketMQ 事务消息、Outbox Pattern + CDC 是三种工程化解法，核心思想都是「让 DB 写与消息发送原子化」。本章用 Java/Go 代码完整实现三种方案。
>
> **学完能回答**：
> 1. 「先写 DB 后发 MQ」和「先发 MQ 后写 DB」各有什么问题？
> 2. 本地消息表的原理？
> 3. RocketMQ 事务消息的两阶段流程？回查机制？
> 4. Outbox Pattern + CDC 相比本地消息表的优势？
> 5. 三种方案如何选型？
>
> **前置**：[06-事务-2PC与3PC](./06-事务-2PC与3PC.md)、[06-事务-TCC与Saga](./06-事务-TCC与Saga.md) · **预计时长**：3-4 小时 · **标记**：⭐🔥

---

## 6.0 章节地图（本文件）

```
           消息事务
              │
   ┌──────────┼──────────┐
   │          │          │
 本地消息表  RocketMQ   Outbox + CDC
   │        事务消息       │
   │          │          │
 业务表 +  Half Message  Debezium
 消息表同   + 回查       解析 binlog
 一事务                  发 Kafka
```

- 6.1 问题背景
- 6.2 本地消息表
- 6.3 RocketMQ 事务消息
- 6.4 Outbox Pattern + CDC
- 6.5 三种方案对比
- 6.6 🏭 工业实战
- 6.7 面试要点
- 6.8 论文与延伸阅读

---

## 6.1 问题背景

### 6.1.1 经典场景

> **写 DB + 发 MQ**：业务操作要落库，同时发消息通知下游服务。

**例**：电商下单

1. 写订单表（DB）
2. 发「订单创建」消息（MQ）
3. 库存服务消费消息扣库存

### 6.1.2 朴素方案的问题 ⚠️

#### 方案 A：先写 DB，后发 MQ

```java
@Transactional
public void createOrder(Order order) {
    orderRepo.insert(order);   // 写 DB
    mqProducer.send(order);    // 发 MQ
}
```

**问题**：

- DB 写成功，MQ 发送失败 → 数据落库但下游不知
- 应用崩溃在两步之间 → 数据落库但消息丢失

#### 方案 B：先发 MQ，后写 DB

```java
public void createOrder(Order order) {
    mqProducer.send(order);    // 发 MQ
    orderRepo.insert(order);   // 写 DB
}
```

**问题**：

- MQ 发送成功，DB 写失败 → 消息已发但数据未落库
- 下游消费消息处理了不存在的订单

#### 方案 C：分布式事务（2PC）

- 性能差，DB + MQ 不支持 XA
- 不实际

### 6.1.3 核心矛盾

> **DB 与 MQ 是两个独立系统，无法原生原子化**。需要工程方案让两者「最终一致」。

---

## 6.2 本地消息表 ⭐🔥

### 6.2.1 思路

> 把「发消息」这件事变成「写本地消息表」，与业务表在同一 DB 事务中原子提交。后台任务扫描消息表，发送到 MQ，发送成功后标记。

### 6.2.2 架构

```
   应用层
     │
     ▼
  ┌─────────────────────┐
  │ DB Transaction      │
  │  ┌──────────────┐   │
  │  │ business_tbl │   │  业务数据
  │  └──────────────┘   │
  │  ┌──────────────┐   │
  │  │ message_tbl  │   │  消息记录（待发送）
  │  └──────────────┘   │
  └─────────────────────┘
            │
            ▼ (后台扫描)
  ┌─────────────────────┐
  │   MQ (Kafka/RocketMQ)│
  └─────────────────────┘
            │
            ▼
        下游消费者
```

### 6.2.3 流程

1. 业务事务中：写业务表 + 写消息表（同一事务，原子提交）
2. 后台扫描消息表，取「待发送」消息
3. 发送 MQ
4. 发送成功 → 标记消息为「已发送」
5. 下游消费消息

### 6.2.4 Java 实现

```java
@Service
public class OrderService {
    @Autowired
    private OrderMapper orderMapper;
    
    @Autowired
    private MessageMapper messageMapper;
    
    @Transactional
    public void createOrder(Order order) {
        // 1. 写业务表
        orderMapper.insert(order);
        
        // 2. 写消息表（同一事务，原子提交）
        LocalMessage msg = new LocalMessage();
        msg.setId(UUID.randomUUID().toString());
        msg.setTopic("order_created");
        msg.setPayload(JSON.toJSONString(order));
        msg.setStatus(MessageStatus.PENDING);
        msg.setRetryCount(0);
        msg.setCreatedAt(new Date());
        messageMapper.insert(msg);
        
        // 事务提交后，业务表和消息表要么都成功，要么都失败
    }
}

// 后台扫描任务
@Component
public class MessageScheduler {
    @Autowired
    private MessageMapper messageMapper;
    
    @Autowired
    private MQProducer mqProducer;
    
    @Scheduled(fixedDelay = 1000)  // 每秒扫描
    public void sendPendingMessages() {
        List<LocalMessage> messages = messageMapper.findPending(100);
        for (LocalMessage msg : messages) {
            try {
                mqProducer.send(msg.getTopic(), msg.getPayload());
                // 发送成功，标记
                messageMapper.updateStatus(msg.getId(), MessageStatus.SENT);
            } catch (Exception e) {
                // 失败，增加重试次数
                messageMapper.incrementRetry(msg.getId());
                if (msg.getRetryCount() >= MAX_RETRY) {
                    // 超过最大重试，标记为失败，告警
                    messageMapper.updateStatus(msg.getId(), MessageStatus.FAILED);
                    alertService.alert("Message send failed: " + msg.getId());
                }
            }
        }
    }
}
```

### 6.2.5 SQL 表设计

```sql
CREATE TABLE local_message (
    id VARCHAR(64) PRIMARY KEY,
    topic VARCHAR(100) NOT NULL,
    payload TEXT NOT NULL,
    status VARCHAR(20) NOT NULL,  -- PENDING / SENT / FAILED
    retry_count INT DEFAULT 0,
    max_retry INT DEFAULT 5,
    created_at TIMESTAMP,
    sent_at TIMESTAMP,
    INDEX idx_status_created (status, created_at)
);
```

### 6.2.6 优势与劣势

**优势**：

- 实现简单，无需额外中间件
- 强一致：业务与消息表原子
- 可靠：消息最终必发送（重试 + 告警）

**劣势**：

- 业务表与消息表耦合（同一 DB）
- 后台扫描延迟（秒级）
- 消息表数据增长，需定期清理
- 不适合高吞吐（扫描压力）

### 6.2.7 ⚠️ 注意事项

- ⚠️ **消息必须幂等**：消费者需去重（详见 [§12](./12-幂等性.md)）
- ⚠️ **扫描并发控制**：多实例扫描需分布式锁
- ⚠️ **消息表清理**：定期归档已发送消息
- ⚠️ **重试退避**：避免风暴，用指数退避

---

## 6.3 RocketMQ 事务消息 ⭐🔥

### 6.3.1 思路

> RocketMQ 原生支持「事务消息」：先发「半消息」（对消费者不可见），执行本地事务，根据本地事务结果 Commit/Rollback 半消息。

### 6.3.2 两阶段流程 ⭐

```
Producer                  Broker                Consumer
   │                        │                       │
   │ 1. 发送半消息           │                       │
   ├───────────────────────►│                       │
   │                        │ (半消息对消费端不可见) │
   │ 2. 执行本地事务         │                       │
   │ ◄──────────────────────┤                       │
   │                        │                       │
   │ 3a. 本地事务成功 → Commit                       │
   ├───────────────────────►│                       │
   │                        │ 4. 投递消息 ──────────►│
   │                        │                       │
   │ 3b. 本地事务失败 → Rollback                     │
   ├───────────────────────►│                       │
   │                        │ (丢弃半消息)           │
   │                        │                       │
   │  [超时未收到 Commit/Rollback]                   │
   │                        │                       │
   │ 5. 回查本地事务状态     │                       │
   │◄───────────────────────┤                       │
   │ 6. 返回 Commit/Rollback                        │
   ├───────────────────────►│                       │
```

### 6.3.3 关键机制：回查（Check Back）⭐

> 若 Producer 发送半消息后崩溃，未发 Commit/Rollback，Broker 会定期回查 Producer 本地事务状态。

Producer 需实现 `TransactionListener`：

```java
public interface TransactionListener {
    // 执行本地事务
    LocalTransactionState executeLocalTransaction(Message msg, Object arg);
    
    // 回查本地事务状态
    LocalTransactionState checkLocalTransaction(MessageExt msg);
}
```

### 6.3.4 Java 实现

```java
public class OrderTransactionListener implements TransactionListener {
    @Autowired
    private OrderMapper orderMapper;
    
    @Override
    public LocalTransactionState executeLocalTransaction(Message msg, Object arg) {
        Order order = JSON.parseObject(new String(msg.getBody()), Order.class);
        try {
            // 执行本地事务
            orderMapper.insert(order);
            return LocalTransactionState.COMMIT_MESSAGE;
        } catch (Exception e) {
            return LocalTransactionState.ROLLBACK_MESSAGE;
        }
    }
    
    @Override
    public LocalTransactionState checkLocalTransaction(MessageExt msg) {
        // 回查：根据消息 ID 检查订单是否已落库
        String orderId = msg.getKeys();
        Order order = orderMapper.selectById(orderId);
        if (order != null) {
            return LocalTransactionState.COMMIT_MESSAGE;
        } else {
            return LocalTransactionState.ROLLBACK_MESSAGE;
        }
    }
}

// Producer
public class OrderService {
    private TransactionMQProducer producer;
    
    public void createOrder(Order order) throws Exception {
        Message msg = new Message(
            "order_topic",
            JSON.toJSONString(order).getBytes(),
            order.getId()  // 用作回查 key
        );
        
        // 发送事务消息
        producer.sendMessageInTransaction(msg, null);
    }
}
```

### 6.3.5 优势与劣势

**优势**：

- 无需业务表 + 消息表耦合
- RocketMQ 原生支持，可靠性高
- 回查机制处理 Producer 故障

**劣势**：

- 必须 RocketMQ（Kafka 不支持）
- 业务需实现回查逻辑
- 回查依赖本地事务可查（需保留业务记录）

### 6.3.6 ⚠️ 注意事项

- ⚠️ **回查必须幂等**：Broker 可能多次回查
- ⚠️ **回查超时**：默认 60s，需根据业务调整
- ⚠️ **回查数据保留**：业务表需保留足够时间供回查
- ⚠️ **半消息不可见**：半消息对消费者不可见，但占用存储

---

## 6.4 Outbox Pattern + CDC ⭐🔥

### 6.4.1 思路

> Outbox Pattern：业务事务同时写业务表 + Outbox 表（待发消息记录）。CDC（Change Data Capture）组件（如 Debezium）监听 DB binlog，将 Outbox 表变更转发到 Kafka。

### 6.4.2 架构

```
   应用层
     │
     ▼
  ┌─────────────────────────┐
  │ DB Transaction          │
  │  ┌──────────────────┐   │
  │  │ business_tbl     │   │
  │  └──────────────────┘   │
  │  ┌──────────────────┐   │
  │  │ outbox_tbl       │   │  ← CDC 监听此表
  │  └──────────────────┘   │
  └─────────────────────────┘
            │ binlog
            ▼
  ┌─────────────────────────┐
  │ Debezium (CDC)          │
  └────────┬────────────────┘
           │
           ▼
  ┌─────────────────────────┐
  │ Kafka                   │
  └─────────────────────────┘
           │
           ▼
       下游消费者
```

### 6.4.2 与本地消息表的区别

| 维度 | 本地消息表 | Outbox + CDC |
|------|-----------|-------------|
| 消息发送方式 | 应用后台扫描 | CDC 监听 binlog |
| 耦合度 | 应用需扫描逻辑 | 应用无感知 |
| 延迟 | 秒级 | 毫秒级 |
| 性能 | 扫描开销 | binlog 解析 |
| 部署复杂度 | 简单 | 需 CDC 组件 |
| 适合规模 | 中小 | 大规模 |

### 6.4.3 Outbox 表设计

```sql
CREATE TABLE outbox (
    id VARCHAR(64) PRIMARY KEY,
    aggregatetype VARCHAR(100),  -- 主题分类
    aggregateid VARCHAR(100),    -- 聚合 ID
    type VARCHAR(100),           -- 事件类型
    payload TEXT NOT NULL,       -- 事件内容（JSON）
    created_at TIMESTAMP,
    INDEX idx_aggregate (aggregatetype, aggregateid)
);
```

### 6.4.4 Java 实现

```java
@Service
public class OrderService {
    @Autowired
    private OrderMapper orderMapper;
    
    @Autowired
    private OutboxMapper outboxMapper;
    
    @Transactional
    public void createOrder(Order order) {
        // 1. 写业务表
        orderMapper.insert(order);
        
        // 2. 写 outbox 表（同一事务）
        OutboxEvent event = new OutboxEvent();
        event.setId(UUID.randomUUID().toString());
        event.setAggregateType("Order");
        event.setAggregateId(order.getId());
        event.setType("OrderCreated");
        event.setPayload(JSON.toJSONString(order));
        event.setCreatedAt(new Date());
        outboxMapper.insert(event);
        
        // 事务提交后，binlog 包含 outbox 表变更
        // Debezium 自动捕获并转发到 Kafka
    }
}
```

### 6.4.5 Debezium 配置

```yaml
# Debezium MySQL Connector 配置
{
  "name": "order-outbox-connector",
  "config": {
    "connector.class": "io.debezium.connector.mysql.MySqlConnector",
    "database.hostname": "mysql",
    "database.port": "3306",
    "database.user": "debezium",
    "database.password": "xxx",
    "database.server.id": "184054",
    "database.server.name": "order_service",
    "database.include.list": "order_db",
    "table.include.list": "order_db.outbox",
    "database.history.kafka.bootstrap.servers": "kafka:9092",
    "database.history.kafka.topic": "schema-changes.order"
  }
}
```

### 6.4.6 Outbox Event Router（Debezium 插件）

> Debezium 默认会把 `outbox` 表的 INSERT 转发为带元数据的 Kafka 消息。Event Router SMT（Single Message Transformation）可将消息转为业务事件格式。

```json
{
  "transforms": "outbox",
  "transforms.outbox.type": "io.debezium.transforms.outbox.EventRouter",
  "transforms.outbox.table.field.event.id": "id",
  "transforms.outbox.table.field.event.key": "aggregateid",
  "transforms.outbox.table.field.event.type": "type",
  "transforms.outbox.table.field.event.payload": "payload",
  "transforms.outbox.route.by.field": "aggregatetype",
  "transforms.outbox.route.topic.replacement": "outbox.${routedByValue}"
}
```

### 6.4.7 Go 实现

```go
// 业务事务
func (s *OrderService) CreateOrder(req *CreateOrderRequest) error {
    return s.db.Transaction(func(tx *sql.Tx) error {
        // 1. 写业务表
        order := NewOrder(req)
        if _, err := tx.Exec(
            "INSERT INTO orders (id, user_id, amount) VALUES (?, ?, ?)",
            order.ID, order.UserID, order.Amount,
        ); err != nil {
            return err
        }
        
        // 2. 写 outbox 表
        event := NewOutboxEvent("Order", order.ID, "OrderCreated", order)
        if _, err := tx.Exec(
            "INSERT INTO outbox (id, aggregatetype, aggregateid, type, payload) VALUES (?, ?, ?, ?, ?)",
            event.ID, event.AggregateType, event.AggregateID, event.Type, event.Payload,
        ); err != nil {
            return err
        }
        
        return nil
    })
    // 事务提交后，Debezium 自动捕获 binlog 并转发到 Kafka
}
```

### 6.4.8 优势与劣势

**优势**：

- 应用无感知（仅需写 outbox 表）
- 毫秒级延迟
- 高吞吐（基于 binlog）
- 解耦业务与消息发送

**劣势**：

- 需部署 CDC 组件（Debezium + Kafka Connect）
- binlog 格式要求（row 模式）
- Outbox 表数据需清理

### 6.4.9 ⚠️ 注意事项

- ⚠️ **binlog 必须是 row 模式**：statement 模式不适用
- ⚠️ **Outbox 表清理**：CDC 已消费后可删除，但需保留一段时间防丢
- ⚠️ **消息顺序**：同一聚合 ID 的消息需保证顺序（用 Kafka partition key）
- ⚠️ **CDC 单点**：Debezium 故障需 HA

---

## 6.5 三种方案对比

| 维度 | 本地消息表 | RocketMQ 事务消息 | Outbox + CDC |
|------|-----------|------------------|-------------|
| 实现 | 简单 | 中等 | 复杂（需 CDC） |
| 耦合度 | 高（业务+消息表） | 中（需回查） | 低（应用无感知） |
| 延迟 | 秒级 | 秒级 | 毫秒级 |
| 吞吐 | 中 | 中 | 高 |
| MQ 限制 | 任意 | 必须 RocketMQ | 任意（Kafka 推荐） |
| 适用规模 | 中小 | 中 | 大 |
| 部署复杂度 | 低 | 中 | 高 |

### 6.5.1 选型建议

| 场景 | 推荐方案 |
|------|---------|
| 中小项目，已有任意 MQ | 本地消息表 |
| 用 RocketMQ，希望开箱即用 | RocketMQ 事务消息 |
| 大规模微服务，事件驱动架构 | Outbox + CDC |
| 跨异构 MQ | Outbox + CDC |
| 实时性要求高 | Outbox + CDC |

---

## 6.6 🏭 工业实战

### 6.6.1 阿里：RocketMQ 事务消息

- 淘宝订单系统核心方案
- 每秒万级事务消息
- 配合幂等消费保证 Exactly-Once

### 6.6.2 字节：Outbox + Kafka

- 内部自研 CDC（基于 binlog）
- 微服务事件驱动架构
- 毫秒级消息延迟

### 6.6.3 Uber：Outbox + Kafka

- 开源[uDMB](https://github.com/uber/uDMB)
- 配合 Cadence 工作流引擎

### 6.6.4 ⚠️ 工业陷阱

- ⚠️ **消息重复**：所有方案都可能重复，消费者必须幂等
- ⚠️ **消息顺序**：跨分区消息无顺序保证
- ⚠️ **CDC 延迟尖峰**：DB 高负载时 binlog 同步变慢
- ⚠️ **回查失败**：RocketMQ 回查依赖业务表，业务表清理过快会失败
- 🔥 **生产建议**：

  - 消费者幂等是底线（详见 [§12](./12-幂等性.md)）
  - 监控消息延迟与重试次数
  - Outbox 表定期归档
  - CDC 组件 HA 部署

---

## 6.7 面试要点

### ⭐ Q1：「先写 DB 后发 MQ」有什么问题？

**骨架**：

1. DB 写成功，MQ 发送失败 → 数据落库但下游不知
2. 应用崩溃在两步之间 → 数据落库但消息丢失
3. 业务与消息不一致

**关键点**：DB 与 MQ 无法原子

**加分项**：提到「先发 MQ 后写 DB」也有反向问题

### ⭐ Q2：本地消息表的原理？

**骨架**：

1. 业务事务中同时写业务表 + 消息表（原子）
2. 后台扫描消息表，发送 MQ
3. 发送成功后标记消息
4. 失败重试，超阈值告警

**关键点**：业务表与消息表同一事务原子

**加分项**：提到消费者需幂等

### ⭐ Q3：RocketMQ 事务消息的两阶段流程？

**骨架**：

1. 发送半消息（对消费者不可见）
2. 执行本地事务
3. 本地事务成功 → Commit；失败 → Rollback
4. 超时未确认 → Broker 回查 Producer 本地事务状态

**关键点**：半消息 + 回查

**加分项**：提到回查必须幂等

### ⭐ Q4：RocketMQ 事务消息的回查机制？

**骨架**：

1. Producer 发半消息后崩溃 → Broker 未收到 Commit/Rollback
2. Broker 定期回查 Producer
3. Producer 实现 checkLocalTransaction() 返回状态
4. Broker 根据返回状态决定 Commit/Rollback

**关键点**：回查依赖本地事务可查

**加分项**：提到回查需保留业务记录

### ⭐ Q5：Outbox Pattern 是什么？

**骨架**：

1. 业务事务同时写业务表 + outbox 表（原子）
2. CDC 组件监听 DB binlog，捕获 outbox 表变更
3. CDC 转发到 Kafka
4. 应用无感知，仅需写 outbox 表

**关键点**：CDC 监听 binlog

**加分项**：提到 Debezium 是常用 CDC 工具

### ⭐ Q6：Outbox 相比本地消息表的优势？

**骨架**：

1. 应用无感知（不需扫描逻辑）
2. 毫秒级延迟（vs 秒级）
3. 高吞吐（基于 binlog）
4. 解耦业务与消息发送

**关键点**：CDC 取代应用扫描

**加分项**：提到部署复杂度是代价

### ⭐ Q7：三种方案如何选型？

**骨架**：

1. 中小项目、任意 MQ → 本地消息表
2. 用 RocketMQ、开箱即用 → 事务消息
3. 大规模、事件驱动 → Outbox + CDC
4. 实时性要求高 → Outbox + CDC

**关键点**：规模 + MQ 类型 + 实时性

**加分项**：提到每种方案的部署复杂度

### ⭐ Q8：为什么所有方案都要求消费者幂等？

**骨架**：

1. 消息可能重发（网络重试、Producer 崩溃重发）
2. At-least-once 投递语义
3. 消费者去重才能保证 Exactly-Once
4. 用消息 ID + 状态表实现

**关键点**：At-least-once + 幂等 = Exactly-Once

**加分项**：提到 Kafka 幂等生产者 + 事务的另一种思路

### ⭐ Q9：Outbox 表如何设计？

**骨架**：

1. id：唯一 ID
2. aggregatetype：主题分类（如 Order）
3. aggregateid：聚合 ID（如 order_id，用作 Kafka key）
4. type：事件类型（如 OrderCreated）
5. payload：JSON 内容
6. created_at：创建时间

**关键点**：聚合 ID 用作 Kafka key 保序

**加分项**：提到 Event Router SMT 转换

### ⭐ Q10：CDC 方案的 binlog 必须是什么格式？

**骨架**：

1. 必须是 **row 模式**
2. statement 模式不适用（CDC 无法解析具体行变更）
3. mixed 模式不保证
4. MySQL 配置 `binlog_format=ROW`

**关键点**：row 模式

**加分项**：提到 Debezium 默认要求 row 格式

---

## 6.8 论文与延伸阅读

### 📜 经典论文

- 📜 [Pat Helland 2007] *Life beyond Distributed Transactions* —— Outbox 思想来源

### 🔗 教材与资料

- 📖 [RocketMQ 事务消息文档](https://rocketmq.apache.org/docs/featureBehavior/04transactionmessage/)（访问于 2026-06-23）
- 🔗 [Debezium Outbox Pattern](https://debezium.io/documentation/reference/stable/transformations/outbox-event-router.html)（访问于 2026-06-23）
- 🔗 [Microservices Outbox Pattern](https://microservices.io/patterns/data/transactional-outbox.html)（访问于 2026-06-23）

---

## 本章 TODO

- [ ] 补充图 6-7：本地消息表架构图
- [ ] 补充图 6-8：RocketMQ 事务消息时序图
- [ ] 补充图 6-9：Outbox + CDC 数据流图
- [ ] 核对 Debezium 最新配置语法
- [ ] 交叉引用 [§9 MQ-Kafka深度](./09-MQ-Kafka深度.md)（Exactly-Once）

## 交叉引用

- **前置**：[06-事务-2PC与3PC](./06-事务-2PC与3PC.md)
- **前置**：[06-事务-TCC与Saga](./06-事务-TCC与Saga.md)
- **后续**：[06-事务-Percolator与Spanner](./06-事务-Percolator与Spanner.md)
- **后续**：[06-事务-工业实战与选型](./06-事务-工业实战与选型.md)
- **横向**：[§9 MQ](./09-MQ-基础与投递语义.md)（事务消息底层）
- **横向**：[§12 幂等性](./12-幂等性.md)（消费者幂等）

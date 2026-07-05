# 消息队列 —— RocketMQ 与事务消息

> 章号: §9.3
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 🔥工程 🏭工业
> 前置: [[09-1-消息队列-基础与投递语义]] [[09-2-消息队列-Kafka深度]] [[06-事务-消息事务与Outbox]]

---

## 0. RocketMQ 的定位

RocketMQ (阿里巴巴 2012,开源 2016,捐赠 Apache 2017) 是参考 Kafka 设计、针对电商金融场景优化的 MQ:

- **事务消息**:原生的"半消息 + 回查"机制
- **顺序消息**:严格分区顺序
- **定时/延迟消息**:原生支持
- **消息轨迹**:全链路追踪
- **金融级**:多次双 11 验证,适合强一致场景

Kafka 偏大数据,RocketMQ 偏业务消息。

---

## 1. 架构

```
              ┌──────────────────┐
              │  Producer        │
              └────────┬─────────┘
                       ↓
        ┌──────────────────────────────┐
        │  NameServer Cluster (无状态)   │  ← 元数据
        └──────┬───────────────────────┘
               ↓
   ┌───────────┴───────────┐
   ↓                       ↓
[Broker Master]    [Broker Master]
   ↓                       ↓
[Broker Slave]    [Broker Slave]
   ↑                       ↑
   └──── 同步/异步复制 ──────┘
               ↓
              ┌──────────────────┐
              │  Consumer        │
              └──────────────────┘
```

### 1.1 NameServer

- 无状态,可部署多个(互不通信)
- Broker 注册 topic/route 信息
- Producer/Consumer 通过 NameServer 找 Broker
- 比 ZK 轻量(无共识,无 Quorum)

### 1.2 Broker

- Master:读写
- Slave:备份,可读
- 同步/异步复制可配
- Master 故障时 Slave 不能自动升级为 Master(需 Dledger 或 Controller)

### 1.3 Dledger (4.5+)

基于 Raft 的 Broker HA:

- 自动 Leader 选举
- 强一致复制
- 故障自动切换

### 1.4 Controller (5.x)

类似 KRaft,内置 Raft 替代 Dledger:

- 统一的 Controller 模式
- 支持自动 failover
- 兼容旧 Master-Slave 模式

---

## 2. 数据模型

### 2.1 Topic / MessageQueue

- Topic:逻辑分类
- MessageQueue:类似 Kafka Partition,有序日志
- 每个 Topic 默认 4 个 MessageQueue

### 2.2 CommitLog

RocketMQ 的核心创新:**所有 topic 的消息混存到一个 CommitLog**:

```
CommitLog (单文件,顺序追加):
  [msg1 from topicA] [msg2 from topicB] [msg3 from topicA] ...
```

优势:

- 顺序写极快(无 topic 切换)
- 文件少,Page Cache 利用率高

劣势:

- 消费时需查 ConsumeQueue 索引

### 2.3 ConsumeQueue

每个 MessageQueue 对应一个 ConsumeQueue,记录消息在 CommitLog 中的偏移:

```
ConsumeQueue (TopicA, Queue 0):
  [offset=100, size=128, tag=hash]  →  msg in CommitLog at 100
  [offset=228, size=64,  tag=hash]  →  msg in CommitLog at 228
  ...
```

消费时:

1. Consumer 读 ConsumeQueue(顺序读)
2. 拿到 CommitLog offset
3. 随机读 CommitLog 拿消息

### 2.4 IndexFile

支持按 key 查询消息(如订单号 → 消息):

```
IndexFile (哈希索引):
  key=order123 → slot → [offset in CommitLog, offset, ...]
```

---

## 3. 消息类型

### 3.1 普通消息

最常用,无特殊语义。

### 3.2 顺序消息

#### 3.2.1 全局顺序

单 MessageQueue,严格全局有序,丧失并行。

#### 3.2.2 分区顺序

同 key 落同 MessageQueue,Queue 内有序:

```java
producer.send(msg, new MessageQueueSelector() {
    public MessageQueue select(List<MessageQueue> mqs, Message msg, Object arg) {
        String userId = (String) arg;
        int index = Math.abs(userId.hashCode()) % mqs.size();
        return mqs.get(index);
    }
}, userId);
```

### 3.3 定时/延迟消息

```java
// 延迟 30 分钟(自动取消未支付订单)
Message msg = new Message("orders", "data".getBytes());
msg.setDelayTimeLevel(3);  // RocketMQ 4.x: 1s 5s 10s 30s 1m 2m 3m 4m 5m 6m 7m 8m 9m 10m 20m 30m 1h 2h
// RocketMQ 5.x: 任意时间戳
msg.setDeliverTimeMs(System.currentTimeMillis() + 30 * 60 * 1000);
producer.send(msg);
```

实现:

- 4.x:延迟消息先入 `SCHEDULE_TOPIC_XXXX`,定时任务扫描,到点后转回原 topic
- 5.x:基于时间轮,支持任意延迟

### 3.4 事务消息

RocketMQ 的招牌特性,详见 §4。

---

## 4. 事务消息

### 4.1 解决的问题

"DB 本地事务 + 发 MQ" 的经典难题:

```
方案 1: 先 DB 后 MQ
  DB 成功 → MQ 发送失败 → 数据不一致(DB 有数据,MQ 没消息)

方案 2: 先 MQ 后 DB
  MQ 成功 → DB 失败 → 数据不一致(MQ 有消息但 DB 没数据)
```

### 4.2 RocketMQ 方案:半消息 + 回查

```
1. Producer 发"半消息"(half message)到 Broker
2. Broker 暂存半消息(不投递给 Consumer)
3. Broker 返回 ACK
4. Producer 执行本地事务
5. Producer 根据 DB 事务结果:
   - commit → Broker 投递消息
   - rollback → Broker 删除消息
6. 若 Producer 没返回(网络故障等):
   - Broker 定期回查 Producer:"这个事务结果是什么?"
   - Producer 查询本地 DB,返回 commit/rollback
```

### 4.3 代码示例

```java
public class OrderTransactionListener implements TransactionListener {

    @Override
    public LocalTransactionState executeLocalTransaction(Message msg, Object arg) {
        // 执行本地事务(扣库存、写订单)
        try {
            orderService.createOrder((OrderRequest) arg);
            return LocalTransactionState.COMMIT_MESSAGE;
        } catch (Exception e) {
            return LocalTransactionState.ROLLBACK_MESSAGE;
        }
    }

    @Override
    public LocalTransactionState checkLocalTransaction(MessageExt msg) {
        // Broker 回查:根据消息体查询 DB,判断事务结果
        String orderId = msg.getKeys();
        Order order = orderService.findById(orderId);
        if (order == null) return LocalTransactionState.ROLLBACK_MESSAGE;
        if (order.getStatus() == OrderStatus.CREATED) return LocalTransactionState.COMMIT_MESSAGE;
        return LocalTransactionState.UNKNOW;  // 还在处理中,稍后再查
    }
}

// Producer
TransactionMQProducer producer = new TransactionMQProducer("order-group");
producer.setTransactionListener(new OrderTransactionListener());
producer.start();

Message msg = new Message("orders", "data".getBytes());
msg.setKeys(orderId);
TransactionSendResult result = producer.sendMessageInTransaction(msg, orderRequest);
```

### 4.4 与 Outbox 模式对比

详见 [[06-事务-消息事务与Outbox]]。对比:

| 维度 | RocketMQ 事务消息 | Outbox + CDC |
|------|------------------|--------------|
| DB 依赖 | 无 | 需要业务表 + outbox 表 |
| 中间件 | RocketMQ | 任意 MQ + Debezium |
| 回查 | 内置回查接口 | 无需回查(CDC 保证) |
| 实时性 | 毫秒级 | 秒级(CDC 延迟) |
| 复杂度 | 中(需实现回查) | 中(需维护 outbox) |

---

## 5. 消费者

### 5.1 Push vs Pull

RocketMQ 客户端名义上是 Push,实际是"长轮询 Pull":

- Consumer 注册 listener
- 客户端后台线程拉取消息
- 拉到后调 listener 处理
- 长轮询减少空拉

### 5.2 消费模式

- **集群消费**(默认):同 Group 内竞争消费,每条消息被一个 consumer 消费
- **广播消费**:同 Group 内所有 consumer 都消费(独立进度)

### 5.3 消费进度

- 集群消费:offset 存 Broker
- 广播消费:offset 存本地文件

### 5.4 消费失败处理

```java
consumer.registerMessageListener((MessageListenerConcurrently) (msgs, context) -> {
    try {
        for (MessageExt msg : msgs) {
            process(msg);
        }
        return ConsumeConcurrentlyStatus.CONSUME_SUCCESS;
    } catch (RetryableException e) {
        return ConsumeConcurrentlyStatus.RECONSUME_LATER;  // 稍后重试
    } catch (NonRetryableException e) {
        // 转死信队列
        return ConsumeConcurrentlyStatus.CONSUME_SUCCESS;
    }
});
```

- 重试:默认 16 次,间隔递增(1s 5s 10s 30s 1m 2m 3m 4m 5m 6m 7m 8m 9m 10m 20m 30m 1h 2h)
- 死信:%DLQ%+ConsumerGroup 队列,需人工处理

---

## 6. 高可用

### 6.1 Master-Slave (4.x)

- Master 读写,Slave 备份
- 同步/异步复制可配
- Master 故障时 Slave 不自动升级,需人工切换

### 6.2 Dledger (4.5+)

基于 Raft 的自动 failover:

- 集群 3+ 节点,Quorum 复制
- Master 故障自动选举
- 数据强一致

### 6.3 Controller (5.x)

类似 Kafka KRaft:

- Controller 节点用 Raft 选举
- 管理 Broker 注册、Topic 路由、Partition Leader
- 兼容旧 Master-Slave

### 6.4 跨 DC 同步

- 双主模式:跨 DC 同步双写,需业务层处理冲突
- 异步复制:跨 DC 异步,延迟低但有数据丢失风险

---

## 7. 工业应用

### 7.1 阿里双 11

- 单集群数万 Broker
- 单 topic 千万级 TPS
- 事务消息保证订单/库存一致
- 顺序消息保证订单状态有序

### 7.2 典型场景

- 订单创建:事务消息保证 DB+MQ 一致
- 订单超时取消:延迟消息(30 分钟)
- 顺序消费:同订单状态变更有序
- 死信告警:重试 16 次失败转 DLQ + 人工介入

---

## 8. 面试要点

**Q1: RocketMQ 和 Kafka 的核心区别?**

> (1) 存储:RocketMQ 所有 topic 混存 CommitLog,Kafka 每 topic/partition 独立文件;(2) 事务:RocketMQ 半消息+回查,Kafka 0.11+ 事务;(3) 顺序:RocketMQ 严格分区顺序,Kafka 同;(4) 延迟消息:RocketMQ 原生,Kafka 需自己实现;(5) 元数据:RocketMQ NameServer(无状态),Kafka ZK/KRaft;(6) 适用:RocketMQ 业务消息(电商金融),Kafka 大数据。

**Q2: RocketMQ 事务消息怎么工作?**

> (1) Producer 发半消息,Broker 暂存(不投递);(2) Producer 执行本地事务;(3) 根据结果 commit/rollback,Broker 投递/删除消息;(4) 若 Producer 没返回,Broker 定期回查 Producer;(5) Producer 查 DB 返回结果。保证 DB+MQ 最终一致。

**Q3: CommitLog + ConsumeQueue 的设计?**

> CommitLog 是所有 topic 消息混存的大文件,顺序写极快。ConsumeQueue 是每 MessageQueue 的索引,记录消息在 CommitLog 中的偏移。消费时先查 ConsumeQueue(顺序读),再随机读 CommitLog 拿消息。优势:写极快,劣势:读需要二跳。

**Q4: RocketMQ 怎么实现延迟消息?**

> 4.x:延迟消息先入 `SCHEDULE_TOPIC_XXXX`,按延迟级别分 queue。后台定时任务扫描到期消息,转回原 topic。只支持固定级别(18 个)。5.x:基于时间轮,支持任意延迟。

**Q5: NameServer 为什么无状态?**

> 简化设计:无共识,无 Quorum,各 NameServer 独立。Broker 注册时给所有 NameServer 发心跳,数据最终一致。代价:不同 NameServer 短期可能数据不一致,客户端容错(从任一 NameServer 拿路由,失败换一个)。

**Q6: RocketMQ 的 DLQ 怎么工作?**

> 消费失败重试 16 次(递增间隔),全部失败后转入 %DLQ%+ConsumerGroup 队列。死信消息需人工处理(查日志、修复、重投)。生产建议:DLQ 监控告警 + 自动化补偿。

**Q7: Dledger 解决什么问题?**

> 4.x 之前 Master-Slave 模式,Master 故障需人工切换。Dledger(4.5+)基于 Raft 自动 failover:Quorum 复制、Leader 自动选举、强一致。代价:需 3+ 节点。

---

## 9. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Alibaba, *RocketMQ* | 2016 | 电商场景 MQ |
| Apache RocketMQ Design | — | CommitLog + ConsumeQueue |

---

## 10. 交叉引用

- [[09-1-消息队列-基础与投递语义]]:MQ 基础
- [[09-2-消息队列-Kafka深度]]:对比 Kafka
- [[06-事务-消息事务与Outbox]]:Outbox 模式
- [[05-共识-Raft]]:Dledger 共识
- [[12-幂等性]]:消费幂等

---

## 11. 速查表

```
核心组件:
  NameServer (无状态) / Broker (Master-Slave / Dledger)
  CommitLog (混存) / ConsumeQueue (索引) / IndexFile (key 查询)

消息类型:
  普通消息 / 顺序消息(分区) / 延迟消息 / 事务消息

事务消息:
  半消息 → 本地事务 → commit/rollback → 回查兜底

DLQ:
  重试 16 次,递增间隔,失败转 %DLQ%+Group

vs Kafka:
  存储: 混存 CommitLog vs 每 partition 独立
  事务: 半消息+回查 vs 0.11+ 事务
  元数据: NameServer 无状态 vs ZK/KRaft
  适用: 业务消息 vs 大数据
```

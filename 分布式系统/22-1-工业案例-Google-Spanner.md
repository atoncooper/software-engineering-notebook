# 工业案例 —— Google Spanner

> 章号: §22.1
> 层级: 工业 / 案例
> 标记: 🏭工业 📜论文 ⭐高频
> 前置: [[08-4-存储-Spanner与NewSQL]] [[03-时间与时钟]] [[05-共识-Paxos]] [[06-事务-Percolator与Spanner]]
> 论文: Chang et al., *Spanner: Google's Globally-Distributed Database*, OSDI 2012

---

## 1. 背景

Google 在 2010s 面临:

- 跨洲广告、支付业务需要强一致 + 全球分布
- Megastore(基于 Bigtable)提供 ACID 但延迟差
- Bigtable 提供高吞吐但只有单行事务

Spanner 目标:**全球分布 + 外部一致性(External Consistency)+ SQL + 高可用**。

---

## 2. 架构

### 2.1 整体

```
                ┌────────────────────────┐
                │  Globally Distributed  │
                │       Spanner          │
                └──┬──────┬──────┬───────┘
                   ↓      ↓      ↓
              Zone1    Zone2   Zone3
              ┌────────────┐
              │ Tablet     │  ← 数据分片
              │  (Paxos Group) │
              └────────────┘
                   ↓
              ┌────────────┐
              │ Colossus   │  ← GFS 演进,存储底层
              └────────────┘
```

### 2.2 层次

- **Universe**:全球所有 Zone
- **Zone**:一个数据中心,部署独立
- **Tablet**:数据分片,每个 Tablet 一个 Paxos Group(副本分布在不同 Zone)
- **Colossus**:底层分布式文件系统(类 GFS)

### 2.3 关键抽象

- **Directory**:数据迁移单位(类似 Tablet 子集)
- **Tablet**:多个 Directory 组成,一个 Paxos Group

---

## 3. TrueTime

### 3.1 核心 API

```
TT.now()   → [earliest, latest]  (区间)
TT.after(t) → t < earliest       (t 已过)
TT.before(t) → latest < t        (t 未到)
```

误差 $\epsilon$,区间宽度 $2\epsilon$。

### 3.2 实现

- **GPS + 原子钟**:每个 DC 配 GPS 接收器 + 原子钟
- **时间同步**:master 广播,slave 同步
- **误差 < 7ms**(工程实测)

详见 [[03-时间与时钟]]。

### 3.3 Commit Wait

写事务提交时:

```
1. 选 timestamp s = TT.now().latest
2. 等 TT.after(s) = true (即等待 7ms)
3. 持久化 + 返回 ACK
```

Commit Wait 保证 $s$ 之前所有事件都已"过去",实现 External Consistency。

---

## 4. 事务模型

### 4.1 写事务(2PC + Paxos)

```
1. 客户端 acquire locks (Paxos group leaders)
2. 客户端选 prepare timestamp
3. 写 prepare log (Paxos)
4. 协调者(Coordinator)选 commit timestamp s
   - s > 所有 prepare timestamp
   - s > TT.now().latest
5. Commit Wait (等 TT.after(s))
6. 写 commit log (Paxos) → 释放锁
```

### 4.2 只读事务

- **快照读**:用 `TT.now().latest` 作为 timestamp,读对应版本
- **强一致读**:在 leader 上读(最新已提交版本)

### 4.3 External Consistency 证明

- 写事务 $T_1$ commit 时间 $s_1$,commit wait 后才返回
- 后续 $T_2$ 开始时,`TT.now().earliest > s_1`
- 故 $T_2$ 选的 $s_2 > s_1$
- 保留 real-time 顺序 → External Consistency

详见 [[08-4-存储-Spanner与NewSQL]]。

---

## 5. Paxos Group 设计

### 5.1 每个 Tablet 一个 Paxos Group

- 一个 Tablet 3 副本(典型),分布在不同 Zone
- Leader 在写多区域
- 写需 Quorum(2/3)+ Leader

### 5.2 Leader Lease

- Leader 通过 Paxos 选出 + 租约(默认 10s)
- 租约期间 leader 唯一
- 避免 leader 切换时双主

### 5.3 跨 Paxos Group 事务

- 2PC 协调者跨多个 Group
- 每个 Group 一个 Participant
- Coordinator Group 决定 commit timestamp

---

## 6. 工程细节

### 6.1 数据模型

- 关系表 + SQL(ProtoBuf Schema)
- 父子表(parent-child)同 Directory,提高局部性
- Interleaved Table:子表行物理上嵌入父表

### 6.2 索引

- Primary Index:按主键
- Secondary Index:分布式,通过 Directory 迁移

### 6.3 查询执行

- 分布式查询执行器
- 基于代价优化(Cost-Based Optimizer)
- 支持 JOIN、AGGREGATE、SUBQUERY

---

## 7. 性能数据(2012 OSDI)

- 跨 DC Paxos:10-15ms 提交延迟(主要 TrueTime 等待)
- 同 DC:1-2ms
- 单 Group 写吞吐:每秒数千事务
- 跨 Group 事务:更慢(2PC + Commit Wait)

### 7.1 容量(2020s 估算)

- 单 Universe:EB 级
- 跨 30+ DC
- 万亿级行

---

## 8. 关键设计选择与教训

### 8.1 TrueTime 是核心

- 没有同步时钟,无法实现 External Consistency
- GPS + 原子钟投资是必要的
- Commit Wait 是必要的代价

### 8.2 Paxos + 2PC 分层

- Paxos 单 Group 强一致(高吞吐)
- 2PC 跨 Group(低吞吐但必要)
- 工程上规避跨 Group 事务(局部性优化)

### 8.3 数据局部性

- Interleaved Table 把相关数据放一起
- 减少 cross-Group 事务
- 类似 TiDB 的"小表 broadcast join"

### 8.4 教训

- TrueTime 偶发故障(2019 一次故障导致 11 分钟不可用)
- Commit Wait 增加延迟,不适合极致低延迟场景
- 复杂度高,维护成本大

---

## 9. 对比开源 NewSQL

| 系统 | 时钟 | 一致性 | 共识 | 适用 |
|------|------|--------|------|------|
| Spanner | TrueTime(GPS+原子钟) | External | Paxos + 2PC | Google 内部 |
| TiDB | TSO(中心化) | Linearizable | Raft + 2PC | MySQL 兼容 |
| CockroachDB | HLC | Serializable | Raft + 2PC | PG 兼容,geo |
| YugabyteDB | Hybrids(Clock+HLC) | Serializable | Raft | PG/Cassandra API |

详见 [[08-4-存储-Spanner与NewSQL]]。

---

## 10. 影响

Spanner 是 NewSQL 鼻祖:

- 证明"全球 + 强一致 + SQL"可行
- 推动开源 NewSQL(TiDB、CockroachDB)
- 推动"分布式时钟"成为研究热点(HLC、TrueTime)

---

## 10.5 完整配置文件与代码示例

### 10.5.1 DDL(Spanner SQL)

```sql
-- ============ 创建数据库 ============
-- 通过 gcloud 或 Console:
-- gcloud spanner databases create orders --instance=prod-us

-- ============ 父子表(Interleaved)============
CREATE TABLE users (
    user_id      INT64 NOT NULL,
    email        STRING(MAX),
    name         STRING(MAX),
    created_at   TIMESTAMP NOT NULL OPTIONS (allow_commit_timestamp=true),
) PRIMARY KEY (user_id);

CREATE TABLE orders (
    user_id      INT64 NOT NULL,
    order_id     INT64 NOT NULL,
    amount       NUMERIC,
    status       STRING(20),
    created_at   TIMESTAMP NOT NULL OPTIONS (allow_commit_timestamp=true),
) PRIMARY KEY (user_id, order_id),
  INTERLEAVE IN PARENT users ON DELETE CASCADE;

CREATE TABLE order_items (
    user_id      INT64 NOT NULL,
    order_id     INT64 NOT NULL,
    item_id      INT64 NOT NULL,
    product_id   INT64 NOT NULL,
    quantity     INT64,
    price        NUMERIC,
) PRIMARY KEY (user_id, order_id, item_id),
  INTERLEAVE IN PARENT orders ON DELETE CASCADE;

-- ============ 二级索引 ============
CREATE INDEX idx_orders_by_created 
    ON orders (created_at DESC);

CREATE INDEX idx_orders_by_status 
    ON orders (status, created_at DESC);

-- Interleaved 索引(同 Tablet)
CREATE INDEX idx_order_items_by_product 
    ON order_items (product_id) 
    INTERLEAVE IN orders;

-- STORING(把额外字段存进索引,避免回表)
CREATE INDEX idx_orders_by_status_with_amount
    ON orders (status, created_at DESC)
    STORING (amount);

-- ============ 改变 Schema(在线)============
ALTER TABLE orders ADD COLUMN shipping_address STRING(MAX);
ALTER TABLE orders ALTER COLUMN status SET DEFAULT 'CREATED';
CREATE INDEX idx_orders_email ON orders (email);

-- ============ TTL ============
ALTER TABLE orders SET OPTIONS (
    deletion_policy = INTERLEAVE,
    version_retention_period = '1d'
);
```

### 10.5.2 Python 客户端(google-cloud-spanner)

```python
from google.cloud import spanner
from google.cloud.spanner_v1 import param_types
from contextlib import contextmanager
import uuid

# 连接
client = spanner.Client(project="my-gcp-project")
instance = client.instance("prod-us")
database = instance.database("orders")


@contextmanager
def read_write_transaction():
    with database.batch() as batch:
        yield batch


# ============ 单行写 ============
def create_user(user_id: int, email: str, name: str):
    with database.batch() as batch:
        batch.insert(
            table="users",
            columns=("user_id", "email", "name", "created_at"),
            values=[(user_id, email, name, spanner.COMMIT_TIMESTAMP)],
        )


# ============ 事务(乐观 + 2PC)============
def transfer_order_to_user(from_user: int, to_user: int, order_id: int):
    def txn(transaction):
        # 乐观锁 + 拿锁
        result = transaction.execute_sql(
            "SELECT user_id, amount, status FROM orders "
            "WHERE user_id = @from_user AND order_id = @order_id",
            params={"from_user": from_user, "order_id": order_id},
            param_types={
                "from_user": param_types.INT64,
                "order_id": param_types.INT64,
            },
        )
        row = list(result)[0]
        if row[2] != "PENDING":
            raise ValueError("Order not transferable")
        
        # 更新归属
        transaction.update(
            table="orders",
            columns=("user_id", "order_id", "status"),
            values=[(to_user, order_id, "TRANSFERRED")],
        )
    
    # spanner 自动处理 2PC + Commit Wait
    database.run_in_transaction(txn)


# ============ 强一致只读(快照)============
def get_user_orders(user_id: int):
    # 强一致快照读(默认 staleness=strong)
    with database.snapshot() as snapshot:
        results = snapshot.execute_sql(
            "SELECT order_id, amount, status, created_at "
            "FROM orders WHERE user_id = @user_id "
            "ORDER BY created_at DESC LIMIT 100",
            params={"user_id": user_id},
            param_types={"user_id": param_types.INT64},
        )
        return [row for row in results]


# ============ Stale Read(允许读到旧版本,降低延迟)============
def get_recent_orders_with_stale_read(user_id: int, max_staleness_seconds: int = 10):
    from google.cloud.spanner_v1.types import ExecuteSqlRequest
    import datetime
    
    snapshot = database.snapshot(
        read_timestamp=None,
        min_read_timestamp=None,
        max_staleness=datetime.timedelta(seconds=max_staleness_seconds),
        exact_staleness=None,
    )
    with snapshot:
        results = snapshot.execute_sql(
            "SELECT order_id, amount FROM orders WHERE user_id = @uid",
            params={"uid": user_id},
            param_types={"uid": param_types.INT64},
        )
        return list(results)


# ============ 批量写入(高吞吐)============
def batch_insert_orders(orders: list[dict]):
    with database.batch() as batch:
        batch.insert(
            table="orders",
            columns=("user_id", "order_id", "amount", "status", "created_at"),
            values=[
                (o["user_id"], o["order_id"], o["amount"], "CREATED",
                 spanner.COMMIT_TIMESTAMP)
                for o in orders
            ],
        )


# ============ Partitioned DML(全表更新,无锁)============
def update_all_pending_to_cancelled():
    # 不占用大锁,后台分片执行
    with database.batch() as batch:
        batch.partitioned_update(
            table="orders",
            columns=("status",),
            values=[("CANCELLED",)],
            where="status = 'PENDING'",
        )
```

### 10.5.3 Go 客户端

```go
package main

import (
    "context"
    "cloud.google.com/go/spanner"
    "google.golang.org/api/iterator"
)

type SpannerRepo struct {
    client *spanner.Client
}

func NewSpannerRepo(project, instance, database string) (*SpannerRepo, error) {
    ctx := context.Background()
    client, err := spanner.NewClient(ctx,
        fmt.Sprintf("projects/%s/instances/%s/databases/%s",
            project, instance, database))
    if err != nil {
        return nil, err
    }
    return &SpannerRepo{client: client}, nil
}

// 读写事务
func (r *SpannerRepo) CreateOrder(ctx context.Context, o *Order) error {
    _, err := r.client.ReadWriteTransaction(ctx,
        func(ctx context.Context, txn *spanner.ReadWrite_txn) error {
            // 检查库存
            var stock int64
            row, err := txn.ReadRow(ctx, "products",
                spanner.Key{o.ProductID}, []string{"stock"})
            if err != nil { return err }
            if err := row.Column(0, &stock); err != nil { return err }
            if stock < int64(o.Quantity) {
                return fmt.Errorf("insufficient stock")
            }
            
            // 扣库存
            if _, err := txn.Update(ctx, "products",
                []string{"product_id", "stock"},
                [][]interface{}{{o.ProductID, stock - int64(o.Quantity)}}); err != nil {
                return err
            }
            
            // 创建订单
            m, err := spanner.InsertStruct("orders", o)
            if err != nil { return err }
            return txn.BufferWrite([]*spanner.Mutation{m})
        })
    return err
}

// 只读快照(强一致)
func (r *SpannerRepo) GetUserOrders(ctx context.Context, userID int64) ([]*Order, error) {
    ro := r.client.ReadOnlyTransaction()
    defer ro.Close()
    
    stmt := spanner.Statement{
        SQL: `SELECT order_id, amount, status, created_at
              FROM orders WHERE user_id = @userID
              ORDER BY created_at DESC LIMIT 100`,
        Params: map[string]interface{}{"userID": userID},
    }
    
    iter := ro.Query(ctx, stmt)
    defer iter.Stop()
    
    var orders []*Order
    for {
        row, err := iter.Next()
        if err == iterator.Done { break }
        if err != nil { return nil, err }
        var o Order
        if err := row.ToStruct(&o); err != nil { return nil, err }
        orders = append(orders, &o)
    }
    return orders, nil
}
```

### 10.5.4 Terraform 基础设施配置

```hcl
# spanner.tf
resource "google_spanner_instance" "prod" {
  name             = "prod-us"
  config           = "nam-us"
  display_name     = "Production US"
  processing_units = 2000                    # 2000 PU = 100 节点
  edition          = "ENTERPRISE_PLUS"
  
  labels = {
    env     = "prod"
    team    = "orders"
    billing = "orders-team"
  }
}

resource "google_spanner_database" "orders" {
  instance = google_spanner_instance.prod.name
  name     = "orders"
  ddl {
    statements = [
      "CREATE TABLE users (user_id INT64 NOT NULL, email STRING(MAX)) PRIMARY KEY (user_id)",
      "CREATE TABLE orders (user_id INT64 NOT NULL, order_id INT64 NOT NULL, amount NUMERIC, status STRING(20), created_at TIMESTAMP OPTIONS (allow_commit_timestamp=true)) PRIMARY KEY (user_id, order_id), INTERLEAVE IN PARENT users ON DELETE CASCADE",
    ]
  }
  
  version_retention_period = "1d"
  enable_drop_protection   = true
}

# 备份
resource "google_spanner_database_backup" "daily" {
  instance = google_spanner_instance.prod.name
  database = google_spanner_database.orders.name
  name     = "backup-daily-${formatdate("YYYYMMDD", timestamp())}"
  
  expire_time = timeadd(timestamp(), "7d")
  
  lifecycle {
    create_before_destroy = true
  }
}

# IAM
resource "google_spanner_database_iam_member" "app_rw" {
  instance = google_spanner_instance.prod.name
  database = google_spanner_database.orders.name
  role     = "roles/spanner.databaseUser"
  member   = "serviceAccount:orders-sa@my-project.iam.gserviceaccount.com"
}
```

### 10.5.5 Java(Spring Data)

```java
// application.yml
spring:
  cloud:
    gcp:
      spanner:
        project-id: my-gcp-project
        instance-id: prod-us
        database: orders
        credentials:
          location: classpath:spanner-sa.json

// Entity
@Table(name = "orders")
public class Order {
    @PrimaryKey(keyOrder = 1)
    @Column(name = "user_id")
    private Long userId;
    
    @PrimaryKey(keyOrder = 2)
    @Column(name = "order_id")
    private Long orderId;
    
    @Column(name = "amount")
    private BigDecimal amount;
    
    @Column(name = "status")
    private String status;
    
    @Column(name = "created_at")
    private Instant createdAt;
}

// Repository
public interface OrderRepository extends SpannerRepository<Order, Long[]> {
    List<Order> findByUserIdOrderByCreatedAtDesc(Long userId);
    List<Order> findByStatusAndCreatedAtAfter(String status, Instant after);
    
    @Query("SELECT * FROM orders WHERE user_id = @userId "
         + "AND status = @status ORDER BY created_at DESC LIMIT @limit")
    List<Order> findUserOrdersByStatus(
        @Param("userId") Long userId,
        @Param("status") String status,
        @Param("limit") long limit
    );
}

// Service
@Service
public class OrderService {
    @Autowired private OrderRepository orderRepo;
    
    @Transactional
    public Order createOrder(Long userId, BigDecimal amount) {
        Order order = new Order();
        order.setUserId(userId);
        order.setOrderId(UUID.randomUUID().getMostSignificantBits());
        order.setAmount(amount);
        order.setStatus("CREATED");
        order.setCreatedAt(Instant.now());
        return orderRepo.save(order);
    }
    
    // 只读事务(强一致)
    @Transactional(readOnly = true)
    public List<Order> getRecentOrders(Long userId) {
        return orderRepo.findByUserIdOrderByCreatedAtDesc(userId);
    }
}
```

### 10.5.6 Commit Timestamps(审计)

```sql
-- 表加 commit timestamp 列
ALTER TABLE orders ADD COLUMN last_modified 
    TIMESTAMP OPTIONS (allow_commit_timestamp=true);

-- 写入时让 Spanner 填充
INSERT INTO orders (user_id, order_id, amount, status, created_at, last_modified)
VALUES (1, 1001, 99.99, 'CREATED', PENDING_COMMIT_TIMESTAMP(), PENDING_COMMIT_TIMESTAMP());

-- 查最近 5 分钟修改的订单
SELECT * FROM orders 
WHERE last_modified > TIMESTAMP_SUB(CURRENT_TIMESTAMP(), INTERVAL 5 MINUTE);
```

---

## 11. 速查表

```
Spanner 核心:
  TrueTime (GPS + 原子钟, ε < 7ms)
  Commit Wait (等 TT.after(s))
  External Consistency (线性一致 + real-time 顺序)
  
架构:
  Universe → Zone → Tablet → Paxos Group (3 副本)
  Tablet: Directory 集合 (迁移单位)
  Colossus: 底层存储

事务:
  写: 2PC + Paxos, Commit Wait
  读: 快照读 (latest ts), 强一致读 (leader)
  
性能:
  跨 DC: 10-15ms (TT 主导)
  单 DC: 1-2ms
  单 Group: 数千 TPS
  
教训:
  TrueTime 是核心
  数据局部性减少跨 Group
  Commit Wait 是必要代价
```

---

## 12. 交叉引用

- [[08-4-存储-Spanner与NewSQL]]:Spanner 原理与对比
- [[03-时间与时钟]]:TrueTime / HLC / Vector Clock
- [[05-共识-Paxos]]:Paxos 算法
- [[06-事务-Percolator与Spanner]]:Spanner 事务
- [[21-3-学术附录-线性一致性]]:External Consistency

---

## 13. 参考文献

- Chang et al. *Spanner: Google's Globally-Distributed Database*. OSDI 2012.
- Corbett et al. *Spanner: Google's Globally-Distributed Database (Implementation)*. 2012.
- Peng, Dabek. *Large-scale Incremental Processing Using Distributed Transactions and Notifications*. OSDI 2010.
- Spanner 官方文档:https://cloud.google.com/spanner
- Wilson et al. *Cloud Spanner: Distributed Consistency at Scale*. 2019.

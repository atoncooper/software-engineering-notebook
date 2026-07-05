# 工业案例 —— AWS DynamoDB

> 章号: §22.2
> 层级: 工业 / 案例
> 标记: 🏭工业 📜论文 ⭐高频
> 前置: [[08-2-存储-Dynamo]] [[07-分片与路由]] [[02-理论基础]]
> 论文: DeCandia et al., *Dynamo: Amazon's Highly Available Key-value Store*, SOSP 2007

---

## 1. 背景

Amazon 电商 2006 面临:

- 购物车、会话、推荐:海量低价值数据
- 关系 DB(MySQL)扩展性差
- 牺牲强一致换可用性,业务可接受

Dynamo(论文)→ DynamoDB(产品)演进。

---

## 2. Dynamo 论文(2007)

### 2.1 核心设计

- **AP**(可调一致)
- **一致性哈希** + 虚拟节点
- **Quorum** W+R>N
- **Vector Clock** 处理冲突
- **Merkle Tree** 反熵
- **Gossip** 传播成员信息

详见 [[08-2-存储-Dynamo]]。

### 2.2 关键技术

```
N: 副本数 (默认 3)
W: 写确认数 (可调)
R: 读返回数 (可调)

W + R > N: 强一致 (但非线性一致)
W + R <= N: 最终一致
```

### 2.3 实战效果

- 99.9% 延迟 < 10ms
- 单节点 1000-2000 QPS
- 故障切换自动,无需人工

---

## 3. DynamoDB 产品(2012)

### 3.1 与 Dynamo 论文的差异

| 维度 | Dynamo(论文) | DynamoDB(产品) |
|------|---------------|-----------------|
| 部署 | 单一应用内部 | 公有云服务 |
| 数据模型 | KV | KV + 文档(嵌套) |
| 容量 | 手动 | 自动 partition
| 一致性 | 可调 | 强一致/最终二选 |
| 索引 | 无 | LSI/GSI |
| 事务 | 无 | 2018+ 支持跨 item |

### 3.2 架构

```
Client → Routing Service → Storage Node (Partition)
                              ↓
                          Replication Group (3 副本, Raft)
```

- **Partition**:数据分片,由 Replication Group 管理
- **Replication Group**:3 副本,Raft 复制(不同于 Dynamo 论文的 Quorum!)
- **Routing Service**:请求路由 + 鉴权

### 3.3 自动扩展

- 监控 partition 容量/流量
- 自动 split(单 partition 太大)
- 自动 merge(空 partition)

---

## 4. 数据模型

### 4.1 表

- **Partition Key**(HASH):决定 partition
- **Sort Key**(RANGE):partition 内排序
- 单 PK:简单 KV
- PK + SK:支持范围查询

### 4.2 索引

- **Local Secondary Index(LSI)**:同 PK,不同 SK(创建时定义)
- **Global Secondary Index(GSI)**:不同 PK + SK(可后加)
- LSI 与主表同 partition,GSI 独立

### 4.3 容量单位

- **RCU**(Read Capacity Unit):4KB/秒,强一致 1 RCU,最终一致 0.5 RCU
- **WCU**(Write Capacity Unit):1KB/秒,1 WCU
- 按需(on-demand)vs 预置(provisioned)两种模式

---

## 5. 一致性

### 5.1 强一致读 vs 最终一致读

```python
import boto3
dynamodb = boto3.resource('dynamodb')
table = dynamodb.Table('users')

# 最终一致(默认,0.5 RCU)
resp = table.get_item(Key={'user_id': '123'})

# 强一致(1 RCU)
resp = table.get_item(Key={'user_id': '123'}, ConsistentRead=True)
```

### 5.2 事务(2018+)

```python
with table.batch_writer() as batch:
    # 单 item 操作
    pass

# 跨 item 事务(2PC 风格)
client.transact_write_items(
    TransactItems=[
        {'Put': {'TableName': 'users', 'Item': {...}}},
        {'Update': {'TableName': 'accounts', 'Key': {...}, ...}},
    ]
)
```

最多 100 个 item,跨同 region。

---

## 6. 关键设计选择

### 6.1 从 Quorum 到 Raft

Dynamo 论文用 Quorum W+R>N,但 DynamoDB 实际用 **Raft** 复制(每 partition 一个 Raft group):

- Raft 提供线性一致(Quorum 不行)
- 更易理解、维护
- 代价:写延迟增加(Raft 共识)

### 6.2 全局表(Global Tables)

- 跨 region 复制
- **多主**:每个 region 可写
- **最终一致**:region 间异步
- 冲突解决:LWW(timestamp)或自定义

### 6.3 DynamoDB Streams

- 表变更事件流(类似 CDC)
- 触发 Lambda / Kinesis
- 用于异步处理、审计

---

## 7. 性能数据

- **单 partition**:1000 WCU + 3000 RCU
- **延迟**:个位数 ms(强一致),< 10ms(99.9%)
- **跨 region**:几十 ms(异步复制)

### 7.1 容量

- 单表可到 TB 级
- 单 partition 10GB 上限(超过自动 split)

---

## 8. 故障与教训

### 8.1 2015 N. Virginia 故障

- DynamoDB metadata service 故障
- 影响大量依赖 DynamoDB 的服务(Datadog 等)
- 教训:metadata 服务是单点

### 8.2 2017 Kinesis 故障(类似)

- Kinesis metadata 服务的线程饥饿
- 间接影响 DynamoDB 客户

### 8.3 教训

- metadata service 必须高可用
- 客户端需重试 + 退避
- 监控容量(RCU/WCU)防止 throttle

---

## 9. 与其他 NoSQL 对比

| 系统 | 模型 | 一致性 | 共识 | 适用 |
|------|------|--------|------|------|
| DynamoDB | KV + 文档 | 强一致/最终可选 | Raft(每 partition) | 云原生 KV |
| Cassandra | 宽列 | 可调 | Gossip + Quorum | 海量写 + 跨 DC |
| MongoDB | 文档 | 强(副本集) | Raft | 灵活 schema |
| Redis | KV+数据结构 | 单机强 | 主从异步 | 缓存 |
| Riak | KV | 可调 + CRDT | Gossip + Vector Clock | AP KV(已衰) |

---

## 10. 最佳实践

### 10.1 PK 设计

- **避免热点**:不要用时间戳做 PK(顺序写)
- **高基数**:PK 应有大量不同值,均衡分布
- **业务相关**:PK 设计直接决定查询模式

### 10.2 索引策略

- LSI 与主表同 partition,有限(5 个)
- GSI 独立,但最终一致,需考虑 RC/WC

### 10.3 容量规划

- 按需模式适合突发流量
- 预置模式适合稳定流量,可省成本

### 10.4 监控

- ThrottledRequests
- SystemErrors
- ConsumedReadCapacityUnits / ConsumedWriteCapacityUnits

---

## 10.5 完整配置文件与代码示例

### 10.5.1 CloudFormation(基础设施即代码)

```yaml
# dynamodb.yaml
AWSTemplateFormatVersion: '2010-09-09'
Description: DynamoDB Orders Table

Resources:
  OrdersTable:
    Type: AWS::DynamoDB::Table
    Properties:
      TableName: orders
      BillingMode: PAY_PER_REQUEST                    # 按需
      TableClass: STANDARD
      PointInTimeRecoverySpecification:
        PointInTimeRecoveryEnabled: true
      SSESpecification:
        SSEEnabled: true
        SSEType: KMS
      StreamSpecification:
        StreamViewType: NEW_AND_OLD_IMAGES             # CDC
      
      AttributeDefinitions:
        - { AttributeName: user_id, AttributeType: S }
        - { AttributeName: order_id, AttributeType: S }
        - { AttributeName: created_at, AttributeType: S }
        - { AttributeName: status,    AttributeType: S }
      
      KeySchema:
        - { AttributeName: user_id,  KeyType: HASH }
        - { AttributeName: order_id, KeyType: RANGE }
      
      GlobalSecondaryIndexes:
        - IndexName: status-created-index
          KeySchema:
            - { AttributeName: status,     KeyType: HASH }
            - { AttributeName: created_at, KeyType: RANGE }
          Projection:
            ProjectionType: INCLUDE
            NonKeyAttributes: [amount, items]
          # 注意:GSI 容量在按需模式下自动管理
      
      LocalSecondaryIndexes:
        - IndexName: created-index
          KeySchema:
            - { AttributeName: user_id,     KeyType: HASH }
            - { AttributeName: created_at,  KeyType: RANGE }
          Projection:
            ProjectionType: ALL
      
      Tags:
        - { Key: env,     Value: prod }
        - { Key: team,    Value: orders }
        - { Key: billing, Value: orders-team }

  # ============ Global Table(多 Region)============
  OrdersTableReplicaUSEast:
    Type: AWS::DynamoDB::Table
    Properties:
      TableName: orders
      BillingMode: PAY_PER_REQUEST
      Region: us-east-1
      Replicas:
        - RegionName: us-west-2
        - RegionName: eu-west-1
      TableClass: STANDARD
```

### 10.5.2 Python 客户端(boto3)

```python
import boto3
from boto3.dynamodb.conditions import Key, Attr
from botocore.exceptions import ClientError
from contextlib import contextmanager
import time
import uuid

dynamodb = boto3.resource('dynamodb', region_name='us-east-1')
table = dynamodb.Table('orders')


# ============ 单行 CRUD ============
def create_order(user_id: str, order_data: dict):
    order_id = str(uuid.uuid4())
    item = {
        'user_id': user_id,
        'order_id': order_id,
        'amount': order_data['amount'],
        'status': 'CREATED',
        'items': order_data['items'],
        'created_at': int(time.time() * 1000),
        'ttl': int(time.time()) + 86400 * 90,        # 90 天 TTL
    }
    table.put_item(
        Item=item,
        ConditionExpression='attribute_not_exists(user_id) AND attribute_not_exists(order_id)'
    )
    return order_id


def get_order(user_id: str, order_id: str, consistent: bool = False):
    resp = table.get_item(
        Key={'user_id': user_id, 'order_id': order_id},
        ConsistentRead=consistent              # True=强一致(1 RCU)
    )
    return resp.get('Item')


def update_order_status(user_id: str, order_id: str, new_status: str):
    table.update_item(
        Key={'user_id': user_id, 'order_id': order_id},
        UpdateExpression='SET #s = :s, updated_at = :t',
        ConditionExpression='#s = :old',       # CAS 乐观锁
        ExpressionAttributeNames={'#s': 'status'},
        ExpressionAttributeValues={
            ':s': new_status,
            ':old': 'CREATED',
            ':t': int(time.time() * 1000),
        }
    )


def delete_order(user_id: str, order_id: str):
    table.delete_item(
        Key={'user_id': user_id, 'order_id': order_id},
        ConditionExpression='attribute_exists(user_id) AND attribute_exists(order_id)'
    )


# ============ 查询(同 user_id 下)============
def query_user_orders(user_id: str, limit: int = 100):
    resp = table.query(
        KeyConditionExpression=Key('user_id').eq(user_id),
        Limit=limit,
        ScanIndexForward=False,                # 倒序
        ConsistentRead=False,                  # 最终一致(0.5 RCU)
    )
    return resp['Items']


# ============ GSI 查询(按 status)============
def query_by_status(status: str, created_after: int):
    resp = table.query(
        IndexName='status-created-index',
        KeyConditionExpression=Key('status').eq(status) 
                              & Key('created_at').gt(created_after),
        ProjectionExpression='user_id, order_id, amount',
    )
    return resp['Items']


# ============ 事务(2018+,跨 item)============
def transfer_amount(from_id: str, to_id: str, amount: int):
    client = boto3.client('dynamodb')
    try:
        client.transact_write_items(
            TransactItems=[
                {
                    'Update': {
                        'TableName': 'accounts',
                        'Key': {'user_id': {'S': from_id}},
                        'UpdateExpression': 'SET balance = balance - :amt',
                        'ConditionExpression': 'balance >= :amt',
                        'ExpressionAttributeValues': {':amt': {'N': str(amount)}}
                    }
                },
                {
                    'Update': {
                        'TableName': 'accounts',
                        'Key': {'user_id': {'S': to_id}},
                        'UpdateExpression': 'SET balance = balance + :amt',
                        'ExpressionAttributeValues': {':amt': {'N': str(amount)}}
                    }
                }
            ]
        )
    except ClientError as e:
        if e.response['Error']['Code'] == 'TransactionCanceledException':
            cancel_reasons = e.response['CancellationReasons']
            raise ValueError(f"Transaction failed: {cancel_reasons}")
        raise


# ============ Batch(批量)============
def batch_create_orders(orders: list[dict]):
    with table.batch_writer() as batch:
        for order_data in orders:
            batch.put_item(Item={
                'user_id': order_data['user_id'],
                'order_id': str(uuid.uuid4()),
                'amount': order_data['amount'],
                'status': 'CREATED',
                'created_at': int(time.time() * 1000),
            })


# ============ 分页查询(Pagination)============
def paginate_user_orders(user_id: str):
    last_evaluated_key = None
    while True:
        kwargs = {
            'KeyConditionExpression': Key('user_id').eq(user_id),
            'Limit': 100,
        }
        if last_evaluated_key:
            kwargs['ExclusiveStartKey'] = last_evaluated_key
        
        resp = table.query(**kwargs)
        yield from resp['Items']
        
        last_evaluated_key = resp.get('LastEvaluatedKey')
        if not last_evaluated_key:
            break


# ============ Stream(CDC → Lambda)============
def process_stream_event(event, context):
    for record in event['Records']:
        if record['eventName'] == 'INSERT':
            new_image = record['dynamodb']['NewImage']
            order = deserialize(new_image)
            # 发到 SNS / SQS / Elasticsearch
            send_notification(order)
        elif record['eventName'] == 'MODIFY':
            old_image = record['dynamodb'].get('OldImage')
            new_image = record['dynamodb']['NewImage']
            handle_change(old_image, new_image)
        elif record['eventName'] == 'REMOVE':
            old_image = record['dynamodb']['OldImage']
            handle_delete(old_image)


def deserialize(image: dict) -> dict:
    """DynamoDB Stream 格式转 Python dict"""
    result = {}
    for k, v in image.items():
        if 'S' in v: result[k] = v['S']
        elif 'N' in v: result[k] = float(v['N']) if '.' in v['N'] else int(v['N'])
        elif 'BOOL' in v: result[k] = v['BOOL']
        elif 'L' in v: result[k] = [deserialize(i) for i in v['L']]
        elif 'M' in v: result[k] = {k2: deserialize(v2) for k2, v2 in v['M'].items()}
    return result
```

### 10.5.3 Java 客户端(AWS SDK v2)

```java
// application.yml
aws:
  region: us-east-1
  dynamodb:
    table: orders
    consistent-read: false

// Config
@Configuration
public class DynamoDBConfig {
    
    @Bean
    public DynamoDbClient dynamoDbClient(@Value("${aws.region}") String region) {
        return DynamoDbClient.builder()
            .region(Region.of(region))
            .credentialsProvider(DefaultCredentialsProvider.create())
            .overrideConfiguration(b -> b
                .addMetricPublisher(LoggingMetricPublisher.create())
                .retryPolicy(r -> r.numRetries(3))
            )
            .build();
    }
    
    @Bean
    public DynamoDbEnhancedClient enhancedClient(DynamoDbClient client) {
        return DynamoDbEnhancedClient.builder()
            .dynamoDbClient(client)
            .build();
    }
}

// Entity
@DynamoDbBean
public class Order {
    private String userId;
    private String orderId;
    private BigDecimal amount;
    private String status;
    private Long createdAt;
    
    @DynamoDbPartitionKey
    @DynamoDbAttribute("user_id")
    public String getUserId() { return userId; }
    
    @DynamoDbSortKey
    @DynamoDbAttribute("order_id")
    public String getOrderId() { return orderId; }
    
    // getters/setters ...
}

// Repository
@Repository
public class OrderRepository {
    
    @Autowired
    private DynamoDbEnhancedClient client;
    
    @Autowired
    @Value("${aws.dynamodb.table}")
    private String tableName;
    
    private DynamoDbTable<Order> getTable() {
        return client.table(tableName, TableSchema.fromBean(Order.class));
    }
    
    public void save(Order order) {
        getTable().putItem(order);
    }
    
    public Order get(String userId, String orderId, boolean consistentRead) {
        Key key = Key.builder().partitionValue(userId).sortValue(orderId).build();
        return getTable().getItem(r -> r.key(key).consistentRead(consistentRead));
    }
    
    public List<Order> queryByUser(String userId, int limit) {
        QueryConditional conditional = QueryConditional.keyEqualTo(
            Key.builder().partitionValue(userId).build()
        );
        return getTable().query(q -> q.queryConditional(conditional).limit(limit).scanIndexForward(false))
            .items()
            .stream()
            .toList();
    }
    
    // 事务(2022+)
    public void transferAmount(String from, String to, int amount) {
        DynamoDbTable<Account> accounts = client.table("accounts", 
            TableSchema.fromBean(Account.class));
        
        client.transactWriteItems(b -> b
            .addUpdateItem(accounts, i -> i
                .item(Account.builder().userId(from).build())
                .conditionExpression(Expression.builder()
                    .expression("balance >= :amt")
                    .putExpressionValue(":amt", AttributeValue.builder().n(String.valueOf(amount)).build())
                    .build())
                .updateExpression(Expression.builder()
                    .expression("SET balance = balance - :amt")
                    .putExpressionValue(":amt", AttributeValue.builder().n(String.valueOf(amount)).build())
                    .build()))
            .addUpdateItem(accounts, i -> i
                .item(Account.builder().userId(to).build())
                .updateExpression(Expression.builder()
                    .expression("SET balance = balance + :amt")
                    .putExpressionValue(":amt", AttributeValue.builder().n(String.valueOf(amount)).build())
                    .build())));
    }
}
```

### 10.5.4 Lambda + DynamoDB Streams

```python
# lambda_function.py
import boto3
import json
import os

sns = boto3.client('sns')
es = boto3.client('opensearch')

def lambda_handler(event, context):
    for record in event['Records']:
        event_name = record['eventName']
        
        if event_name in ('INSERT', 'MODIFY'):
            new_image = record['dynamodb']['NewImage']
            order = deserialize(new_image)
            
            # 1. 发到 SNS 通知
            sns.publish(
                TopicArn=os.environ['ORDER_TOPIC_ARN'],
                Message=json.dumps(order),
                Subject=f'Order {event_name}'
            )
            
            # 2. 索引到 OpenSearch
            es.index(
                index='orders',
                id=order['order_id'],
                body=order
            )
            
            # 3. 触发下游
            if order.get('status') == 'PAID':
                trigger_shipping(order)
        
        elif event_name == 'REMOVE':
            old_image = record['dynamodb']['OldImage']
            handle_delete(deserialize(old_image))
    
    return {'statusCode': 200, 'processed': len(event['Records'])}


def deserialize(image: dict) -> dict:
    result = {}
    for k, v in image.items():
        if 'S' in v: result[k] = v['S']
        elif 'N' in v: result[k] = float(v['N']) if '.' in v['N'] else int(v['N'])
        elif 'BOOL' in v: result[k] = v['BOOL']
        elif 'L' in v: result[k] = [deserialize(i) for i in v['L']]
        elif 'M' in v: result[k] = {k2: deserialize(v2) for k2, v2 in v['M'].items()}
    return result
```

### 10.5.5 容量与监控

```yaml
# CloudWatch Alarm
Resources:
  ThrottledRequestsAlarm:
    Type: AWS::CloudWatch::Alarm
    Properties:
      AlarmName: orders-throttled
      AlarmDescription: DynamoDB throttled requests
      MetricName: ThrottledRequests
      Namespace: AWS/DynamoDB
      Statistic: Sum
      Period: 60
      EvaluationPeriods: 1
      Threshold: 1
      ComparisonOperator: GreaterThanThreshold
      Dimensions:
        - Name: TableName
          Value: orders
      AlarmActions:
        - !Ref AlertSNSTopic
  
  HighLatencyAlarm:
    Type: AWS::CloudWatch::Alarm
    Properties:
      AlarmName: orders-high-latency
      MetricName: SuccessfulRequestLatency
      Namespace: AWS/DynamoDB
      Statistic: p99
      Period: 60
      EvaluationPeriods: 3
      Threshold: 100                        # ms
      ComparisonOperator: GreaterThanThreshold
      Dimensions:
        - Name: TableName
          Value: orders
        - Name: Operation
          Value: GetItem
```

### 10.5.6 DAX(内存加速)

```python
# DAX 客户端
import amazondax
import boto3

# 普通 DynamoDB
dynamo = boto3.resource('dynamodb', region_name='us-east-1')

# DAX 加速(微秒延迟)
dax = amazondax.AmazonDaxClientResource(
    endpoint_url='https://dax-cluster.dax-clusters.us-east-1.amazonaws.com',
    region_name='us-east-1'
)

table = dax.Table('users')

# 第一次访问:DynamoDB 取数据 → DAX 缓存
user = table.get_item(Key={'user_id': '123'})['Item']

# 后续访问:直接从 DAX 取(4x faster)
user = table.get_item(Key={'user_id': '123'})['Item']
```

---

## 11. 速查表

```
DynamoDB 核心:
  Partition + Replication Group (3 副本 Raft)
  PK (HASH) + SK (RANGE)
  RCU/WCU 容量单位
  
一致性:
  强一致读 (1 RCU, leader)
  最终一致读 (0.5 RCU, follower)
  事务 (2018+, 2PC 风格, 100 item 上限)
  
索引:
  LSI (同 PK, 创建时定义, 最多 5)
  GSI (不同 PK, 可后加, 最终一致)
  
全局:
  Global Tables (跨 region 多主, LWW)
  DynamoDB Streams (CDC)
  
vs Dynamo 论文:
  Dynamo 论文: Quorum + Gossip
  DynamoDB 实际: Raft (每 partition)
  原因: Raft 提供线性一致
  
教训:
  metadata 服务是关键
  PK 设计决定性能
  监控 throttle
```

---

## 12. 交叉引用

- [[08-2-存储-Dynamo]]:Dynamo 论文原理
- [[08-5-存储-Cassandra与Redis]]:Cassandra 对比
- [[07-分片与路由]]:一致性哈希
- [[05-共识-Raft]]:Raft 共识
- [[02-理论基础]]:CAP/AP 选型

---

## 13. 参考文献

- DeCandia et al. *Dynamo: Amazon's Highly Available Key-value Store*. SOSP 2007.
- AWS DynamoDB Developer Guide. https://docs.aws.amazon.com/amazondynamodb
- Adya et al. *Efficient Argumentation for Distributed Transactions*. 2018. (DynamoDB Transactions)
- Harrington, Aman. *DynamoDB Streams*. AWS re:Invent 2014.
- Atikoglu et al. *Workload Analysis of a Large-Scale Key-Value Store*. SIGMETRICS 2012.

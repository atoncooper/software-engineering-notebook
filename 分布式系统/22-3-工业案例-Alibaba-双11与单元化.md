# 工业案例 —— Alibaba 双 11 与单元化(LDC)

> 章号: §22.3
> 层级: 工业 / 案例
> 标记: 🏭工业 ⭐高频 🔥工程
> 前置: [[14-故障与容错]] [[08-5-存储-Cassandra与Redis]] [[06-事务-TCC与Saga]]
> 来源: 阿里中间件团队技术博客 / 双 11 技术架构演进公开分享

---

## 1. 背景

阿里双 11(GMV 几千亿人民币):

- 2019 峰值:订单创建 54.4w 笔/秒
- 跨地域:买家/卖家分布全国
- 容灾:单机房故障不能影响整体

挑战:

- 数据库单点(MySQL)
- 跨地域延迟
- 故障切换难

---

## 2. 单元化架构(LDC, Logic Data Center)

### 2.1 核心思想

按业务维度(如 user_id)把流量和数据"切割"到固定单元(Cell):

- 每个单元内部自包含(应用 + DB + 缓存 + MQ)
- 单元内业务闭环,避免跨单元调用
- 跨单元流量通过异步消息

### 2.2 架构图

```
            ┌──────────────┐
            │   用户请求     │
            └──────┬───────┘
                   ↓
            ┌──────────────┐
            │  接入层 Router │  (按 user_id 路由)
            └──┬───┬───┬───┘
               ↓   ↓   ↓
            ┌────┐ ┌────┐ ┌────┐
            │Cell│ │Cell│ │Cell│  (按 hash 分配)
            │ 1  │ │ 2  │ │ N  │
            └────┘ └────┘ └────┘
              ↓      ↓      ↓
            ┌────────────────┐
            │  中心单元         │  (中心化数据: 用户基础信息, 库存)
            │  (跨单元只读 / 异步)│
            └────────────────┘
```

### 2.3 路由策略

- **路由 key**:user_id(或 buyer_id)
- **单元分配**:`hash(user_id) % N`
- **状态**:单元内 DB 存用户订单数据

### 2.4 中心单元 vs 普通单元

- **中心单元**:存全局数据(用户基础、商品、库存)
- **普通单元**:存用户订单、交易
- **跨单元访问**:中心单元只读(强一致),写通过异步消息

---

## 3. 关键技术

### 3.1 数据同步

- **单元内主从**:MySQL Master-Slave,主写从读
- **跨单元**:Otter(阿里开源,基于 Canal 的跨 DC 同步)
- **中心单元 → 普通单元**:用户基础信息异步同步

### 3.2 全局唯一 ID

- 不用 DB 自增(单点)
- 用 **Leaf**(美团)/ **TDDL Sequence**(阿里)
- 雪花算法 + 时钟回拨处理

### 3.3 跨单元事务

- **避免**:业务设计规避跨单元事务
- **TCC**:Try-Confirm-Cancel,补偿型
- **消息事务**(RocketMQ):保证最终一致

详见 [[06-事务-TCC与Saga]] 和 [[09-3-消息队列-RocketMQ]]。

### 3.4 流量调度

- **接入层**:VIP + DNS
- **路由层**:按 user_id hash 到 Cell
- **降级**:单 Cell 故障 → 流量切到其他 Cell(用户数据可能在异地副本)

---

## 4. 双 11 关键演进

### 4.1 2009-2013:单机房扩容

- MySQL 分库分表(TDDL)
- Redis 集群
- MQ 推送(HSF)

瓶颈:单机房天花板

### 4.2 2013-2015:异地多活起步

- 同城双活(同城市两 DC,强同步)
- 异地冷备(异步)
- 单元化探索

### 4.3 2015-2018:异地多活

- 张北 / 深圳 / 上海 多单元
- 跨地域订单创建:用户路由到固定 Cell
- 2018 张北机房故障,11 分钟切流到深圳

### 4.4 2019+:云原生

- Kubernetes 化
- Serverless(函数计算)
- AI 推荐(深度学习实时推理)

---

## 5. 容灾与故障切换

### 5.1 RPO / RTO

- **RPO(数据丢失)**:跨 Cell 异步,秒级丢失
- **RTO(恢复时间)**:分钟级(切流)

### 5.2 切流策略

- **故障检测**:心跳 + 监控
- **决策**:人工 + 自动(防止误切)
- **切流**:DNS 切换 + 接入层重路由

### 5.3 2018 张北故障

- 2018-09 张北机房光缆故障
- 部分单元不可用
- 自动 + 人工切流到深圳
- 11 分钟恢复
- 教训:跨 Cell 数据同步延迟需考虑

---

## 6. 中间件栈

| 中间件 | 用途 |
|--------|------|
| TDDL | 分库分表 + 读写分离 |
| HSF | RPC 框架 |
| Diamond | 配置中心(类似 Nacos) |
| ConfigServer | 服务发现 |
| MetaQ(RocketMQ 前身) | 消息队列 |
| Tair | 分布式 KV(类 Redis) |
| Sentinel | 限流熔断 |
| EagleEye | 链路追踪 |
| ARMS | 应用监控 |

---

## 7. 性能数据

- 订单峰值(2019):54.4w 笔/秒
- 支付峰值:同上量级
- 单 Cell:支持万级 TPS

### 7.1 关键指标

- DB 写延迟:< 10ms
- 缓存命中率:> 95%
- RPC 延迟:< 20ms(同 Cell)

---

## 8. 教训与最佳实践

### 8.1 单元化设计的代价

- **复杂度高**:路由、数据同步、跨 Cell 异常处理
- **业务改造**:需识别"可单元化"业务
- **运维难度**:多 Cell 同步部署

### 8.2 业务可单元化判断

- **可单元化**:用户订单、购物车、收藏(按 user_id 切分)
- **不可单元化**:商品、库存(全局共享) → 中心单元

### 8.3 跨 Cell 调用避免

- 业务流程设计:用户请求一次只在单 Cell 内完成
- 异步消息:跨 Cell 通信用 MQ

### 8.4 监控与告警

- **业务**:订单量、支付成功率
- **系统**:Cell 间同步延迟、RPC 延迟
- **告警**:秒级响应

---

## 9. 与其他公司对比

| 公司 | 多活方案 |
|------|---------|
| Alibaba | LDC 单元化(用户维度) |
| Tencent | 同城双活 + 异地灾备 |
| Meituan | Cell 架构(类似 LDC) |
| Bytedance | 多活 + 全球加速 |
| AWS | Region + AZ(应用层多活) |
| Netflix | Active-Active(AWS 多 Region) |

---

## 10. 速查表

```
LDC 核心:
  按 user_id 路由到固定 Cell
  Cell 内自包含 (App + DB + Cache + MQ)
  中心单元 (全局数据) + 普通单元 (用户数据)
  跨 Cell 异步消息

容灾:
  RPO: 秒级 (异步)
  RTO: 分钟级 (切流)

技术栈:
  TDDL (分库分表) + HSF (RPC) + Diamond (配置)
  MetaQ/RocketMQ + Tair + Sentinel + EagleEye

演进:
  2009-13: 单机房扩容
  2013-15: 同城双活
  2015-18: 异地多活 (单元化)
  2019+:   云原生

教训:
  单元化复杂度高
  业务可单元化判断
  避免跨 Cell 同步事务
  监控同步延迟
```

---

## 10.5 完整配置文件示例

### 10.5.1 接入层路由(Nginx + Lua)

```nginx
# /etc/nginx/conf.d/router.conf
upstream cell_1 { server 10.0.1.10:8080; server 10.0.1.11:8080; }
upstream cell_2 { server 10.0.2.10:8080; server 10.0.2.11:8080; }
upstream cell_3 { server 10.0.3.10:8080; server 10.0.3.11:8080; }
upstream central { server 10.0.0.10:8080; server 10.0.0.11:8080; }

lua_shared_dict healthcheck 1m;

init_worker_by_lua_block {
    local hc = require "resty.healthcheck"
    local checker = hc.new({
        name = "cell-health",
        shm = "healthcheck",
        checks = {
            active = {
                http_path = "/health",
                healthy  = { interval = 1, successes = 2 },
                unhealthy= { interval = 1, http_failures = 2 }
            }
        }
    })
    checker:add_target("10.0.1.10", 8080, "cell_1")
    checker:add_target("10.0.1.11", 8080, "cell_1")
    package.loaded.checker = checker
}

location /api/order {
    access_by_lua_block {
        local user_id = ngx.var.http_x_user_id
        if not user_id then ngx.exit(401) end
        
        -- 单元化路由:hash(user_id) → cell
        local cell_hash = ngx.crc32_long(user_id) % 3 + 1
        local upstream_name = "cell_" .. cell_hash
        
        -- 故障切流
        local checker = package.loaded.checker
        if not checker:is_healthy(upstream_name) then
            upstream_name = "central"
        end
        
        ngx.var.upstream = upstream_name
        ngx.req.set_header("X-Cell", upstream_name)
    }
    
    proxy_pass http://$upstream;
    proxy_connect_timeout 200ms;
    proxy_send_timeout 5s;
    proxy_read_timeout 5s;
}

# 秒杀限流
location /api/seckill {
    access_by_lua_block {
        local resty_limit_req = require "resty.limit.req"
        local lim = resty_limit_req.new("seckill", 100000)
        local delay, err = lim:incoming(ngx.var.http_x_user_id, true)
        if not delay then
            if err == "rejected" then ngx.exit(429) end
            ngx.exit(500)
        end
        if delay >= 0.001 then ngx.sleep(delay) end
    }
    proxy_pass http://central;
}
```

### 10.5.2 TDDL 分库分表配置

```xml
<bean id="ordersDataSource" 
      class="com.taobao.tddl.client.jdbc.TDataSource">
    <property name="appName" value="ORDERS_APP"/>
    <property name="shardRules">
        <list>
            <!-- 按 user_id 分 64 库 -->
            <bean class="com.taobao.tddl.client.loader.DbShardRule">
                <property name="dbName" value="ORDERS_GROUP"/>
                <property name="dbRule" value="HASH(#user_id#).mod(64)"/>
                <property name="dbIndexArray" 
                          value="ORDERS_GROUP_0,ORDERS_GROUP_1,...,ORDERS_GROUP_63"/>
            </bean>
            <!-- 每库分 8 表 -->
            <bean class="com.taobao.tddl.client.loader.TableShardRule">
                <property name="tableName" value="orders"/>
                <property name="tableRule" value="HASH(#user_id#).mod(8)"/>
                <property name="tableArray" 
                          value="orders_0,orders_1,...,orders_7"/>
            </bean>
        </list>
    </property>
    <property name="readWriteSeparation" value="true"/>
    <property name="masterSlaveRules">
        <list>
            <bean class="com.taobao.tddl.client.loader.MasterSlaveRule">
                <property name="masterDbName" value="ORDERS_GROUP_{0}_M"/>
                <property name="slaveDbNames" 
                          value="ORDERS_GROUP_{0}_S1,ORDERS_GROUP_{0}_S2"/>
                <property name="slaveRule" value="WEIGHT_RANDOM"/>
            </bean>
        </list>
    </property>
</bean>
```

### 10.5.3 Sentinel 限流降级配置

```java
@Configuration
public class SentinelConfig {
    
    @PostConstruct
    public void init() {
        // QPS 流控:秒杀接口 10w QPS
        FlowRule seckillRule = new FlowRule();
        seckillRule.setResource("seckill");
        seckillRule.setGrade(RuleConstant.FLOW_GRADE_QPS);
        seckillRule.setCount(100000);
        seckillRule.setLimitApp("default");
        seckillRule.setStrategy(RuleConstant.STRATEGY_DIRECT);
        
        // 线程数流控:订单创建 2000 并发
        FlowRule orderRule = new FlowRule();
        orderRule.setResource("createOrder");
        orderRule.setGrade(RuleConstant.FLOW_GRADE_THREAD);
        orderRule.setCount(2000);
        
        // 集群流控
        ClusterFlowRule clusterRule = new ClusterFlowRule();
        clusterRule.setResource("seckill");
        clusterRule.setClusterMode(true);
        clusterRule.setClusterConfig(new ClusterFlowConfig()
            .setThresholdType(ClusterFlowConfig.GLOBAL_FLOW)
            .setFallbackToLocalWhenFail(true));
        
        // 熔断:错误率 > 50%
        DegradeRule degradeRule = new DegradeRule();
        degradeRule.setResource("payment");
        degradeRule.setGrade(RuleConstant.DEGRADE_GRADE_EXCEPTION_RATIO);
        degradeRule.setCount(0.5);
        degradeRule.setTimeWindow(10);
        degradeRule.setMinRequestAmount(20);
        degradeRule.setStatIntervalMs(1000);
        
        FlowRuleManager.loadRules(Arrays.asList(seckillRule, orderRule));
        DegradeRuleManager.loadRules(Collections.singletonList(degradeRule));
    }
}

@Service
public class SeckillService {
    
    @SentinelResource(value = "seckill", 
                      blockHandler = "seckillBlockHandler",
                      fallback = "seckillFallback")
    public SeckillResult seckill(Long userId, Long itemId) {
        // Redis Lua 原子预减库存
        String script = 
            "if redis.call('exists', KEYS[1]) == 0 then return -1 end " +
            "local stock = tonumber(redis.call('get', KEYS[1])) " +
            "if stock <= 0 then return 0 end " +
            "redis.call('decr', KEYS[1]) return 1";
        
        Long result = redisTemplate.execute(
            new DefaultRedisScript<>(script, Long.class),
            Collections.singletonList("seckill:stock:" + itemId));
        
        if (result == null || result == -1) return SeckillResult.fail("商品不存在");
        if (result == 0) return SeckillResult.fail("库存不足");
        
        // 异步下单(MQ 削峰)
        OrderMessage msg = new OrderMessage(userId, itemId);
        rocketMQTemplate.asyncSend("seckill-orders", msg, new SendCallback() {
            @Override public void onSuccess(SendResult sr) {}
            @Override public void onException(Throwable e) {
                redisTemplate.opsForValue().increment("seckill:stock:" + itemId);
            }
        });
        
        return SeckillResult.success("下单中");
    }
    
    public SeckillResult seckillBlockHandler(Long userId, Long itemId, BlockException ex) {
        return SeckillResult.fail("系统繁忙,请稍后再试");
    }
    
    public SeckillResult seckillFallback(Long userId, Long itemId, Throwable t) {
        return SeckillResult.fail("系统异常");
    }
}
```

### 10.5.4 RocketMQ 事务消息

```java
@Service
public class OrderTransactionService {
    
    @Autowired
    private TransactionMQProducer producer;
    
    public void createOrder(Order order) {
        Message<Order> msg = MessageBuilder.withPayload(order)
            .setHeader("keys", order.getId().toString())
            .build();
        
        // 按 userId 选 queue,保证同用户顺序
        producer.sendMessageInTransaction(
            (mqs, m, arg) -> {
                Long userId = (Long) arg;
                return mqs.get((int)(userId % mqs.size()));
            },
            msg, order.getUserId()
        );
    }
}

@RocketMQTransactionListener
public class OrderTransactionListener implements RocketMQLocalTransactionListener {
    
    @Autowired private OrderService orderService;
    @Autowired private RedisTemplate<String, String> redisTemplate;
    
    @Override
    public RocketMQLocalTransactionState executeLocalTransaction(Message msg, Object arg) {
        String txId = msg.getHeaders().get("keys", String.class);
        try {
            Order order = (Order) msg.getPayload();
            orderService.createOrder(order);
            redisTemplate.opsForValue().set("tx:" + txId, "COMMIT", 24, TimeUnit.HOURS);
            return RocketMQLocalTransactionState.COMMIT;
        } catch (Exception e) {
            redisTemplate.opsForValue().set("tx:" + txId, "ROLLBACK", 24, TimeUnit.HOURS);
            return RocketMQLocalTransactionState.ROLLBACK;
        }
    }
    
    @Override
    public RocketMQLocalTransactionState checkLocalTransaction(Message msg) {
        String txId = msg.getHeaders().get("keys", String.class);
        String state = redisTemplate.opsForValue().get("tx:" + txId);
        if ("COMMIT".equals(state))  return RocketMQLocalTransactionState.COMMIT;
        if ("ROLLBACK".equals(state)) return RocketMQLocalTransactionState.ROLLBACK;
        Order order = orderService.findById(Long.parseLong(txId));
        return order != null 
            ? RocketMQLocalTransactionState.COMMIT 
            : RocketMQLocalTransactionState.UNKNOWN;
    }
}
```

### 10.5.5 单元化部署(K8s + HPA)

```yaml
# cell-1-deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: order-service
  namespace: cell-1
  labels:
    cell: cell-1
    region: cn-hangzhou
spec:
  replicas: 50
  selector:
    matchLabels:
      app: order-service
      cell: cell-1
  template:
    metadata:
      labels:
        app: order-service
        cell: cell-1
        region: cn-hangzhou
    spec:
      affinity:
        nodeAffinity:
          requiredDuringSchedulingIgnoredDuringExecution:
            nodeSelectorTerms:
              - matchExpressions:
                  - key: cell
                    operator: In
                    values: ["cell-1"]
        podAntiAffinity:
          preferredDuringSchedulingIgnoredDuringExecution:
            - weight: 100
              podAffinityTerm:
                labelSelector:
                  matchLabels: { app: order-service, cell: cell-1 }
                topologyKey: kubernetes.io/hostname
      containers:
        - name: order-service
          image: registry.cn-hangzhou.aliyuncs.com/orders:1.0.0
          resources:
            requests: { cpu: 2, memory: 4Gi }
            limits:   { cpu: 4, memory: 8Gi }
          env:
            - name: CELL_NAME
              value: "cell-1"
            - name: DB_HOST
              valueFrom:
                configMapKeyRef:
                  name: cell-1-config
                  key: db.host
          livenessProbe:
            httpGet: { path: /actuator/health/liveness, port: 8080 }
            initialDelaySeconds: 60
            periodSeconds: 10
          readinessProbe:
            httpGet: { path: /actuator/health/readiness, port: 8080 }
            initialDelaySeconds: 30
            periodSeconds: 5
---
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: order-service-hpa
  namespace: cell-1
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: order-service
  minReplicas: 10
  maxReplicas: 200
  metrics:
    - type: Pods
      pods:
        metric:
          name: http_requests_per_second
        target:
          type: AverageValue
          averageValue: "500"
    - type: Resource
      resource:
        name: cpu
        target:
          type: Utilization
          averageUtilization: 70
```

### 10.5.6 跨 Cell 数据同步(Otter)

```yaml
# otter pipeline 配置
otter.manager.address = otter-manager:1099
otter.node.address = otter-node-1:2088

# JSON pipeline 配置
{
  "pipeline": {
    "name": "orders-cell1-to-cell2",
    "source": {
      "type": "mysql",
      "host": "mysql-cell-1-master", "port": 3306,
      "user": "otter", "password": "***",
      "database": "orders", "table": "orders"
    },
    "target": {
      "type": "mysql",
      "host": "mysql-cell-2-slave", "port": 3306,
      "user": "otter", "password": "***",
      "database": "orders", "table": "orders"
    },
    "filter": { "ddl": false, "dml": true,
                "columns": ["id","user_id","amount","status","created_at"] },
    "batch": { "size": 1000, "timeout": 1000, "retry": 3 }
  }
}
```

### 10.5.7 监控告警(Prometheus)

```yaml
groups:
  - name: double11
    rules:
      - alert: CellSyncLag
        expr: avg(otter_pipeline_delay_seconds) by (pipeline) > 10
        for: 1m
        labels: { severity: critical }
        annotations:
          summary: "Cell sync lag > 10s"
      
      - alert: HighErrorRate
        expr: |
          sum(rate(http_requests_total{status=~"5.."}[1m])) by (cell)
          / sum(rate(http_requests_total[1m])) by (cell) > 0.01
        for: 30s
        labels: { severity: warning }
        annotations:
          summary: "Cell {{ $labels.cell }} 5xx error rate > 1%"
      
      - alert: MQConsumerLag
        expr: sum(kafka_consumergroup_lag) by (topic, consumergroup) > 10000
        for: 1m
        labels: { severity: critical }
        annotations:
          summary: "Kafka lag > 10000 on {{ $labels.topic }}"
```

---

## 11. 交叉引用

- [[14-故障与容错]]:多活 / RPO-RTO
- [[06-事务-TCC与Saga]]:TCC 在双 11 的应用
- [[09-3-消息队列-RocketMQ]]:阿里 MQ
- [[13-1-治理-负载均衡与限流]]:Sentinel 限流
- [[13-2-治理-可观测性与混沌工程]]:链路追踪

---

## 12. 参考文献

- 程立. *支付宝大规模业务实践*. QCon 2015.
- 阿里技术. *双 11 技术架构演进*. 阿里技术公众号历年分享.
- 毕玄. *异地多活核心技术*. 阿里技术.
- Alibaba. *Cell-based Architecture for High Availability*. ICDCS 2018.
- Tair 开源项目(Redis 兼容,多 DC 复制).

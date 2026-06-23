## DFx —— 面向性能设计（Design for Performance）

DFP（Design for Performance）是"面向性能的设计"：在**设计阶段**就把"系统会被高并发、大数据量、低延迟场景考验"作为前提，通过架构和代码层面的缓存、批处理、异步、池化、分区等手段，让系统在目标负载下满足延迟与吞吐量契约。

> 与 [DFx-面向可靠性设计.md](./DFx-面向可靠性设计.md) 的关系：可靠性讲"故障下还能用"，性能讲"正常时跑得快"。两者经常冲突——缓存提升性能但牺牲强一致；冗余提升可用性但增加同步开销。工程是在这两者间权衡。

> 面试提示：性能是中高级岗位的核心考点。**性能指标定义（P99 vs 平均值）、性能模型（Little's Law、Amdahl）、缓存一致性、池化、零拷贝、读写分离、分库分表**是高频。能讲清"瓶颈在哪、为什么这么改、改了换什么"比背 API 重要。

---

## 一、性能工程基础

### 1.1 性能的两个维度

| 维度 | 含义 | 单位 |
|------|------|------|
| **延迟（Latency）** | 单个请求的处理时间 | ms / μs |
| **吞吐量（Throughput）** | 单位时间处理的请求数 / 数据量 | QPS / TPS / MB/s |

> 关键洞察：**延迟和吞吐量不是线性关系**。低并发下吞吐随并发线性增长；接近瓶颈时延迟急剧上升，吞吐反而下降（GC 压力、锁竞争、上下文切换）。

```
   吞吐量
      ↑        ╱── 瓶颈
      │      ╱
      │    ╱
      │  ╱
      │╱
      └──────────→ 并发数
      延迟随并发上升：低并发平坦，高并发陡升
```

### 1.2 延迟的分位数（Percentile）

| 指标 | 含义 |
|------|------|
| **avg** | 平均值，被长尾拉高，掩盖问题 |
| **P50** | 中位数，50% 请求低于此值 |
| **P90** | 90% 请求低于此值 |
| **P99** | 99% 请求低于此值（**SRE 黄金指标**） |
| **P999** | 99.9% 请求低于此值（高 SLA 系统） |
| **max** | 最慢的一个，可能是 GC、抖动 |

> 为什么看 P99 而不是 avg？假设 100 个请求，99 个 10ms、1 个 1s。avg = 19.9ms 看起来不错，但 P99 = 1s，每 100 个用户就有 1 个体验极差。**长尾才是用户体验的杀手**。

### 1.3 延迟数量级直觉

> Jeff Dean 经典数字（1s 为基准）：

| 操作 | 时间 |
|------|------|
| L1 缓存 | 0.5 ns |
| 分支预测错误 | 5 ns |
| L2 缓存 | 7 ns |
| 互斥锁加解锁 | 25 ns |
| 主存访问 | 100 ns |
| Zippy 压缩 1KB | 3 μs |
| 1Gbps 网络发 1KB | 10 μs |
| SSD 随机读 4KB | 150 μs |
| 同机房网络往返 | 500 μs |
| 7200rpm 磁盘寻道 | 10 ms |
| 同城网络往返 | 5-10 ms |
| 跨城网络往返 | 30-100 ms |
| 跨洲网络往返 | 100-300 ms |

> **记忆口诀**：内存比 SSD 快 1000 倍，SSD 比磁盘快 100 倍，同机房比跨城快 100 倍。性能优化方向：**让数据离 CPU 更近、让 IO 更少、让网络更近**。

### 1.4 并发与并行

| 概念 | 含义 |
|------|------|
| **并发（Concurrency）** | 多任务交替执行（CPU 切换） |
| **并行（Parallelism）** | 多任务同时执行（多核） |

> Rob Pike: "Concurrency is about dealing with lots of things at once. Parallelism is about doing lots of things at once."

---

## 二、性能模型

### 2.1 Little's Law（排队论基石）

```
   L = λ × W

   L = 系统中平均请求数（并发数）
   λ = 到达速率（QPS）
   W = 平均处理时间（延迟）
```

> **应用**：已知 QPS 和延迟，可以算出系统内的并发请求数，从而配置线程池 / 连接池大小。
>
> 例：QPS = 1000，每个请求平均 50ms，则 L = 1000 × 0.05 = 50。线程池至少配 50 才不排队。

### 2.2 Amdahl's Law（阿姆达尔定律）

加速比受限于**串行部分**：

```
   S = 1 / ((1 - p) + p / n)

   p = 可并行部分占比
   n = 并行度（核数）
```

> **洞察**：如果 10% 的代码是串行的，那么无论你用多少核，加速比上限是 1 / 0.1 = 10 倍。**串行部分是性能天花板**。这也是为什么锁竞争、串行 IO、全局状态会拖垮多核扩展。

### 2.3 Universal Scalability Law（USL，通用可扩展性定律）

Amdahl 只考虑串行部分，USL 还考虑**协调成本**（多节点之间的通信、一致性）：

```
   X(N) = λ × N / (1 + α(N-1) + βN(N-1))

   α = 串行竞争系数（锁、共享资源）
   β = 协调系数（一致性、通信）
```

- α > 0：性能随并发线性增长后达到峰值，再下降（锁竞争）。
- β > 0：性能随并发增长变缓甚至下降（跨节点协调）。

> **应用**：实测不同并发下的吞吐量，拟合 α 和 β，预测扩容收益。如果 β 大，加机器收益递减——这是分布式系统为什么不能"无限加机器"的理论基础。

### 2.4 USE 方法（Utilization-Saturation-Errors）

Brendan Gregg 提出的性能分析框架：

| 维度 | 含义 | 例子 |
|------|------|------|
| **Utilization** | 资源利用率 | CPU 80%、网卡 60% |
| **Saturation** | 资源饱和度（排队） | run queue 长度、等待锁的线程数 |
| **Errors** | 错误数 | 网络丢包、磁盘错误 |

> **使用方式**：对每种资源（CPU、内存、磁盘、网络）都问这三个问题，覆盖所有资源 × 三维度。

### 2.5 RED 方法（服务监控）

| 维度 | 含义 |
|------|------|
| **Rate** | 请求速率（QPS） |
| **Errors** | 错误率 |
| **Duration** | 延迟分布 |

> RED 适合微服务监控，USE 适合资源监控。两者互补。

---

## 三、性能瓶颈定位

### 3.1 瓶颈分类

| 类型 | 现象 | 工具 |
|------|------|------|
| **CPU bound** | CPU 100%，吞吐不升 | top、perf、async-profiler |
| **Memory bound** | GC 频繁、OOM | jstat、pprof |
| **IO bound** | 磁盘 IO 高、网络等待 | iostat、iotop |
| **Lock bound** | 线程等待锁、上下文切换高 | arthas、pprof mutex |
| **Network bound** | 带宽打满、RTT 高 | iftop、tcpdump |

### 3.2 瓶颈迁移定律

> 优化一个瓶颈后，瓶颈会转移到下一个最弱的地方。性能优化是**逐层揭盖**的过程。

```
   CPU 满了 → 优化算法 → IO 成为瓶颈
   IO 优化  → 加缓存 → 网络成为瓶颈
   网络优化 → 数据库成为瓶颈
   ...
```

### 3.3 性能分析三步法

1. **量化**：先测，不要凭感觉。建立基线（baseline）。
2. **定位**：找到瓶颈（profiling、tracing）。
3. **优化**：针对瓶颈下手，**改完重新测**验证效果。

> 反例："我觉得这里慢，加个缓存吧"——没有量化，不知道是不是这里慢，也不知道加了缓存有没有用。

### 3.4 关键工具

| 场景 | 工具 |
|------|------|
| Java CPU 火焰图 | async-profiler、arthas profiler |
| Java 内存 | jmap、jstat、MAT |
| Java 线程 | jstack、arthas thread |
| Go CPU | pprof CPU |
| Go 内存 | pprof heap、allocs |
| Go mutex | pprof mutex、block |
| 系统 | top、iostat、vmstat、sar |
| 网络 | tcpdump、iftop、wireshark |
| 链路 | Jaeger、SkyWalking |

---

## 四、性能设计原则总览

```
                  性能问题
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
   减少计算量     提高并行度     减少等待
        │            │            │
   ┌────┴────┐  ┌────┴────┐  ┌────┴────┐
   │ 缓存     │  │ 异步     │  │ 池化     │
   │ 批处理   │  │ 并发     │  │ 零拷贝   │
   │ 惰性求值 │  │ 无锁     │  │ 连接复用│
   │ 索引     │  │ 分片     │  │ 背压     │
   └─────────┘  └─────────┘  └─────────┘
                     │
                     ▼
              减少数据移动
              (压缩 / 分区 / 就近)
```

| 原则 | 作用 | 章节 |
|------|------|------|
| 缓存 | 减少计算 | 五 |
| 批处理 | 减少往返 | 六 |
| 异步化 | 提高并行 | 七 |
| 池化 | 资源复用 | 八 |
| 零拷贝 | 减少数据移动 | 九 |
| 读写分离 | 分散负载 | 十 |
| 分片 | 水平扩展 | 十一 |
| 预计算 | 用空间换时间 | 十二 |
| 惰性求值 | 不做无用功 | 十三 |
| 无锁化 | 减少竞争 | 十四 |
| 压缩 | 减少传输量 | 十五 |
| 索引 | 加速查询 | 十六 |

---

## 五、缓存（Caching）

### 5.1 意图

把**计算结果或热点数据**暂存到更快的存储，下次直接命中，避免重算或远端读取。

> 性能优化第一神器。**几乎所有的性能问题都可以靠缓存缓解**——但缓存引入一致性问题，需要谨慎。

### 5.2 缓存层次

```
   CPU L1/L2/L3  ← 硬件层
        ↓
   进程内缓存（Caffeine、Guava Cache）
        ↓
   本地堆外（Ehcache off-heap）
        ↓
   集群缓存（Redis、Memcached）
        ↓
   CDN（边缘缓存）
        ↓
   数据库
```

> **每一层比下一层快 10-100 倍**。多级缓存是性能优化的核心结构。

### 5.3 缓存策略

#### 5.3.1 Cache Aside（旁路缓存）—— 最常用

```
   读：先查缓存，命中返回；未命中查 DB，回填缓存
   写：更新 DB，删除缓存（不是更新缓存）
```

**为什么写时删除而非更新？**
- 更新缓存有并发问题：A、B 并发写，A 后写但缓存被 B 覆盖。
- 删除是幂等的，更新不是。
- 有些数据写多读少，更新缓存是浪费。

#### 5.3.2 Read Through / Write Through

缓存层代理读写 DB，对应用透明。一致性强但延迟高。

#### 5.3.3 Write Behind（Write Back）

写只写缓存，**异步**刷盘。性能最高，但崩溃可能丢数据。适合日志、监控等容忍丢失的场景。

### 5.4 缓存淘汰策略

| 策略 | 含义 | 适用 |
|------|------|------|
| **LRU** | 最近最少使用 | 通用，假设"最近用过的还会用" |
| **LFU** | 最少使用频率 | 热点数据明显 |
| **FIFO** | 先进先出 | 简单，效果差 |
| **W-TinyLFU** | LRU + LFU 混合 | Caffeine 默认，抗污染 |
| **Random** | 随机淘汰 | 实现简单 |

### 5.5 缓存三大问题

#### 5.5.1 缓存穿透（Cache Penetration）

查询**不存在**的数据，缓存永远不命中，请求穿透到 DB。

**解决**：
- 空值缓存：DB 没查到也缓存 null（短 TTL）。
- 布隆过滤器：请求前先过滤掉"肯定不存在"的 key。

#### 5.5.2 缓存击穿（Cache Breakdown）

**热点 key** 过期瞬间，大量请求同时穿透。

**解决**：
- 互斥锁：只让一个请求查 DB，其他等待。
- 逻辑过期：缓存永不过期，后台异步刷新。
- 热点 key 永不过期 + 主动更新。

#### 5.5.3 缓存雪崩（Cache Avalanche）

**大量 key 同时过期**，或缓存服务挂掉，全部请求穿透。

**解决**：
- 过期时间加随机：`TTL = base + random(0, 300s)`。
- 多级缓存：本地 + Redis，Redis 挂了本地顶。
- 限流降级：DB 调用限流，避免被压垮。
- Redis 高可用：Sentinel / Cluster。

### 5.6 缓存一致性

| 方案 | 一致性 | 复杂度 |
|------|--------|--------|
| 先更新 DB 再删缓存 | 较弱 | 低 |
| 延迟双删 | 中 | 中 |
| 订阅 binlog 异步刷缓存 | 较强 | 高 |
| 分布式锁串行化 | 强 | 高，性能差 |

#### 延迟双删

```
   1. 删除缓存
   2. 更新 DB
   3. sleep(N ms)   // 等读请求把旧值回填
   4. 再删缓存
```

> 仍非完美，但能覆盖大部分场景。

### 5.7 Java 版（Caffeine + Redis 多级缓存）

```java
public class MultiLevelCache<K, V> {
    private final Cache<K, V> local;       // Caffeine
    private final RedisTemplate<String, V> redis;
    private final Function<K, V> loader;   // DB loader
    private final String prefix;

    public MultiLevelCache(RedisTemplate<String, V> redis, Function<K, V> loader, String prefix) {
        this.redis = redis;
        this.loader = loader;
        this.prefix = prefix;
        this.local = Caffeine.newBuilder()
            .maximumSize(10_000)
            .expireAfterWrite(Duration.ofSeconds(30))
            .build();
    }

    public V get(K key) {
        // L1：本地
        V v = local.getIfPresent(key);
        if (v != null) return v;

        // L2：Redis
        String redisKey = prefix + key;
        v = redis.opsForValue().get(redisKey);
        if (v != null) {
            local.put(key, v);
            return v;
        }

        // L3：DB（加锁防击穿）
        synchronized (("lock:" + redisKey).intern()) {
            v = redis.opsForValue().get(redisKey);   // double check
            if (v != null) {
                local.put(key, v);
                return v;
            }
            v = loader.apply(key);
            if (v != null) {
                // TTL 加随机防雪崩
                long ttl = 300 + ThreadLocalRandom.current().nextLong(60);
                redis.opsForValue().set(redisKey, v, Duration.ofSeconds(ttl));
                local.put(key, v);
            }
            return v;
        }
    }

    public void invalidate(K key) {
        local.invalidate(key);
        redis.delete(prefix + key);
    }
}
```

### 5.8 Go 版（bigcache + Redis）

```go
type Cache[V any] struct {
    local  *lru.Cache
    redis  *redis.Client
    loader func(ctx context.Context, key string) (V, error)
    prefix string
    mu     sync.Mutex
}

func New[V any](redis *redis.Client, loader func(context.Context, string) (V, error), prefix string) *Cache[V] {
    local, _ := lru.New(10000)
    return &Cache[V]{
        local:  local,
        redis:  redis,
        loader: loader,
        prefix: prefix,
    }
}

func (c *Cache[V]) Get(ctx context.Context, key string) (V, error) {
    var zero V

    // L1
    if v, ok := c.local.Get(key); ok {
        return v.(V), nil
    }

    // L2
    redisKey := c.prefix + key
    if b, err := c.redis.Get(ctx, redisKey).Bytes(); err == nil {
        var v V
        if json.Unmarshal(b, &v) == nil {
            c.local.Add(key, v)
            return v, nil
        }
    }

    // L3：singleflight 防击穿
    v, err, _ := singleflight.Do(redisKey, func() (interface{}, error) {
        return c.loader(ctx, key)
    })
    if err != nil {
        return zero, err
    }

    // TTL 加随机
    ttl := 300 + rand.Intn(60)
    if b, err := json.Marshal(v); err == nil {
        c.redis.Set(ctx, redisKey, b, time.Duration(ttl)*time.Second)
    }
    c.local.Add(key, v)
    return v.(V), nil
}
```

> Go 的 `golang.org/x/sync/singleflight` 是防缓存击穿的标准武器——同一 key 并发请求只会触发一次下游调用。

### 5.9 面试速答

- **Q：缓存穿透/击穿/雪崩区别？** A：穿透是查不存在的（布隆过滤）；击穿是热 key 过期（互斥锁）；雪崩是大量同时过期（TTL 加随机）。
- **Q：写缓存为什么删除而非更新？** A：删除幂等，避免并发写覆盖；写多读少时更新浪费；避免缓存和 DB 不一致。
- **Q：缓存一致性怎么保证？** A：延迟双删覆盖大部分场景；强一致用 binlog 订阅或分布式锁，但牺牲性能。
- **Q：Caffeine 比 Guava Cache 强在哪？** A：W-TinyLFU 算法抗缓存污染，吞吐量更高，异步刷新。

---

## 六、批处理（Batching）

### 6.1 意图

把多个独立操作合并成一次批量操作，减少**网络往返、IO 次数、锁竞争**。

> 数据库 N+1 问题、MQ 单条发、HTTP 单条调，都是批处理能优化的典型场景。

### 6.2 批处理的收益

```
   单条插入 1000 行：1000 次 RTT × 1ms = 1s
   批量插入 1000 行：1 次 RTT × 5ms = 5ms   ← 200 倍提升
```

### 6.3 应用场景

| 场景 | 单条 | 批量 |
|------|------|------|
| DB 插入 | `INSERT` N 次 | `INSERT VALUES (...),(...),...` |
| DB 查询 | N 次 SELECT | `WHERE id IN (...)` |
| Redis | N 次 GET | `MGET` / pipeline |
| MQ | N 次 send | batch send |
| HTTP | N 次 RPC | 批量接口 |
| 日志 | N 次写盘 | buffer flush |

### 6.4 Java 版

```java
// 反例：N+1
for (Long id : ids) {
    User u = userDao.findById(id);   // N 次 DB 查询
}

// 正例：批量
List<User> users = userDao.findByIds(ids);   // 1 次 IN 查询

// 批量插入
@Insert("<script>" +
    "INSERT INTO users(id, name) VALUES " +
    "<foreach collection='list' item='u' separator=','>" +
    "(#{u.id}, #{u.name})" +
    "</foreach>" +
    "</script>")
void batchInsert(@Param("list") List<User> users);

// Redis pipeline
List<Object> results = redis.executePipelined(new SessionCallback<>() {
    public Object execute(RedisOperations ops) {
        for (String key : keys) {
            ops.opsForValue().get(key);
        }
        return null;
    }
});
```

### 6.5 Go 版

```go
// 反例：N+1
for _, id := range ids {
    u, _ := userRepo.FindByID(ctx, id)
}

// 正例：批量
users, _ := userRepo.FindByIDs(ctx, ids)

// Redis pipeline
pipe := redis.Pipeline()
cmds := make([]*redis.StringCmd, len(keys))
for i, k := range keys {
    cmds[i] = pipe.Get(ctx, k)
}
pipe.Exec(ctx)   // 一次 RTT
for i := range cmds {
    val, _ := cmds[i].Result()
    _ = val
}

// 批量插入 GORM
db.CreateInBatches(users, 100)   // 每批 100 条
```

### 6.6 批处理的代价

- **延迟**：凑批期间请求等待。
- **失败放大**：一批失败整批重试（需要分批重试）。
- **内存**：批量数据驻留内存。

> 实践：**有界批次 + 超时触发**——凑够 N 条或等 T 毫秒就发出，避免无限等待。

### 6.7 面试速答

- **Q：N+1 问题怎么解决？** A：批量查询（`IN`）、JOIN、二级缓存。
- **Q：Redis pipeline 和事务区别？** A：pipeline 是打包多条命令减少 RTT，不保证原子性；MULTI 事务保证原子但性能差。
- **Q：批量大小怎么定？** A：根据网络 MTU、DB 参数（`max_allowed_packet`）、内存权衡，通常 100-1000。

---

## 七、异步化（Asynchronous）

### 7.1 意图

把**不需要立即返回结果**的操作异步化，让主流程快速返回，提升吞吐量和用户体验。

### 7.2 同步 vs 异步

```
   同步：用户 → 下单 → 扣库存 → 送积分 → 发短信 → 返回（总耗时 500ms）
   异步：用户 → 下单 → 扣库存 → 发 MQ → 返回（100ms）
                            ↓
                       异步消费：送积分、发短信
```

### 7.3 应用场景

| 场景 | 异步方式 |
|------|---------|
| 非核心链路 | MQ 异步 |
| 长任务 | 任务队列 + 轮询 / 推送 |
| 日志、监控 | 异步采集 |
| 报表生成 | 后台任务 + 通知 |
| 第三方调用 | Future / Promise |

### 7.4 难点：异步的一致性

异步意味着主流程不保证后续操作完成，需要：

- **MQ 持久化 + ack**：保证消息不丢。
- **幂等消费**：重复消息不产生副作用。
- **失败补偿**：消费失败要有重试 + DLQ。
- **最终一致**：用户容忍短暂不一致。

### 7.5 Java 版

```java
// CompletableFuture 异步编排
public CompletableFuture<OrderResult> createOrder(OrderRequest req) {
    return CompletableFuture.supplyAsync(() -> orderRepo.save(req), executor)
        .thenComposeAsync(order -> {
            // 扣库存
            return inventoryService.decrease(order).thenApply(v -> order);
        }, executor)
        .whenCompleteAsync((order, err) -> {
            // 异步发 MQ（不阻塞主链路）
            if (err == null) {
                mqSender.sendAsync("order.created", order);
            }
        }, executor);
}

// 注解异步
@Async("bizExecutor")
public void sendNotification(Order order) {
    smsService.send(order.getPhone(), "下单成功");
}

// 配置线程池
@Bean("bizExecutor")
public Executor bizExecutor() {
    ThreadPoolTaskExecutor exec = new ThreadPoolTaskExecutor();
    exec.setCorePoolSize(20);
    exec.setMaxPoolSize(100);
    exec.setQueueCapacity(200);
    exec.setThreadNamePrefix("biz-");
    exec.setRejectedExecutionHandler(new ThreadPoolExecutor.CallerRunsPolicy());
    exec.initialize();
    return exec;
}
```

### 7.6 Go 版

Go 的 goroutine + channel 天生异步：

```go
func CreateOrder(ctx context.Context, req *OrderRequest) (*Order, error) {
    order, err := orderRepo.Save(ctx, req)
    if err != nil {
        return nil, err
    }

    if err := inventoryService.Decrease(ctx, order); err != nil {
        return nil, err
    }

    // 异步发 MQ：不阻塞返回
    go func() {
        bg, cancel := context.WithTimeout(context.Background(), 5*time.Second)
        defer cancel()
        if err := mqSender.Send(bg, "order.created", order); err != nil {
            log.Printf("send mq failed: %v", err)
            // 入重试队列
        }
    }()

    return order, nil
}

// errgroup 等待多个异步任务
g, ctx := errgroup.WithContext(ctx)
g.Go(func() error { return sendEmail(ctx, order) })
g.Go(func() error { return sendSMS(ctx, order) })
g.Go(func() error { return updateStats(ctx, order) })
if err := g.Wait(); err != nil {
    // 处理
}
```

> 注意：goroutine 中**不能直接用请求的 ctx**——请求返回后 ctx 被取消，goroutine 里的操作会失败。要用独立的 `context.Background()` + 超时。

### 7.7 异步 vs 多线程

| 维度 | 异步 | 多线程 |
|------|------|--------|
| 模型 | 回调 / 协程 | 线程阻塞 |
| 开销 | 低 | 高（线程栈、上下文切换） |
| 适合 | IO 密集 | CPU 密集 |
| 复杂度 | 回调地狱 / 协程 | 同步思维，加锁 |

### 7.8 面试速答

- **Q：异步化的代价？** A：失去强一致、调试困难、需要补偿机制。
- **Q：CompletableFuture 比 Future 强在哪？** A：可编排（thenCompose/thenCombine）、可设超时、异常处理。
- **Q：Go 为什么用 goroutine 而不是线程？** A：goroutine 2KB 栈起步，线程 1MB；调度在用户态，切换成本低；可以开百万级 goroutine。
- **Q：异步发 MQ 失败怎么办？** A：本地事务表 + 重试；或事务消息（RocketMQ）；或 outbox 模式。

---

## 八、池化（Pooling）

### 8.1 意图

预先创建一批**可复用的资源**（连接、线程、对象），避免每次创建销毁的开销。

> "创建昂贵"的资源必须池化：数据库连接、HTTP 连接、线程、大对象。

### 8.2 池化类型

| 池 | 资源 | 工具 |
|----|------|------|
| 线程池 | 线程 | ThreadPoolExecutor、Executors |
| 连接池 | DB 连接 | HikariCP、Druid |
| HTTP 连接池 | HTTP 连接 | OkHttp、Apache HttpClient |
| 对象池 | 通用对象 | Apache Commons Pool |
| Redis 连接池 | Redis 连接 | Lettuce、Jedis Pool |

### 8.3 池的大小怎么定

#### 8.3.1 线程池

| 任务类型 | 公式 |
|---------|------|
| CPU 密集 | `N + 1`（N = 核数） |
| IO 密集 | `2N` 或 `N × (1 + W/C)`（W=等待时间，C=计算时间） |

> Brian Goetz 公式：`threads = N × (1 + W/C)`。W/C 是等待 / 计算比，可通过 profiling 测。

#### 8.3.2 连接池

```
   pool_size = (并发请求数 × 单请求占用连接时间) / 单连接复用周期
```

> 经验：HikariCP 建议连接池大小不要超过 `2N + 1`，过大反而因上下文切换变慢。数据库连接数有限，过多会拖垮 DB。

### 8.4 拒绝策略

| 策略 | 行为 |
|------|------|
| AbortPolicy | 抛异常（默认） |
| CallerRunsPolicy | 由提交者执行（背压） |
| DiscardPolicy | 静默丢弃 |
| DiscardOldestPolicy | 丢最老的 |

> 实践：**CallerRunsPolicy 是天然的背压**——队列满时让调用方自己跑，自动降速。但要注意可能阻塞主线程。

### 8.5 Java 版

```java
// 线程池：必须显式配置，禁用 Executors（队列无界 OOM）
ThreadPoolExecutor executor = new ThreadPoolExecutor(
    20,                                   // core
    100,                                  // max
    60, TimeUnit.SECONDS,                 // idle
    new LinkedBlockingQueue<>(200),       // 有界队列
    new ThreadFactoryBuilder().setNameFormat("biz-%d").build(),
    new ThreadPoolExecutor.CallerRunsPolicy()   // 背压
);

// 数据库连接池：HikariCP
HikariConfig config = new HikariConfig();
config.setJdbcUrl("jdbc:mysql://...");
config.setMaximumPoolSize(20);
config.setMinimumIdle(5);
config.setConnectionTimeout(3000);        // 获取连接超时
config.setIdleTimeout(600000);            // 空闲超时
config.setMaxLifetime(1800000);           // 连接最大生命周期
config.setLeakDetectionThreshold(60000);  // 连接泄漏检测
HikariDataSource ds = new HikariDataSource(config);
```

### 8.6 Go 版

Go 用 sync.Pool 复用对象，避免 GC 压力：

```go
var bufPool = sync.Pool{
    New: func() interface{} {
        return bytes.NewBuffer(make([]byte, 0, 1024))
    },
}

func Handle(w http.ResponseWriter, r *http.Request) {
    buf := bufPool.Get().(*bytes.Buffer)
    defer func() {
        buf.Reset()
        bufPool.Put(buf)
    }()
    // 使用 buf
    buf.WriteString("...")
}

// 数据库连接池
db, _ := sql.Open("mysql", dsn)
db.SetMaxOpenConns(20)
db.SetMaxIdleConns(5)
db.SetConnMaxLifetime(30 * time.Minute)
db.SetConnMaxIdleTime(10 * time.Minute)

// HTTP 连接池
client := &http.Client{
    Transport: &http.Transport{
        MaxIdleConns:        100,
        MaxIdleConnsPerHost: 20,
        IdleConnTimeout:     90 * time.Second,
    },
    Timeout: 5 * time.Second,
}
```

> Go 没有"线程池"概念——goroutine 本身就是轻量的，需要限并发用 channel 当信号量或 `golang.org/x/sync/semaphore`。

### 8.7 池化的坑

| 坑 | 后果 |
|----|------|
| 队列无界 | OOM（`Executors.newFixedThreadPool` 用 LinkedBlockingQueue 无界） |
| 池过大 | 上下文切换、DB 连接耗尽 |
| 池过小 | 排队等待、延迟上升 |
| 连接泄漏 | 借了不还，池耗尽 |
| 不区分业务 | 一个业务拖垮所有 |

### 8.8 面试速答

- **Q：为什么禁用 Executors？** A：`newFixedThreadPool` 队列无界可能 OOM；`newCachedThreadPool` 线程数无上限可能创建大量线程。要用 ThreadPoolExecutor 显式配置。
- **Q：线程池大小怎么算？** A：CPU 密集 N+1；IO 密集 2N 或 N×(1+W/C)。
- **Q：CallerRunsPolicy 的作用？** A：队列满时让提交者执行，形成背压，自动降速。
- **Q：HikariCP 为什么快？** A：精简的字节码、FastList 替代 ArrayList、无锁设计、并发优化。

---

## 九、零拷贝（Zero-Copy）

### 9.1 意图

减少数据在内核态和用户态之间的拷贝，以及上下文切换次数。

### 9.2 传统读文件 + 发网络的拷贝

```
   读文件 send：
   1. read(): 磁盘 → 内核 buffer → 用户 buffer（1 次拷贝 + 2 次切换）
   2. write(): 用户 buffer → socket buffer → 网卡（1 次拷贝 + 2 次切换）
   总计：4 次拷贝，4 次上下文切换
```

### 9.3 零拷贝技术

#### 9.3.1 sendfile

```
   sendfile(): 磁盘 → 内核 buffer → 网卡（DMA 拷贝）
   总计：2 次拷贝（都是 DMA），2 次切换
```

数据全程在内核态，不进用户态。

#### 9.3.2 mmap

把文件映射到内存，用户态直接访问，避免 read 系统调用。适合读写大文件。

#### 9.3.3 splice

管道（pipe）传输，无拷贝。

### 9.4 Java 版

```java
// NIO FileChannel.transferTo 内部调 sendfile
try (FileChannel in = new FileInputStream("file").getChannel();
     FileChannel out = new FileOutputStream("out").getChannel()) {
    in.transferTo(0, in.size(), out);
}

// 网络发送：Netty 的 FileRegion
FileRegion region = new DefaultFileRegion(fileChannel, 0, fileChannel.size());
channel.writeAndFlush(region);

// Netty 的 CompositeByteBuf：合并 buffer 不拷贝
CompositeByteBuf composite = ByteBufAllocator.DEFAULT.compositeBuffer();
composite.addComponents(true, buf1, buf2);
```

### 9.5 Go 版

```go
// io.Copy 底层用 sendfile（Linux）
f, _ := os.Open("file")
defer f.Close()
io.Copy(socketConn, f)

// syscall.Sendfile 显式调用
syscall.Sendfile(int(socketConn.Fd()), int(f.Fd()), &offset, count)

// bytes.Buffer 复用避免分配（与 sync.Pool 配合）
```

### 9.6 应用场景

| 场景 | 技术 |
|------|------|
| Kafka 消息发送 | sendfile |
| Nginx 静态文件 | sendfile |
| 大文件读 | mmap |
| 网络代理 | splice |

> Kafka 高吞吐的秘诀之一：消费者拉消息时，broker 直接 sendfile 从日志文件到 socket，零拷贝。

### 9.7 面试速答

- **Q：零拷贝为什么快？** A：减少用户态/内核态数据拷贝和上下文切换。
- **Q：sendfile 和 mmap 区别？** A：sendfile 是文件到 socket 的单向传输；mmap 是把文件映射到内存，可读可写。
- **Q：Kafka 为什么快？** A：顺序写、零拷贝（sendfile）、pagecache、批处理、分区并行。

---

## 十、读写分离（Read-Write Separation）

### 10.1 意图

主库承担写，**从库承担读**，分散负载。适合读多写少的场景。

### 10.2 架构

```
   写 ──▶ 主库 ──复制──▶ 从库1 ──▶ 读
                    ──▶ 从库2 ──▶ 读
                    ──▶ 从库3 ──▶ 读
```

### 10.3 难点：主从延迟

主库写入到从库同步有延迟（毫秒到秒级）。**写完立即读可能读到旧数据**。

**解决**：
- **强制读主**：写后一段时间内读主库。
- **半同步复制**：主库等至少一个从库 ack 才返回。
- **会话粘性**：同一会话的读写都走主库。
- **业务容忍**：对延迟不敏感的业务走从库。

### 10.4 Java 版（ShardingSphere）

```yaml
spring:
  shardingsphere:
    datasource:
      names: master,slave0,slave1
      master: { ... }
      slave0: { ... }
      slave1: { ... }
    rules:
      readwrite-splitting:
        data-sources:
          ds:
            write-data-source-name: master
            read-data-source-names: slave0,slave1
            load-balancer-name: round-robin
        load-balancers:
          round-robin:
            type: ROUND_ROBIN
```

```java
// 强制读主（写后立即读）
@Hint("master")
public Order getOrder(long id) {
    return orderMapper.selectById(id);
}
```

### 10.5 Go 版（GORM + 多 DSN）

```go
type DB struct {
    master *gorm.DB
    slaves []*gorm.DB
}

func (db *DB) Read(ctx context.Context) *gorm.DB {
    if forceMaster(ctx) {
        return db.master
    }
    // 轮询选从库
    idx := atomic.AddInt64(&db.counter, 1) % int64(len(db.slaves))
    return db.slaves[idx]
}

func (db *DB) Write() *gorm.DB {
    return db.master
}

// 使用
db.Read(ctx).First(&order, id)
db.Write().Create(&order)
```

### 10.6 读写分离的局限

- **写仍然单点**：主库是写瓶颈。
- **延迟问题**：强一致读仍需读主。
- **复杂度**：路由逻辑、故障切换。

> 进一步扩展：**分库分表**（下一节）解决写瓶颈。

### 10.7 面试速答

- **Q：主从延迟怎么处理？** A：写后读主、半同步复制、会话粘性、业务容忍。
- **Q：读写分离适合什么场景？** A：读多写少（典型 10:1 以上），如电商商品页、内容站。
- **Q：主库挂了怎么办？** A：从库提升为主（MHA、Orchestrator），需处理未同步的数据。

---

## 十一、分片（Sharding）

### 11.1 意图

把数据**水平拆分**到多个节点，每个节点只存一部分，突破单节点容量和性能瓶颈。

### 11.2 分片维度

| 维度 | 含义 |
|------|------|
| **范围分片** | 按 ID 范围（0-1M 在节点1，1M-2M 在节点2） |
| **哈希分片** | hash(key) % N |
| **一致性哈希** | 节点变动时迁移少 |
| **虚拟槽** | Redis Cluster 的 16384 槽 |
| **业务分片** | 按租户、地区、时间 |

### 11.3 分片键的选择

**关键原则**：分片键要能**避免跨分片查询**。

| 反例 | 问题 |
|------|------|
| 按用户 ID 分片，但查询按订单 ID | 查不到，要扫所有分片 |
| 按时间分片，但查询按用户 | 跨所有时间分片 |

> 实践：**核心查询路径必须带分片键**。如电商按 `user_id` 分片，所有查询必须带 `user_id`。

### 11.4 分片的难题

#### 11.4.1 跨分片查询

```
   SELECT * FROM orders WHERE status = 'PAID'
   → 需要扫所有分片，结果合并
```

**解决**：
- 二级索引表：维护 `status → 分片列表` 的映射。
- 搜索引擎：把可查询字段同步到 ES。
- 数据冗余：按多维度分别分片。

#### 11.4.2 跨分片事务

```
   转账：A 在分片1，B 在分片2
   A 扣钱、B 加钱 → 跨分片事务
```

**解决**：
- 分布式事务（2PC、TCC、Saga）。
- 最终一致：本地事务 + 消息。

#### 11.4.3 全局唯一 ID

单库 `AUTO_INCREMENT` 不行，需要：
- UUID：无序，索引差。
- 雪花算法（Snowflake）：时间 + 机器 + 序列。
- 号段模式（Leaf）：预分配 ID 段。

#### 11.4.4 扩容

哈希分片扩容需要**全量数据迁移**。一致性哈希 + 虚拟节点缓解。

### 11.5 Java 版（ShardingSphere）

```yaml
spring:
  shardingsphere:
    rules:
      sharding:
        tables:
          orders:
            actual-data-nodes: ds${0..3}.orders_${0..3}
            database-strategy:
              standard:
                sharding-column: user_id
                sharding-algorithm-name: db-hash
            table-strategy:
              standard:
                sharding-column: user_id
                sharding-algorithm-name: table-hash
        sharding-algorithms:
          db-hash:
            type: HASH_MOD
            props: { sharding-count: 4 }
          table-hash:
            type: HASH_MOD
            props: { sharding-count: 4 }
        key-generators:
          snowflake:
            type: SNOWFLAKE
```

### 11.6 Go 版（手动分片路由）

```go
type ShardedDB struct {
    dbs    []*gorm.DB
    shardFn func(key int64) int
}

func New(dbs []*gorm.DB) *ShardedDB {
    return &ShardedDB{
        dbs: dbs,
        shardFn: func(key int64) int {
            return int(key % int64(len(dbs)))
        },
    }
}

func (s *ShardedDB) GetShard(userID int64) *gorm.DB {
    return s.dbs[s.shardFn(userID)]
}

// 雪花算法生成 ID
type Snowflake struct {
    machineID int64
    sequence  int64
    lastTs    int64
    mu        sync.Mutex
}

func (s *Snowflake) Next() int64 {
    s.mu.Lock()
    defer s.mu.Unlock()
    ts := time.Now().UnixMilli()
    if ts == s.lastTs {
        s.sequence = (s.sequence + 1) & 0xFFF
        if s.sequence == 0 {
            ts = s.waitNextMs(ts)
        }
    } else {
        s.sequence = 0
    }
    s.lastTs = ts
    return (ts << 22) | (s.machineID << 12) | s.sequence
}
```

### 11.7 分库分表的时机

| 信号 | 行动 |
|------|------|
| 单表数据 > 1000 万 | 考虑分表 |
| 单库 QPS > 5000 | 考虑分库 |
| 单库磁盘 > 1TB | 考虑分库 |
| 慢查询多 | 先优化索引 / SQL，再考虑分片 |

> **不要过早分片**。分片后运维复杂度指数级上升。先尝试：索引优化、读写分离、归档冷数据、垂直拆分（按字段拆表），最后才水平分片。

### 11.8 面试速答

- **Q：分片键怎么选？** A：选核心查询路径的字段，避免跨分片查询。如电商按 user_id。
- **Q：分片后跨分片查询怎么办？** A：二级索引表、ES 同步、数据冗余、限制查询条件必须带分片键。
- **Q：分片后全局唯一 ID 怎么生成？** A：雪花算法、号段模式（Leaf）。UUID 不适合做主键（无序影响索引）。
- **Q：什么时候分库分表？** A：单表千万级、单库 QPS 5000+、磁盘 1TB+。但先尝试索引、归档、读写分离。

---

## 十二、预计算（Pre-computation）

### 12.1 意图

把**实时计算成本高**的结果提前算好存储，查询时直接读。用空间换时间。

### 12.2 应用场景

| 场景 | 实时 vs 预计算 |
|------|---------------|
| 商品销量排行 | 实时 SUM 慢 → 每小时预计算存表 |
| 用户画像 | 实时算慢 → 每天离线算好 |
| 报表 | 实时聚合慢 → T+1 预聚合 |
| 搜索 | 实时扫表慢 → 建倒排索引 |
| 推荐 | 实时推荐慢 → 离线训练 + 实时打分 |

### 12.3 架构

```
   离线层（T+1 / 小时级）
       │
       ▼
   预计算结果存储（Redis / DB / ES）
       │
       ▼
   实时层（查询时直接读预计算结果）
```

### 12.4 Java 版

```java
// 离线任务（@Scheduled）
@Scheduled(cron = "0 0 * * * *")   // 每小时
public void preComputeRanking() {
    // 大表聚合
    List<ProductRank> ranks = jdbc.query(
        "SELECT product_id, SUM(amount) total " +
        "FROM orders WHERE created_at >= DATE_SUB(NOW(), INTERVAL 1 HOUR) " +
        "GROUP BY product_id ORDER BY total DESC LIMIT 100",
        (rs, i) -> new ProductRank(rs.getLong(1), rs.getBigDecimal(2)));

    // 写入 Redis
    redis.opsForList().delete("rank:hot");
    for (ProductRank r : ranks) {
        redis.opsForList().rightPush("rank:hot", JSON.toJSONString(r));
    }
}

// 实时查询
public List<ProductRank> getHotProducts() {
    List<String> raw = redis.opsForList().range("rank:hot", 0, 99);
    return raw.stream().map(s -> JSON.parseObject(s, ProductRank.class)).toList();
}
```

### 12.5 物化视图

数据库层面的预计算：

```sql
-- PostgreSQL 物化视图
CREATE MATERIALIZED VIEW order_stats AS
SELECT product_id, COUNT(*) cnt, SUM(amount) total
FROM orders GROUP BY product_id;

CREATE INDEX idx_stats_product ON order_stats(product_id);

-- 刷新
REFRESH MATERIALIZED VIEW CONCURRENTLY order_stats;
```

### 12.6 预计算的代价

- **存储成本**：预计算结果占空间。
- **新鲜度**：T+1 数据不是最新的。
- **维护成本**：刷新任务的可靠性。

> 实践：**冷数据用预计算，热数据用实时计算 + 缓存**。如排行榜：Top 100 预计算，用户自己排位实时算。

### 12.7 面试速答

- **Q：预计算和缓存区别？** A：缓存是"查询结果暂存"；预计算是"主动提前算"。预计算更适合聚合统计。
- **Q：什么时候用预计算？** A：聚合查询、排行榜、报表、推荐——实时计算成本高的场景。
- **Q：预计算的新鲜度问题？** A：T+1 / 小时级。对延迟敏感的可加实时增量层（Lambda 架构）。

---

## 十三、惰性求值（Lazy Evaluation）

### 13.1 意图

**用到才算**，避免无用功。

### 13.2 应用场景

| 场景 | 惰性策略 |
|------|---------|
| 分页 | 不全查，只查一页 |
| 流式处理 | 一条条处理而非全加载 |
| 字段延迟加载 | 关联对象用到才查 |
| 短路求值 | && 遇 false 停 |
| 生成器 | 按需产出 |

### 13.3 Java 版

```java
// Stream 惰性求值
int sum = list.stream()
    .filter(x -> x > 0)          // 惰性，不立即执行
    .map(x -> x * 2)             // 惰性
    .limit(100)                  // 短路：只取前 100
    .mapToInt(Integer::intValue)
    .sum();                       // 终止操作才触发

// JPA 懒加载
@Entity
public class Order {
    @ManyToOne(fetch = FetchType.LAZY)   // 用到才查
    private User user;
}

// 分页
PageRequest page = PageRequest.of(0, 20);   // 第1页，20条
Page<Order> p = repo.findAll(page);
```

### 13.4 Go 版

```go
// 分页
func FindPage(db *gorm.DB, page, size int) ([]Order, int64, error) {
    var total int64
    db.Model(&Order{}).Count(&total)
    var orders []Order
    err := db.Offset((page - 1) * size).Limit(size).Find(&orders).Error
    return orders, total, err
}

// 流式读取大文件
func ProcessLineByLine(path string) error {
    f, err := os.Open(path)
    if err != nil { return err }
    defer f.Close()
    scanner := bufio.NewScanner(f)
    for scanner.Scan() {
        process(scanner.Bytes())   // 一行行处理
    }
    return scanner.Err()
}

// 生成器
func Fib() <-chan int {
    ch := make(chan int)
    go func() {
        a, b := 0, 1
        for {
            ch <- a
            a, b = b, a+b
        }
    }()
    return ch
}

// 用到才取
for i := 0; i < 10; i++ {
    fmt.Println(<-Fib())
}
```

### 13.5 面试速答

- **Q：JPA 懒加载的坑？** A：脱离 Session 后访问懒加载字段抛 LazyInitializationException；N+1 问题。
- **Q：流式处理的好处？** A：内存占用恒定，可处理超大文件；提前终止减少计算。

---

## 十四、无锁化（Lock-Free）

### 14.1 意图

用 **CAS（Compare-And-Swap）** 等原子操作替代锁，避免线程阻塞和上下文切换。

### 14.2 锁的问题

- **阻塞**：线程睡眠，上下文切换成本高。
- **优先级反转**：低优先级线程持锁，高优先级等。
- **死锁**：循环等待。
- **抖动**：竞争激烈时大量时间花在锁上。

### 14.3 CAS 原理

```
   CAS(addr, expected, new):
     if *addr == expected:
       *addr = new
       return true
     return false
```

硬件指令（x86 cmpxchg）保证原子。失败重试（自旋）。

### 14.4 ABA 问题

```
   线程1 读到 A
   线程2 把 A 改成 B 再改回 A
   线程1 CAS(A, C) 成功，但中间状态被忽略
```

**解决**：版本号（AtomicStampedReference）。

### 14.5 Java 版

```java
// AtomicLong
AtomicLong counter = new AtomicLong();
counter.incrementAndGet();   // CAS 自增

// LongAdder：高并发更优（分段累加）
LongAdder adder = new LongAdder();
adder.increment();

// ConcurrentHashMap：无锁读，分段写（JDK8 后 CAS + synchronized 单桶）
map.computeIfAbsent(key, k -> compute(k));

// Disruptor：无锁环形队列，单生产者单消费者完全无锁
```

### 14.6 Go 版

```go
// atomic 包
var counter int64
atomic.AddInt64(&counter, 1)
val := atomic.LoadInt64(&counter)

// CAS
for {
    old := atomic.LoadInt64(&counter)
    new := old + 1
    if atomic.CompareAndSwapInt64(&counter, old, new) {
        break
    }
}

// sync.Map：读多写少场景无锁读
var m sync.Map
m.Store("k", "v")
v, ok := m.Load("k")
```

### 14.7 无锁的代价

- **CPU 自旋**：竞争激烈时空转浪费 CPU。
- **复杂**：实现难度高，易错。
- **不适用所有场景**：复杂逻辑还是用锁。

> 实践：**低竞争用锁、高竞争用无锁**。Java `synchronized` 在低竞争下性能已经很好（偏向锁、轻量级锁优化），不要盲目用 CAS。

### 14.8 面试速答

- **Q：CAS 的 ABA 问题？** A：值从 A→B→A，CAS 检测不到。用版本号解决（AtomicStampedReference）。
- **Q：LongAdder 为什么比 AtomicLong 快？** A：高并发下 AtomicLong 单点竞争；LongAdder 分段累加，最后合并，减少竞争。
- **Q：什么时候用无锁？** A：操作简单（计数、引用计数）、竞争激烈但每个操作快。

---

## 十五、压缩（Compression）

### 15.1 意图

减少数据量，**降低网络传输时间、存储成本**。

### 15.2 压缩的代价

| 收益 | 代价 |
|------|------|
| 网络传输快 | CPU 消耗 |
| 存储省 | 压缩/解压延迟 |

> **关键判断**：网络是瓶颈时压缩划算；CPU 是瓶颈时反而慢。

### 15.3 压缩算法对比

| 算法 | 压缩比 | 速度 | 适用 |
|------|--------|------|------|
| gzip | 高 | 慢 | 通用 |
| snappy | 中 | 极快 | 实时（Kafka、Lucene） |
| lz4 | 中 | 极快 | 实时 |
| zstd | 高 | 快 | 通用（新标准） |
| brotli | 极高 | 慢 | 静态资源（HTTP） |

### 15.4 应用场景

| 场景 | 算法 |
|------|------|
| HTTP 响应 | gzip / brotli |
| Kafka 消息 | snappy / lz4 / zstd |
| 日志存储 | zstd |
| 数据库列存 | snappy / lz4 |
| 静态资源 | brotli（最高压缩比） |

### 15.5 Java 版

```java
// HTTP gzip
@GetMapping("/data")
public ResponseEntity<byte[]> data() {
    byte[] data = loadData();
    return ResponseEntity.ok()
        .header("Content-Encoding", "gzip")
        .body(compress(data));
}

// Netty 启用压缩
serverBootstrap.childHandler(new ChannelInitializer<>() {
    protected void initChannel(SocketChannel ch) {
        ch.pipeline().addLast(new HttpContentCompressor());
    }
});
```

### 15.6 Go 版

```go
// HTTP 服务端自动 gzip
import "github.com/gin-contrib/gzip"

r := gin.New()
r.Use(gzip.Gzip(gzip.DefaultCompression))

// 手动压缩
var buf bytes.Buffer
gz := gzip.NewWriter(&buf)
gz.Write(data)
gz.Close()
compressed := buf.Bytes()

// Kafka 生产者配置 snappy
writer := &kafka.Writer{
    Addr:      kafka.TCP("broker:9092"),
    Topic:     "events",
    Compression: kafka.Snappy,
}
```

### 15.7 面试速答

- **Q：压缩什么时候反而慢？** A：CPU 是瓶颈时、数据已压缩（如 JPEG、视频）。
- **Q：Kafka 为什么默认 snappy？** A：速度快（实时场景优先），压缩比够用。
- **Q：HTTP brotli 比 gzip 强在哪？** A：压缩比更高（小 15-25%），但压缩慢，适合静态资源预压缩。

---

## 十六、索引（Indexing）

### 16.1 意图

为数据库表的某些列建立**额外数据结构**，加速查询。

### 16.2 索引类型

| 类型 | 含义 | 适用 |
|------|------|------|
| B+ 树 | 平衡多路树 | 范围查询、等值查询（默认） |
| Hash | 哈希表 | 等值查询（不支持范围） |
| 联合索引 | 多列组合 | 多列查询 |
| 覆盖索引 | 包含查询所有字段 | 免回表 |
| 全文索引 | 倒排索引 | 文本搜索 |
| 位图索引 | 位图 | 低基数列 |

### 16.3 索引原则

#### 16.3.1 最左前缀

联合索引 `(a, b, c)` 能用于：
- `WHERE a = ?`
- `WHERE a = ? AND b = ?`
- `WHERE a = ? AND b = ? AND c = ?`

不能用于：
- `WHERE b = ?`（缺 a）
- `WHERE c = ?`（缺 a, b）

#### 16.3.2 覆盖索引

如果索引包含查询所需的所有字段，**不需要回表**：

```sql
-- 索引 (user_id, status)
SELECT user_id, status FROM orders WHERE user_id = 123;
-- 直接从索引返回，不查数据行
```

#### 16.3.3 避免索引失效

| 反例 | 原因 |
|------|------|
| `WHERE date(created_at) = '2024-01-01'` | 函数包裹列 |
| `WHERE name LIKE '%abc'` | 前缀模糊 |
| `WHERE age + 1 = 18` | 列参与运算 |
| `WHERE status != 1` | 不等于（取决于选择性） |
| 隐式类型转换 | 字符串列传数字 |

### 16.4 执行计划分析

```sql
EXPLAIN SELECT * FROM orders WHERE user_id = 123;
```

关键列：
- **type**：`const > eq_ref > ref > range > index > ALL`。`ALL` 是全表扫描，要优化。
- **key**：实际用的索引。
- **rows**：估算扫描行数。
- **Extra**：`Using index` 是覆盖索引（好），`Using filesort` 是文件排序（差），`Using temporary` 是临时表（差）。

### 16.5 索引的代价

- 写变慢：每次写要更新索引。
- 存储成本：索引占空间。
- 维护成本：索引越多优化器越难选对。

> 实践：**单表索引不超过 5 个**，每个索引都要有明确的查询场景。

### 16.6 面试速答

- **Q：联合索引最左前缀原理？** A：B+ 树按索引字段顺序排序，跳过前缀无法利用有序性。
- **Q：覆盖索引为什么快？** A：不回表，少一次 IO。
- **Q：LIKE '%abc' 为什么不用索引？** A：前缀未知，无法利用 B+ 树有序性。`LIKE 'abc%'` 可以。
- **Q：索引越多越好吗？** A：不是。写变慢、占空间、优化器选择困难。

---

## 十七、JVM 性能调优

### 17.1 JVM 内存模型

```
   堆（Heap）        ── 对象
     ├── 新生代
     │    ├── Eden
     │    └── Survivor × 2
     └── 老年代
   方法区 / 元空间    ── 类元数据
   栈                 ── 方法栈帧
   本地方法栈
   程序计数器
   直接内存           ── NIO Buffer
```

### 17.2 GC 算法

| 算法 | 特点 | 收集器 |
|------|------|--------|
| 标记-清除 | 碎片 | CMS（旧） |
| 标记-复制 | 无碎片，浪费空间 | ParNew、G1、ZGC |
| 标记-整理 | 无碎片，慢 | Serial Old、G1 |

### 17.3 主流收集器

| 收集器 | 适用 | 特点 |
|--------|------|------|
| **G1** | 大堆、低延迟 | 分区（Region），可预测停顿 |
| **ZGC** | 超低延迟 | < 10ms 停顿，并发整理 |
| **Shenandoah** | 超低延迟 | 类似 ZGC |

### 17.4 GC 调优目标

- **吞吐量**：GC 时间占比 < 5%。
- **延迟**：GC 停顿 < 200ms（G1）/ < 10ms（ZGC）。
- ** footprint**：堆使用率合理。

### 17.5 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| Full GC 频繁 | 老年代不足 | 加堆、调新生代比例 |
| Young GC 频繁 | 新生代不足 | 调大 Eden |
| GC 停顿长 | 大对象、标记慢 | G1 / ZGC、避免大对象 |
| 内存泄漏 | 对象无法回收 | dump + MAT 分析 |

### 17.6 关键参数

```
-Xms4g -Xmx4g                 # 初始/最大堆
-Xmn2g                        # 新生代大小
-XX:+UseG1GC                  # 用 G1
-XX:MaxGCPauseMillis=200      # 目标停顿
-XX:+HeapDumpOnOutOfMemoryError
-XX:HeapDumpPath=/dump        # OOM 自动 dump
```

### 17.7 Go GC 对比

Go 用**并发标记清除**，没有分代（Go 1.19 后部分引入分代），目标低延迟：

- STO 短（< 1ms）。
- 写屏障代价低。
- GOGC 控制触发频率（默认 100，堆翻倍触发）。
- GOMEMLIMIT 限制内存上限（Go 1.19+）。

```go
// 调整 GC 触发
debug.SetGCPercent(200)   // 堆翻 3 倍才 GC
debug.SetMemoryLimit(1 << 30)   // 1GB 上限
```

---

## 十八、网络性能

### 18.1 HTTP 性能优化

| 手段 | 收益 |
|------|------|
| Keep-Alive | 复用 TCP 连接，省握手 |
| HTTP/2 多路复用 | 一个连接并发多请求 |
| HTTP/3 QUIC | UDP，抗丢包 |
| 压缩 | 减少传输 |
| CDN 就近 | 减少网络延迟 |
| 域名分片 | 突破并发连接数限制（HTTP/1） |

### 18.2 TCP 调优

| 参数 | 含义 |
|------|------|
| `tcp_nodelay` | 禁用 Nagle，小包立即发 |
| `tcp_quickack` | 快速 ack |
| `tcp_tw_reuse` | 复用 TIME_WAIT |
| `somaxconn` | 全连接队列大小 |
| `tcp_max_syn_backlog` | 半连接队列 |

### 18.3 长连接 vs 短连接

| 维度 | 短连接 | 长连接 |
|------|--------|--------|
| 握手 | 每次都握手 | 一次握手 |
| 资源 | 每次新 socket | 复用 |
| 复杂度 | 简单 | 需心跳保活 |
| 适用 | 低频 | 高频 |

> 微服务内部、DB、Redis 都用长连接 + 连接池。

### 18.4 面试速答

- **Q：HTTP/2 比 HTTP/1 强在哪？** A：多路复用（一个 TCP 连接并发多请求）、头部压缩（HPACK）、服务端推送。
- **Q：HTTP/3 为什么用 UDP？** A：TCP 队头阻塞（一个包丢全连接阻塞）；QUIC 在 UDP 上自己实现可靠传输，流之间独立。
- **Q：Nagle 算法为什么慢？** A：攒小包为大包再发，增加延迟。低延迟场景要 `tcp_nodelay` 关闭。

---

## 十九、性能测试

### 19.1 测试类型

| 类型 | 目的 |
|------|------|
| 负载测试 | 找到系统能承受的最大负载 |
| 压力测试 | 超过最大负载看系统表现 |
| 容量测试 | 目标负载下资源消耗 |
| 稳定性测试 | 长时间运行找泄漏 |
| 峰值测试 | 瞬时峰值能否扛住 |

### 19.2 关键指标

- **QPS / TPS**
- **P50 / P99 延迟**
- **错误率**
- **资源利用率**（CPU、内存、网络、IO）
- **饱和度**（队列、连接数）

### 19.3 压测工具

| 工具 | 特点 |
|------|------|
| JMeter | 图形化，功能全 |
| wrk | 命令行，轻量快 |
| wrk2 | wrk + 延迟修正 |
| Vegeta（Go） | Go 写，易集成 |
| k6 | 脚本化，现代化 |
| Gatling | Scala，高并发 |

### 19.4 压测原则

1. **环境接近生产**：配置、数据量、网络。
2. **预热后再测**：JIT 编译、缓存预热。
3. **逐步加压**：找到拐点，不是一上来就满载。
4. **监控全链路**：不止看响应时间，看资源。
5. **持续足够久**：触发 GC、缓存淘汰。

### 19.5 容量规划

```
   目标 QPS = 峰值 QPS × 安全系数（通常 2-3）
   所需机器数 = 目标 QPS / 单机 QPS
   + 留冗余（一台机器挂了仍能满足）
```

---

## 二十、性能反模式

### 20.1 N+1 查询

```java
// 反例
List<Order> orders = orderRepo.findAll();
for (Order o : orders) {
    User u = userRepo.findById(o.getUserId());   // N 次
}

// 正例
List<Order> orders = orderRepo.findAll();
Set<Long> userIds = orders.stream().map(Order::getUserId).collect(toSet());
Map<Long, User> users = userRepo.findByIds(userIds).stream()
    .collect(toMap(User::getId, u -> u));
```

### 20.2 大对象传输

```java
// 反例：返回完整 User（含所有字段）
public User getUser(long id) { return userRepo.findById(id); }

// 正例：DTO 只返回必要字段
public UserDTO getUser(long id) { return userRepo.findDTOById(id); }
```

### 20.3 同步调用第三方

```java
// 反例：同步调 3 个第三方
String a = callA();
String b = callB();
String c = callC();
// 总耗时 = A + B + C

// 正例：并行
CompletableFuture<String> fa = CompletableFuture.supplyAsync(this::callA);
CompletableFuture<String> fb = CompletableFuture.supplyAsync(this::callB);
CompletableFuture<String> fc = CompletableFuture.supplyAsync(this::callC);
CompletableFuture.allOf(fa, fb, fc).join();
// 总耗时 = max(A, B, C)
```

### 20.4 锁粒度过大

```java
// 反例
public synchronized void process() {
    // 整个方法上锁，所有调用串行
}

// 正例：缩小锁粒度
public void process() {
    localCompute();   // 不需要锁
    synchronized(this) {
        updateShared();
    }
}
```

### 20.5 在循环中做 IO

```java
// 反例
for (Item i : items) {
    redis.set(i.getKey(), i.getValue());   // 1000 次 RTT
}

// 正例
redis.executePipelined(ops -> {
    for (Item i : items) {
        ops.opsForValue().set(i.getKey(), i.getValue());
    }
});
```

### 20.6 不分页

```java
// 反例
List<Order> all = orderRepo.findAll();   // 全表加载

// 正例
Page<Order> page = orderRepo.findAll(PageRequest.of(0, 20));
```

### 20.7 过早优化

> "Premature optimization is the root of all evil." —— Donald Knuth
>
> 在没有 profiling 数据支持前，不要凭直觉优化。先写正确的代码，再测，再优化热点。

---

## 二十一、性能设计自检清单

### 21.1 缓存层
- [ ] 热点数据是否缓存？
- [ ] 是否多级缓存？
- [ ] 缓存击穿/穿透/雪崩是否防护？
- [ ] 缓存淘汰策略是否合适？

### 21.2 数据库层
- [ ] 慢查询是否优化？
- [ ] 索引是否合理？
- [ ] 是否有 N+1？
- [ ] 是否分页？
- [ ] 是否读写分离？
- [ ] 是否分片？

### 21.3 并发层
- [ ] 线程池是否合理配置？
- [ ] 锁粒度是否最小？
- [ ] 是否能用无锁？
- [ ] 是否异步化非核心流程？

### 21.4 网络层
- [ ] 是否长连接 + 连接池？
- [ ] 是否批量调用？
- [ ] 是否压缩？
- [ ] 是否零拷贝？

### 21.5 资源层
- [ ] 是否池化昂贵资源？
- [ ] 是否惰性加载？
- [ ] 大对象是否避免？
- [ ] 是否预计算热点结果？

### 21.6 监控层
- [ ] P99 是否监控？
- [ ] 是否有性能基线？
- [ ] 是否定期压测？
- [ ] 是否 profiling 找热点？

---

## 二十二、原则速记表

| 手段 | 解决问题 | 一句话 |
|------|---------|--------|
| 缓存 | 重复计算 | 多级 + 防穿透击穿雪崩 |
| 批处理 | 多次往返 | 一次批量省 N-1 RTT |
| 异步 | 阻塞等待 | 主链路只做核心 |
| 池化 | 创建销毁开销 | 线程 / 连接 / 对象池 |
| 零拷贝 | 数据移动 | sendfile / mmap |
| 读写分离 | 读负载 | 主写从读 |
| 分片 | 单点瓶颈 | 水平拆分 + 分片键 |
| 预计算 | 实时计算贵 | 提前算好 |
| 惰性求值 | 无用功 | 用到才算 |
| 无锁 | 锁竞争 | CAS + 自旋 |
| 压缩 | 传输量 | CPU 换网络 |
| 索引 | 全表扫 | B+ 树 + 最左前缀 |

---

## 二十三、面试综合题

### Q1：接口从 1s 优化到 100ms，怎么做？

> 1. **量化定位**：用 APM / 链路追踪找到耗时分布（DB？下游 RPC？计算？）。
> 2. **DB 层**：检查 SQL 执行计划、加索引、消除 N+1、分页。
> 3. **缓存层**：热点数据加 Redis 缓存，本地缓存兜底。
> 4. **批处理**：多次调用改批量。
> 5. **并行化**：串行的独立调用改并行（CompletableFuture / errgroup）。
> 6. **异步化**：非核心流程改 MQ 异步。
> 7. **减少数据量**：只查必要字段、流式处理。
> 8. **JVM 调优**：减少 GC 停顿（G1 / ZGC）。
> 9. **网络优化**：长连接、压缩、CDN。
> 10. **再次量化**：验证优化效果，找下一个瓶颈。

### Q2：高并发下怎么避免锁竞争？

> 1. **缩小锁粒度**：只锁共享资源，不锁整个方法。
> 2. **分段锁**：ConcurrentHashMap 分段、LongAdder 分段累加。
> 3. **读写锁**：读多写少用 ReadWriteLock / StampedLock。
> 4. **无锁**：CAS、Atomic 类、Disruptor。
> 5. **ThreadLocal**：每个线程一份副本，无竞争。
> 6. **不可变对象**：只读不写，无需锁。
> 7. **单线程化**：Actor 模型、Disruptor 单消费者。

### Q3：缓存和 DB 一致性怎么保证？

> 1. **Cache Aside**：写时先更 DB 再删缓存。
> 2. **延迟双删**：删 → 更 → 等一会 → 再删。
> 3. **binlog 订阅**：Canal 订阅 binlog 异步刷缓存，最终一致。
> 4. **强一致场景**：分布式锁串行化，但牺牲性能。
> 5. **接受短暂不一致**：大部分业务可接受，监控不一致时长。

### Q4：怎么设计一个支持千万 QPS 的系统？

> 1. **无状态服务**：水平扩展，负载均衡。
> 2. **多级缓存**：CDN → 本地 → Redis → DB。
> 3. **分片**：按用户 / 业务分片到多 DB。
> 4. **异步化**：MQ 削峰填谷。
> 5. **批处理**：合并请求。
> 6. **限流降级**：保护系统不被打挂。
> 7. **多机房多活**：异地容灾 + 就近接入。
> 8. **可观测**：监控 P99、错误率、饱和度。
> 9. **容量规划**：单机 QPS × 机器数 × 安全系数。

### Q5：JVM Full GC 频繁怎么排查？

> 1. **看 GC 日志**：jstat -gcutil 看 FGCT 频率、原因。
> 2. **dump 堆**：jmap -dump，MAT 分析大对象。
> 3. **查内存泄漏**：是否有对象无法回收（缓存无界、监听器未移除）。
> 4. **大对象**：是否大量分配大数组 / 大字符串。
> 5. **调参**：增大老年代、调整新生代比例、换 G1/ZGC。
> 6. **代码层**：避免在循环中创建对象、对象池复用。

### Q6：Go 为什么比 Java 在某些场景快？

> 1. **无虚拟机开销**：Go 编译为原生机器码，JVM 有 JIT 预热。
> 2. **goroutine 轻量**：2KB 栈 vs Java 线程 1MB，可开百万级。
> 3. **GC 简单**：并发标记清除，低停顿；Java G1/ZGC 也在追赶。
> 4. **值语义**：减少堆分配，GC 压力小。
> 5. **无运行时反射**：编译期确定。
>
> 但 Java 在 JIT 预热后峰值性能不输 Go，且生态成熟。两者各有优势。

### Q7：为什么"过早优化是万恶之源"？

> 1. **未测先优化方向可能错**：你以为的瓶颈不是真瓶颈。
> 2. **优化增加复杂度**：可读性、可维护性下降。
> 3. **需求会变**：今天优化的代码明天可能重写。
> 4. **机会成本**：花在优化上的时间没做更有价值的事。
>
> 正确做法：先写正确的代码 → 测 → 找到热点 → 针对性优化 → 验证。

---

## 二十四、参考与延伸

- **《Systems Performance》**（Brendan Gregg）：性能分析圣经，USE 方法。
- **《Designing Data-Intensive Applications》**（Martin Kleppmann）：缓存、索引、复制、分区。
- **《High Performance MySQL》**：MySQL 索引、查询优化。
- **《Java Performance》**（Scott Oaks）：JVM 调优。
- **《Site Reliability Engineering》**（Google）：SLO、性能监控。
- **《Computer Systems: A Programmer's Perspective》**：底层原理，理解缓存层次。

> 配套阅读：
> - [DFx-面向可靠性设计.md](./DFx-面向可靠性设计.md)：性能和可靠性经常冲突，权衡是工程的核心
> - [软件设计原则.md](./软件设计原则.md)：SRP、KISS、YAGNI 是性能优化的代码层基础
> - [架构基本原则.md](./架构基本原则.md)：扇入扇出、模块化是性能的架构层基础

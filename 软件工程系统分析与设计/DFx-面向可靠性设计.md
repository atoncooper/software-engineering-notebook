## DFx —— 面向可靠性设计（Design for Reliability）

DFx（Design for X）是"面向某属性的工程设计"总称，X 可以是 Reliability（可靠性）、Maintainability（可维护性）、Security（安全）、Performance（性能）、Cost（成本）、Testability（可测试性）、Usability（可用性）等。

本文聚焦 **DFR（Design for Reliability，面向可靠性设计）**：在**设计阶段**就把"系统会失败"作为前提，通过架构和代码层面的容错、隔离、降级、冗余等手段，让系统在局部故障时仍能对外提供的服务满足契约。

> 与 [软件设计原则.md](./软件设计原则.md) 的关系：设计原则关注"代码层面的好坏"，DFx 关注"系统在故障下的行为"。Fail Fast、单一职责、依赖倒置是 DFR 的微观基础。

> 面试提示：互联网中高级岗位的高频考点是**熔断 / 限流 / 降级 / 重试 / 幂等**五件套，以及**可用性 9 的换算**、**雪崩机理**。能讲清"为什么这么设计"比背 API 重要。

---

## 一、DFx 全景

| DFx | 关注属性 | 关键问题 |
|-----|---------|---------|
| **DFR** | 可靠性 Reliability | 故障下还能用吗？ |
| **DFM** | 可维护性 Maintainability | 出问题好修吗？ |
| **DFT** | 可测试性 Testability | 能验证对错吗？ |
| **DFS** | 安全性 Security | 攻不破吗？ |
| **DFP** | 性能 Performance | 够快吗？ |
| **DFC** | 成本 Cost | 够便宜吗？ |
| **DFU** | 可用性 Usability | 用户会用吗？ |
| **DFE** | 环境友好 Environment | 绿色吗？ |

> 这些维度经常**互相冲突**：高可靠通常意味着高成本、高复杂度。DFR 的核心是**在成本约束下达到目标可用性**。

---

## 二、可靠性工程基础

### 2.1 RAS 三要素

| 缩写 | 全称 | 含义 |
|------|------|------|
| **R** | Reliability | 可靠性：系统连续无故障运行的能力 |
| **A** | Availability | 可用性：系统在任意时刻可用的概率 |
| **S** | Serviceability / Maintainability | 可维护性：系统出故障后恢复的能力 |

三者关系：**可用性 = 可靠性 + 可维护性**。系统越不容易坏（R 高），坏了修得越快（S 高），可用性就越高（A 高）。

### 2.2 核心指标

| 指标 | 全称 | 含义 | 公式 |
|------|------|------|------|
| **MTTF** | Mean Time To Failure | 平均无故障时间（不可修复系统） | 总运行时间 / 故障数 |
| **MTBF** | Mean Time Between Failures | 平均故障间隔时间（可修复系统） | MTTF + MTTR |
| **MTTR** | Mean Time To Repair | 平均修复时间 | 总停机时间 / 故障数 |
| **Availability** | 可用性 | 系统可用概率 | MTBF / (MTBF + MTTR) |

> 关键洞察：**提升可用性有两条路**——降低故障率（提升 MTBF）或加快恢复（降低 MTTR）。工程上后者往往更划算：从 99.9% 提到 99.99%，把 MTTR 从 1 小时降到 6 分钟比让 MTBF 翻倍容易得多。

### 2.3 可用性"几个 9"

| 可用性 | 年停机 | 等级 |
|--------|--------|------|
| 99% | 3.65 天 | 2 个 9 |
| 99.9% | 8.76 小时 | 3 个 9 |
| 99.99% | 52.6 分钟 | 4 个 9 |
| 99.999% | 5.26 分钟 | 5 个 9（电信级） |
| 99.9999% | 31.5 秒 | 6 个 9（极少数核心系统） |

> 记忆口诀：**每多一个 9，停机时间缩短 10 倍**，但工程成本指数级上升。从 2 个 9 到 3 个 9 靠"主备切换"；从 4 个 9 到 5 个 9 必须靠"多活 + 自动故障转移"，单靠运维做不到。

### 2.4 失效与故障

| 术语 | 含义 |
|------|------|
| **Fault（错误/缺陷）** | 代码 bug、配置错误、硬件瑕疵——潜在问题 |
| **Error（错误状态）** | Fault 被触发后系统进入异常状态 |
| **Failure（失效）** | Error 影响到对外服务，违背契约 |

> 经典链条：**Fault → Error → Failure**。DFR 的目标是在每一层切断链条：
> - 防 Fault：Code Review、静态扫描、类型系统
> - 防 Error→Failure：容错、隔离、降级
> - 防 Failure 扩散：熔断、舱壁、限流

---

## 三、典型失效模式

### 3.1 单点故障（SPOF）

系统中某个组件一旦失效，整个系统不可用。典型：唯一数据库、唯一 Redis、唯一网关、唯一 DNS。

**解决**：冗余 + 故障转移。任何"唯一"都要审视。

### 3.2 级联故障（Cascading Failure）

A 故障导致 B 被拖垮，B 又拖垮 C，最终雪崩。最典型链路：

```
   下游 DB 慢 → 上游线程池被占满 → 上游拒绝服务 → 上游的上游也阻塞
```

### 3.3 雪崩机理

雪崩通常由三个放大器叠加：

1. **资源耗尽**：线程池、连接池、内存被打满。
2. **重试放大**：客户端超时重试，让本已过载的服务雪上加霜（重试风暴）。
3. **依赖耦合**：同步调用链路任一环慢，整链阻塞。

```
   正常：1000 QPS，每个请求 50ms
   下游变慢到 500ms：
     → 线程池 10 倍积压
     → 上游连接超时 → 客户端重试 → QPS 变 2000
     → 下游更慢 → 死循环
```

### 3.4 狗皮膏药效应（Thundering Herd）

故障恢复瞬间，所有客户端同时重连 / 重试，再次打挂刚恢复的服务。

**解决**：抖动重试（Jitter）、指数退避、客户端限流。

### 3.5 伪可用

服务"在线"但实际不可用：返回 200 但数据错乱、响应超慢、依赖缓存但缓存过期。

**解决**：语义健康检查、SLO 监控、不要用 TCP 存活当健康检查。

---

## 四、面向可靠性的设计原则总览

```
                  故障发生时
                      │
        ┌─────────────┼─────────────┐
        ▼             ▼             ▼
   不让故障扩散     让系统降级运行    快速恢复
        │             │             │
   ┌────┴────┐   ┌────┴────┐   ┌────┴────┐
   │ 舱壁隔离 │   │ 限流    │   │ 故障转移│
   │ 熔断     │   │ 降级    │   │ 重试    │
   │ 超时     │   │ 背压    │   │ 健康检查│
   │ 幂等     │   │ 容错返回│   │ 优雅停机│
   └─────────┘   └─────────┘   └─────────┘
                      │
                      ▼
                  冗余设计
              (主备 / 多活 / Quorum)
```

| 原则 | 作用 | 对应章节 |
|------|------|---------|
| 冗余设计 | 消除单点 | 五 |
| 故障转移 | 自动切换到健康实例 | 六 |
| 超时控制 | 防止无限等待 | 七 |
| 重试与退避 | 应对瞬时故障 | 八 |
| 幂等性 | 重试不产生副作用 | 九 |
| 熔断器 | 防止级联故障 | 十 |
| 限流 | 保护过载 | 十一 |
| 降级 | 牺牲非核心保核心 | 十二 |
| 舱壁隔离 | 故障不扩散 | 十三 |
| 背压 | 慢消费者不拖死生产者 | 十四 |
| 健康检查 | 探测存活与就绪 | 十五 |
| 优雅停机 | 不丢请求 | 十六 |

---

## 五、冗余设计（Redundancy）

### 5.1 意图

通过**多副本**消除单点故障。任一副本失效，其他副本接替。

### 5.2 冗余形态

| 形态 | 说明 | 典型 |
|------|------|------|
| **冷备**（Cold Standby） | 备机不运行，故障时启动 | 数据库每日备份 |
| **温备**（Warm Standby） | 备机运行但不接流量，定期同步 | DB 从库 |
| **热备**（Hot Standby） | 备机实时同步，随时接管 | 主从 DB、Redis Sentinel |
| **多活**（Multi-Active） | 多实例同时接流量 | 异地多活、Active-Active |

### 5.3 冗余的代价

- **成本**：N 副本意味着 N 倍资源。
- **一致性**：多副本同步引入 CAP 取舍。
- **复杂度**：故障检测、切换逻辑、脑裂处理。

> **冗余度公式**：N+K 冗余表示 N 个工作副本 + K 个备用副本，能容忍 K 个失效。如 3 副本 Kafka 能容忍 1 个失效（2F+1 模式下 Quorum = 2）。

### 5.4 Java 版（多副本调用）

```java
public class ReplicaClient {
    private final List<String> endpoints;   // 多个副本地址
    private final ExecutorService executor;
    private final int timeoutMs;

    public ReplicaClient(List<String> endpoints, int timeoutMs) {
        this.endpoints = endpoints;
        this.timeoutMs = timeoutMs;
        this.executor = Executors.newFixedThreadPool(endpoints.size());
    }

    // 并行调用所有副本，取第一个成功响应
    public String callAny(String req) throws Exception {
        List<Future<String>> futures = new ArrayList<>();
        for (String ep : endpoints) {
            futures.add(executor.submit(() -> callOne(ep, req)));
        }
        Exception lastErr = null;
        for (Future<String> f : futures) {
            try {
                return f.get(timeoutMs, TimeUnit.MILLISECONDS);   // 第一个成功就返回
            } catch (Exception e) {
                lastErr = e;
            }
        }
        throw new RuntimeException("all replicas failed", lastErr);
    }

    // Quorum 写：多数成功即认为成功
    public boolean writeQuorum(String req) throws Exception {
        int success = 0;
        int quorum = endpoints.size() / 2 + 1;
        List<Future<Boolean>> futures = new ArrayList<>();
        for (String ep : endpoints) {
            futures.add(executor.submit(() -> writeOne(ep, req)));
        }
        for (Future<Boolean> f : futures) {
            try {
                if (f.get(timeoutMs, TimeUnit.MILLISECONDS)) {
                    success++;
                    if (success >= quorum) return true;
                }
            } catch (Exception ignored) { }
        }
        return false;
    }

    private String callOne(String ep, String req) { /* ... */ return "ok"; }
    private boolean writeOne(String ep, String req) { /* ... */ return true; }
}
```

### 5.5 Go 版

```go
type ReplicaClient struct {
    endpoints []string
    timeout   time.Duration
}

func NewReplicaClient(endpoints []string, timeout time.Duration) *ReplicaClient {
    return &ReplicaClient{endpoints: endpoints, timeout: timeout}
}

// 并发调用，任一成功即返回
func (c *ReplicaClient) CallAny(ctx context.Context, req string) (string, error) {
    ctx, cancel := context.WithTimeout(ctx, c.timeout)
    defer cancel()

    results := make(chan string, len(c.endpoints))
    for _, ep := range c.endpoints {
        go func(ep string) {
            resp, err := c.callOne(ctx, ep, req)
            if err == nil {
                select {
                case results <- resp:
                case <-ctx.Done():
                }
            }
        }(ep)
    }

    select {
    case r := <-results:
        return r, nil
    case <-ctx.Done():
        return "", errors.New("all replicas failed or timed out")
    }
}

// Quorum 写：多数成功即认为成功
func (c *ReplicaClient) WriteQuorum(ctx context.Context, req string) error {
    ctx, cancel := context.WithTimeout(ctx, c.timeout)
    defer cancel()

    var success int32
    quorum := len(c.endpoints)/2 + 1
    var wg sync.WaitGroup
    for _, ep := range c.endpoints {
        wg.Add(1)
        go func(ep string) {
            defer wg.Done()
            if err := c.writeOne(ctx, ep, req); err == nil {
                if atomic.AddInt32(&success, 1) >= int32(quorum) {
                    cancel()   // 够数了，取消其他
                }
            }
        }(ep)
    }
    wg.Wait()

    if int(success) >= quorum {
        return nil
    }
    return errors.New("quorum not reached")
}

func (c *ReplicaClient) callOne(ctx context.Context, ep, req string) (string, error) { /* ... */ return "ok", nil }
func (c *ReplicaClient) writeOne(ctx context.Context, ep, req string) error           { /* ... */ return nil }
```

> Go 的 `context.Context` + `errgroup` 是实现并发容错调用的标准武器。Java 21+ 的虚拟线程（Virtual Threads）让"一个请求一个线程"的简单写法重新可行。

---

## 六、故障转移（Failover）

### 6.1 意图

主节点故障时，**自动**把流量切换到备用节点，对用户透明。

### 6.2 故障转移的关键问题

| 问题 | 解决 |
|------|------|
| 如何检测主故障 | 心跳 + 多数投票（避免误判） |
| 如何选举新主 | Raft / Paxos / ZAB |
| 如何避免脑裂 | Quorum + Fencing Token |
| 如何同步数据 | 同步复制 / 半同步 / 异步 |
| 客户端如何感知 | DNS / 配置中心 / SDK 内置列表 |

### 6.3 脑裂（Split-Brain）

网络分区导致两个子网各自选举出主，数据不一致。

**解决**：**Fencing Token**（隔离令牌）——每次主切换，token 递增。旧主用旧 token 写入会被拒绝。

```
   主 A (token=5) ──网络分区── 主 B (token=6)
   客户端连到 A 写：A 用 token=5 提交
   存储层发现 5 < 6，拒绝 → A 写失败
```

### 6.4 故障转移的层级

| 层级 | 机制 | 典型 |
|------|------|------|
| DNS | 健康节点 IP 优先 | GSLB |
| 负载均衡 | 健康检查 + 自动摘除 | Nginx upstream、ALB |
| 服务发现 | 注册中心心跳摘除 | Nacos、Consul、Eureka |
| SDK | 客户端持有多个地址，失败重试下一个 | gRPC、Redis Sentinel |
| 数据库 | 主从切换 | MHA、Orchestrator、Sentinel |

### 6.5 Java 版（带故障转移的 HTTP 客户端）

```java
public class FailoverHttpClient {
    private final List<String> endpoints;
    private final HttpClient client = HttpClient.newBuilder()
            .connectTimeout(Duration.ofMillis(500))
            .build();

    public FailoverHttpClient(List<String> endpoints) { this.endpoints = endpoints; }

    public String get(String path) {
        Exception lastErr = null;
        for (String ep : endpoints) {
            try {
                HttpRequest req = HttpRequest.newBuilder()
                        .uri(URI.create(ep + path))
                        .timeout(Duration.ofSeconds(2))
                        .GET().build();
                HttpResponse<String> resp = client.send(req, HttpResponse.BodyHandlers.ofString());
                if (resp.statusCode() < 500) {
                    return resp.body();
                }
            } catch (Exception e) {
                lastErr = e;   // 失败，下一个
            }
        }
        throw new RuntimeException("all endpoints failed", lastErr);
    }
}
```

### 6.6 Go 版

```go
type FailoverClient struct {
    endpoints []string
    client    *http.Client
}

func NewFailoverClient(endpoints []string) *FailoverClient {
    return &FailoverClient{
        endpoints: endpoints,
        client: &http.Client{
            Timeout: 2 * time.Second,
        },
    }
}

func (c *FailoverClient) Get(ctx context.Context, path string) (string, error) {
    var lastErr error
    for _, ep := range c.endpoints {
        req, _ := http.NewRequestWithContext(ctx, "GET", ep+path, nil)
        resp, err := c.client.Do(req)
        if err == nil {
            if resp.StatusCode < 500 {
                defer resp.Body.Close()
                b, _ := io.ReadAll(resp.Body)
                return string(b), nil
            }
            resp.Body.Close()
            continue
        }
        lastErr = err
    }
    if lastErr != nil {
        return "", fmt.Errorf("all endpoints failed: %w", lastErr)
    }
    return "", errors.New("all endpoints returned 5xx")
}
```

> gRPC 客户端自带故障转移：配置多个 address，一个失败自动切换。

---

## 七、超时控制（Timeout）

### 7.1 意图

为每个外部调用设置**严格超时**，防止无限等待拖垮调用方。

> 这是 DFR 最便宜也最有效的手段。**没有超时的调用迟早会引发事故**。

### 7.2 难点：超时层级

```
   用户请求（10s）
     └─ 网关超时（8s）
         └─ 服务 A 超时（6s）
             └─ 服务 B 超时（4s）
                 └─ 数据库超时（2s）
```

**核心原则**：**下游超时必须小于上游超时**。否则下游还在等，上游已经超时返回，浪费资源。

### 7.3 超时类型

| 类型 | 含义 |
|------|------|
| 连接超时（Connect Timeout） | 建立 TCP 连接的超时 |
| 读取超时（Read Timeout） | 等待响应数据的超时 |
| 写入超时（Write Timeout） | 发送请求的超时 |
| 端到端超时（End-to-End） | 整个请求的总超时 |

### 7.4 超时设置经验

| 调用类型 | 建议超时 |
|---------|---------|
| 内存数据库（Redis） | 50-100ms |
| 关系数据库（SQL） | 200-500ms |
| 内部 RPC | 500ms-2s |
| 外部第三方 API | 3-5s |
| 用户请求端到端 | 5-10s |

> **超时不是越小越好**：过短会误杀慢请求，过长失去保护意义。应根据 P99 延迟设置：`timeout = P99 * 2`。

### 7.5 Java 版

```java
// 严格超时
public class UserService {
    private final HttpClient client = HttpClient.newBuilder()
            .connectTimeout(Duration.ofMillis(500))   // 连接超时
            .build();

    public User getUser(long id) {
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create("http://user-svc/" + id))
                .timeout(Duration.ofSeconds(2))   // 请求超时
                .GET().build();
        try {
            HttpResponse<String> resp = client.send(req, HttpResponse.BodyHandlers.ofString());
            return parse(resp.body());
        } catch (HttpTimeoutException e) {
            throw new ServiceUnavailableException("user service timeout", e);
        } catch (Exception e) {
            throw new ServiceUnavailableException("user service error", e);
        }
    }
}
```

### 7.6 Go 版

```go
func GetUser(ctx context.Context, id int64) (*User, error) {
    // 从父 context 派生子 context，加超时
    ctx, cancel := context.WithTimeout(ctx, 2*time.Second)
    defer cancel()

    req, _ := http.NewRequestWithContext(ctx, "GET",
        fmt.Sprintf("http://user-svc/%d", id), nil)
    resp, err := http.DefaultClient.Do(req)
    if err != nil {
        if errors.Is(err, context.DeadlineExceeded) {
            return nil, fmt.Errorf("user service timeout: %w", err)
        }
        return nil, fmt.Errorf("user service error: %w", err)
    }
    defer resp.Body.Close()
    // ...
    return user, nil
}
```

> Go 的 `context.WithTimeout` 是超时控制的金标准：超时自动取消，取消会沿调用链向下传播，所有下游操作一起中止。Java 没有内置等价物，靠 `Future.cancel()` 或响应式编程（Reactor）实现。

---

## 八、重试与退避（Retry & Backoff）

### 8.1 意图

瞬时故障（网络抖动、瞬时过载）下，**重试**可以提升成功率。但重试是双刃剑，必须配合**退避**和**抖动**。

### 8.2 难点：重试风暴

```
   下游过载，500ms 响应
   上游超时 2s，失败重试 3 次 → 实际 QPS 翻 4 倍
   下游更过载 → 雪崩
```

**重试三原则**：

1. **只重试可重试的错误**：网络错误、连接超时、5xx 可重试；4xx（参数错）、业务错不重试。
2. **指数退避 + 抖动**：避免重试同步化。
3. **限制重试次数和总时长**：默认 2-3 次。

### 8.3 退避策略

| 策略 | 公式 | 特点 |
|------|------|------|
| 固定间隔 | `delay = 1s` | 简单，但同步化严重 |
| 线性退避 | `delay = n * 1s` | 渐进 |
| 指数退避 | `delay = base * 2^n` | 最常用 |
| 指数退避 + 抖动 | `delay = base * 2^n * (0.5 + rand*0.5)` | 推荐，打散重试 |
| 装饰器退避（Decorrelated） | `delay = min(prev * 3, max)` | 自适应 |

### 8.4 Java 版

```java
public class Retryer {
    private final int maxAttempts;
    private final long baseDelayMs;
    private final long maxDelayMs;
    private final Random random = new Random();

    public Retryer(int maxAttempts, long baseMs, long maxMs) {
        this.maxAttempts = maxAttempts;
        this.baseDelayMs = baseMs;
        this.maxDelayMs = maxMs;
    }

    public <T> T call(Callable<T> fn) throws Exception {
        Exception lastErr = null;
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            try {
                return fn.call();
            } catch (Exception e) {
                if (!isRetryable(e) || attempt == maxAttempts - 1) throw e;
                lastErr = e;
                long delay = nextDelay(attempt);
                Thread.sleep(delay);
            }
        }
        throw new RuntimeException("unreachable", lastErr);
    }

    private long nextDelay(int attempt) {
        long exp = (long) (baseDelayMs * Math.pow(2, attempt));
        long capped = Math.min(exp, maxDelayMs);
        long jitter = (long) (capped * 0.5 * random.nextDouble());   // 0~50% 抖动
        return capped / 2 + jitter;   // 50%~100% 抖动
    }

    private boolean isRetryable(Exception e) {
        if (e instanceof HttpTimeoutException) return true;
        if (e instanceof ConnectException) return true;
        if (e instanceof HttpResponseException) {
            return ((HttpResponseException) e).getStatusCode() >= 500;
        }
        return false;
    }
}

// 使用
Retryer retryer = new Retryer(3, 100, 2000);
String result = retryer.call(() -> callRemote());
```

#### Spring Retry 注解版

```java
@Retryable(value = {ServiceUnavailableException.class},
           maxAttempts = 3,
           backoff = @Backoff(delay = 100, multiplier = 2, random = true))
public String callRemote() { /* ... */ }

@Recover
public String recover(ServiceUnavailableException e) {
    return "fallback";
}
```

### 8.5 Go 版

```go
type Retryer struct {
    MaxAttempts int
    BaseDelay   time.Duration
    MaxDelay    time.Duration
}

func (r *Retryer) Do(ctx context.Context, fn func(context.Context) error) error {
    var lastErr error
    for attempt := 0; attempt < r.MaxAttempts; attempt++ {
        err := fn(ctx)
        if err == nil {
            return nil
        }
        if !isRetryable(err) || attempt == r.MaxAttempts-1 {
            return err
        }
        lastErr = err

        delay := r.nextDelay(attempt)
        timer := time.NewTimer(delay)
        select {
        case <-ctx.Done():
            timer.Stop()
            return ctx.Err()
        case <-timer.C:
        }
    }
    return lastErr
}

func (r *Retryer) nextDelay(attempt int) time.Duration {
    exp := float64(r.BaseDelay) * math.Pow(2, float64(attempt))
    capped := time.Duration(math.Min(exp, float64(r.MaxDelay)))
    // Full Jitter: [0, capped]
    jitter := time.Duration(rand.Int63n(int64(capped)))
    return jitter
}

func isRetryable(err error) bool {
    var netErr net.Error
    if errors.As(err, &netErr) && netErr.Timeout() {
        return true
    }
    // 5xx 重试
    var httpErr *HTTPError
    if errors.As(err, &httpErr) && httpErr.Code >= 500 {
        return true
    }
    return false
}

// 使用
retryer := &Retryer{MaxAttempts: 3, BaseDelay: 100 * time.Millisecond, MaxDelay: 2 * time.Second}
err := retryer.Do(ctx, func(ctx context.Context) error {
    return callRemote(ctx)
})
```

### 8.6 重试的禁忌

| 禁忌 | 后果 |
|------|------|
| 非幂等接口直接重试 | 重复扣款、重复下单 |
| 不区分错误类型重试 | 业务错（4xx）也重试，浪费 |
| 同步重试无退避 | 重试风暴 |
| 重试层级过多（多层都重试） | 放大效应，1 次变 N^M 次 |
| 不限重试总时长 | 用户已超时，重试还在跑 |

> **重试预算**（Retry Budget）：限制单位时间内的重试比例，如重试不超过正常请求的 10%。Google SRE 推荐。

### 8.7 面试速答

- **Q：什么时候该重试？** A：网络抖动、连接失败、5xx 等瞬时错误重试；4xx、业务错不重试。
- **Q：为什么要加抖动？** A：避免所有客户端同步重试，再次压垮服务（狗皮膏药效应）。
- **Q：重试和熔断关系？** A：先熔断再重试。熔断器打开时直接失败，不重试，否则火上浇油。
- **Q：非幂等接口能重试吗？** A：默认不能。要么改成幂等，要么用幂等 token / 去重表保证。

---

## 九、幂等性（Idempotency）

### 9.1 意图

**同样请求执行一次和多次，效果相同**。这是重试的前提——非幂等接口重试会产生副作用。

### 9.2 幂等的本质

```
   f(f(x)) == f(x)
```

- 天然幂等：GET、PUT（覆盖写）、DELETE（第二次删返回 not found 也是幂等）。
- 非幂等：POST（新建）、增计数、扣余额（不带版本号）。

### 9.3 难点：分布式下的幂等

单机幂等靠数据库唯一约束，分布式幂等要解决：

1. **网络重试**：客户端超时重试，可能服务端已执行。
2. **重复消息**：MQ 至少一次投递（At-Least-Once），消费者会收到重复消息。
3. **并发请求**：同一请求被并发发送两次。

### 9.4 实现幂等的方法

| 方法 | 适用场景 | 实现 |
|------|---------|------|
| 唯一约束 | 新建场景 | DB 唯一索引 |
| Token 机制 | 表单提交 | 客户端先获取 token，提交时带 token，服务端原子校验+删除 |
| 状态机 | 流程性操作 | 状态只能向前流转，重复请求被状态拒绝 |
| 版本号 / 乐观锁 | 更新场景 | `UPDATE ... WHERE id=? AND version=?` |
| 去重表 | 通用 | 独立表记录已处理请求 ID |
| Redis SETNX | 短期去重 | `SET requestId NX EX 600` |

### 9.5 Java 版（Token + 去重表）

```java
@Service
public class OrderService {
    @Autowired private RedisTemplate<String, String> redis;
    @Autowired private OrderRepository repo;

    public Order createOrder(CreateOrderRequest req) {
        String requestId = req.getRequestId();
        if (requestId == null) throw new IllegalArgumentException("requestId required");

        // Redis SETNX 抢占（短期去重）
        Boolean ok = redis.opsForValue().setIfAbsent("idem:" + requestId, "1", Duration.ofMinutes(10));
        if (Boolean.FALSE.equals(ok)) {
            // 已处理过，返回上次结果
            return repo.findByRequestId(requestId).orElseThrow();
        }

        try {
            // 数据库唯一约束兜底（防止 Redis 失效）
            Order order = new Order(req);
            return repo.save(order);   // requestId 唯一索引
        } catch (DataIntegrityViolationException e) {
            // 并发场景：另一线程已写入
            return repo.findByRequestId(requestId).orElseThrow();
        }
    }
}
```

### 9.6 Go 版

```go
func (s *OrderService) CreateOrder(ctx context.Context, req *CreateOrderRequest) (*Order, error) {
    if req.RequestID == "" {
        return nil, errors.New("requestId required")
    }

    // Redis SETNX 抢占
    ok, err := s.redis.SetNX(ctx, "idem:"+req.RequestID, "1", 10*time.Minute).Result()
    if err != nil {
        return nil, fmt.Errorf("redis error: %w", err)
    }
    if !ok {
        // 已处理过
        return s.repo.FindByRequestID(ctx, req.RequestID)
    }

    // DB 唯一约束兜底
    order, err := s.repo.Create(ctx, req)
    if err != nil {
        if isDuplicateKey(err) {
            return s.repo.FindByRequestID(ctx, req.RequestID)
        }
        // 失败要回滚 Redis，允许下次重试
        s.redis.Del(ctx, "idem:"+req.RequestID)
        return nil, err
    }
    return order, nil
}
```

### 9.7 幂等的代价

- **存储成本**：去重表、Redis key 都要存。
- **延迟**：每次请求多一次查重。
- **一致性**：Redis 和 DB 之间的状态同步。

> 实践：**只对关键非幂等操作做幂等**（支付、下单、扣库存），CRUD 查询天然幂等不需要。

### 9.8 面试速答

- **Q：幂等的定义？** A：f(f(x)) == f(x)，多次执行效果相同。
- **Q：POST 一定非幂等吗？** A：不一定。带 requestId + 去重的 POST 是幂等的。HTTP 语义上 POST 非幂等，但业务可设计为幂等。
- **Q：支付接口怎么保证幂等？** A：客户端生成 requestId，服务端 Redis SETNX + DB 唯一约束兜底，重复请求返回上次结果。
- **Q：MQ 消费幂等？** A：消息带唯一 ID，消费者用去重表或 Redis 标记已处理，重复投递时跳过。

---

## 十、熔断器（Circuit Breaker）

### 10.1 意图

当下游持续故障时，**停止调用**直接快速失败，避免拖垮上游、给下游恢复时间。

### 10.2 三态机

```
              失败率超阈值
   ┌───────────────┐          ┌───────────────┐
   │   CLOSED      │─────────▶│    OPEN       │
   │  正常放行     │          │  直接失败     │
   └───────────────┘          └───────┬───────┘
        ▲                              │
        │ 成功                         │ 等待冷却时间
        │                              ▼
        │                       ┌───────────────┐
        └───────────────────────│  HALF_OPEN    │
              失败              │  半开试探      │
                                └───────────────┘
```

- **CLOSED**：正常调用，统计失败率。
- **OPEN**：失败率超阈值，直接拒绝请求，等待冷却时间。
- **HALF_OPEN**：冷却后放行少量请求试探，成功则 CLOSED，失败则 OPEN。

### 10.3 难点：统计窗口

| 算法 | 特点 |
|------|------|
| 计数器 | 简单，边界突变 |
| 滑动窗口 | 平滑，内存稍高 |
| 时间桶（Bucket） | 折中，Google 推荐 |

### 10.4 熔断条件

- **错误率**：错误率 > 阈值（如 50%）且请求量 > 最小样本数（防止少量请求误判）。
- **慢调用率**：RT > 阈值的请求占比超阈值。
- **连续失败**：连续 N 次失败立即熔断（敏感场景）。

### 10.5 Java 版（Resilience4j）

```java
CircuitBreakerConfig config = CircuitBreakerConfig.custom()
    .failureRateThreshold(50)                          // 失败率 50% 触发
    .slowCallRateThreshold(80)                         // 慢调用率 80% 触发
    .slowCallDurationThreshold(Duration.ofSeconds(2))  // 慢调用定义
    .minimumNumberOfCalls(20)                          // 最小样本数
    .slidingWindowSize(40)                             // 滑动窗口
    .slidingWindowType(SlidingWindowType.COUNT_BASED)
    .waitDurationInOpenState(Duration.ofSeconds(10))   // OPEN 持续时间
    .permittedNumberOfCallsInHalfOpenState(5)          // HALF_OPEN 试探数
    .build();

CircuitBreaker cb = CircuitBreaker.of("user-svc", config);

// 包装调用
Supplier<User> supplier = CircuitBreaker.decorateSupplier(cb,
    () -> callUserService(id));

Try<User> result = Try.ofSupplier(supplier)
    .recover(CallNotPermittedException.class, e -> fallbackUser());
```

#### Spring 注解版

```java
@CircuitBreaker(name = "user-svc", fallbackMethod = "fallback")
public User getUser(long id) {
    return callRemote(id);
}

public User fallback(long id, Throwable t) {
    return new User(id, "unknown");
}
```

### 10.6 Go 版（Sony gobreaker）

```go
import "github.com/sony/gobreaker"

var cb *gobreaker.CircuitBreaker

func init() {
    cb = gobreaker.NewCircuitBreaker(gobreaker.Settings{
        Name:        "user-svc",
        MaxRequests: 5,                                   // HALF_OPEN 试探数
        Interval:    60 * time.Second,                    // 统计周期
        Timeout:     10 * time.Second,                    // OPEN 持续时间
        ReadyToTrip: func(counts gobreaker.Counts) bool { // 触发条件
            ratio := float64(counts.TotalFailures) / float64(counts.Requests)
            return counts.Requests >= 20 && ratio >= 0.5
        },
        OnStateChange: func(name string, from, to gobreaker.State) {
            log.Printf("CB %s: %s -> %s", name, from, to)
        },
    })
}

func GetUser(ctx context.Context, id int64) (*User, error) {
    result, err := cb.Execute(func() (interface{}, error) {
        return callUserService(ctx, id)
    })
    if err != nil {
        // 熔断器打开时返回降级
        if errors.Is(err, gobreaker.ErrOpenState) {
            return fallbackUser(id), nil
        }
        return nil, err
    }
    return result.(*User), nil
}
```

### 10.7 熔断 vs 限流

| 维度 | 熔断 | 限流 |
|------|------|------|
| 触发条件 | 下游故障 | 总流量超载 |
| 目的 | 保护下游 + 自身 | 保护自身 |
| 状态 | 有状态（OPEN/CLOSED） | 通常无状态 |
| 恢复 | 自动 HALF_OPEN 试探 | 流量降低自动恢复 |

### 10.8 面试速答

- **Q：熔断器的三个状态？** A：CLOSED 正常、OPEN 直接失败、HALF_OPEN 半开试探。
- **Q：熔断和限流区别？** A：熔断是下游故障触发，停止调用保护下游；限流是自身过载触发，拒绝部分请求保护自身。
- **Q：HALF_OPEN 的作用？** A：试探下游是否恢复，避免一直 OPEN 不恢复，也避免突然全量放行再次打挂。
- **Q：熔断器失败率怎么算？** A：滑动窗口内失败数 / 总数，需达到最小样本数才生效，防止误判。
- **Q：熔断打开后客户端怎么办？** A：返回降级（缓存、默认值、友好提示），不能让用户看到 500。

---

## 十一、限流（Rate Limiting）

### 11.1 意图

限制单位时间内的请求数，保护系统不被流量打垮。

### 11.2 限流维度

| 维度 | 例子 |
|------|------|
| 全局限流 | 整个服务 10000 QPS |
| 单机限流 | 每台机器 1000 QPS |
| 接口限流 | 下单接口 100 QPS |
| 用户限流 | 单用户 10 QPS |
| IP 限流 | 单 IP 100 QPS |
| 租户限流 | 每个租户独立配额 |

### 11.3 限流算法

#### 11.3.1 固定窗口（Fixed Window）

```
   |--------|--------|--------|
   0s      1s      2s      3s
   每秒最多 100 请求
```

**问题**：边界突刺——0.99s 来 100 个，1.01s 又来 100 个，0.02s 内 200 个请求。

#### 11.3.2 滑动窗口（Sliding Window）

把窗口切成多个小格子，统计最近 N 个格子的总数。比固定窗口平滑。

```
   |--|--|--|--|--|--|
   0  0.2 0.4 0.6 0.8 1.0
   统计最近 5 个格子
```

#### 11.3.3 漏桶（Leaky Bucket）

请求进桶，以**固定速率**漏出。超出桶容量直接拒绝。

```
   请求流 ──▶ [漏桶] ──▶ 固定速率出桶
              容量 N
```

特点：**平滑输出**，但无法应对突发流量。

#### 11.3.4 令牌桶（Token Bucket）

以固定速率往桶里放令牌，请求拿走令牌才处理。桶满则丢令牌。

```
   每秒 r 个令牌 ──▶ [桶容量 N]
                          │
   请求 ──▶ 取令牌 ──▶ 处理 / 拒绝
```

特点：**允许突发**（桶里令牌多时），但平均速率受控。**最常用**。

#### 11.3.5 算法对比

| 算法 | 平滑性 | 突发支持 | 实现 | 适用 |
|------|--------|---------|------|------|
| 固定窗口 | 差 | 边界突刺 | 最简 | 粗粒度 |
| 滑动窗口 | 好 | 有限 | 中等 | 通用 |
| 漏桶 | 最好 | 无 | 中等 | 流量整形 |
| 令牌桶 | 好 | 有 | 中等 | API 限流（推荐） |

### 11.4 分布式限流

单机限流靠内存计数器，分布式限流需要共享存储：

| 方案 | 实现 | 特点 |
|------|------|------|
| Redis INCR + EXPIRE | 简单计数 | 有竞态，不推荐 |
| Redis + Lua | 原子操作 | 推荐 |
| Redis-Cell | 令牌桶模块 | 高级 |
| Sentinel | 阿里开源 | 集群限流 |
| 网关限流 | Nginx limit_req | 入口层 |

#### Redis + Lua 令牌桶

```lua
-- key: 限流 key, rate: 速率, capacity: 桶容量, now: 当前时间, requested: 申请令牌数
local key = KEYS[1]
local rate = tonumber(ARGV[1])
local capacity = tonumber(ARGV[2])
local now = tonumber(ARGV[3])
local requested = tonumber(ARGV[4])

local bucket = redis.call("HMGET", key, "tokens", "timestamp")
local tokens = tonumber(bucket[1]) or capacity
local last = tonumber(bucket[2]) or now

-- 按时间差补充令牌
local delta = math.max(0, now - last)
tokens = math.min(capacity, tokens + delta * rate)

local allowed = 0
if tokens >= requested then
    tokens = tokens - requested
    allowed = 1
end

redis.call("HMSET", key, "tokens", tokens, "timestamp", now)
redis.call("EXPIRE", key, math.ceil(capacity / rate * 2))

return allowed
```

### 11.5 Java 版（Guava 令牌桶）

```java
// 单机限流
RateLimiter limiter = RateLimiter.create(100);   // 100 QPS

public void handle() {
    if (!limiter.tryAcquire(1, 100, TimeUnit.MILLISECONDS)) {
        throw new TooManyRequestsException();
    }
    // 处理请求
}

// 预热令牌桶：避免冷启动突刺
RateLimiter warmup = RateLimiter.create(100, 10, TimeUnit.SECONDS);
```

#### 分布式限流（Redisson）

```java
RRateLimiter limiter = redisson.getRateLimiter("api:user:123");
limiter.trySetRate(RateType.OVERALL, 100, 1, RateIntervalUnit.SECONDS);

if (!limiter.tryAcquire(1)) {
    throw new TooManyRequestsException();
}
```

### 11.6 Go 版（golang.org/x/time/rate）

```go
import "golang.org/x/time/rate"

// 单机令牌桶
var limiter = rate.NewLimiter(rate.Limit(100), 10)   // 100 QPS，桶容量 10

func Handle(w http.ResponseWriter, r *http.Request) {
    if !limiter.Allow() {
        http.Error(w, "too many requests", http.StatusTooManyRequests)
        return
    }
    // 处理
}

// 按 IP 限流
var limiters sync.Map

func getLimiter(ip string) *rate.Limiter {
    v, _ := limiters.LoadOrStore(ip, rate.NewLimiter(rate.Limit(10), 5))
    return v.(*rate.Limiter)
}
```

### 11.7 限流响应

| 行为 | 含义 |
|------|------|
| 拒绝（Reject） | 返回 429 Too Many Requests |
| 排队（Queue） | 请求等待，超时再拒绝 |
| 降级（Degrade） | 返回缓存 / 默认值 |

### 11.8 面试速答

- **Q：令牌桶和漏桶区别？** A：令牌桶允许突发（桶里令牌多时一次取多个），漏桶强制固定速率输出。API 限流推荐令牌桶。
- **Q：固定窗口的边界突刺问题？** A：窗口边界两侧各来 N 个请求，瞬间 2N 流量。滑动窗口缓解。
- **Q：分布式限流怎么做？** A：Redis + Lua 原子操作，或 Sentinel 集群限流。注意时钟同步问题。
- **Q：限流被触发后怎么办？** A：429 + Retry-After 头；或排队等待；或降级返回缓存。

---

## 十二、降级（Graceful Degradation）

### 12.1 意图

系统过载或部分故障时，**牺牲非核心功能**或**降低质量**，保住核心功能可用。

### 12.2 降级策略

| 策略 | 例子 |
|------|------|
| **功能降级** | 关闭推荐、评论、个性化 |
| **数据降级** | 返回缓存而非实时数据 |
| **精度降级** | 推荐列表少返回几条 |
| **异步降级** | 同步改异步（下单后异步送积分） |
| **默认值降级** | 用户信息查不到返回默认 |
| **静态降级** | 返回静态兜底页 |

### 12.3 降级触发方式

| 方式 | 触发条件 |
|------|---------|
| 手动开关 | 运维通过配置中心一键降级 |
| 自动降级 | 超时、熔断、限流后自动降级 |
| 定时降级 | 大促期间定时关闭非核心 |
| 依赖降级 | 下游故障时自动降级 |

### 12.4 降级层次

```
   业务层：核心链路保住，非核心关闭
       │
   服务层：依赖故障返回缓存/默认
       │
   接口层：超时返回兜底
       │
   数据层：主库故障读从库
```

### 12.5 Java 版

```java
public class ProductService {
    @Autowired private ProductRepo repo;
    @Autowired private RecommendService recommend;
    @Autowired private Cache cache;
    @Autowired private CircuitBreaker cb;

    public ProductDetail getDetail(long id) {
        Product p;
        try {
            p = cb.executeSupplier(() -> repo.findById(id));
        } catch (Exception e) {
            // 降级：读缓存
            p = cache.get("product:" + id, Product.class);
            if (p == null) throw new ServiceUnavailableException();
        }

        // 推荐列表降级：失败返回空
        List<Long> recommends = List.of();
        try {
            recommends = recommend.recommend(id);
        } catch (Exception ignored) {
            // 非核心，降级为空
        }

        return new ProductDetail(p, recommends);
    }
}
```

### 12.6 Go 版

```go
func (s *ProductService) GetDetail(ctx context.Context, id int64) (*ProductDetail, error) {
    // 核心数据：必须成功，失败则读缓存
    p, err := s.repo.FindByID(ctx, id)
    if err != nil {
        if cached, cerr := s.cache.Get(ctx, "product:"+strconv.FormatInt(id, 10)); cerr == nil {
            p = cached
        } else {
            return nil, fmt.Errorf("product unavailable: %w", err)
        }
    }

    // 非核心推荐：失败降级为空
    recommends := []int64{}
    recCtx, cancel := context.WithTimeout(ctx, 200*time.Millisecond)
    defer cancel()
    if recs, err := s.recommend.Recommend(recCtx, id); err == nil {
        recommends = recs
    }
    // 失败不抛错，静默降级

    return &ProductDetail{Product: p, Recommends: recommends}, nil
}
```

### 12.7 降级 vs 熔断 vs 限流

| 机制 | 触发 | 行为 |
|------|------|------|
| 熔断 | 下游故障 | 停止调用，直接失败或降级 |
| 限流 | 流量超载 | 拒绝部分请求 |
| 降级 | 故障 / 大促 | 牺牲非核心，保核心 |

> 三者协同：**限流防过载 → 熔断防级联 → 降级保核心**。

### 12.8 面试速答

- **Q：降级的核心思想？** A：牺牲非核心保核心，牺牲质量保可用。
- **Q：降级和熔断区别？** A：熔断是停止调用；降级是返回兜底。熔断后通常搭配降级返回兜底。
- **Q：怎么决定哪些功能可降级？** A：分级——P0 核心（下单、支付）不降；P1 重要（推荐、评论）可降级；P2 边缘（统计、通知）可关。

---

## 十三、舱壁隔离（Bulkhead）

### 13.1 意图

把系统资源**划分成多个独立池**，某个池被打爆不影响其他池。源自船舱隔离设计——一舱进水不沉船。

### 13.2 隔离方式

| 方式 | 含义 |
|------|------|
| 线程池隔离 | 不同依赖用不同线程池 |
| 信号量隔离 | 限制并发数 |
| 进程隔离 | 不同服务独立进程 |
| 容器隔离 | Docker / namespace |
| 物理隔离 | 不同机房、不同集群 |

### 13.3 难点：线程池隔离 vs 信号量隔离

| 维度 | 线程池 | 信号量 |
|------|--------|--------|
| 开销 | 高（线程上下文切换） | 低 |
| 超时控制 | 强（异步可中断） | 弱（需调用方主动） |
| 异步支持 | 好 | 不支持 |
| 适用 | 跨进程调用 | 同进程内部 |

### 13.4 反例：共享线程池

```java
// 共享线程池：一个慢调用拖死所有
ExecutorService pool = Executors.newFixedThreadPool(100);

// 业务 A 调下游，下游慢，A 占满 100 线程
pool.submit(() -> slowRemoteCall());

// 业务 B 调下游，无可用线程，全部排队 → B 不可用
pool.submit(() -> fastCall());
```

### 13.5 正例：独立线程池

```java
ExecutorService poolA = Executors.newFixedThreadPool(50);
ExecutorService poolB = Executors.newFixedThreadPool(50);

// A 慢只占 A 池，B 池不受影响
poolA.submit(() -> slowRemoteCall());
poolB.submit(() -> fastCall());
```

### 13.6 Java 版（Resilience4j Bulkhead）

```java
// 线程池隔离
BulkheadConfig config = BulkheadConfig.custom()
    .maxConcurrentCalls(50)            // 最大并发
    .maxWaitDuration(Duration.ofMillis(100))
    .build();

Bulkhead bulkhead = Bulkhead.of("user-svc", config);
Supplier<User> supplier = Bulkhead.decorateSupplier(bulkhead, () -> callRemote());

// 信号量隔离（轻量）
SemaphoreBulkhead sem = SemaphoreBulkhead.of("internal", config);
```

### 13.7 Go 版

Go 的 goroutine 非常轻量，无需显式线程池。但**并发数仍需限制**——用 channel 当信号量：

```go
type Bulkhead struct {
    sem chan struct{}
}

func New(maxConcurrency int) *Bulkhead {
    return &Bulkhead{sem: make(chan struct{}, maxConcurrency)}
}

func (b *Bulkhead) Do(fn func() error) error {
    select {
    case b.sem <- struct{}{}:
        defer func() { <-b.sem }()
        return fn()
    default:
        return errors.New("bulkhead full")
    }
}

// 带超时
func (b *Bulkhead) DoWithTimeout(ctx context.Context, fn func(context.Context) error) error {
    select {
    case b.sem <- struct{}{}:
        defer func() { <-b.sem }()
        return fn(ctx)
    case <-ctx.Done():
        return ctx.Err()
    }
}

// 使用
bh := New(50)
err := bh.Do(func() error { return callRemote() })
```

### 13.8 应用场景

- 不同租户隔离：大客户不能拖死小客户。
- 不同业务隔离：评论业务打挂不影响下单。
- 不同依赖隔离：第三方 API 不稳定，不能拖垮自身。
- 不同优先级隔离：低优先级请求不抢占高优先级。

### 13.9 面试速答

- **Q：舱壁隔离解决什么？** A：把资源分池，避免一个慢依赖拖垮整个服务的所有依赖。
- **Q：线程池隔离和信号量隔离怎么选？** A：跨进程调用用线程池（异步可超时）；同进程用信号量（开销低）。
- **Q：Hystrix 默认用哪种？** A：默认线程池隔离，性能开销大但隔离强。Resilience4j 信号量更轻量。
- **Q：Go 怎么做隔离？** A：goroutine 轻量，用 channel 当信号量限并发；不同业务用不同 channel。

---

## 十四、背压（Backpressure）

### 14.1 意图

当下游消费速度 < 上游生产速度时，**反向施加压力**让上游减速，而不是无限堆积导致 OOM。

### 14.2 难点：处理策略

| 策略 | 含义 |
|------|------|
| 缓冲（Buffer） | 队列暂存，有界队列满后再决策 |
| 丢弃（Drop） | 新消息丢弃 |
| 丢弃最新（Drop Latest） | 丢弃新消息，保旧 |
| 丢弃最旧（Drop Oldest） | 丢旧保新 |
| 阻塞（Block） | 生产者阻塞等待 |
| 错误（Error） | 直接报错 |
| 降级采样（Sample） | 按时间窗口取代表值 |

### 14.3 Java 版（Reactor）

```java
// 有界缓冲，背压策略
Flux<Integer> flux = Flux.range(1, Integer.MAX_VALUE)
    .onBackpressureBuffer(1000,                 // 缓冲区
        n -> log.warn("dropped: {}", n),         // 溢出回调
        BufferOverflowStrategy.DROP_OLDEST)
    .subscribeOn(Schedulers.boundedElastic())
    .publishOn(Schedulers.single(), 64);         // 下游预取 64

// 慢消费者订阅
flux.subscribe(new BaseSubscriber<Integer>() {
    @Override
    protected void hookOnSubscribe(Subscription s) {
        request(10);   // 一次要 10 个
    }
    @Override
    protected void hookOnNext(Integer value) {
        try { Thread.sleep(100); } catch (Exception ignored) {}   // 慢消费
        request(10);   // 处理完再要
    }
});
```

### 14.4 Go 版

Go channel 天然是背压机制——**有缓冲 channel 满了就阻塞生产者**。

```go
// 有缓冲 channel 当背压
ch := make(chan int, 1000)

// 生产者：channel 满则阻塞
go func() {
    for i := 0; ; i++ {
        ch <- i   // 满了会阻塞，自然背压
    }
}()

// 消费者：慢消费
for v := range ch {
    time.Sleep(100 * time.Millisecond)
    process(v)
}

// 非阻塞 + 丢弃策略
select {
case ch <- v:
    // 成功
default:
    log.Warn("dropped")   // 满了丢弃
}
```

### 14.5 背压 vs 限流

| 维度 | 背压 | 限流 |
|------|------|------|
| 触发方向 | 下游 → 上游反向 | 入口处 |
| 目的 | 防止堆积 | 防止过载 |
| 机制 | 阻塞 / 反馈 | 拒绝 / 排队 |

### 14.6 面试速答

- **Q：背压解决什么？** A：生产快于消费时防止无限堆积 OOM，通过反压让生产者减速。
- **Q：有界队列满了怎么办？** A：阻塞、丢弃、降级采样、错误——按业务选。日志类丢弃最旧，订单类阻塞。
- **Q：Go channel 为什么是天然的背压？** A：有缓冲 channel 满了，发送方阻塞，自动减速。
- **Q：Reactive Streams 标准的背压怎么实现？** A：消费者通过 `request(n)` 声明能处理多少，生产者按需推送。

---

## 十五、健康检查（Health Check）

### 15.1 意图

让负载均衡 / 编排系统**探测服务状态**，自动摘除不健康实例。

### 15.2 两类探针

| 探针 | 含义 | K8s |
|------|------|-----|
| **存活探针（Liveness）** | 进程是否活着，失败重启 | livenessProbe |
| **就绪探针（Readiness）** | 是否能接流量，失败摘除 | readinessProbe |
| **启动探针（Startup）** | 启动是否完成，慢启动应用 | startupProbe |

> 关键区分：
> - **Liveness 失败 → 重启容器**（修复死锁等）。
> - **Readiness 失败 → 从 Service 摘除**（不重启，等恢复）。
> - 误用 Liveness 当 Readiness：服务还没 ready 就被重启，永远起不来。

### 15.3 健康检查的层次

| 层次 | 检查内容 |
|------|---------|
| 进程存活 | TCP 端口能连 |
| HTTP 健康端点 | `/health` 返回 200 |
| 依赖检查 | DB / Redis / 下游可达 |
| 业务就绪 | 缓存预热完成、配置加载完成 |

### 15.4 反例：健康检查太重

```java
// 反例：健康检查每次查 DB + Redis + 调下游
@GetMapping("/health")
public ResponseEntity health() {
    db.query("SELECT 1");
    redis.ping();
    downstream.check();
    return ResponseEntity.ok();
}
```

问题：健康检查频繁调用，反而把依赖打挂；任一依赖抖动就被摘除，雪崩。

### 15.5 正例：分级健康检查

```java
@RestController
@RequestMapping("/health")
public class HealthController {
    @Autowired private DbHealth dbHealth;
    @Autowired private RedisHealth redisHealth;

    // 轻量：进程活着
    @GetMapping("/live")
    public ResponseEntity liveness() {
        return ResponseEntity.ok("alive");
    }

    // 中等：自身依赖
    @GetMapping("/ready")
    public ResponseEntity readiness() {
        if (!dbHealth.isHealthy() || !redisHealth.isHealthy()) {
            return ResponseEntity.status(503).body("not ready");
        }
        return ResponseEntity.ok("ready");
    }

    // 完整：所有依赖（仅供运维查询，不接 LB）
    @GetMapping
    public HealthReport full() {
        return HealthReport.builder()
            .db(dbHealth.detail())
            .redis(redisHealth.detail())
            .build();
    }
}
```

### 15.6 Go 版

```go
type HealthChecker interface {
    Check(ctx context.Context) error
}

type HealthHandler struct {
    checkers map[string]HealthChecker
}

func (h *HealthHandler) Liveness(w http.ResponseWriter, r *http.Request) {
    w.WriteHeader(http.StatusOK)
    w.Write([]byte("alive"))
}

func (h *HealthHandler) Readiness(w http.ResponseWriter, r *http.Request) {
    ctx, cancel := context.WithTimeout(r.Context(), 500*time.Millisecond)
    defer cancel()

    status := http.StatusOK
    failed := []string{}
    for name, c := range h.checkers {
        if err := c.Check(ctx); err != nil {
            failed = append(failed, name)
            status = http.StatusServiceUnavailable
        }
    }

    w.WriteHeader(status)
    json.NewEncoder(w).Encode(map[string]any{
        "status": status,
        "failed": failed,
    })
}
```

### 15.7 健康检查的坑

| 坑 | 解决 |
|----|------|
| 依赖检查太敏感，抖动就摘除 | 健康检查失败阈值（连续 N 次才摘） |
| 依赖检查太重，反向打挂依赖 | 缓存上次结果，间隔检查 |
| 摘除后无法恢复 | 探针恢复阈值，连续成功才恢复 |
| 假阴性（健康检查误报） | TCP 检查不够，加语义检查 |
| 假阳性（健康但实际不可用） | 检查关键路径而非无关组件 |

### 15.8 面试速答

- **Q：Liveness 和 Readiness 区别？** A：Liveness 决定是否重启，Readiness 决定是否接流量。
- **Q：用 Liveness 检查 DB 好不好？** A：不好。DB 抖动 → Liveness 失败 → Pod 重启 → 重启完 DB 还没好 → 又重启。应该用 Readiness。
- **Q：健康检查应该检查什么？** A：自身进程存活 + 关键依赖可达。不要检查下游所有依赖，避免级联误判。
- **Q：K8s 探针的失败阈值？** A：`failureThreshold` 连续失败 N 次才认为失败，避免抖动误判。

---

## 十六、优雅停机（Graceful Shutdown）

### 16.1 意图

进程收到终止信号后，**停止接新请求**，**等待在途请求处理完**，再退出。避免请求丢失和连接中断。

### 16.2 停机流程

```
   SIGTERM 信号
       │
       ▼
   1. 从注册中心注销（不再接新流量）
       │
       ▼
   2. 停止接受新请求
       │
       ▼
   3. 等待在途请求处理完（带超时）
       │
       ▼
   4. 关闭资源（DB 连接、MQ 消费者）
       │
       ▼
   5. 退出进程
```

### 16.3 难点：K8s 下的优雅停机

K8s 删除 Pod 时序：

1. Pod 状态变 Terminating。
2. 从 Service endpoints 摘除（异步，可能延迟）。
3. 发送 SIGTERM。
4. 等待 `terminationGracePeriodSeconds`（默认 30s）。
5. SIGKILL 强杀。

**坑**：摘除是异步的，SIGTERM 后可能还有流量进来。解决：

- 收到 SIGTERM 后**先等几秒**再停止接请求（等摘除生效）。
- 配合 `preStop` hook：`sleep 5` 让摘除先完成。

```yaml
spec:
  terminationGracePeriodSeconds: 60
  containers:
    - name: app
      lifecycle:
        preStop:
          exec:
            command: ["sh", "-c", "sleep 5 && curl -X POST http://localhost:8080/drain"]
```

### 16.4 Java 版（Spring Boot）

```java
@Component
public class GracefulShutdown implements ApplicationListener<ContextClosedEvent> {
    @Autowired private TomcatConnectorCustomizer connector;
    private volatile boolean shuttingDown = false;

    public boolean isShuttingDown() { return shuttingDown; }

    @Override
    public void onApplicationEvent(ContextClosedEvent event) {
        shuttingDown = true;
        // 1. 暂停接受新请求
        connector.getConnector().pause();
        // 2. 等待在途请求
        Executor executor = connector.getConnector().getProtocolHandler().getExecutor();
        if (executor instanceof ThreadPoolExecutor) {
            ThreadPoolExecutor tpe = (ThreadPoolExecutor) executor;
            tpe.shutdown();
            try {
                tpe.awaitTermination(30, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        // 3. 注销注册中心、关闭资源
        deregisterFromDiscovery();
    }
}
```

Spring Boot 2.3+ 内置优雅停机：

```yaml
server:
  shutdown: graceful          # 优雅停机
spring:
  lifecycle:
    timeout-per-shutdown-phase: 30s
```

### 16.5 Go 版

```go
type Server struct {
    httpServer *http.Server
    consumers  []Consumer
    registry   ServiceRegistry
}

func (s *Server) Run() error {
    // 注册到注册中心
    s.registry.Register()

    // 监听信号
    stop := make(chan os.Signal, 1)
    signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

    go func() {
        if err := s.httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
            log.Fatal(err)
        }
    }()

    <-stop   // 等待 SIGTERM
    log.Println("shutting down...")

    // 1. 注销
    s.registry.Deregister()
    time.Sleep(5 * time.Second)   // 等摘除生效

    // 2. 停止接受新请求 + 等待在途
    ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
    defer cancel()
    if err := s.httpServer.Shutdown(ctx); err != nil {
        log.Printf("shutdown error: %v", err)
    }

    // 3. 停止 MQ 消费者
    for _, c := range s.consumers {
        c.Stop(ctx)
    }

    log.Println("server stopped")
    return nil
}
```

### 16.6 面试速答

- **Q：为什么 SIGTERM 后要 sleep 几秒？** A：K8s 摘除 endpoints 是异步的，立即停机会丢摘除延迟期间进来的请求。
- **Q：优雅停机的超时怎么设？** A：应大于 P99 处理时间，配合 K8s `terminationGracePeriodSeconds` 留余量。一般 20-30s。
- **Q：在途长任务怎么办？** A：检查点机制（Checkpoint），停机前持久化进度，重启后从断点继续。
- **Q：MQ 消费者怎么优雅停？** A：停止拉取新消息，处理完当前批次，提交 offset，再关闭。

---

## 十七、数据可靠性

### 17.1 数据丢失的来源

| 来源 | 例子 |
|------|------|
| 写丢失 | DB 主从延迟，主挂了未同步的写丢 |
| 缓存击穿 | 缓存过期瞬间大量请求穿透 |
| 消息丢失 | MQ 投递丢失、消费者崩溃未 ack |
| 存储损坏 | 磁盘损坏、bit rot |

### 17.2 数据可靠性手段

#### 17.2.1 副本（Replication）

- **同步复制**：主写多副本成功才返回。强一致，延迟高。
- **异步复制**：主写完即返回，后台同步。性能好，可能丢数据。
- **半同步**：至少一个副本确认才返回。MySQL 半同步。

#### 17.2.2 纠删码（Erasure Coding）

数据切成 N 块，生成 K 块校验，丢失任意 K 块仍可恢复。比副本省空间（N=10,K=4 时空间利用率 71%，而 3 副本只有 33%）。HDFS、Ceph、对象存储常用。

#### 17.2.3 WAL（Write-Ahead Log）

写操作先记日志，再落盘。崩溃后重放日志恢复。

```
   写请求 → 写 WAL → 写内存 → 返回成功
                ↓（异步）
              落盘数据文件
   崩溃后：读 WAL 重放未落盘的操作
```

#### 17.2.4 CRC / 校验和

存储数据时计算校验和，读取时验证。检测 bit rot（静默数据损坏）。ZFS、Ceph、对象存储都用。

#### 17.2.5 持久化保证级别

| 级别 | 含义 |
|------|------|
| MEMORY_ONLY | 只在内存，重启丢 |
| ASYNC_PERSIST | 异步刷盘，崩溃可能丢 |
| SYNC_PERSIST | 同步刷盘，不丢 |
| REPLICATED_SYNC | 同步副本，多机都不丢才返回 |

### 17.3 MQ 消息可靠性

#### 生产端

- **确认机制**：生产者等 broker ack 才认为发送成功。
- **重试 + 幂等**：失败重试，broker 端去重。
- **事务消息**：本地事务 + 消息发送原子化。

#### Broker

- **持久化**：消息落盘。
- **副本**：多副本同步。

#### 消费端

- **手动 ack**：处理完再 ack，不自动 ack。
- **死信队列**：处理失败的消息进 DLQ，避免无限重试。
- **幂等消费**：重复消息不产生副作用。

### 17.4 缓存可靠性

| 问题 | 解决 |
|------|------|
| 缓存穿透（查不存在） | 布隆过滤器、空值缓存 |
| 缓存击穿（热 key 过期） | 互斥锁、永不过期 + 后台刷新 |
| 缓存雪崩（大量同时过期） | 过期时间加随机、多级缓存 |
| 缓存与 DB 不一致 | Cache Aside、延迟双删、订阅 binlog |

---

## 十八、可观测性（Observability）

可观测性是 DFR 的"感官系统"——没有可观测性，可靠性设计就是瞎子。

### 18.1 三支柱

| 支柱 | 含义 | 工具 |
|------|------|------|
| **Metrics** | 聚合数值，监控告警 | Prometheus、Micrometer |
| **Logging** | 离散事件日志 | ELK、Loki |
| **Tracing** | 跨服务调用链 | Jaeger、SkyWalking、OpenTelemetry |

### 18.2 黄金信号（Google SRE 四大信号）

| 信号 | 含义 | 监控什么 |
|------|------|---------|
| **延迟（Latency）** | 请求处理时间 | P50 / P99 / P999 |
| **流量（Traffic）** | 请求量 | QPS / 并发数 |
| **错误（Errors）** | 失败率 | 5xx 比例 / 业务错率 |
| **饱和度（Saturation）** | 资源利用率 | CPU / 内存 / 连接数 / 队列深度 |

### 18.3 SLO / SLI / SLA

| 术语 | 含义 | 例子 |
|------|------|------|
| **SLI** | 服务水平指标（测什么） | P99 延迟、可用性 |
| **SLO** | 服务水平目标（目标值） | P99 < 200ms、可用性 99.9% |
| **SLA** | 服务水平协议（违约赔偿） | 可用性 < 99.9% 退款 10% |

> 关键：**SLO < SLA**。SLO 是内部目标，SLA 是对外承诺，留 buffer 防止违约。

### 18.4 错误预算（Error Budget）

可用性 99.9% → 每月允许 43 分钟停机。这 43 分钟是"错误预算"，可以用来：

- 冒险发布新功能。
- 主动故障演练。
- 上线有风险但收益高的变更。

预算耗尽 → 停止冒险发布，专注稳定性。

### 18.5 告警原则

| 原则 | 含义 |
|------|------|
| 基于症状告警 | 告"用户感知问题"而非"原因"（如 P99 高，而非 CPU 高） |
| 减少告警噪音 | 只告需要人介入的 |
| 告警可执行 | 每条告警有 runbook |
| 分级告警 | P0 电话、P1 IM、P2 记录 |

> 反例：告"CPU > 80%"——可能是正常峰值，无需介入。应告"P99 延迟 > 1s 持续 5 分钟"。

---

## 十九、混沌工程（Chaos Engineering）

### 19.1 意图

**主动注入故障**，验证系统在故障下的表现，发现隐藏的可靠性缺陷。

> Netflix Chaos Monkey 是先驱：随机杀生产环境的实例，逼团队写出能容忍节点失效的代码。

### 19.2 混沌实验五步法

1. **假设**：系统在 X 故障下应仍满足 SLO。
2. **设计实验**：注入什么故障、范围多大。
3. **执行**：在生产的**小范围**先做。
4. **观测**：监控 SLO 是否被违反。
5. **修复**：发现缺陷，改进系统。

### 19.3 故障注入类型

| 类型 | 工具 |
|------|------|
| 杀进程 | Chaos Monkey |
| 网络延迟 / 丢包 | tc、Toxiproxy |
| 网络分区 | Chaos Mesh |
| CPU / 内存压力 | Stress-ng |
| DNS 故障 | 修改 /etc/hosts |
| 磁盘满 | dd if=/dev/zero |
| 时钟漂移 | chrony 偏移 |

### 19.4 渐进式演练

```
   单实例故障 → 多实例故障 → 可用区故障 → 区域故障
       │
       ▼
   非生产环境 → 生产小范围 → 生产大范围
```

**绝对不要**在生产直接搞大规模演练——先在测试环境验证，再小范围生产，逐步放大。

### 19.5 面试速答

- **Q：混沌工程的价值？** A：主动发现隐藏缺陷，验证容错设计有效，比等真实故障便宜。
- **Q：混沌工程和压力测试区别？** A：压测验证"正常流量下表现"；混沌验证"故障下表现"。
- **Q：混沌实验的边界？** A：必须有 SLO 监控、爆炸半径可控、能快速回滚。不在没有监控的系统上做。

---

## 二十、可靠性设计自检清单

设计任何系统前过一遍：

### 20.1 单点与冗余
- [ ] 是否有 SPOF？
- [ ] 关键组件是否多副本？
- [ ] 故障转移是否自动？

### 20.2 流量保护
- [ ] 入口是否有限流？
- [ ] 下游调用是否有超时？
- [ ] 是否有熔断器？
- [ ] 重试是否有退避 + 抖动 + 上限？

### 20.3 数据可靠性
- [ ] 关键写是否落盘？
- [ ] 是否有副本？
- [ ] 缓存是否有降级方案？
- [ ] MQ 是否有 ack + DLQ？

### 20.4 故障隔离
- [ ] 不同业务是否资源隔离？
- [ ] 关键路径是否独立线程池？
- [ ] 是否有舱壁？

### 20.5 恢复能力
- [ ] 是否有健康检查？
- [ ] 是否有优雅停机？
- [ ] 是否有快速回滚机制？
- [ ] MTTR 是否可量化？

### 20.6 可观测
- [ ] 是否有四大黄金信号监控？
- [ ] 是否有链路追踪？
- [ ] 是否有 SLO + 错误预算？
- [ ] 告警是否基于症状？

### 20.7 演练
- [ ] 是否做过故障演练？
- [ ] 是否定期跑 chaos 实验？
- [ ] 是否有故障复盘机制？

---

## 二十一、原则速记表

| 手段 | 解决问题 | 一句话 |
|------|---------|--------|
| 冗余 | 单点 | 多副本 + 故障转移 |
| 故障转移 | 主故障 | 自动切到备 |
| 超时 | 无限等待 | 每个调用都设超时 |
| 重试 | 瞬时故障 | 退避 + 抖动 + 上限 |
| 幂等 | 重试副作用 | f(f(x)) = f(x) |
| 熔断 | 级联故障 | 下游挂了别再调 |
| 限流 | 流量过载 | 令牌桶保护自身 |
| 降级 | 保核心 | 牺牲非核心 |
| 舱壁 | 故障扩散 | 资源分池 |
| 背压 | 生产消费失衡 | 反向减速 |
| 健康检查 | 摘除坏实例 | Liveness vs Readiness |
| 优雅停机 | 不丢请求 | 先停新、再等旧 |
| WAL | 数据不丢 | 先日志再数据 |
| 副本 / 纠删码 | 存储故障 | 多机冗余 |
| 混沌工程 | 隐藏缺陷 | 主动注入故障 |

---

## 二十二、面试综合题

### Q1：服务 A 调 B 调 C，C 慢了导致 A 雪崩，怎么救？

> 多层次：
> 1. **A 调 B 加超时**（2s），避免无限等待。
> 2. **B 调 C 加熔断**，C 持续慢就停止调用，返回降级。
> 3. **A 调 B 加重试**（小心：必须配合退避，且 B 已熔断时不重试）。
> 4. **A 加舱壁**：调 B 的线程池独立，不拖死 A 的其他业务。
> 5. **C 自身限流**：拒绝超量请求，避免被拖死。
> 6. **降级**：B 对 C 的调用失败时返回缓存或默认值。

### Q2：支付接口怎么设计可靠性？

> 1. **幂等**：客户端生成 requestId，服务端去重（Redis SETNX + DB 唯一约束）。
> 2. **同步落库 + 异步清结算**：核心支付同步，对账异步。
> 3. **WAL / 事务日志**：先写日志再执行，崩溃可恢复。
> 4. **超时 + 重试**：超时短（5s），重试 2 次，退避。
> 5. **熔断 + 降级**：第三方支付通道故障，切备用通道。
> 6. **对账机制**：T+1 与第三方对账，发现差异补单。
> 7. **可回滚**：每笔支付可逆向（退款）。

### Q3：Redis 缓存挂了，DB 被打挂怎么办？

> 1. **多级缓存**：本地缓存（Caffeine）+ Redis + DB，Redis 挂了本地缓存顶一阵。
> 2. **限流降级**：Redis 挂了，DB 调用限流到可承受水位。
> 3. **熔断**：DB 持续慢就熔断，返回降级（旧数据或默认）。
> 4. **Redis 高可用**：Sentinel / Cluster，不要单点。
> 5. **预热**：Redis 恢复后逐步预热，不要瞬间全量回源。

### Q4：消息重复消费怎么处理？

> 1. **消费幂等**：消息带唯一 ID，消费端用 Redis/DB 去重。
> 2. **业务幂等**：状态机、版本号、唯一约束。
> 3. **手动 ack**：处理完再 ack，处理失败不 ack。
> 4. **DLQ**：多次失败进死信队列人工处理。
> 5. **对账**：定期对账发现重复，补偿。

### Q5：99.99% 可用性怎么做到？

> 1. **多活**：异地多活，DNS 智能解析。
> 2. **无单点**：所有组件冗余。
> 3. **自动故障转移**：< 1 分钟切换。
> 4. **灰度发布**：小流量先上，问题影响小。
> 5. **快速回滚**：一键回滚，MTTR < 5 分钟。
> 6. **限流 + 熔断 + 降级**：保护系统不被拖死。
> 7. **可观测**：SLO 监控，错误预算管理。
> 8. **混沌演练**：定期验证容错。
> 9. **On-call + Runbook**：故障快速响应。

### Q6：超时、重试、熔断、限流、降级、舱壁，分别什么时候用？

> - **超时**：永远要设，每个外部调用都要。
> - **重试**：瞬时故障，幂等接口才用。
> - **熔断**：下游持续故障时停止调用。
> - **限流**：自身被流量打时拒绝部分。
> - **降级**：故障 / 大促时牺牲非核心。
> - **舱壁**：不同业务、不同依赖资源隔离。
>
> 一般顺序：**限流 → 熔断 → 降级 → 重试（小心）→ 超时兜底**。

### Q7：分布式系统为什么这么强调幂等？

> 分布式下网络不可靠：超时重试、MQ 重复投递、客户端重发都是常态。**非幂等接口在这些场景下必然产生重复副作用**（重复扣款、重复下单）。幂等是分布式系统能正确重试的前提，没有幂等的重试就是制造 bug。

---

## 二十三、参考与延伸

- **《Site Reliability Engineering》**（Google）：SRE 圣经，SLO、错误预算、黄金信号。
- **《Release It!》**（Michael Nygard）：稳定性模式经典，熔断、舱壁、超时等模式的来源。
- **《Designing Data-Intensive Applications》**（Martin Kleppmann）：副本、一致性、WAL 等底层原理。
- **《Chaos Engineering》**（Casey Rosenthal & Nora Jones）：Netflix 混沌工程实践。
- **《Microservice Patterns》**（Chris Richardson）：微服务下的可靠性模式。

> 配套阅读：
> - [软件设计原则.md](./软件设计原则.md)：Fail Fast、单一职责是 DFR 的代码层基础
> - [架构基本原则.md](./架构基本原则.md)：高内聚低耦合、单入单出是 DFR 的模块层基础
> - [设计模式-行为模式.md](./设计模式-行为模式.md)：状态模式、策略模式、观察者模式在 DFR 中大量应用

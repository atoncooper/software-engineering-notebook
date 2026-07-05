# 工业案例 —— Netflix 微服务治理

> 章号: §22.4
> 层级: 工业 / 案例
> 标记: 🏭工业 🔥工程 ⭐高频
> 前置: [[13-1-治理-负载均衡与限流]] [[13-2-治理-可观测性与混沌工程]] [[13-3-治理-ServiceMesh与混沌工程]]
> 来源: Netflix TechBlog 历年公开分享

---

## 1. 背景

Netflix(2008-):

- DVD 租赁 → 流媒体
- 单体 → 微服务(2009-2012)
- 自建 IDC → AWS(完全云原生,2015)

挑战:

- 全球 2 亿+ 用户
- 1000+ 微服务
- 跨 Region 高可用

---

## 2. 微服务架构

### 2.1 演进

```
2008: 单体 (Java Web)
   ↓
2012: 微服务 (几百个服务)
   ↓
2015: 完全云原生 (AWS 多 Region)
   ↓
2020+: 边缘计算 + Serverless
```

### 2.2 服务分层

```
Edge (Netflix.com / App)
   ↓
API Gateway (Zuul)
   ↓
Domain Services (播放、推荐、账单)
   ↓
Aggregator Services
   ↓
Data Services (Cassandra, EVcache, MySQL)
```

---

## 3. 关键中间件

### 3.1 Zuul(API Gateway)

- 基于 Netty 异步
- 动态路由、过滤、限流、认证
- Zuul 2(2018)更轻量,基于 RxJava

```java
// Zuul Filter 示例
public class RateLimitFilter extends ZuulFilter {
    @Override
    public String filterType() { return "pre"; }
    
    @Override
    public boolean shouldFilter() { return true; }
    
    @Override
    public Object run() {
        RequestContext ctx = RequestContext.getCurrentContext();
        String userId = ctx.getRequest().getHeader("X-User-Id");
        
        if (rateLimiter.tryAcquire(userId)) {
            ctx.setSendZuulResponse(true);
        } else {
            ctx.setSendZuulResponse(false);
            ctx.setResponseStatusCode(429);
        }
        return null;
    }
}
```

### 3.2 Eureka(服务发现)

- AP 设计(CP vs AP 选 AP)
- 心跳续约 + 自我保护机制
- 跨 Region 同步

```java
@EnableEurekaClient
@SpringBootApplication
public class UserServiceApplication { ... }
```

### 3.3 Hystrix(熔断器,2012-2018)

- Circuit Breaker 模式
- 隔离舱(Bulkhead)
- Fallback 降级

```java
@HystrixCommand(
    fallbackMethod = "defaultRecommendations",
    commandProperties = {
        @HystrixProperty(name="circuitBreaker.requestVolumeThreshold", value="20"),
        @HystrixProperty(name="circuitBreaker.errorThresholdPercentage", value="50"),
        @HystrixProperty(name="metrics.rollingStats.timeInMilliseconds", value="10000")
    }
)
public List<Recommendation> getRecommendations(String userId) {
    return recommendationService.fetch(userId);
}

public List<Recommendation> defaultRecommendations(String userId) {
    return Collections.emptyList();  // 降级
}
```

Netflix 2018 后转用 Resilience4j(Hystrix 进入维护)。

### 3.4 Ribbon(LB)

- 客户端 LB
- 多策略:轮询、随机、权重、Zone-Aware

### 3.5 EVCache(分布式缓存)

- 基于 Memcached
- 多 Region 复制
- 用于热点数据(用户 profile、视频元数据)

---

## 4. 多 Region 高可用

### 4.1 Active-Active

- 三个 Region(us-east-1, us-west-2, eu-west-1)
- 全部 Active,流量按地理位置路由
- Cassandra 跨 Region 复制
- EVCache 异步复制

### 4.2 故障切换

- 单 Region 故障 → Route53 切换 DNS
- 切换时间:分钟级
- 2017 us-east-1 故障,Netflix 平稳切换

### 4.3 数据一致性

- Cassandra 可调一致,跨 Region 最终一致
- EVCache 最终一致,秒级
- 业务容忍最终一致(推荐、播放进度)

---

## 5. 混沌工程

Netflix 是混沌工程鼻祖。

### 5.1 Chaos Monkey(2011)

- 随机 kill 生产 EC2 实例
- 强制服务容错

### 5.2 Simian Army

- **Chaos Monkey**:kill 实例
- **Latency Monkey**:注入网络延迟
- **Conformity Monkey**:检查合规
- **Security Monkey**:安全审计
- **Janitor Monkey**:清理资源
- **Doctor Monkey**:实例健康检查

### 5.3 故障注入测试(FIT)

- 在生产环境注入故障
- 测试系统级容错

详见 [[13-2-治理-可观测性与混沌工程]]。

---

## 6. 可观测性

### 6.1 三支柱

- **Logging**:ELK(Elasticsearch + Logstash + Kibana)
- **Metrics**:Atlas(自研)+ Vector
- **Tracing**:OpenZipkin

### 6.2 关键指标

- **SPS**(Streams Per Second):每秒播放数
- **Error Rate**:错误率
- **Latency**:延迟分布(p50/p90/p99)

### 6.3 告警

- 基于错误预算(Error Budget)
- 多级告警(P0/P1/P2)

---

## 7. 性能与规模

- 1000+ 微服务
- 每秒数十万 RPC
- 每天处理 PB 级数据
- 全球 190+ 国家

---

## 8. 教训与最佳实践

### 8.1 微服务边界

- **业务能力**划分(领域驱动设计 DDD)
- **数据 ownership**:每服务独占自己的 DB
- **API 契约**:OpenAPI/Swagger

### 8.2 容错设计

- **默认失败**:假设依赖会失败
- **超时 + 重试 + 熔断**:三件套
- **降级**:核心功能退化可用

### 8.3 自动化

- **部署**:Spinnaker(开源持续部署)
- **扩缩容**:基于流量自动
- **监控**:全栈可观测

### 8.4 组织(Conway's Law)

- 微服务 → 微团队
- 每服务 2-5 人小团队
- DevOps 文化(开发运维一体)

---

## 9. 演进与现状

### 9.1 Hystrix → Resilience4j

- Hystrix 2018 进入维护
- Resilience4j 基于 Java 8 函数式,更轻量

### 9.2 Zuul → Spring Cloud Gateway

- 部分场景换 Spring Cloud Gateway
- 但 Zuul 仍在 Netflix 内部主力

### 9.3 Eureka → 内部下一代发现

- 内部仍在用 Eureka
- 但探索基于 K8s 的服务发现

### 9.4 微服务 → Service Mesh

- 探索 Envoy/Istio
- 逐步迁移

详见 [[13-3-治理-ServiceMesh与混沌工程]]。

---

## 10. 与其他公司对比

| 维度 | Netflix | Alibaba | Bytedance |
|------|---------|---------|-----------|
| 部署 | 全 AWS | 自建 + 云 | 自建 + 云 |
| 多活 | Region Active-Active | Cell 单元化 | 多活 |
| 服务发现 | Eureka(AP) | Nacos(ConfigServer) | 内部 |
| 熔断 | Hystrix → Resilience4j | Sentinel | 内部 |
| 混沌 | Chaos Monkey | Chaos Blade | 内部 |
| Mesh | 探索中 | 内部 SOFAStack | 内部 |

---

## 10.5 完整配置文件示例

### 10.5.1 Eureka Server 配置(`application.yml`)

```yaml
spring:
  application:
    name: eureka-server
server:
  port: 8761

eureka:
  server:
    enable-self-preservation: true              # 自我保护(网络分区时不剔除)
    eviction-interval-timer-in-ms: 60000
    response-cache-update-interval-ms: 30000
    renewal-percent-threshold: 0.85             # 85% 续约率阈值
    peer-eureka-nodes-update-interval-ms: 600000
    wait-time-in-ms-when-sync-empty: 300000     # 启动等待(避免空集群误判)
    registry-sync-retries: 3
    registry-sync-retry-wait-ms: 30000
  instance:
    hostname: eureka-1.netflix.com
    appname: eureka
    prefer-ip-address: false
    lease-renewal-interval-in-seconds: 30
    lease-expiration-duration-in-seconds: 90
  client:
    register-with-eureka: true                  # 集群模式互相注册
    fetch-registry: true
    service-url:
      defaultZone: http://eureka-1:8761/eureka,http://eureka-2:8761/eureka,http://eureka-3:8761/eureka
    region: us-east-1
    availability-zones:
      us-east-1: us-east-1a,us-east-1b,us-east-1c
    eureka-server-read-timeout-seconds: 8
    eureka-server-connect-timeout-seconds: 5

# 跨 Region 同步
eureka:
  server:
    batch-replication: true
    use-read-only-response-cache: false         # 减少 stale
  client:
    region: us-east-1
    registry-fetch-interval-seconds: 5
    # 跨 Region Eureka
    eureka-server-u-r-l:
      us-west-2: http://eureka-west-1:8761/eureka,http://eureka-west-2:8761/eureka
      eu-west-1: http://eureka-eu-1:8761/eureka,http://eureka-eu-2:8761/eureka
```

### 10.5.2 Eureka Client(微服务侧)

```yaml
spring:
  application:
    name: user-service
  cloud:
    netflix:
      eureka:
        client:
          enabled: true

eureka:
  client:
    service-url:
      defaultZone: http://eureka-1:8761/eureka,http://eureka-2:8761/eureka,http://eureka-3:8761/eureka
    register-with-eureka: true
    fetch-registry: true
    registry-fetch-interval-seconds: 30
    instance-info-replication-interval-seconds: 30
    initial-instance-info-replication-interval-seconds: 40
    eureka-service-url-poll-interval-seconds: 300
    cache-refresh-executor-thread-pool-size: 2
    cache-refresh-executor-exponential-back-off-bound: 10
  instance:
    prefer-ip-address: true
    ip-address: 10.0.1.10
    lease-renewal-interval-in-seconds: 30       # 心跳
    lease-expiration-duration-in-seconds: 90    # 90s 无心跳剔除
    instance-id: ${eureka.instance.ip-address}:${server.port}:${spring.application.name}
    metadata-map:
      zone: us-east-1a
      version: 1.2.3
      enabled: ${spring.profiles.active:default}      
    health-check-url-path: /actuator/health
    status-page-url-path: /actuator/info
    home-page-url-path: /

management:
  endpoints:
    web:
      exposure:
        include: health,info,metrics,prometheus
  endpoint:
    health:
      show-details: always
  health:
    status:
      order: DOWN,OUT_OF_SERVICE,UNKNOWN,UP
```

### 10.5.3 Resilience4j(替代 Hystrix,推荐)

```yaml
resilience4j:
  circuitbreaker:
    instances:
      recommendationService:
        register-health-indicator: true
        sliding-window-type: COUNT_BASED
        sliding-window-size: 100
        minimum-number-of-calls: 20
        failure-rate-threshold: 50               # 50% 失败率熔断
        slow-call-rate-threshold: 60             # 60% 慢调用熔断
        slow-call-duration-threshold: 2s
        wait-duration-in-open-state: 30s          # 熔断后等 30s 尝试
        permitted-number-of-calls-in-half-open-state: 10
        automatic-transition-from-open-to-half-open-enabled: true
        record-exceptions:
          - java.io.IOException
          - java.util.concurrent.TimeoutException
        ignore-exceptions:
          - com.netflix.business.BusinessException

  retry:
    instances:
      recommendationService:
        max-attempts: 3
        wait-duration: 500ms
        exponential-backoff-multiplier: 2
        exponential-max-wait-duration: 5s
        retry-exceptions:
          - java.io.IOException
          - feign.RetryableException

  bulkhead:
    instances:
      recommendationService:
        max-concurrent-calls: 50
        max-wait-duration: 500ms

  ratelimiter:
    instances:
      recommendationService:
        limit-for-period: 100                    # 每周期 100 次
        limit-refresh-period: 1s
        timeout-duration: 0                      # 不等待,直接拒绝

  timelimiter:
    instances:
      recommendationService:
        timeout-duration: 3s
        cancel-running-future: true
```

```java
// 使用
@Service
public class RecommendationClient {
    
    @CircuitBreaker(name = "recommendationService", fallbackMethod = "fallback")
    @Retry(name = "recommendationService")
    @Bulkhead(name = "recommendationService")
    @TimeLimiter(name = "recommendationService")
    public CompletableFuture<List<Recommendation>> getRecommendations(String userId) {
        return CompletableFuture.supplyAsync(() -> 
            restTemplate.getForObject("/recs/" + userId, List.class)
        );
    }
    
    public CompletableFuture<List<Recommendation>> fallback(String userId, Throwable t) {
        return CompletableFuture.completedFuture(getCachedRecommendations(userId));
    }
}
```

### 10.5.4 Zuul Gateway 配置(`application.yml`)

```yaml
server:
  port: 8765

spring:
  application:
    name: zuul-gateway
  cloud:
    netflix:
      zuul:
        enabled: true

zuul:
  ignored-services: '*'                          # 不暴露所有服务
  routes:
    user-service:
      path: /api/users/**
      service-id: user-service
      strip-prefix: true
      sensitive-headers: Cookie,Set-Cookie,Authorization
      retryable: true
    order-service:
      path: /api/orders/**
      service-id: order-service
    recommendation-service:
      path: /api/recs/**
      service-id: recommendation-service
  host:
    connect-timeout-millis: 5000
    socket-timeout-millis: 30000
    max-total-connections: 200
    max-per-route-connections: 20
  semaphore:
    max-semaphores: 200
  ribbon:
    eager-load:
      enabled: true
      clients: user-service,order-service,recommendation-service

# Ribbon (client LB)
ribbon:
  ReadTimeout: 30000
  ConnectTimeout: 5000
  MaxTotalConnections: 200
  MaxConnectionsPerHost: 50
  MaxAutoRetries: 0
  MaxAutoRetriesNextServer: 1
  OkToRetryOnAllOperations: false
  ServerListRefreshInterval: 5000
  NFLoadBalancerRuleClassName: com.netflix.loadbalancer.ZoneAvoidanceRule

hystrix:
  command:
    default:
      execution:
        isolation:
          strategy: SEMAPHORE
          thread:
            timeoutInMilliseconds: 60000
          semaphore:
            maxConcurrentRequests: 200
        timeout:
          enabled: true
      circuitBreaker:
        requestVolumeThreshold: 20
        errorThresholdPercentage: 50
        sleepWindowInMilliseconds: 30000
      fallback:
        enabled: true
  threadpool:
    default:
      coreSize: 50
      maximumSize: 100
      allowMaximumSizeToDivergeFromCoreSize: true
      maxQueueSize: 100
      queueSizeRejectionThreshold: 80

management:
  endpoints:
    web:
      exposure:
        include: '*'
  endpoint:
    health:
      show-details: always
    metrics:
      enabled: true
    prometheus:
      enabled: true
```

### 10.5.5 Zuul Filter(Groovy/Java)

```java
@Component
public class RateLimitFilter extends ZuulFilter {
    
    @Autowired
    private RateLimiter rateLimiter;
    
    @Override
    public String filterType() {
        return "pre";                            // pre/route/post/error
    }
    
    @Override
    public int filterOrder() {
        return 5;
    }
    
    @Override
    public boolean shouldFilter() {
        RequestContext ctx = RequestContext.getCurrentContext();
        return ctx.getRequest().getRequestURI().startsWith("/api/");
    }
    
    @Override
    public Object run() throws ZuulException {
        RequestContext ctx = RequestContext.getCurrentContext();
        HttpServletRequest req = ctx.getRequest();
        
        String userId = req.getHeader("X-User-Id");
        String path = req.getRequestURI();
        
        if (!rateLimiter.tryAcquire(userId, path)) {
            ctx.setSendZuulResponse(false);
            ctx.setResponseStatusCode(429);
            ctx.setResponseBody("{\"error\":\"rate_limit_exceeded\"}");
            ctx.getResponse().setContentType("application/json;charset=UTF-8");
            return null;
        }
        
        ctx.addZuulRequestHeader("X-Request-Id", UUID.randomUUID().toString());
        return null;
    }
}
```

### 10.5.6 EVCache 配置

```java
@Configuration
@EnableEVCacheClient
public class EVCacheConfig {
    
    @Bean
    public EVCacheConfig evcacheConfig() {
        EVCacheConfig.Builder builder = new EVCacheConfig.Builder()
            .setDefaultServerGroup("DEFAULT")
            .setServerGroup("DEFAULT", "cache-1:11211,cache-2:11211,cache-3:11211")
            .setServerGroup("EU", "cache-eu-1:11211,cache-eu-2:11211")
            .setServerGroup("US_WEST", "cache-w-1:11211,cache-w-2:11211")
            .setDefaultTTL(3600)
            .setBufferSize(8192)
            .setReadTimeoutMillis(100)
            .setWriteTimeoutMillis(100);
        return builder.build();
    }
}

@Service
public class UserProfileCache {
    
    @Autowired
    private EVCache evcache;
    
    public void setUserProfile(String userId, UserProfile profile) {
        try {
            evcache.set(userId, 3600,                           // TTL 1h
                jsonSerialize(profile),
                "DEFAULT",                                      // 主 DC
                "EU", "US_WEST"                                 // 跨 DC 复制
            );
        } catch (Exception e) {
            log.error("EVCache set failed", e);
        }
    }
    
    public UserProfile getUserProfile(String userId) {
        try {
            String json = evcache.get(userId, "DEFAULT");
            if (json == null) {
                return null;                                    // cache miss
            }
            return jsonDeserialize(json);
        } catch (Exception e) {
            log.warn("EVCache get failed, fallback to DB", e);
            return loadFromDB(userId);
        }
    }
}
```

### 10.5.7 Docker Compose(本地开发栈)

```yaml
version: '3.8'
services:
  eureka-1:
    image: steeltoeoss/eureka-server:latest
    ports: ["8761:8761"]
    environment:
      - EUREKA_SERVER_ENABLE_SELF_PRESERVATION=true

  user-service:
    build: ./user-service
    depends_on: [eureka-1, evcache-1]
    environment:
      - EUREKA_CLIENT_SERVICEURL_DEFAULTZONE=http://eureka-1:8761/eureka
      - EVCACHE_SERVERGROUP_DEFAULT=evcache-1:11211
    ports: ["8081:8081"]

  order-service:
    build: ./order-service
    depends_on: [eureka-1, evcache-1]
    environment:
      - EUREKA_CLIENT_SERVICEURL_DEFAULTZONE=http://eureka-1:8761/eureka
    ports: ["8082:8082"]

  zuul-gateway:
    build: ./gateway
    depends_on: [eureka-1, user-service, order-service]
    ports: ["8765:8765"]
    environment:
      - EUREKA_CLIENT_SERVICEURL_DEFAULTZONE=http://eureka-1:8761/eureka

  evcache-1:
    image: memcached:1.6-alpine
    ports: ["11211:11211"]

  prometheus:
    image: prom/prometheus
    ports: ["9090:9090"]
    volumes:
      - ./prometheus.yml:/etc/prometheus/prometheus.yml

  grafana:
    image: grafana/grafana
    ports: ["3000:3000"]
```

---

## 11. 速查表

```
Netflix 微服务栈:
  Gateway: Zuul
  Discovery: Eureka (AP)
  LB: Ribbon (client-side)
  Circuit Breaker: Hystrix → Resilience4j
  Cache: EVCache (Memcached-based)
  DB: Cassandra (跨 Region)
  
多 Region:
  Active-Active (3 Region)
  Route53 DNS 切换
  RPO: 秒级 (异步)
  RTO: 分钟级

混沌工程:
  Chaos Monkey (kill 实例)
  Simian Army (Latency/Conformity/Security...)
  FIT (生产故障注入)

教训:
  微服务按业务能力切分
  数据 ownership 独立
  超时 + 重试 + 熔断 三件套
  Conway's Law: 微服务 = 微团队
```

---

## 12. 交叉引用

- [[13-1-治理-负载均衡与限流]]:限流与负载均衡
- [[13-2-治理-可观测性与混沌工程]]:混沌工程
- [[13-3-治理-ServiceMesh与混沌工程]]:Service Mesh
- [[14-故障与容错]]:多活与容错
- [[22-3-工业案例-Alibaba-双11与单元化]]:对比阿里方案

---

## 13. 参考文献

- Netflix TechBlog. https://netflixtechblog.com
- Bennett, Tseitlin. *Chaos Monkey Released*. 2012.
- Hystrix Wiki. https://github.com/Netflix/Hystrix
- Resilience4j Documentation. https://resilience4j.readme.io
- Zuul 2 介绍. Netflix TechBlog 2018.
- Boucher et al. *Full Cycle Developers at Netflix*. 2018.

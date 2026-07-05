# 治理 —— Service Mesh 与混沌工程

> 章号: §13.3
> 层级: 面试 / 原理 / 工业
> 标记: 🔥工程 🏭工业 🎓学术
> 前置: [[13-1-治理-负载均衡与限流]] [[13-2-治理-可观测性与混沌工程]]

---

## 0. Service Mesh 的诞生

微服务架构带来挑战:

- 服务发现、负载均衡、熔断、限流、重试
- 多语言栈(Java + Go + Python + Node.js)各自实现
- 升级治理能力需改业务代码
- 可观测性碎片化

**Service Mesh** 把这些"治理能力"从业务代码剥离到 Sidecar 代理:

```
传统微服务:
  [App (含治理逻辑)] → [App (含治理逻辑)]
       ↓                    ↓
     框架                 框架

Service Mesh:
  [App] → [Sidecar] → [Sidecar] → [App]
              ↓          ↓
           控制平面 (统一管理)
```

业务代码只关心业务,治理逻辑由 Sidecar 统一处理。

---

## 1. Service Mesh 架构

### 1.1 数据平面 vs 控制平面

```
                ┌────────────────────────────┐
                │      Control Plane          │
                │  (Istio / Linkerd / Consul) │
                │   - 服务发现                  │
                │   - 路由规则                  │
                │   - 策略下发                  │
                │   - 证书管理                  │
                └──────────┬─────────────────┘
                           ↓ xDS
        ┌──────────────────┴──────────────────┐
        ↓                                     ↓
┌───────────────┐                     ┌───────────────┐
│  Pod A         │                     │  Pod B         │
│  ┌──────────┐ │                     │  ┌──────────┐ │
│  │  App     │ │                     │  │  App     │ │
│  │  (业务)  │ │                     │  │  (业务)  │ │
│  └────┬─────┘ │                     │  └────┬─────┘ │
│       ↓       │                     │       ↑       │
│  ┌──────────┐ │                     │  ┌──────────┐ │
│  │ Sidecar  │←────────────────────→│  │ Sidecar  │ │
│  │ (Envoy)  │       mTLS            │  │ (Envoy)  │ │
│  └──────────┘                     │  └──────────┘ │
└───────────────┘                     └───────────────┘
     数据平面                                数据平面
```

### 1.2 数据平面

负责实际流量转发:

- **Envoy**:C++ 写,Lyft 开源,Istio 默认
- **Linkerd-proxy**:Rust 写,Linkerd 用
- **MOSN**:阿里 Go 写,ANT 内部用
- **NGINX / HAProxy**:传统代理,可作 Sidecar

### 1.3 控制平面

负责配置下发:

- **Istio**:Google + IBM + Lyft,功能最全
- **Linkerd**:Buoyant,轻量级
- **Consul Connect**:HashiCorp
- **Open Service Mesh**:微软,已停滞

---

## 2. Istio 核心概念

### 2.1 资源模型

```yaml
# Gateway: 入口流量
apiVersion: networking.istio.io/v1alpha3
kind: Gateway
metadata:
  name: payment-gateway
spec:
  servers:
    - port: { number: 443, name: https, protocol: HTTPS }
      tls: { mode: SIMPLE, credentialName: payment-cert }
      hosts: ["payment.example.com"]

---
# VirtualService: 路由规则
apiVersion: networking.istio.io/v1alpha3
kind: VirtualService
metadata:
  name: payment
spec:
  hosts: ["payment.example.com"]
  gateways: [payment-gateway]
  http:
    - match: [{ uri: { prefix: "/api/v1" } }]
      route:
        - destination:
            host: payment
            subset: v2
          weight: 90
        - destination:
            host: payment
            subset: v1
          weight: 10  # 10% 灰度

---
# DestinationRule: 负载均衡 + 熔断
apiVersion: networking.istio.io/v1alpha3
kind: DestinationRule
metadata:
  name: payment
spec:
  host: payment
  subsets:
    - name: v1
      labels: { version: v1 }
    - name: v2
      labels: { version: v2 }
  trafficPolicy:
    loadBalancer: { simple: LEAST_REQUEST }
    connectionPool:
      tcp: { maxConnections: 100 }
      http: { http1MaxPendingRequests: 50 }
    outlierDetection:
      consecutive5xxErrors: 5
      interval: 30s
      baseEjectionTime: 30s
```

### 2.2 流量管理

- **路由**:按 path / header / weight 分发
- **负载均衡**:轮询 / 最少请求 / 随机 / 一致性哈希
- **熔断**:连接池 + 异常检测
- **重试**:自动重试可幂等请求
- **超时**:per-route 超时
- **故障注入**:测试容错(延迟 / 中断)
- **灰度发布**:基于权重 / header

### 2.3 安全

- **mTLS**:Sidecar 间双向 TLS,自动证书轮换
- **AuthorizationPolicy**:细粒度访问控制
- **RequestAuthentication**:JWT 验证

```yaml
apiVersion: security.istio.io/v1beta1
kind: PeerAuthentication
metadata:
  name: default
spec:
  mtls:
    mode: STRICT  # 强制 mTLS

---
apiVersion: security.istio.io/v1beta1
kind: AuthorizationPolicy
metadata:
  name: payment-allow
spec:
  selector:
    matchLabels: { app: payment }
  action: ALLOW
  rules:
    - from:
        - source: { principals: ["cluster.local/ns/default/sa/frontend"] }
      to:
        - operation: { methods: ["GET", "POST"], paths: ["/api/*"] }
```

### 2.4 可观测性

Istio 自动生成:

- **Metrics**:请求量、延迟、错误率、TCP 指标
- **Traces**:自动注入 trace header,集成 Jaeger
- **Access Logs**:结构化访问日志

```yaml
apiVersion: telemetry.istio.io/v1alpha1
kind: Telemetry
metadata:
  name: mesh-default
spec:
  accessLogging:
    - providers:
        - name: envoy
  tracing:
    - providers:
        - name: otel
          type: OTLP
```

---

## 3. xDS 协议

控制平面与数据平面通过 xDS 协议通信:

| 协议 | 含义 |
|------|------|
| **LDS** (Listener Discovery) | 监听器配置 |
| **RDS** (Route Discovery) | 路由配置 |
| **CDS** (Cluster Discovery) | 集群(后端)配置 |
| **EDS** (Endpoint Discovery) | 端点(实例)配置 |
| **SDS** (Secret Discovery) | 证书/密钥 |

```
Control Plane ──xDS──→ Envoy
                  ←─ACK─
                  (动态更新,无需重启)
```

xDS 是 gRPC 流式,支持动态配置下发,Envoy 热加载。

---

## 4. Ambient Mesh (Istio 1.18+)

### 4.1 Sidecar 模式的问题

- 资源开销:每 Pod 一个 Sidecar,内存 + CPU
- 注入复杂:需重启 Pod
- 端口冲突:App 与 Sidecar 端口协调
- 升级困难:Sidecar 升级需重启 Pod

### 4.2 Ambient 模式

```
Node:
  [App Pod] [App Pod] [App Pod]
       ↓         ↓         ↓
  ┌─────────────────────────────┐
  │  ztunnel (L4,每节点一个)     │  ← mTLS、L4 路由
  └─────────────┬───────────────┘
                ↓ (需要 L7 时)
  ┌─────────────────────────────┐
  │  waypoint proxy (L7,可选)   │  ← L7 路由、流量管理
  └─────────────────────────────┘
```

- **ztunnel**:每节点一个,处理 L4(mTLS + 简单路由)
- **waypoint**:按需部署,处理 L7(细粒度流量管理)

优势:

- 资源开销降低(共享 ztunnel)
- 升级不重启 Pod
- L4/L7 分层,按需启用

---

## 5. 其他 Service Mesh

### 5.1 Linkerd

- 轻量级,Rust 写的 sidecar
- 默认配置即用
- 功能不如 Istio 全,但易上手

### 5.2 Consul Connect

- 与 Consul 服务发现深度集成
- 多平台支持(K8s / VM / Nomad)

### 5.3 Kuma

- 基于 Envoy
- 多集群支持
- CNCF 项目

### 5.4 Open Service Mesh (OSM)

- 微软开源
- 已停止维护(2023)

---

## 6. Service Mesh 工业实践

### 6.1 何时引入 Service Mesh

适合:

- 微服务数 > 50,治理复杂
- 多语言栈
- 跨集群 / 跨 DC
- 强安全需求(mTLS)
- 频繁灰度发布

不适合:

- 微服务少(< 10),传统框架足够
- 性能极敏感(Sidecar 增加延迟 1~5ms)
- 团队无 K8s 经验

### 6.2 性能开销

- 延迟:Sidecar 增加 1~5ms(本地) / 5~20ms(跨节点)
- CPU:每 Sidecar 50~200m
- 内存:每 Sidecar 50~200MB

### 6.3 阿里实践

- 自研 MOSN(Go) 替代 Envoy
- 内部大规模(万级 Pod)
- 与自研配置中心集成

### 6.4 字节实践

- 自研 Service Mesh
- Sidecar + Node 级代理混合

### 6.5 灰度发布

```yaml
# 基于 header 灰度
apiVersion: networking.istio.io/v1alpha3
kind: VirtualService
spec:
  http:
    - match:
        - headers: { x-canary: { exact: "true" } }
      route:
        - destination: { host: payment, subset: v2 }  # 灰度版
    - route:
        - destination: { host: payment, subset: v1 }  # 稳定版
```

```yaml
# 基于权重灰度
spec:
  http:
    - route:
        - destination: { host: payment, subset: v1 }
          weight: 90
        - destination: { host: payment, subset: v2 }
          weight: 10  # 10% 流量到 v2
```

### 6.6 故障注入

```yaml
apiVersion: networking.istio.io/v1alpha3
kind: VirtualService
spec:
  http:
    - match: [{ headers: { x-test: { exact: "true" } } }]
      fault:
        delay:
          percentage: { value: 100 }
          fixedDelay: 5s   # 注入 5s 延迟
        abort:
          percentage: { value: 10 }
          httpStatus: 503  # 10% 返回 503
      route:
        - destination: { host: payment }
```

用于混沌工程:主动注入故障验证容错。

---

## 7. API Gateway vs Service Mesh

| 维度 | API Gateway | Service Mesh |
|------|-------------|--------------|
| 位置 | 入口(南北流量) | 服务间(东西流量) |
| 用途 | 外部入口、认证、限流、路由 | 内部治理、mTLS、可观测 |
| 部署 | 集中式 | Sidecar 分布式 |
| 协议 | HTTP/HTTPS 为主 | 多协议 |
| 例子 | Kong / APISIX / Nginx | Istio / Linkerd |

互补:API Gateway 处理南北,Service Mesh 处理东西。大型系统两者都用。

---

## 8. 面试要点

**Q1: Service Mesh 是什么?为什么需要?**

> 把治理逻辑(负载均衡、熔断、限流、可观测)从业务代码剥离到 Sidecar 代理。需要因为:多语言栈各自实现治理重复;升级治理能力需改业务;可观测性碎片化。Service Mesh 让业务专注业务,治理统一管理。

**Q2: 数据平面和控制平面的区别?**

> 数据平面:Sidecar(Envoy),实际转发流量、执行策略。控制平面:Istiod,配置下发、证书管理、服务发现。两者通过 xDS 协议通信(gRPC 流式,动态更新)。

**Q3: Istio 的核心资源有哪些?**

> Gateway(入口流量)、VirtualService(路由规则)、DestinationRule(负载均衡/熔断/子集)、ServiceEntry(外部服务)、PeerAuthentication(mTLS)、AuthorizationPolicy(访问控制)、Telemetry(可观测)。

**Q4: Sidecar 模式有什么问题?Ambient 怎么解决?**

> Sidecar 问题:每 Pod 一个 Sidecar 资源开销大;升级需重启 Pod;端口冲突。Ambient:ztunnel(每节点一个,处理 L4)+ waypoint(按需,L7)。资源开销降低,升级不重启 Pod。

**Q5: Service Mesh 性能开销?**

> 延迟:Sidecar 增加 1~5ms(本地)/ 5~20ms(跨节点)。CPU:每 Sidecar 50~200m。内存:50~200MB。对于延迟极敏感场景(超低 latency)需评估。

**Q6: xDS 协议是什么?**

> 控制平面与数据平面的通信协议。LDS(Listener)、RDS(Route)、CDS(Cluster)、EDS(Endpoint)、SDS(Secret)。gRPC 流式,支持动态配置下发,Envoy 热加载(不重启)。

**Q7: API Gateway 和 Service Mesh 怎么协作?**

> API Gateway 处理南北流量(外部入口,认证、限流、HTTPS),Service Mesh 处理东西流量(服务间,mTLS、可观测、细粒度路由)。大型系统两者都用,API Gateway 作入口,Service Mesh 治理内部。

**Q8: Service Mesh 什么时候不适用?**

> (1) 微服务少(< 10),传统框架足够;(2) 性能极敏感(Sidecar 延迟);(3) 团队无 K8s 经验;(4) 单语言栈且已有成熟治理(如 Spring Cloud)。Service Mesh 不是银弹,引入有成本。

---

## 9. 交叉引用

- [[13-1-治理-负载均衡与限流]]:治理基础
- [[13-2-治理-可观测性与混沌工程]]:可观测性
- [[14-故障与容错]]:故障注入与混沌
- [[10-协调服务]]:服务发现

---

## 10. 速查表

```
Service Mesh = 数据平面 (Sidecar) + 控制平面

数据平面: Envoy / Linkerd / MOSN / NGINX
控制平面: Istio / Linkerd / Consul / Kuma

Istio 核心资源:
  Gateway: 入口
  VirtualService: 路由
  DestinationRule: LB + 熔断
  PeerAuthentication: mTLS
  AuthorizationPolicy: 授权
  Telemetry: 可观测

xDS: LDS/RDS/CDS/EDS/SDS (gRPC 流式动态下发)

Ambient Mesh:
  ztunnel (L4,每节点) + waypoint (L7,可选)
  资源开销降低,升级不重启 Pod

vs API Gateway:
  Gateway: 南北流量 (入口)
  Mesh: 东西流量 (服务间)
  大型系统两者都用

适用: 微服务 > 50,多语言,跨集群,强安全
不适用: 微服务少,极低延迟,无 K8s 经验
```

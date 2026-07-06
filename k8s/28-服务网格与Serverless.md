# 28. 服务网格与 Serverless

> 关键词:Istio、Envoy、xDS、Linkerd、Sidecar、Ambient Mesh、ztunnel、waypoint、Knative、Serving、Eventing、KPA、Activator、冷启动、OpenFaaS、OpenWhisk、Argo Workflows、Tekton、CI/CD、Pipeline、Scale-to-zero、mTLS、JWT、AuthorizationPolicy、VirtualService、DestinationRule、Gateway、ServiceEntry、PeerAuthentication、WasmPlugin、流量切分、金丝雀、流量镜像、故障注入、熔断、重试、超时

> 版本基线:Istio 1.21 + Linkerd 2.14 + Knative 1.13 + Argo Workflows 3.5 + Tekton 0.60 + Envoy 1.30 + Kubernetes 1.30。

------

## 28.1 问题定义与边界

### 28.1.1 本章解决什么

K8s 提供了 **Service + kube-proxy + NetworkPolicy + Ingress** 的基础网络与服务抽象,解决了「Pod 找到 Pod」「Pod 能不能访问 Pod」「外部流量进入集群」三件事。但在 **微服务规模化** 之后,这套机制远远不够:

- 为什么 1000+ 微服务上线后,版本切流、金丝雀、A/B 测试、流量镜像都靠改 Deployment 副本数?能不能 **按百分比切流量**?
- 为什么每个微服务都要自己实现 **超时、重试、熔断、限流**?这些代码和业务无关,能不能下沉到基础设施?
- 为什么 mTLS(双向加密)要在 1000 个服务里各自配证书?能不能 **全局一键开启**?
- 为什么分布式追踪要在每个服务里塞 SDK?能不能 **无侵入** 采集?
- 为什么 K8s Deployment 永远有副本数,**没流量也在烧钱**?能不能 **流量来了再起、流量走了缩到 0**?
- 为什么 CI/CD 流水线和 K8s 是两套世界?能不能 **用 K8s 原生 CRD 描述流水线**?

**本章解决**:

> 用 **服务网格(Istio/Linkerd)+ Serverless(Knative/OpenFaaS)+ 流水线(Argo Workflows/Tekton)** 三件套,把 K8s 从「容器编排平台」升级为「微服务通信平台 + 按需计算平台 + 流水线平台」。

**解决**:

- 服务网格三大核心能力:流量管理、安全(mTLS/JWT/授权)、可观测(指标/追踪/日志)
- Sidecar 模式 vs Ambient Mesh 模式(Sidecarless)的取舍
- Envoy xDS 协议(LDS/RDS/CDS/EDS/SDS)与配置分发机制
- Istio 完整 CRD 体系:VirtualService / DestinationRule / Gateway / ServiceEntry / WorkloadEntry / PeerAuthentication / RequestAuthentication / AuthorizationPolicy / WasmPlugin
- Knative Serving 完整架构:Route / Revision / Configuration / Service + KPA + Activator + 冷启动
- Knative Eventing:Broker / Trigger / Source / Channel 事件驱动
- Argo Workflows 与 Tekton 的流水线能力与对比
- OpenFaaS / OpenWhisk 等 FaaS 框架
- Serverless 容器(ACK ASK / AWS Fargate / Google Cloud Run / Azure ACI)与网格的协同

**不解决**:

- **K8s 基础网络** → 见 [05-Service与网络](./05-Service与网络.md)(Service / Endpoint / Ingress / DNS)
- **kube-proxy 转发原理** → 见 [14-kube-proxy与服务转发](./14-kube-proxy与服务转发.md)(iptables / IPVS / eBPF)
- **CNI 与网络模型** → 见 [15-CNI与网络模型](./15-CNI与网络模型.md)(Calico / Cilium / Flannel)
- **NetworkPolicy** → 见 [18-NetworkPolicy与流量管控](./18-NetworkPolicy与流量管控.md)(L3/L4 隔离)
- **CRD / Operator** → 见 [27-CRD与Operator生态](./27-CRD与Operator生态.md)(Operator 模式 / Helm / Argo CD)
- **大厂 K8s 平台** → 见 [25-大厂K8s平台](./25-大厂K8s平台.md)(阿里 ACK / 字节 TCE / GKE / EKS)

### 28.1.2 服务网格解决什么

| 痛点 | K8s 原生方案 | 服务网格方案 |
|------|-------------|-------------|
| 金丝雀按百分比切流 | 改 Deployment 副本数(粗粒度) | VirtualService weight: 5%(精确) |
| 跨服务超时/重试 | 每个服务自己写 SDK | DestinationRule 统一配置 |
| 熔断 | Hystrix / Sentinel SDK | DestinationRule outlierDetection |
| mTLS | 每服务配证书 | PeerAuthentication 一键开启 |
| JWT 校验 | 业务代码校验 | RequestAuthentication 声明式 |
| 授权策略 | RBAC(粗)| AuthorizationPolicy(L7 细粒度) |
| 分布式追踪 | 每服务塞 SDK | Sidecar 无侵入采集 |
| 流量镜像 | 手动复制流量 | VirtualService mirror |
| 故障注入 | Chaos 工具 | VirtualService fault |

**核心价值**:把 **与业务无关的通信逻辑** 从应用代码中剥离,下沉到 Sidecar(或 Ambient 节点级代理),实现 **语言无关、框架无关、无侵入** 的统一治理。

### 28.1.3 Serverless 解决什么

| 痛点 | K8s 原生方案 | Serverless 方案 |
|------|-------------|----------------|
| 没流量也烧钱 | Deployment 永远有副本 | Scale-to-zero |
| 流量突增 | HPA + CA(分钟级) | Knative KPA(秒级) |
| 冷启动慢 | 不关注 | Activator + 预热优化到 200ms |
| 按需计费 | 按节点计费 | 按请求次数/毫秒计费 |
| 事件驱动 | 自己写 Consumer | Knative Eventing / OpenFaaS |
| 函数粒度 | Pod 粒度 | Function 粒度 |

**核心价值**:把 K8s 的 **「always-on 容器」** 模型升级为 **「按需启动、缩到 0、按毫秒计费」** 的 FaaS 模型,适合低频访问、突发流量、事件驱动的场景。

### 28.1.4 与其他章节的边界

| 维度 | 05 Service 与网络 | 14 kube-proxy | 18 NetworkPolicy | 27 CRD 与 Operator | **28 服务网格与 Serverless** |
|------|------------------|---------------|------------------|---------------------|---------------------------|
| 视角 | K8s 原生网络抽象 | 转发原理 | L3/L4 隔离 | Operator 生态 | **L7 治理 + Serverless + 流水线** |
| 层级 | L4 | L4(kernel) | L3/L4 | 控制面 | **L7 + 控制面 + 数据面** |
| 重点 | Service / Endpoint / Ingress | iptables / IPVS | Pod 隔离 | CRD + Controller | **流量治理 + 安全 + 可观测 + FaaS** |

------

## 28.2 直觉解释

### 28.2.1 服务网格 = 微服务的「通信秘书」

把微服务网格想象成 **一家大型跨国公司的通信秘书体系**:

| 公司通信秘书 | 服务网格 |
|-------------|---------|
| 每个高管配一个秘书 | 每个 Pod 配一个 Sidecar(Envoy) |
| 秘书负责发邮件、约会议、过滤骚扰 | Sidecar 负责转发、重试、熔断 |
| 高管只关心业务,不关心通信细节 | 业务代码只管业务逻辑 |
| 总裁办统一管理所有秘书 | Istiod 控制面统一管理所有 Sidecar |
| 秘书之间用加密邮件 | mTLS 双向加密 |
| 秘书记录每封邮件的发送情况 | 可观测性(指标/追踪/日志) |
| 高管出差换办公室,秘书跟着走 | Pod 调度到哪,Sidecar 注入到哪 |
| 总裁办下发规则(谁能见谁) | AuthorizationPolicy 授权策略 |

**关键直觉**:

1. **业务/通信分离**:应用代码只关心业务逻辑(下单、支付、查询),通信逻辑(超时、重试、加密、追踪)全部由 Sidecar 接管。
2. **语言无关**:Java / Go / Python / Node 服务都用同一套 Sidecar,治理策略统一。
3. **无侵入**:不需要改一行业务代码,就能获得 mTLS、追踪、熔断能力。
4. **声明式**:用 CRD(VirtualService / DestinationRule)描述「想要什么」,控制面下发到 Sidecar。

### 28.2.2 Serverless = K8s + 按需起 + 缩到 0

把 Serverless 想象成 **共享会议室**:

| 共享会议室 | Serverless |
|-----------|-----------|
| 没人用时锁门(不耗电) | Scale-to-zero(不烧钱) |
| 有人预约时开门 | 请求来了 Activator 唤醒 |
| 开门到可用需要几分钟(冷启动) | 冷启动 200ms-2s |
| 用完自动锁门 | 闲置 60s 后缩到 0 |
| 按使用时长计费 | 按请求次数/毫秒计费 |
| 突发多人涌入,自动加开会议室 | KPA 自动扩容 |
| 事件触发(老板叫开会) | Eventing 事件驱动 |

**关键直觉**:

1. **从「always-on」到「on-demand」**:K8s Deployment 永远有副本,Serverless 没流量时副本为 0。
2. **冷启动是核心矛盾**:缩到 0 省钱,但冷启动慢。优化到 200ms 是工业级目标。
3. **事件驱动**:不只是 HTTP 请求,Kafka 消息、对象存储事件、定时器都能触发。
4. **按需计费**:从「按节点/小时」到「按请求/毫秒」,低频场景成本降 90%+。

### 28.2.3 CI/CD 流水线 = K8s 原生的「工厂流水线」

把 Argo Workflows / Tekton 想象成 **汽车工厂的流水线**:

| 汽车流水线 | K8s 流水线 |
|-----------|-----------|
| 每个工位做一个工序 | 每个 Task 做一步(编译/测试/打包) |
| 工位之间传递半成品 | Step 之间用 workspace 传递产物 |
| 整条线由调度员指挥 | Pipeline / Workflow 控制器编排 |
| 工人在车间干活 | TaskRun / StepRun 在 Pod 里跑 |
| 流水线可以并行/分支 | DAG / fan-out / fan-in |
| 质检不合格返工 | 失败重试 / 条件分支 |
| 流水线参数化(车型/颜色) | params / 工作流参数化 |

**关键直觉**:

1. **流水线即代码**:用 YAML 描述 CI/CD,版本化、可审计。
2. **K8s 原生**:每个 Step 是一个 Pod,天然利用 K8s 的调度、隔离、资源管理。
3. **Argo Workflows 偏 DAG 编排**,适合批处理、ML、ETL;**Tekton 偏 CD 标准**,是 Tekton CD 的底座。

------

## 28.3 核心概念与架构

### 28.3.1 服务网格三层架构

```
                  +--------------------------------------------------+
                  |              服务网格三层架构                     |
                  +--------------------------------------------------+

   +--------------------------------------------------------------------+
   |                    管理面(Management Plane)                       |
   |                                                                    |
   |   - Mesh 级配置管理(多集群、多租户)                                |
   |   - 策略下发与审计                                                  |
   |   - 可观测性聚合(指标/追踪/日志统一视图)                           |
   |   - 工具: Istio Mesh Manager / Kiali / Meshery                    |
   +--------------------------------------------------------------------+
                                       |
                                       v
   +--------------------------------------------------------------------+
   |                    控制面(Control Plane)                          |
   |                                                                    |
   |   +--------------+  +--------------+  +--------------+            |
   |   |   Pilot      |  |   Citadel    |  |   Galley     |            |
   |   |(配置分发)    |  | (证书签发)   |  | (配置校验)   |            |
   |   +--------------+  +--------------+  +--------------+            |
   |           |                  |                  |                  |
   |           v                  v                  v                  |
   |     xDS 推送           mTLS 证书         配置语法校验              |
   |   (LDS/RDS/CDS/EDS)    (SDS 通道)        (CRD 验证)               |
   |                                                                    |
   |   工具: Istiod / Linkerd control plane / Consul Connect            |
   +--------------------------------------------------------------------+
                                       |
                                       v (xDS / SDS / 配置下发)
   +--------------------------------------------------------------------+
   |                    数据面(Data Plane)                             |
   |                                                                    |
   |   +---------+  +---------+  +---------+  +---------+              |
   |   |  Pod A  |  |  Pod B  |  |  Pod C  |  |  Pod D  |              |
   |   |+-------+|  |+-------+|  |+-------+|  |+-------+|              |
   |   || Sidecar||  || Sidecar||  || Sidecar||  || Sidecar||            |
   |   |+---^---+|  |+---^---+|  |+---^---+|  |+---^---+|              |
   |   +----|----+  +----|----+  +----|----+  +----|----+              |
   |        |            |            |            |                    |
   |        +------------+------------+------------+                    |
   |                    mTLS 加密通信                                   |
   |                                                                    |
   |   工具: Envoy(Istio)/ Linkerd2-proxy(Rust)/ Nginx                |
   +--------------------------------------------------------------------+
```

### 28.3.2 Sidecar 模式

**Sidecar 模式** 是服务网格的经典部署形态:每个 Pod 注入一个 Envoy 容器,拦截该 Pod 的所有进出流量。

```
   传统 Pod:
   +------------------------+
   |       Pod              |
   |  +------------------+  |
   |  |  Application     |  |
   |  |  (业务容器)       |  |
   |  +------------------+  |
   +------------------------+

   Sidecar Pod:
   +--------------------------------+
   |            Pod                 |
   |  +--------------------------+  |
   |  |  Application             |  |
   |  |  (业务容器)               |  |
   |  +------------^-------------+  |
   |               | lo / iptables  |
   |  +------------|-------------+  |
   |  |  istio-proxy (Envoy)     |  |
   |  |  - 15006 入站             |  |
   |  |  - 15001 出站             |  |
   |  +--------------------------+  |
   +--------------------------------+
```

**流量拦截机制**(iptables redirect):

```
   入站流量(进入 Pod):
   外部请求 -> Pod IP:8080
              | iptables PREROUTING
              | REDIRECT to 15006
              v
          Envoy 入站监听
              |
              v
          应用容器 8080

   出站流量(离开 Pod):
   应用容器 -> 外部 IP:3306
              | iptables OUTPUT
              | REDIRECT to 15001
              v
          Envoy 出站监听
              |
              v
          外部服务(经过 mTLS)
```

**Sidecar 资源开销**:
- CPU: ~100m (idle) - 500m (高负载)
- 内存: ~100MB (典型) - 500MB (大规模集群配置)
- 1万 Pod 集群:Sidecar 总开销 = 10000 x 100MB = 1TB 内存(惊人!)

### 28.3.3 Ambient Mesh 模式(Istio 1.18+,Sidecarless)

为解决 Sidecar 资源开销问题,Istio 1.18 引入 **Ambient Mesh**(无 Sidecar 模式),采用 **ztunnel + waypoint** 两层架构:

```
   Ambient Mesh 节点级架构:

   +------------------------------------------------------+
   |                   Node                               |
   |                                                      |
   |  +---------+  +---------+  +---------+              |
   |  | Pod A   |  | Pod B   |  | Pod C   |              |
   |  | 业务    |  | 业务    |  | 业务    |              |
   |  +----^----+  +----^----+  +----^----+              |
   |       |            |           |                     |
   |       +------------+-----------+                     |
   |                   |                                 |
   |       +-----------v-----------+                     |
   |       |   ztunnel (DaemonSet) |                     |
   |       |   - L4 mTLS            |                     |
   |       |   - 节点级代理          |                     |
   |       |   - Rust 实现          |                     |
   |       +-----------^-----------+                     |
   |                   |                                 |
   |       +-----------|-----------+                     |
   |       |   waypoint (按需)     |                     |
   |       |   - L7 治理            |                     |
   |       |   - Envoy 部署         |                     |
   |       |   - 命名空间/服务级    |                     |
   |       +-----------------------+                     |
   +------------------------------------------------------+

   分层:
   - ztunnel: 每节点一个,L4 mTLS(无 L7),极低开销(~10MB)
   - waypoint: 按需部署,L7 治理(只有需要 L7 策略的服务才起)
```

**Sidecar vs Ambient 对比**:

| 维度 | Sidecar 模式 | Ambient 模式 |
|------|-------------|-------------|
| 部署粒度 | 每 Pod 一个 | 每节点一个 ztunnel + 按需 waypoint |
| 资源开销 | 100MB/Pod | 10MB/节点 + 按需 waypoint |
| L4 mTLS | 支持 | 支持(ztunnel) |
| L7 治理 | 支持(Envoy) | 支持(waypoint,按需) |
| 流量拦截 | iptables redirect | HBONE(HTTP-Based Overlay Network) |
| 升级影响 | Pod 重启 | ztunnel 升级不影响 Pod |
| 兼容性 | 成熟 | Istio 1.21+ GA,生产验证中 |
| 适合场景 | 通用 | 大规模集群、低资源开销场景 |

### 28.3.4 Envoy xDS 协议

Envoy 通过 **xDS(Discovery Service)** 协议从控制面动态获取配置:

```
   xDS 协议族:

   +----------------------------------------------------------+
   |                   Envoy xDS 协议                         |
   +----------------------------------------------------------+

   LDS  Listener Discovery Service       监听器配置(端口/过滤器链)
   RDS  Route Discovery Service          路由配置(路径/header 转发)
   CDS  Cluster Discovery Service        集群配置(上游服务集合)
   EDS  Endpoint Discovery Service       端点配置(具体 Pod IP)
   SDS  Secret Discovery Service         密钥配置(mTLS 证书)
   ------------------------------------
   ADS  Aggregated Discovery Service     聚合 xDS(顺序保证)

   推送流程(Istiod -> Envoy):
   +----------+                    +----------+
   | Istiod   |  +- LDS -------->  |  Envoy   |
   | (Pilot)  |  +- RDS -------->  |          |
   |          |  +- CDS -------->  |          |
   |          |  +- EDS -------->  |          |
   |          |  +- SDS -------->  |          |
   +----------+                    +----------+

   推送触发:
   1. K8s Service / Endpoints 变化 -> EDS 推送
   2. VirtualService / DestinationRule 变化 -> RDS / CDS 推送
   3. Gateway 变化 -> LDS 推送
   4. 证书轮转 -> SDS 推送

   推送模式:
   - SOTW(State of the World):全量推送,简单但开销大
   - Incremental xDS:增量推送,Istio 1.x 默认
   - ADS:聚合推送,保证顺序(LDS -> RDS -> CDS -> EDS)
```

**xDS 推送风暴问题**:
- 1万 Pod 集群,Endpoints 一次变化触发 1万次 EDS 推送
- Istio 通过 **去抖动(debounce)** + **合并推送(compression)** 优化
- 推送延迟 P99 通常 < 1s,大规模集群需调优

### 28.3.5 Istio 完整架构

```
   +----------------------------------------------------------------+
   |                      Istio 1.21 完整架构                       |
   +----------------------------------------------------------------+

                            +-------------+
                            |   kubectl   |
                            |   istioctl  |
                            +------+------+
                                   | CRD apply
                                   v
   +---------------------------------------------------------------+
   |                        K8s API Server                         |
   |                                                                |
   |  CRD: VirtualService / DestinationRule / Gateway /            |
   |       ServiceEntry / WorkloadEntry / PeerAuthentication /      |
   |       RequestAuthentication / AuthorizationPolicy /            |
   |       WasmPlugin / Sidecar / EnvoyFilter                      |
   +-------------------------------+-------------------------------+
                                   | Watch
                                   v
   +---------------------------------------------------------------+
   |                        Istiod (控制面)                         |
   |                                                                |
   |  +----------+  +----------+  +----------+  +----------+       |
   |  |  Pilot   |  | Citadel  |  |  Galley  |  |  Sidecar |       |
   |  |          |  |          |  |          |  | injector |       |
   |  | - 路由   |  | - 证书   |  | - 配置   |  | - 注入   |       |
   |  | - 负载   |  |   签发   |  |   校验   |  |   Sidecar|       |
   |  |   均衡   |  | - mTLS   |  | - 协议  |  |          |       |
   |  | - 弹性   |  |   SDS    |  |   转换   |  |          |       |
   |  +----^-----+  +----^-----+  +----------+  +----^-----+       |
   |       |             |                          |              |
   |       +------+------^                          |              |
   |              | xDS / SDS                       | Mutating     |
   |              |                                  | Webhook      |
   +--------------|----------------------------------|-------------+
                  |                                  |
                  v (xDS 推送)                       v (Pod 创建时注入)
   +--------------------------------------------------------------+
   |                      数据面(Envoy Sidecar)                   |
   |                                                               |
   |  +-----+  +-----+  +-----+  +-----+                          |
   |  |Pod A|  |Pod B|  |Pod C|  |Pod D|                          |
   |  |+Envoy|  |+Envoy|  |+Envoy|  |+Envoy|                       |
   |  +--^--+  +--^--+  +--^--+  +--^--+                          |
   |     |        |        |        |                              |
   |     +--------+--------+--------+                              |
   |           mTLS 加密 + L7 治理                                  |
   +---------------------------------------------------------------+
```

**Istiod 三大组件**(1.5+ 已合并为单进程):

| 组件 | 职责 | 对应原独立组件 |
|------|------|---------------|
| Pilot | 配置分发、路由规则、负载均衡、弹性能力 | Istio Pilot + Istio Policy |
| Citadel | 证书签发、mTLS、SDS 通道 | Istio Citadel |
| Galley | 配置校验、协议转换、Webhook | Istio Galley |
| Sidecar Injector | Mutating Webhook,Pod 创建时注入 Envoy | istio-sidecar-injector |

### 28.3.6 Linkerd 架构

Linkerd 是 **轻量级服务网格**,数据面用 **Rust** 实现(Linkerd2-proxy),控制面极简:

```
   +----------------------------------------------------------+
   |                  Linkerd 2.14 架构                      |
   +----------------------------------------------------------+

   +------------------------------------------------------+
   |              控制面(linkerd-control-plane)          |
   |                                                      |
   |  +------------+  +------------+  +------------+      |
   |  | destination|  | identity   |  | proxy-     |      |
   |  | (服务发现) |  | (mTLS CA)  |  | injector   |      |
   |  +------------+  +------------+  +------------+      |
   |  +------------+  +------------+                      |
   |  |  web (UI)  |  |  tap (调试)|                      |
   |  +------------+  +------------+                      |
   +------------------------------------------------------+
                            | gRPC
                            v
   +------------------------------------------------------+
   |            数据面(linkerd2-proxy, Rust)              |
   |                                                      |
   |  +-----+  +-----+  +-----+                          |
   |  |Pod A|  |Pod B|  |Pod C|                          |
   |  |+proxy|  |+proxy|  |+proxy|                         |
   |  +-----+  +-----+  +-----+                          |
   +------------------------------------------------------+
```

**Linkerd vs Istio 对比**:

| 维度 | Linkerd | Istio |
|------|---------|-------|
| 数据面语言 | Rust(linkerd2-proxy) | C++(Envoy) |
| 资源开销 | 极低(~10-30MB) | 高(~100-200MB) |
| 功能 | 流量管理 + mTLS + 可观测(够用) | 全功能(L7 治理 + 策略 + 扩展) |
| 学习曲线 | 平缓 | 陡峭 |
| CRD 复杂度 | 简单(3-4 个) | 复杂(20+ 个) |
| 生态 | 中 | 大(CNCF 毕业项目,大厂支持) |
| 适合场景 | 中小规模、低资源 | 大规模、复杂治理 |

### 28.3.7 Knative Serving 架构

Knative 是基于 K8s 的 **Serverless 框架**,核心能力是 **缩容到 0 + 自动扩容 + 事件驱动**。

```
   +--------------------------------------------------------------+
   |                  Knative 1.13 Serving 架构                   |
   +--------------------------------------------------------------+

   用户创建:
        |
        v
   +-------------+  creates  +------------------+
   |  Service    |---------->|  Configuration   |
   |  (ksvc)     |           |  (config)        |
   +-------------+           +--------^---------+
                                      | creates
                                      v
                             +------------------+
                             |    Revision      |
                             |  (不可变版本)    |
                             +--------^---------+
                                      |
   +-------------+  references  +-----|----------+
   |   Route     |------------- >|   Revision     |
   |  (流量切分) |              |                 |
   +-------------+              +-----------------+

   控制器:
   +--------------------------------------------------+
   |             Knative Serving Controller           |
   |                                                  |
   |  - Service Controller: 创建 Configuration+Route  |
   |  - Configuration Controller: 创建 Revision        |
   |  - Revision Controller: 创建 Deployment+SKS       |
   |  - Route Controller: 创建 KIngress + 更新状态     |
   +--------------------------------------------------+

   自动扩缩容:
   +--------------------------------------------------+
   |           Knative Autoscaler (KPA)               |
   |                                                  |
   |  +----------+  metrics  +------------------+    |
   |  |  Pod     |---------->|  Autoscaler      |    |
   |  |(queue    |           |  - 决策扩缩容    |    |
   |  |  proxy)  |           |  - scale-to-zero |    |
   |  +----------+           +--------^---------+    |
   |                                   |              |
   |  +----------+  请求触发  +--------v---------+    |
   |  |Activator |<-----------|  冷启动兜底       |    |
   |  |(0副本时  |            |  - 缓冲请求      |    |
   |  |  接请求) |            |  - 唤醒 Pod      |    |
   |  +----------+            +------------------+    |
   +--------------------------------------------------+
```

**Knative Serving 核心 CRD**:

| CRD | 职责 | 类比 |
|-----|------|------|
| Service(ksvc) | 用户接口,定义服务 | K8s Service + Deployment |
| Configuration | 配置(template:镜像/env/资源) | Deployment template |
| Revision | 不可变版本(每次配置变更生成) | Deployment 的 ReplicaSet |
| Route | 流量切分(按 revision 百分比) | Istio VirtualService |

### 28.3.8 Knative Eventing 架构

Knative Eventing 提供 **事件驱动** 能力,解耦事件源与事件消费者:

```
   +----------------------------------------------------------+
   |                Knative Eventing 架构                     |
   +----------------------------------------------------------+

   事件源:
   +----------+  +----------+  +----------+  +----------+
   |  Kafka   |  |  GitHub  |  |  S3      |  |  Timer   |
   |  Source  |  |  Source  |  |  Source  |  |  Source  |
   +----^-----+  +----^-----+  +----^-----+  +----^-----+
        |             |             |             |
        +-------------+-------------+-------------+
                           | CloudEvents
                           v
   +--------------------------------------------------+
   |              Broker(事件总线)                    |
   |  - 入口: ingress                                |
   |  - 存储: Channel(Kafka / InMemory / NATS)       |
   +------------------------^-------------------------+
                           |
                           v
   +--------------------------------------------------+
   |              Trigger(事件过滤 + 路由)            |
   |  - attributes: 过滤条件                          |
   |  - subscriber: 订阅者(Knative Service)          |
   +------------------------^-------------------------+
                           |
                           v
   +--------------------------------------------------+
   |              Knative Service(消费者)             |
   |  - 缩到 0,事件来了再起                           |
   +--------------------------------------------------+
```

**Eventing 核心 CRD**:

| CRD | 职责 |
|-----|------|
| Source | 事件源(Kafka / GitHub / S3 / 自定义) |
| Broker | 事件总线(命名空间级) |
| Trigger | 事件过滤 + 订阅 |
| Channel | 事件存储后端 |
| Sequence | 事件顺序处理 |
| Parallel | 事件并行处理 |

### 28.3.9 Argo Workflows 架构

Argo Workflows 是 **K8s 原生的工作流引擎**,用 CRD 描述 DAG,每个 Step 是一个 Pod:

```
   +----------------------------------------------------------+
   |                  Argo Workflows 3.5 架构                 |
   +----------------------------------------------------------+

   +------------+  submit  +------------------------------+
   |  kubectl   |--------->|      Workflow CRD            |
   |  argo CLI  |          |  - spec: templates/steps     |
   +------------+          +--------------|---------------+
                                         | Watch
                                         v
   +------------------------------------------------------+
   |              Argo Workflow Controller                |
   |  - DAG 解析                                          |
   |  - 调度 Step                                         |
   |  - 创建 Pod                                          |
   |  - 监控状态                                          |
   +------------------------^-----------------------------+
                           | create Pod
                           v
   +------------------------------------------------------+
   |                  K8s Pod(每个 Step)                 |
   |  - init: argoexec(编排容器,执行 step)               |
   |  - main: 用户容器(实际任务)                         |
   +------------------------------------------------------+

   工作流模型:
   - Steps: 步骤序列(顺序/并行)
   - DAG: 有向无环图(依赖关系)
   - Loop: 循环
   - Conditional: 条件分支
   - Retry: 失败重试
```

### 28.3.10 Tekton 架构

Tekton 是 **K8s 原生的 CD 框架**,是 Tekton CD 的底座,采用 **Task + Pipeline** 模型:

```
   +----------------------------------------------------------+
   |                    Tekton 0.60 架构                     |
   +----------------------------------------------------------+

   +------------+  define  +------------------+
   |  Task      |--------->|  Step(容器)      |
   |  (可复用)  |          |  - script        |
   +------------+          |  - image         |
                           |  - workspace     |
   +------------+  define  +------------------+
   |  Pipeline  |--------->+------------------+
   |  (编排)    |          |  TaskRef         |
   +------------+          |  + runAfter      |
                           |  + params        |
                           +------------------+

   运行时:
   +------------+  trigger  +------------------+
   |  PipelineRun|--------->|  TaskRun         |
   |  (运行实例) |          |  (任务实例)       |
   +------------+          +--------^---------+
                                    | create Pod
                                    v
                           +------------------+
                           |  Pod(每个 Step   |
                           |  一个容器)        |
                           +------------------+

   Tekton 控制器:
   - PipelineRun Controller: 编排 TaskRun
   - TaskRun Controller: 创建 Pod
   - Resolution Controller: 远程 Task/Pipeline 解析
```

**Tekton vs Argo Workflows**:

| 维度 | Argo Workflows | Tekton |
|------|---------------|--------|
| 定位 | 通用工作流引擎(ETL/ML/CI/CD) | CD 标准框架(Tekton CD 底座) |
| 模型 | Workflow + Template | Task + Pipeline + Run |
| 重用 | templateRef | TaskRef(更细粒度) |
| 工作区 | volumeClaimTemplate | Workspace( PVC / ConfigMap) |
| DAG | 原生支持 | runAfter + results |
| 并行 | withItems / parallelism | PipelineRun 并行 |
| 生态 | 大(ML/ETL 场景多) | CD 标准化(Tekton CD) |
| 适合场景 | 复杂 DAG / ML Pipeline | 标准化 CI/CD Pipeline |

------

## 28.4 操作流程与命令

### 28.4.1 Istio 安装与验证

```bash
# 1. 下载 istioctl 1.21
curl -L https://istio.io/downloadIstio | ISTIO_VERSION=1.21.0 sh -
cd istio-1.21.0
export PATH=$PWD/bin:$PATH

# 2. 查看内置 profile
istioctl profile list
# default / demo / minimal / ambient / openshift / external / preview

# 3. 安装(default profile,含 Istiod + Prometheus + Kiali)
istioctl install --set profile=default -y

# 4. 验证安装
istioctl verify-install
istioctl version
kubectl get pods -n istio-system

# 5. 启用 Sidecar 自动注入(命名空间级)
kubectl label namespace default istio-injection=enabled

# 6. 部署示例应用(bookinfo)
kubectl apply -f samples/bookinfo/platform/kube/bookinfo.yaml

# 7. 配置分析(检查配置错误)
istioctl analyze
# ✔ No validation issues found when analyzing namespace: default.

# 8. 查看 Sidecar 状态
istioctl proxy-status
# NAME                                                   CLUSTER   CDS   LDS   EDS   RDS   ECDS   ISTIOD   VERSION
# details-v1-xxx.default                                Kubernetes SYNCED SYNCED SYNCED SYNCED        istiod-xxx 1.21.0

# 9. 查看 Envoy 配置
istioctl proxy-config listener <pod-name>
istioctl proxy-config cluster <pod-name>
istioctl proxy-config route <pod-name>
istioctl proxy-config endpoint <pod-name>
istioctl proxy-config secret <pod-name>

# 10. 升级 Istio
istioctl upgrade
istioctl manifest generate > istio-manifest.yaml  # 生成清单用于 GitOps
```

### 28.4.2 Ambient Mesh 启用(Istio 1.21+)

```bash
# 1. 安装 Ambient profile
istioctl install --set profile=ambient -y

# 2. 启用命名空间的 Ambient 模式(无 Sidecar)
kubectl label namespace default istio.io/dataplane-mode=ambient

# 3. 验证 ztunnel DaemonSet
kubectl get ds -n istio-system ztunnel
# NAME            DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE
# ztunnel         5         5         5       5            5

# 4. 部署 waypoint(需要 L7 治理的命名空间)
kubectl apply -f - <<EOF
apiVersion: istio.io/v1alpha1
kind: Gateway
metadata:
  name: waypoint
  namespace: default
spec:
  controllerName: istio-waypoint
EOF

# 5. 验证 mTLS 已生效(ztunnel L4)
istioctl ztunnel-config workloads
istioctl ztunnel-config services
```

### 28.4.3 Linkerd 安装

```bash
# 1. 安装 linkerd CLI
curl -sL https://run.linkerd.io/install | sh
export PATH=$PATH:$HOME/.linkerd2/bin

# 2. 预检查
linkerd check --pre

# 3. 安装控制面
linkerd install | kubectl apply -f -
linkerd check

# 4. 安装数据面(命名空间级注入)
kubectl annotate namespace default linkerd.io/inject=enabled
kubectl rollout restart deployment -n default

# 5. 查看 Dashboard
linkerd viz install | kubectl apply -f -
linkerd viz dashboard &

# 6. 查看流量
linkerd viz stat deploy
linkerd viz routes deploy/webapp
linkerd viz tap deploy/webapp  # 实时流量调试
```

### 28.4.4 Knative Serving 安装与使用

```bash
# 1. 安装 Knative Serving 1.13
kubectl apply -f https://github.com/knative/serving/releases/download/knative-v1.13.0/serving-crds.yaml
kubectl apply -f https://github.com/knative/serving/releases/download/knative-v1.13.0/serving-core.yaml

# 2. 安装网络层(Istio / Contour / Kourier)
kubectl apply -f https://github.com/knative/net-istio/releases/download/knative-v1.13.0/release.yaml

# 3. 配置域名(测试用)
kubectl apply -f - <<EOF
apiVersion: v1
kind: ConfigMap
metadata:
  name: config-domain
  namespace: knative-serving
data:
  example.com: ""
EOF

# 4. 部署 Knative Service
kubectl apply -f - <<EOF
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: helloworld
spec:
  template:
    metadata:
      annotations:
        autoscaling.knative.dev/scale-to-zero-pod-retention-period: "60s"
        autoscaling.knative.dev/target: "10"
    spec:
      containers:
      - image: gcr.io/knative-samples/helloworld-go
        resources:
          requests:
            cpu: 100m
            memory: 128Mi
EOF

# 5. 查看 Knative 资源
kubectl get ksvc,revision,route,configuration

# 6. 访问服务(获取 URL)
kubectl get ksvc helloworld -o jsonpath='{.status.url}'
curl http://helloworld.default.example.com

# 7. 触发冷启动
# 0 副本 -> 请求来了 -> Activator 唤醒 -> Pod ready -> 响应
kubectl get pods -w  # 观察从 0 到 1 的过程

# 8. 流量切分(蓝绿/金丝雀)
kubectl apply -f - <<EOF
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: helloworld
spec:
  traffic:
  - revisionName: helloworld-00001
    percent: 90
    tag: stable
  - revisionName: helloworld-00002
    percent: 10
    tag: canary
EOF
```

### 28.4.5 Knative Eventing 使用

```bash
# 1. 安装 Eventing
kubectl apply -f https://github.com/knative/eventing/releases/download/knative-v1.13.0/eventing-crds.yaml
kubectl apply -f https://github.com/knative/eventing/releases/download/knative-v1.13.0/eventing-core.yaml

# 2. 创建 Broker
kubectl apply -f - <<EOF
apiVersion: eventing.knative.dev/v1
kind: Broker
metadata:
  name: default
  namespace: default
EOF

# 3. 创建 Trigger(订阅事件)
kubectl apply -f - <<EOF
apiVersion: eventing.knative.dev/v1
kind: Trigger
metadata:
  name: my-trigger
spec:
  broker: default
  filter:
    attributes:
      type: com.example.order.created
  subscriber:
    ref:
      apiVersion: serving.knative.dev/v1
      kind: Service
      name: order-processor
EOF

# 4. 发送事件
curl -v "http://broker-ingress.knative-eventing.svc.cluster.local/default/default" \
  -H "Ce-Id: 1234" \
  -H "Ce-Specversion: 1.0" \
  -H "Ce-Type: com.example.order.created" \
  -H "Ce-Source: my-source" \
  -H "Content-Type: application/json" \
  -d '{"order":"abc123"}'
```

### 28.4.6 Argo Workflows 使用

```bash
# 1. 安装 Argo Workflows 3.5
kubectl create namespace argo
kubectl apply -n argo -f https://github.com/argoproj/argo-workflows/releases/download/v3.5.0/quick-start-postgres.yaml

# 2. 安装 argo CLI
curl -sLO https://github.com/argoproj/argo-workflows/releases/download/v3.5.0/argo-linux-amd64.gz
gunzip argo-linux-amd64.gz
chmod +x argo-linux-amd64
mv argo-linux-amd64 /usr/local/bin/argo

# 3. 提交工作流
argo submit examples/hello-world.yaml
argo list
argo get <workflow-name>
argo logs <workflow-name>

# 4. 查看 DAG 图
argo watch <workflow-name>
```

### 28.4.7 Tekton 使用

```bash
# 1. 安装 Tekton 0.60
kubectl apply -f https://storage.googleapis.com/tekton-releases/pipeline/previous/v0.60.0/release.yaml

# 2. 安装 tkn CLI
curl -sLO https://github.com/tektoncd/cli/releases/download/v0.35.0/tektoncd-cli-0.35.0_Linux-64bit.tar.gz
tar xzf tektoncd-cli-0.35.0_Linux-64bit.tar.gz
mv tkn /usr/local/bin/

# 3. 创建 Task
kubectl apply -f - <<EOF
apiVersion: tekton.dev/v1beta1
kind: Task
metadata:
  name: hello
spec:
  steps:
  - name: say-hello
    image: alpine
    script: |
      echo "Hello, Tekton!"
EOF

# 4. 创建 TaskRun
kubectl apply -f - <<EOF
apiVersion: tekton.dev/v1beta1
kind: TaskRun
metadata:
  name: hello-run
spec:
  taskRef:
    name: hello
EOF

# 5. 查看
tkn task list
tkn taskrun list
tkn taskrun logs hello-run -f
```

------

## 28.5 底层原理

### 28.5.1 Sidecar 注入机制(Mutating Webhook)

Istio 通过 **Mutating Admission Webhook** 在 Pod 创建时自动注入 Envoy Sidecar:

```
   Pod 创建流程(含 Sidecar 注入):

   kubectl apply -f pod.yaml
          |
          v
   +----------------+
   |  API Server    |
   |  接收请求       |
   +-------^--------+
           |
           v
   +----------------+
   |  Authn/Authz   |
   |  认证授权       |
   +-------^--------+
           |
           v
   +----------------+
   |  Mutating      |     +---------------------------+
   |  Webhook 链    |---->|  istio-sidecar-injector   |
   |                |     |  (MutatingWebhook)         |
   +-------^--------+     |                            |
           |              |  1. 检查 namespace label   |
           |              |     istio-injection=enabled|
           |              |  2. 读取 Pod spec          |
           |              |  3. 注入:                  |
           |              |     - istio-proxy 容器      |
           |              |     - init 容器(配 iptables)|
           |              |     - 环境变量              |
           |              |  4. 返回 JSON Patch         |
           |              +-----------|----------------+
           |                          |
           v                          v
   +----------------+    +----------------------+
   |  Validating    |    |  修改后的 Pod spec    |
   |  Webhook       |    |  + istio-proxy        |
   +-------^--------+    +----------|-----------+
           |                         |
           v                         v
   +----------------+    +----------------------+
   |  etcd          |<---|  持久化               |
   +----------------+    +----------------------+

   注入的容器:
   1. istio-init (init 容器):
      - 配置 iptables 规则(15006 入站,15001 出站)
      - 运行完退出

   2. istio-proxy (Sidecar 容器):
      - Envoy 进程
      - 监听 15006(入站)、15001(出站)
      - 监听 15010(健康检查)、15020(merged 监控)
      - 监听 15012(xDS)、15017(sds)
```

**Sidecar 注入的 JSON Patch 示例**:

```json
[
  {
    "op": "add",
    "path": "/spec/initContainers/-",
    "value": {
      "name": "istio-init",
      "image": "docker.io/istio/proxyv2:1.21.0",
      "command": ["istio-iptables"],
      "args": ["-p", "15001", "-z", "15006", ...]
    }
  },
  {
    "op": "add",
    "path": "/spec/containers/-",
    "value": {
      "name": "istio-proxy",
      "image": "docker.io/istio/proxyv2:1.21.0",
      "ports": [
        {"containerPort": 15006, "name": "istio-inbound"},
        {"containerPort": 15001, "name": "istio-outbound"}
      ],
      "env": [
        {"name": "ISTIO_META_CLUSTER_ID", "value": "Kubernetes"},
        {"name": "POD_NAME", "valueFrom": {...}}
      ]
    }
  }
]
```

### 28.5.2 Envoy xDS 配置分发详解

```
   Istiod xDS 推送完整链路:

   1. 配置变更源:
      - K8s Service/Endpoints 变化(kube-apiserver Watch)
      - VirtualService/DestinationRule 变化(kube-apiserver Watch)
      - Gateway/ServiceEntry 变化
      - 证书轮转(每 24h)

   2. Istiod 内部处理:
      +------------------+
      |  Config Store    | <- CRD 资源缓存
      |  (内存)          |
      +--------^---------+
               |
               v
      +------------------+
      |  Push Request    | <- 触发推送的事件
      |  Queue           |
      +--------^---------+
               |
               v
      +------------------+
      |  Debounce        | <- 去抖动(100ms)
      |  (合并多个变更)   |
      +--------^---------+
               |
               v
      +------------------+
      |  Config Builder  | <- 生成 xDS 配置
      |  (per-sidecar)   |
      +--------^---------+
               |
               v
      +------------------+
      |  ADS Server      | <- gRPC 流式推送
      |  (per-sidecar)   |
      +--------^---------+
               |
               v
      +------------------+
      |  Envoy           | <- 接收并应用配置
      |  (Sidecar)       |
      +------------------+

   3. 推送顺序(ADS 保证):
      LDS -> RDS -> CDS -> EDS -> SDS
      (监听器 -> 路由 -> 集群 -> 端点 -> 密钥)

   4. 推送优化:
      - 去抖动: 100ms 内的多个变更合并
      - 增量推送: 只推送变化的部分(Incremental xDS)
      - 按需推送: 只推送给受影响的 Sidecar
      - 压缩: 配置压缩,减少网络开销
      - 缓存: 配置缓存,避免重复计算
```

**xDS 推送性能基准**:

| 集群规模 | Sidecar 数 | EDS 推送 QPS | 推送延迟 P99 | Istiod 内存 |
|---------|-----------|-------------|-------------|------------|
| 100 Pod | 100 | 10 | 50ms | 200MB |
| 1000 Pod | 1000 | 100 | 200ms | 1GB |
| 5000 Pod | 5000 | 500 | 500ms | 4GB |
| 10000 Pod | 10000 | 1000 | 1s | 8GB(需调优)|
| 50000 Pod | 50000 | 5000 | 5s | 32GB(需分片)|

### 28.5.3 Istiod 配置分发(Pilot/Citadel/Galley)

```
   Istiod 内部组件协作:

   +------------------------------------------------------------------+
   |                          Istiod                                  |
   |                                                                  |
   |  +------------+    +-------------+    +----------------+         |
   |  |  Galley    |    |  Pilot      |    |  Citadel       |         |
   |  |            |    |             |    |                |         |
   |  | - 配置校验 |    | - 配置转换  |    | - 证书签发     |         |
   |  | - 协议转换 |    |   (CRD ->   |    |   (CA)         |         |
   |  | - Webhook  |    |    xDS)     |    | - SDS 通道     |         |
   |  |            |    | - 路由计算  |    | - 证书轮转     |         |
   |  |            |    | - 服务发现  |    |   (24h)        |         |
   |  +-----^------+    +------^------+    +-------^--------+         |
   |        |                  |                   |                  |
   |        v                  v                   v                  |
   |  +--------------------------------------------------------+     |
   |  |              K8s API Server Watch                      |     |
   |  |  - Services / Endpoints / Pods                         |     |
   |  |  - VirtualService / DestinationRule / Gateway          |     |
   |  |  - PeerAuthentication / RequestAuthentication          |     |
   |  |  - AuthorizationPolicy                                 |     |
   |  +--------------------------------------------------------+     |
   |                                                                  |
   |  +--------------------------------------------------------+     |
   |  |              xDS / SDS gRPC 推送                       |     |
   |  |  - ADS(Aggregated Discovery Service)                 |     |
   |  |  - 每个 Sidecar 一条 gRPC 长连接                      |     |
   |  +--------------------------------------------------------+     |
   +------------------------------------------------------------------+

   证书签发流程(Citadel):
   1. Pod 启动,istio-proxy 通过 SDS 请求证书
   2. Istiod 验证 Pod 身份(SPIFFE ID)
   3. Istiod 签发证书(自签名 CA,有效期 24h)
   4. 证书通过 SDS 推送给 Sidecar
   5. 证书到期前自动轮转(提前 1h)

   SPIFFE ID 格式:
   spiffe://cluster.local/ns/<namespace>/sa/<service-account>
```

### 28.5.4 Knative Autoscaler(KPA)原理

Knative 的 **KPA(Knative Pod Autoscaler)** 是专为 Serverless 设计的自动扩缩器,核心能力是 **scale-to-zero + 快速扩容**:

```
   Knative KPA 工作流程:

   +------------------+      请求       +------------------+
   |  用户请求        |----------------->|  KIngress /     |
   |                  |                  |  Route          |
   +------------------+                  +--------^--------+
                                                  |
                                                  v
                                          +------------------+
                                          |  Activator       |
                                          |  (流量缓冲)      |
                                          +--------^---------+
                                                   |
                          +------------------------+------------------------+
                          | Pod 副本数 = 0                                 | Pod 副本数 > 0
                          v                                                v
                  +------------------+                            +------------------+
                  |  请求缓冲        |                            |  直接转发到 Pod  |
                  |  触发扩容        |                            |  (绕过 Activator)|
                  |  等待 Pod ready  |                            +------------------+
                  +--------^---------+
                           |
                           v
                  +------------------+
                  |  Autoscaler      |    +------------------+
                  |  (KPA)           |<---|  Queue Proxy     |
                  |                  |    |  (每 Pod 一个)   |
                  |  - 收集 metrics  |    |  - 统计 RPS      |
                  |  - 计算 desired  |    |  - 上报          |
                  |  - scale up/down |    +------------------+
                  +--------^---------+
                           |
                           v
                  +------------------+
                  |  K8s Deployment  |
                  |  (修改 replicas) |
                  +------------------+

   扩缩容决策:
   desired_replicas = max(
     current_rps / target_rps,           # 基于 RPS
     current_concurrency / target,       # 基于并发
     min_scale,                          # 最小副本数
     0  (当 stable_window 内无流量)      # scale-to-zero
   )

   关键参数:
   - autoscaling.knative.dev/target: 10  (目标并发,默认 100)
   - autoscaling.knative.dev/minScale: 0  (最小副本)
   - autoscaling.knative.dev/maxScale: 10 (最大副本)
   - autoscaling.knative.dev/scale-to-zero-pod-retention-period: 60s
   - autoscaling.knative.dev/window: 60s (统计窗口)
   - autoscaling.knative.dev/panic-window: 10s (panic 模式窗口)
   - autoscaling.knative.dev/panic-threshold-percentage: 200
```

**KPA 的两种模式**:

| 模式 | 触发条件 | 行为 |
|------|---------|------|
| Stable 模式 | 正常流量 | 60s 窗口平滑扩缩 |
| Panic 模式 | 流量突增(>2x target) | 10s 窗口快速扩容,避免雪崩 |

### 28.5.5 Knative 冷启动(Cold Start)机制

冷启动是 Serverless 的核心痛点。Knative 通过 **Activator + 预热 + 优化镜像** 降低冷启动延迟:

```
   Knative 冷启动完整流程:

   T=0ms:  用户请求到达
           |
           v
   T=5ms:  KIngress 路由请求
           (检查 Route,发现 0 副本)
           |
           v
   T=10ms: 请求转发到 Activator
           (Activator 缓冲请求)
           |
           v
   T=15ms: Activator 通知 Autoscaler
           (触发 scale up)
           |
           v
   T=20ms: Autoscaler 修改 Deployment replicas=1
           (调用 K8s API)
           |
           v
   T=50ms: 调度器调度 Pod
           (Filter -> Score -> Bind)
           |
           v
   T=100ms: kubelet 拉起容器
           (拉镜像 + 创建容器)
           |  - 镜像已缓存: 100ms
           |  - 镜像未缓存: 5-30s
           v
   T=200ms-2s: 应用启动
           (JVM/Python 慢,Go 快)
           |
           v
   T=300ms-2.5s: Queue Proxy 启动
           (健康检查通过)
           |
           v
   T=350ms-3s: Pod ready
           (Activator 转发缓冲请求)
           |
           v
   T=400ms-3.5s: 用户收到响应

   冷启动优化:
   1. 镜像优化:
      - 使用 distroless / scratch 基础镜像
      - 多阶段构建,减小镜像大小(<50MB)
      - 镜像预拉到节点(daemonset pre-pull)

   2. 运行时优化:
      - 优先 Go / Rust(启动快,~50ms)
      - 避免 JVM(启动慢,~2s)
      - Python 用 gunicorn preload

   3. Knative 优化:
      - minScale=1(避免缩到 0,但失去 Serverless 优势)
      - scale-to-zero-pod-retention-period=5m(保留 Pod 5 分钟)
      - Progressiveness deadline: 600s(避免超时)
      - Container Concurrency: 1(单并发,避免雪崩)

   4. 节点优化:
      - 节点预留资源(避免调度等待)
      - Node Local Image Cache
      - 使用 Firecracker(轻量级 VM,启动 125ms)

   工业级冷启动数据:
   - Go + distroless: 200-400ms
   - Rust + scratch: 150-300ms
   - Python + gunicorn: 500ms-1.5s
   - Java + Spring Boot: 2-5s(需 GraalVM native image 优化到 100ms)
```

### 28.5.6 Tekton Controller + TaskRun/PipelineRun 原理

```
   Tekton 控制器工作流:

   用户创建 PipelineRun
          |
          v
   +-----------------------+
   |  PipelineRun          |
   |  (CRD 实例)           |
   +-----------^-----------+
               | Watch
               v
   +-----------------------+     +-----------------------+
   |  PipelineRun          |---->|  TaskRun              |
   |  Controller           |     |  (per Task)           |
   |                       |     +-----------^-----------+
   |  - 解析 Pipeline      |                 |
   |  - 计算 DAG           |                 | Watch
   |  - 创建 TaskRun       |                 v
   |  - 监控状态           |     +-----------------------+
   |  - 传递 params/results|     |  TaskRun              |
   +-----------------------+     |  Controller           |
                                 |                       |
                                 |  - 解析 Task          |
                                 |  - 创建 Pod           |
                                 |  - 注入 Workspace     |
                                 |  - 监控 Pod 状态      |
                                 |  - 收集 Results       |
                                 +-----------^-----------+
                                             |
                                             v
                                 +-----------------------+
                                 |  Pod                  |
                                 |                       |
                                 |  +-----------------+  |
                                 |  | step-0 (容器)   |  |
                                 |  +-----------------+  |
                                 |  +-----------------+  |
                                 |  | step-1 (容器)   |  |
                                 |  +-----------------+  |
                                 |  +-----------------+  |
                                 |  | nop (占位)      |  |
                                 |  +-----------------+  |
                                 |                       |
                                 |  +-----------------+  |
                                 |  | workspace (PVC) |  |
                                 |  +-----------------+  |
                                 +-----------------------+

   Pod 内容器执行顺序:
   1. init 容器: 挂载 workspace
   2. step-0: 执行第一个 Step
   3. step-1: 执行第二个 Step(共享 workspace)
   4. ...
   5. nop: 占位容器(Tekton 要求至少一个非 step 容器)

   Results 传递:
   - Step 写文件到 /tekton/results/<name>
   - TaskRun Controller 读取,写入 TaskRun.status.results
   - PipelineRun Controller 读取,传递给下游 Task
```

------

## 28.6 代码与配置示例

### 28.6.1 Istio VirtualService(流量路由)

```yaml
# VirtualService: 按 HTTP 路径/header 切分流量
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: reviews
  namespace: default
spec:
  hosts:
  - reviews            # 目标 Service
  gateways:
  - mesh               # 网格内部流量(也可加 gateway 名字用于外部)
  http:
  # 规则 1:Header contains "end-user=jason" -> v2
  - match:
    - headers:
        end-user:
          exact: jason
    route:
    - destination:
        host: reviews
        subset: v2
        port:
          number: 9080

  # 规则 2:金丝雀发布 v3 占 10%
  - route:
    - destination:
        host: reviews
        subset: v1
        port:
          number: 9080
      weight: 90
    - destination:
        host: reviews
        subset: v3
        port:
          number: 9080
      weight: 10
    # 超时 + 重试
    timeout: 5s
    retries:
      attempts: 3
      perTryTimeout: 2s
      retryOn: gateway-error,connect-failure,refused-stream
    # 故障注入(测试用)
    fault:
      delay:
        percentage:
          value: 0.1       # 0.1% 请求注入 5s 延迟
        fixedDelay: 5s
      abort:
        percentage:
          value: 0.05      # 0.05% 请求返回 500
        httpStatus: 500
    # 流量镜像(发 100% 到 v2 镜像)
    mirror:
      host: reviews
      subset: v2
    mirrorPercentage:
      value: 100
```

### 28.6.2 Istio DestinationRule(目标规则 + 熔断)

```yaml
# DestinationRule: 定义子集 + 负载均衡 + 熔断 + 连接池
apiVersion: networking.istio.io/v1beta1
kind: DestinationRule
metadata:
  name: reviews
  namespace: default
spec:
  host: reviews
  trafficPolicy:
    # 负载均衡策略
    loadBalancer:
      simple: LEAST_REQUEST    # ROUND_ROBIN / LEAST_REQUEST / RANDOM / PASSTHROUGH
      # consistentHash:        # 会话保持
      #   httpHeaderName: x-user
    # 连接池
    connectionPool:
      tcp:
        maxConnections: 100
      http:
        http1MaxPendingRequests: 10
        http2MaxRequests: 100
        maxRequestsPerConnection: 10
        maxRetries: 3
        idleTimeout: 30s
    # 熔断(outlier detection)
    outlierDetection:
      consecutive5xxErrors: 5        # 连续 5 次 5xx 触发熔断
      interval: 30s                  # 检测间隔
      baseEjectionTime: 30s          # 熔断时长
      maxEjectionPercent: 50         # 最大熔断比例
      minHealthPercent: 50           # 低于 50% 健康时不熔断
    # TLS
    tls:
      mode: ISTIO_MUTUAL             # 启用 mTLS
  # 子集定义(对应版本)
  subsets:
  - name: v1
    labels:
      version: v1
    trafficPolicy:
      loadBalancer:
        simple: ROUND_ROBIN
  - name: v2
    labels:
      version: v2
  - name: v3
    labels:
      version: v3
```

### 28.6.3 Istio Gateway(外部入口)

```yaml
# Gateway: 接收外部流量(类似 Ingress,但更强大)
apiVersion: networking.istio.io/v1beta1
kind: Gateway
metadata:
  name: bookinfo-gateway
  namespace: default
spec:
  selector:
    istio: ingressgateway       # 使用默认 ingressgateway
  servers:
  - port:
      number: 80
      name: http
      protocol: HTTP
    hosts:
    - "bookinfo.example.com"
    # 重定向到 HTTPS
    redirect:
      redirectCode: 301
      scheme: https
  - port:
      number: 443
      name: https
      protocol: HTTPS
    hosts:
    - "bookinfo.example.com"
    tls:
      mode: SIMPLE              # SIMPLE / MUTUAL / PASSTHROUGH
      credentialName: bookinfo-tls-secret  # K8s Secret

---
# 配套的 VirtualService(绑定到 Gateway)
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: bookinfo
spec:
  hosts:
  - "bookinfo.example.com"
  gateways:
  - bookinfo-gateway            # 绑定到 Gateway
  http:
  - match:
    - uri:
        exact: /productpage
    route:
    - destination:
        host: productpage
        port:
          number: 9080
  - match:
    - uri:
        prefix: /static
    route:
    - destination:
        host: productpage
        port:
          number: 9080
```

### 28.6.4 Istio ServiceEntry(外部服务)

```yaml
# ServiceEntry: 把外部服务纳入网格管理
apiVersion: networking.istio.io/v1beta1
kind: ServiceEntry
metadata:
  name: external-api
spec:
  hosts:
  - api.external.com
  location: MESH_EXTERNAL          # MESH_EXTERNAL / MESH_INTERNAL
  resolution: DNS                  # NONE / STATIC / DNS / DNS_ROUND_ROBIN
  ports:
  - number: 443
    name: https
    protocol: HTTPS
  - number: 80
    name: http
    protocol: HTTP
  # 也可以指定具体 IP(STATIC)
  # addresses:
  # - 192.168.1.100
  # endpoints:
  # - address: 192.168.1.100
  #   ports:
  #     https: 443
```

### 28.6.5 Istio WorkloadEntry(非 K8s 工作负载)

```yaml
# WorkloadEntry: 把 VM / 裸机服务纳入网格
apiVersion: networking.istio.io/v1beta1
kind: WorkloadEntry
metadata:
  name: vm-service
spec:
  serviceAccount: default
  address: 192.168.1.50           # VM IP
  labels:
    app: legacy-service
    version: v1
  network: vm-network
---
# ServiceEntry 引用 WorkloadEntry
apiVersion: networking.istio.io/v1beta1
kind: ServiceEntry
metadata:
  name: legacy-service
spec:
  hosts:
  - legacy-service.default.svc.cluster.local
  location: MESH_INTERNAL
  ports:
  - number: 8080
    name: http
    protocol: HTTP
  resolution: STATIC
  workloadSelector:
    labels:
      app: legacy-service
```

### 28.6.6 Istio PeerAuthentication(mTLS)

```yaml
# PeerAuthentication: 控制 mTLS 模式
apiVersion: security.istio.io/v1beta1
kind: PeerAuthentication
metadata:
  name: default
  namespace: default              # 命名空间级
spec:
  mtls:
    mode: STRICT                  # STRICT / PERMISSIVE / DISABLE / UNSET
  # 端口级覆盖
  portLevelMtls:
    8080:
      mode: PERMISSIVE            # 8080 端口允许明文(兼容旧客户端)

---
# 全局默认(命名空间 istio-system)
apiVersion: security.istio.io/v1beta1
kind: PeerAuthentication
metadata:
  name: default
  namespace: istio-system
spec:
  mtls:
    mode: STRICT                  # 全集群强制 mTLS
```

### 28.6.7 Istio RequestAuthentication(JWT)

```yaml
# RequestAuthentication: JWT 校验
apiVersion: security.istio.io/v1beta1
kind: RequestAuthentication
metadata:
  name: jwt-auth
  namespace: default
spec:
  selector:
    matchLabels:
      app: api-server             # 应用到 api-server
  jwtRules:
  - issuer: "https://auth.example.com/realms/myrealm"
    jwksUri: "https://auth.example.com/realms/myrealm/protocol/openid-connect/certs"
    audiences:
    - "my-api"
    # 从 header 提取 token
    fromHeaders:
    - name: Authorization
      prefix: "Bearer "
    # 也可从 params 提取
    fromParams:
    - "access_token"
    # 转发 token 到上游
    forwardOriginalToken: true
    # 失败响应
    outputPayloadToHeader: "x-jwt-payload"
```

### 28.6.8 Istio AuthorizationPolicy(授权)

```yaml
# AuthorizationPolicy: L7 授权策略
apiVersion: security.istio.io/v1beta1
kind: AuthorizationPolicy
metadata:
  name: api-authz
  namespace: default
spec:
  selector:
    matchLabels:
      app: api-server
  action: ALLOW                   # ALLOW / DENY / AUDIT / CUSTOM
  rules:
  # 规则 1: 允许 namespace istio-system 的请求
  - from:
    - source:
        namespaces: ["istio-system"]

  # 规则 2: 允许带有效 JWT 且 role=admin 的请求访问 /admin
  - from:
    - source:
        requestPrincipals: ["https://auth.example.com/*"]
    to:
    - operation:
        paths: ["/admin/*"]
        methods: ["GET", "POST"]
    when:
    - key: request.auth.claims[role]
      values: ["admin"]

  # 规则 3: 允许特定 IP 段
  - from:
    - source:
        ipBlocks: ["10.0.0.0/8", "192.168.0.0/16"]
        notIpBlocks: ["10.0.0.1/32"]   # 排除某个 IP
```

### 28.6.9 Istio WasmPlugin(扩展)

```yaml
# WasmPlugin: 用 Wasm 扩展 Envoy(自定义插件)
apiVersion: extensions.istio.io/v1alpha1
kind: WasmPlugin
metadata:
  name: custom-auth
  namespace: default
spec:
  selector:
    matchLabels:
      app: api-server
  url: oci://my-registry/wasm-plugin:1.0.0   # OCI 镜像
  # 或 url: file:///opt/wasm/plugin.wasm
  phase: AUTHN                  # UNNAMED_PHASE / AUTHN / AUTHZ / STATS
  priority: 10
  pluginConfig:
    type: my-plugin
    name: custom-auth-config
    configuration:
      secret_key: "xxx"
```

### 28.6.10 Knative Service 完整示例

```yaml
# Knative Service: 生产级配置
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: api-gateway
  namespace: default
  labels:
    app: api-gateway
  annotations:
    # 保留旧 Revision 数量(默认 10)
    serving.knative.dev/rollout-duration: "120s"
spec:
  template:
    metadata:
      annotations:
        # 自动扩缩配置
        autoscaling.knative.dev/class: "kpa.autoscaling.knative.dev"
        autoscaling.knative.dev/target: "10"              # 目标并发
        autoscaling.knative.dev/minScale: "0"             # 最小副本(0 = scale-to-zero)
        autoscaling.knative.dev/maxScale: "20"            # 最大副本
        autoscaling.knative.dev/scale-to-zero-pod-retention-period: "60s"
        autoscaling.knative.dev/window: "60s"
        autoscaling.knative.dev/panic-window: "10s"
        autoscaling.knative.dev/panic-threshold-percentage: "200"

        # 容器并发
        autoscaling.knative.dev/container-concurrency-target-default: "10"

        # 资源优化
        knative.dev/resource-podspec-patch: '{"containers":[{"resources":{"limits":{"cpu":"1","memory":"512Mi"}}}]}'
    spec:
      containerConcurrency: 0          # 0 = 不限制;1 = 单并发
      timeoutSeconds: 300              # 请求超时
      responseStartTimeoutSeconds: 60  # 首字节超时
      idleTimeoutSeconds: 60           # 空闲超时

      containers:
      - name: api
        image: registry.example.com/api-gateway:v1.0.0
        ports:
        - containerPort: 8080
          name: http1
        env:
        - name: APP_ENV
          value: production
        - name: DB_URL
          valueFrom:
            secretKeyRef:
              name: api-secret
              key: db-url
        resources:
          requests:
            cpu: 100m
            memory: 128Mi
          limits:
            cpu: 1000m
            memory: 512Mi
        # 探针(Knative 自动注入)
        readinessProbe:
          httpGet:
            path: /healthz
            port: 8080
          initialDelaySeconds: 0
          periodSeconds: 1
          timeoutSeconds: 1
          failureThreshold: 3
        # 优雅关闭
        lifecycle:
          preStop:
            exec:
              command: ["/bin/sh", "-c", "sleep 5"]

        # 卷挂载
        volumeMounts:
        - name: config
          mountPath: /etc/config

      volumes:
      - name: config
        configMap:
          name: api-config

      # 节点亲和性
      affinity:
        nodeAffinity:
          requiredDuringSchedulingIgnoredDuringExecution:
            nodeSelectorTerms:
            - matchExpressions:
              - key: node-role.kubernetes.io/worker
                operator: Exists

      # 容忍
      tolerations:
      - key: "spot-instance"
        operator: "Equal"
        value: "true"
        effect: "NoSchedule"

---
# 流量切分(金丝雀)
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: api-gateway
spec:
  traffic:
  - revisionName: api-gateway-00001
    percent: 90
    tag: stable
  - revisionName: api-gateway-00002
    percent: 10
    tag: canary
  - latestRevision: true
    percent: 0
    tag: latest
```

### 28.6.11 Argo Workflow 完整示例

```yaml
# Argo Workflow: ML 训练流水线
apiVersion: argoproj.io/v1alpha1
kind: Workflow
metadata:
  generateName: ml-pipeline-
spec:
  entrypoint: dag
  # 参数
  arguments:
    parameters:
    - name: dataset
      value: s3://bucket/data.csv
    - name: model-version
      value: v1.0.0

  # 卷
  volumeClaimTemplates:
  - metadata:
      name: workdir
    spec:
      accessModes: ["ReadWriteOnce"]
      resources:
        requests:
          storage: 10Gi

  templates:
  # DAG 模板
  - name: dag
    dag:
      tasks:
      - name: data-prep
        template: prepare-data
        arguments:
          parameters:
          - name: dataset
            value: "{{workflow.parameters.dataset}}"

      - name: train-model-a
        template: train
        dependencies: [data-prep]
        arguments:
          parameters:
          - name: algorithm
            value: "random-forest"

      - name: train-model-b
        template: train
        dependencies: [data-prep]
        arguments:
          parameters:
          - name: algorithm
            value: "xgboost"

      - name: evaluate
        template: eval
        dependencies: [train-model-a, train-model-b]

      - name: deploy
        template: deploy-model
        dependencies: [evaluate]
        when: "{{tasks.evaluate.outputs.result}} == pass"
        arguments:
          parameters:
          - name: model-version
            value: "{{workflow.parameters.model-version}}"

  # 数据准备
  - name: prepare-data
    inputs:
      parameters:
      - name: dataset
    container:
      image: python:3.11
      command: [python]
      args:
      - -c
      - |
        import pandas as pd
        df = pd.read_csv("{{inputs.parameters.dataset}}")
        # ... 数据清洗
        df.to_csv("/workdir/clean_data.csv", index=False)
      volumeMounts:
      - name: workdir
        mountPath: /workdir

  # 训练
  - name: train
    inputs:
      parameters:
      - name: algorithm
    outputs:
      parameters:
      - name: model-path
        value: /workdir/model.pkl
    container:
      image: ml-base:latest
      command: [python]
      args:
      - -c
      - |
        from sklearn import {{inputs.parameters.algorithm}}
        # ... 训练逻辑
        # 保存模型
      resources:
        limits:
          nvidia.com/gpu: 1
          cpu: 4
          memory: 8Gi
      volumeMounts:
      - name: workdir
        mountPath: /workdir

  # 评估
  - name: eval
    outputs:
      parameters:
      - name: result
        valueFrom:
          path: /workdir/result.txt
    container:
      image: ml-base:latest
      command: [sh, -c]
      args: ["echo pass > /workdir/result.txt"]
      volumeMounts:
      - name: workdir
        mountPath: /workdir

  # 部署
  - name: deploy-model
    inputs:
      parameters:
      - name: model-version
    container:
      image: bitnami/kubectl:latest
      command: [kubectl]
      args:
      - apply
      - -f
      - -
      - |
        apiVersion: serving.knative.dev/v1
        kind: Service
        ...
```

### 28.6.12 Tekton Pipeline 完整示例

```yaml
# Tekton Pipeline: CI/CD 流水线
apiVersion: tekton.dev/v1beta1
kind: Pipeline
metadata:
  name: build-deploy
spec:
  params:
  - name: git-url
    type: string
  - name: image
    type: string
  - name: tag
    type: string
    default: latest

  workspaces:
  - name: source

  tasks:
  # 1. 拉取代码
  - name: git-clone
    taskRef:
      name: git-clone
      bundle: gcr.io/tekton-releases/catalog/tasks/git-clone:0.10
    params:
    - name: url
      value: $(params.git-url)
    workspaces:
    - name: output
      workspace: source

  # 2. 构建镜像(并行: 单元测试 + 镜像构建)
  - name: unit-test
    runAfter: [git-clone]
    taskRef:
      name: golang-test
    workspaces:
    - name: source
      workspace: source

  - name: build-image
    runAfter: [git-clone]
    taskRef:
      name: kaniko
    params:
    - name: IMAGE
      value: $(params.image):$(params.tag)
    - name: DOCKERFILE
      value: ./Dockerfile
    workspaces:
    - name: source
      workspace: source

  # 3. 部署(依赖 build-image + unit-test)
  - name: deploy
    runAfter: [build-image, unit-test]
    taskRef:
      name: deploy-kubectl
    params:
    - name: image
      value: $(params.image):$(params.tag)

---
# Task: kaniko 构建镜像
apiVersion: tekton.dev/v1beta1
kind: Task
metadata:
  name: kaniko
spec:
  params:
  - name: IMAGE
  - name: DOCKERFILE
    default: ./Dockerfile
  workspaces:
  - name: source
  results:
  - name: image-digest
  steps:
  - name: build-and-push
    image: gcr.io/kaniko-project/executor:v1.18.0
    workingDir: $(workspaces.source.path)
    args:
    - --destination=$(params.IMAGE)
    - --dockerfile=$(params.DOCKERFILE)
    - --cache=true
    - --cache-ttl=24h
    - --snapshot-mode=redo
    - --digest-file=$(results.image-digest.path)
    securityContext:
      runAsUser: 0

---
# PipelineRun: 触发流水线
apiVersion: tekton.dev/v1beta1
kind: PipelineRun
metadata:
  generateName: build-deploy-run-
spec:
  pipelineRef:
    name: build-deploy
  params:
  - name: git-url
    value: https://github.com/myorg/myapp.git
  - name: image
    value: registry.example.com/myapp
  - name: tag
    value: v1.0.0
  workspaces:
  - name: source
    volumeClaimTemplate:
      metadata:
        create: true
      spec:
        accessModes: ["ReadWriteOnce"]
        resources:
          requests:
            storage: 1Gi
  serviceAccountName: tekton-deployer
  timeout: 1h
```

### 28.6.13 OpenFaaS Function 示例

```yaml
# OpenFaaS: 部署函数
apiVersion: openfaas.com/v1
kind: Function
metadata:
  name: echo
  namespace: openfaas-fn
spec:
  name: echo
  image: functions/echo:latest
  labels:
    com.openfaas.scale.min: 0          # scale to zero
    com.openfaas.scale.max: 10
    com.openfaas.scale.factor: 5
  annotations:
    com.openfaas.duration: "30s"       # 闲置多久后缩到 0
  limits:
    cpu: 200m
    memory: 256Mi
  requests:
    cpu: 50m
    memory: 128Mi
  environment:
    write_debug: "true"
  # 函数代码(也可用 stack.yaml 部署)
```

------

## 28.7 常见陷阱与调优

### 28.7.1 Sidecar 资源开销

**问题**:1万 Pod 集群,Sidecar 总内存开销 1TB+,CPU 开销巨大。

**根因**:
- 每个 Sidecar 默认 100-200MB 内存
- Envoy 配置缓存全量(所有 Service/Endpoint)
- 大集群的 EDS 推送导致 Envoy 内存膨胀

**调优**:

```yaml
# 1. 限制 Sidecar 配置范围(只看本 namespace 的服务)
apiVersion: networking.istio.io/v1beta1
kind: Sidecar
metadata:
  name: default
  namespace: default
spec:
  egress:
  - hosts:
    - "./*"                    # 只看本 namespace
    - "istio-system/*"          # + istio-system
    # 不看其他 namespace,大幅减少配置
```

```yaml
# 2. 调整 Sidecar 资源
apiVersion: v1
kind: ConfigMap
metadata:
  name: istio-sidecar-injector
  namespace: istio-system
data:
  values: |
    global:
      proxy:
        resources:
          requests:
            cpu: 50m            # 默认 100m
            memory: 64Mi        # 默认 128Mi
          limits:
            cpu: 500m
            memory: 256Mi
        # 限制配置大小
        concurrency: 2          # Envoy worker 线程数(默认 2)
        # 启用配置压缩
        envoyAccessLogService: {}
```

```bash
# 3. 切换 Ambient Mesh(大规模集群)
istioctl install --set profile=ambient
# ztunnel ~10MB/节点,waypoint 按需部署
```

**基准数据**:

| 方案 | 1万 Pod 内存开销 | 1万 Pod CPU 开销 |
|------|-----------------|-----------------|
| 默认 Sidecar | 1TB(100MB/Pod) | 1000 核(100m/Pod) |
| 优化 Sidecar | 500GB(50MB/Pod) | 500 核(50m/Pod) |
| Ambient Mesh | 50GB(10MB/节点,1000 节点) | 100 核 |

### 28.7.2 xDS 推送风暴

**问题**:Pod 频繁上下线(滚动更新/HPA 扩缩)导致 Endpoints 频繁变化,Istiod 推送风暴。

**根因**:
- Endpoints 一次变化 → 触发 EDS 推送
- 1万 Pod 集群,1次 Endpoints 变化 → 1万次 EDS 推送
- Istiod CPU 飙升,Envoy 配置抖动

**调优**:

```yaml
# 1. 调整 Istiod 推送去抖动
apiVersion: v1
kind: ConfigMap
metadata:
  name: istio
  namespace: istio-system
data:
  mesh: |
    defaultConfig:
      proxyMetadata:
        # 增加 debounce 时间
        PILOT_DEBOUNCE_AFTER: 500ms       # 默认 100ms
        PILOT_DEBOUNCE_MAX: 15s           # 默认 10s
        # 限制推送 QPS
        PILOT_MAX_PUSHES_PER_SECOND: 100
        # 启用增量 xDS
        PILOT_ENABLE_INCREMENTAL_XDS: "true"
```

```yaml
# 2. EndpointSlice 代替 Endpoints(降低 watch 压力)
# K8s 1.21+ 默认开启
apiVersion: v1
kind: ConfigMap
metadata:
  name: istio
  namespace: istio-system
data:
  mesh: |
    defaultConfig:
      proxyMetadata:
        # 启用 EndpointSlice
        PILOT_USE_ENDPOINT_SLICE: "true"
```

```bash
# 3. 监控推送延迟
istioctl proxy-status --xds-address istiod.istio-system:15012
# 查看 PUSH PUSH 状态,SYNCED = 正常,STALE = 落后

# 4. 查看 Istiod 推送 QPS
kubectl get --raw /metrics -n istio-system | grep pilot_xds
```

### 28.7.3 Envoy OOM

**问题**:Envoy 进程 OOMKilled,Sidecar 重启。

**根因**:
- 大集群配置膨胀(EDS 端点数百万)
- 连接泄漏(长连接堆积)
- Access Log 缓冲区堆积

**调优**:

```yaml
# 1. 增大 Sidecar 内存 limit
apiVersion: v1
kind: ConfigMap
metadata:
  name: istio-sidecar-injector
  namespace: istio-system
data:
  values: |
    global:
      proxy:
        resources:
          limits:
            memory: 512Mi        # 默认 256Mi,大集群调到 512Mi-1Gi
```

```yaml
# 2. 限制配置范围(Sidecar CRD)
# 见 28.7.1

# 3. 关闭不需要的 Access Log
apiVersion: networking.istio.io/v1beta1
kind: EnvoyFilter
metadata:
  name: disable-access-log
  namespace: istio-system
spec:
  configPatches:
  - applyTo: NETWORK_FILTER
    match:
      listener:
        filterChain:
          filter:
            name: "envoy.filters.network.http_connection_manager"
    patch:
      operation: MERGE
      value:
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
          access_log:
          - name: envoy.access_loggers.null
            typed_config:
              "@type": "type.googleapis.com/envoy.extensions.access_loggers.null.v3.Null"
```

```bash
# 4. 查看 Envoy 内存使用
istioctl proxy-config memory <pod-name>
# 或
kubectl exec <pod-name> -c istio-proxy --   curl -s localhost:15000/stats | grep memory
```

### 28.7.4 Istio 升级坑

**问题**:Istio 版本升级导致全集群 Sidecar 不兼容、流量中断。

**常见坑**:
1. CRD schema 不兼容(1.20 → 1.21 字段废弃)
2. Sidecar 镜像版本不匹配(控制面已升级,数据面未升级)
3. xDS 协议变更(新版本不再推送旧字段)
4. 默认行为变更(如 mTLS 模式从 PERMISSIVE 改为 STRICT)

**升级最佳实践**:

```bash
# 1. 先用 istioctl analyze 检查兼容性
istioctl experimental precheck
# ✔ No issues found

# 2. 使用 revision-based 升级(灰度)
istioctl install --revision 1-21 --set profile=default
# 此时集群有 1-20 和 1-21 两个 Istiod

# 3. 命名空间灰度迁移
kubectl label namespace canary istio.io/rev=1-21
kubectl rollout restart deployment -n canary

# 4. 验证灰度 namespace 正常
istioctl proxy-status -n canary

# 5. 全量迁移
kubectl label namespace default istio.io/rev=1-21
kubectl rollout restart deployment -n default

# 6. 卸载旧版本
istioctl uninstall --revision 1-20
```

```yaml
# 7. CRD 升级(谨慎)
# 备份现有 CRD 配置
kubectl get virtualservices,destinationrules,gateway -A -o yaml > istio-backup.yaml

# 升级 CRD
kubectl apply -f https://github.com/istio/istio/releases/download/1.21.0/crd-all.yaml
```

### 28.7.5 Knative 冷启动优化

**问题**:冷启动慢(2-5s),用户体验差。

**根因**:
- Pod 调度延迟(节点资源不足)
- 镜像拉取慢(大镜像 + 未缓存)
- 应用启动慢(JVM/Python)
- Queue Proxy 启动慢

**优化策略**:

```yaml
# 1. 保持最小副本(牺牲 Serverless 优势)
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: api
spec:
  template:
    metadata:
      annotations:
        autoscaling.knative.dev/minScale: "1"    # 至少 1 副本,无冷启动
        # 或保留 Pod 一段时间
        autoscaling.knative.dev/scale-to-zero-pod-retention-period: "10m"
```

```yaml
# 2. 预热镜像(daemonset 预拉)
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: image-prepull
spec:
  selector:
    matchLabels:
      app: image-prepull
  template:
    metadata:
      labels:
        app: image-prepull
    spec:
      containers:
      - name: prepull
        image: registry.example.com/myapp:v1.0.0
        command: ["sleep", "infinity"]
      tolerations:
      - operator: Exists
```

```yaml
# 3. 优化镜像(Go + distroless,~20MB)
# Dockerfile
FROM golang:1.22 AS builder
WORKDIR /app
COPY . .
RUN CGO_ENABLED=0 go build -o app -ldflags="-s -w" .

FROM gcr.io/distroless/static
COPY --from=builder /app/app /app
CMD ["/app"]
# 镜像大小 ~20MB,拉取 <1s
```

```yaml
# 4. 使用 GraalVM(Java native image,启动 50ms)
FROM ghcr.io/graalvm/native-image:22.3 AS builder
COPY . .
RUN native-image -o app --no-fallback

FROM debian:bookworm-slim
COPY --from=builder /app /app
CMD ["/app"]
```

```yaml
# 5. 节点预留资源(避免调度等待)
# 节点配置
kubelet:
  systemReserved:
    cpu: 500m
    memory: 1Gi
  kubeReserved:
    cpu: 200m
    memory: 500Mi
```

**冷启动基准**:

| 优化前 | 优化后 |
|--------|--------|
| JVM 应用: 5-10s | GraalVM: 50-200ms |
| 大镜像(500MB): 5-30s | 小镜像(20MB): 100-500ms |
| 调度等待: 500ms-2s | 预留资源: 100ms |
| 总计: 10-40s | 总计: 200-500ms |

### 28.7.6 Argo Workflows 大规模卡死

**问题**:工作流数千个 Step 并行,Argo Controller 卡死,Pod 创建缓慢。

**根因**:
- Controller 单实例,Reconcile 队列堆积
- 大量 Pod 创建压垮 APIServer
- Workflow 状态巨大(数 MB),etcd 写入慢

**调优**:

```yaml
# 1. 限制并行度
apiVersion: argoproj.io/v1alpha1
kind: Workflow
metadata:
  name: big-pipeline
spec:
  entrypoint: dag
  # 全局并行限制
  parallelism: 50                  # 最多 50 个并行 Step
  nodeSelector:
    workload: argo
  # Pod GC(清理已完成 Pod)
  podGC:
    strategy: OnPodCompletion      # OnPodSuccess / OnPodCompletion / OnWorkflowCompletion
  # 限制 Workflow 状态大小
  ttlStrategy:
    secondsAfterCompletion: 3600   # 1h 后清理
  templates:
  - name: dag
    dag:
      tasks:
      - name: parallel-task
        template: process
        # 单节点并行限制
        withItems: [{i: 1}, {i: 2}, ..., {i: 1000}]
        # 每批 50 个
        # Argo 会自动限制
```

```yaml
# 2. Controller 调优
apiVersion: apps/v1
kind: Deployment
metadata:
  name: workflow-controller
  namespace: argo
spec:
  template:
    spec:
      containers:
      - name: workflow-controller
        env:
        - name: ARGO_PARALLELISM
          value: "50"
        - name: ARGO_POD_NAMESPACE
          valueFrom:
            fieldRef:
              fieldPath: metadata.namespace
        - name: WORKFLOW_GC_PERIOD
          value: "30m"
        resources:
          limits:
            cpu: 4
            memory: 8Gi
```

```bash
# 3. 分片(多 Controller)
# Argo Workflows 支持多实例分片(按 namespace hash)
helm install argo-workflows argo/argo-workflows \
  --set controller.sharding.enabled=true \
  --set controller.sharding.replicas=3
```

### 28.7.7 Istio 与 NetworkPolicy 冲突

**问题**:启用 NetworkPolicy 后,Istio Sidecar 间通信被阻断,mTLS 失败。

**根因**:
- NetworkPolicy 在 L3/L4 控制(基于 IP/端口)
- Sidecar 出站流量源 IP 是 Pod IP
- mTLS 流量端口是 15006/15001

**解决方案**:

```yaml
# 1. 允许 Sidecar 间通信
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-istio
  namespace: default
spec:
  podSelector: {}
  policyTypes:
  - Ingress
  - Egress
  ingress:
  - from:
    - namespaceSelector: {}
    ports:
    - protocol: TCP
      port: 15006           # istio-inbound
    - protocol: TCP
      port: 15010           # istio health
    - protocol: TCP
      port: 15020           # istio merged metrics
  egress:
  - to:
    - namespaceSelector: {}
    ports:
    - protocol: TCP
      port: 15006
    - protocol: TCP
      port: 15012           # xDS
    - protocol: TCP
      port: 15017           # SDS
```

------

## 28.8 工业案例与基准数据

### 28.8.1 阿里 ASM / MSE(Alibaba Service Mesh)

**背景**:阿里内部 10万+ 微服务,原生 Istio 无法支撑,自研 ASM(Alibaba Service Mesh)+ 商业化 MSE(微服务引擎)。

**架构演进**:

```
   阶段 1(2018):Spring Cloud + HSF(Java SDK)
   - 10万服务,SDK 强耦合
   - 多语言困难(Python/Go 难接入)

   阶段 2(2020):MSE(基于 Istio 修改)
   - Sidecar 模式
   - 单集群 1万 Pod
   - 自研控制面(替代 Istiod)

   阶段 3(2023):ASM(Sidecarless + Ambient)
   - 节点级代理(类似 ztunnel)
   - 支持 10万 Pod
   - 控制面分片
```

**关键数据**:
- 单集群支持 5万 Pod(Istio 原生 1万上限)
- Sidecar 内存优化到 50MB(Istio 原生 100-200MB)
- xDS 推送延迟 P99 < 500ms
- mTLS 加密性能损耗 < 2%(原生 Istio 5-10%)

**核心优化**:
1. 控制面分片(按 namespace hash,5 个 Istiod 实例)
2. xDS 增量推送 + 压缩(配置大小减少 80%)
3. Sidecar 配置范围限制(只看本 namespace)
4. 自研 Envoy 扩展(WasmPlugin + Lua)

### 28.8.2 字节内部 Mesh(ByteMesh)

**背景**:字节跳动内部 5万+ 微服务,自研 ByteMesh(基于 Envoy + 自研控制面)。

**架构特点**:

```
   +----------------------------------------------------------+
   |                  ByteMesh 架构                           |
   +----------------------------------------------------------+

   +------------------+   +------------------+   +----------+
   | 控制面(自研)   |   | 数据面(Envoy)  |   | 管理面   |
   | - 配置分发       |   | - Sidecar 模式   |   | - Web UI |
   | - 服务发现       |   | - 节点级代理(混合)|   | - 监控   |
   | - 策略管理       |   | - Wasm 扩展     |   | - 审计   |
   +------------------+   +------------------+   +----------+
```

**关键数据**:
- 5万 Pod 部署
- Sidecar 内存 30-50MB(深度优化 Envoy)
- 单 Istiod 支持 1万 Sidecar(分片到 5 个 Istiod)
- 冷启动优化到 200ms(自研轻量级运行时)

**核心场景**:
1. 抖音推荐系统:5万服务,流量切分 + A/B 测试
2. 电商双 11:流量染色 + 全链路压测
3. 直播弹幕:WebSocket 长连接治理

### 28.8.3 腾讯 TKE Mesh / Tencent Service Mesh

**背景**:腾讯云 TKE Mesh 基于开源 Istio,提供托管式服务网格。

**关键数据**:
- 支持单集群 2万 Pod
- 控制面托管(用户无需运维 Istiod)
- 与 TKE / CBS / CLB 深度集成
- 多集群网格(跨 region)

**核心优化**:
1. 控制面多副本 + 自动扩缩
2. xDS 推送优化(批量 + 压缩)
3. Sidecar 资源自动调优

### 28.8.4 Google Anthos Service Mesh

**背景**:Google Cloud 的多云网格方案,基于 Istio。

**关键特点**:
- 多集群网格(GKE + On-Prem + EKS + AKS)
- 托管控制面(Google 管理 Istiod)
- mTLS 跨集群自动生效
- 与 Cloud Operations(监控/日志)集成

**基准数据**:
- 单网格支持 5 集群 / 5万 Pod
- 跨集群流量延迟增加 < 1ms
- 控制面 SLA 99.9%

### 28.8.5 AWS App Mesh

**背景**:AWS 原生服务网格,基于 Envoy。

**关键特点**:
- 与 ECS / EKS / Fargate / EC2 集成
- 托管控制面(AWS 管理)
- CloudWatch 监控集成
- IAM 认证集成

**与 Istio 对比**:

| 维度 | AWS App Mesh | Istio |
|------|-------------|-------|
| 控制面 | 托管(AWS 管理) | 自管理(Istiod) |
| CRD | AppMesh CRD | Istio CRD |
| 功能 | 流量管理 + mTLS + 可观测 | 全功能 |
| 多云 | 不支持(AWS only) | 支持 |
| 学习曲线 | 平缓(AWS 风格) | 陡峭 |
| 适合场景 | AWS 单云 | 多云 / 混合云 |

### 28.8.6 工业级基准数据汇总

**Sidecar 内存开销对比**:

| 方案 | Sidecar 内存 | 1万 Pod 总开销 | 优化手段 |
|------|-------------|---------------|---------|
| Istio 默认 | 100-200MB | 1-2TB | - |
| Istio 优化 | 50-100MB | 500GB-1TB | Sidecar CRD + 资源调优 |
| Linkerd | 10-30MB | 100-300GB | Rust 实现 |
| Ambient Mesh | 10MB(节点级) | 10GB(1K 节点) | Sidecarless |
| 阿里 ASM | 50MB | 500GB | 深度定制 |
| 字节 ByteMesh | 30-50MB | 300-500GB | 深度定制 |

**Knative 冷启动对比**:

| 方案 | 冷启动时间 | 优化手段 |
|------|----------|---------|
| 默认(JVM) | 5-10s | - |
| JVM + GraalVM | 200-500ms | native image |
| Go + distroless | 200-400ms | 小镜像 |
| Rust + scratch | 150-300ms | 极简镜像 |
| 字节(优化) | 200ms | 预热 + 轻量级运行时 |
| AWS Lambda | 100-500ms | Firecracker + 预热 |
| Google Cloud Run | 200-800ms | gVisor + 预热 |

**xDS 推送性能对比**:

| 方案 | 1万 Pod EDS 推送延迟 | Istiod 内存 |
|------|---------------------|------------|
| Istio 1.21 默认 | 1s P99 | 8GB |
| Istio + 增量 xDS | 500ms P99 | 4GB |
| 阿里 ASM | 500ms P99 | 4GB(分片) |
| 字节 ByteMesh | 200ms P99 | 2GB(深度优化) |

------

## 28.9 与其他方案的关系

### 28.9.1 服务网格对比:Istio vs Linkerd vs Consul Connect vs Cilium Service Mesh

| 维度 | Istio | Linkerd | Consul Connect | Cilium Service Mesh |
|------|-------|---------|---------------|---------------------|
| 数据面 | Envoy(C++) | linkerd2-proxy(Rust) | Envoy(C++) | Envoy / eBPF |
| 控制面 | Istiod(Go) | linkerd-control-plane(Go) | Consul(Go) | cilium-operator(Go) |
| 部署模式 | Sidecar / Ambient | Sidecar | Sidecar | Sidecar / Sidecarless(eBPF) |
| 资源开销 | 高(100-200MB/Pod) | 极低(10-30MB) | 中(50-100MB) | 极低(eBPF,~5MB) |
| 流量管理 | 强(L7 全功能) | 中(够用) | 中 | 中(eBPF L4/L7) |
| 安全 | mTLS + JWT + AuthZ | mTLS | mTLS + ACL | mTLS + AuthZ |
| 可观测 | 强(指标/追踪/日志) | 中 | 中 | 中 |
| 多集群 | 强 | 中 | 强 | 中 |
| 生态 | CNCF 毕业,大厂支持 | CNCF 毕业 | HashiCorp 生态 | CNCF 毕业,eBPF |
| 学习曲线 | 陡峭 | 平缓 | 中 | 平缓 |
| 适合场景 | 大规模、复杂治理 | 中小规模、低资源 | 多云、混合云 | eBPF 原生、性能优先 |

**选型建议**:
- **大规模、复杂治理、多集群**:Istio
- **中小规模、资源敏感**:Linkerd
- **HashiCorp 生态、多云**:Consul Connect
- **eBPF 原生、Cilium 已部署**:Cilium Service Mesh

### 28.9.2 Ambient Mesh vs Sidecar 模式

| 维度 | Sidecar 模式 | Ambient 模式 |
|------|-------------|-------------|
| 部署粒度 | 每 Pod 一个 Envoy | 每节点 ztunnel + 按需 waypoint |
| 资源开销 | 高(100MB/Pod) | 低(10MB/节点) |
| L4 mTLS | 支持 | 支持(ztunnel) |
| L7 治理 | 支持(Envoy) | 支持(waypoint,按需) |
| 流量拦截 | iptables redirect | HBONE(HTTP-Based Overlay) |
| 升级影响 | Pod 重启(Sidecar 重启) | ztunnel 升级不影响 Pod |
| 故障隔离 | Sidecar 故障影响单 Pod | ztunnel 故障影响全节点 |
| 成熟度 | 生产成熟(Istio 1.0+) | GA(Istio 1.21+),生产验证中 |
| 适合场景 | 通用 | 大规模、低资源、L7 策略少 |

**Ambient Mesh 的取舍**:
- 优点:资源开销低 10x,升级无感
- 缺点:L7 治理需 waypoint(额外开销),故障域更大
- 建议:大规模集群(>1000 Pod)+ L7 策略少 → Ambient;小规模 + L7 治理复杂 → Sidecar

### 28.9.3 Serverless 对比:Knative vs OpenFaaS vs AWS Lambda vs Google Cloud Run

| 维度 | Knative | OpenFaaS | AWS Lambda | Google Cloud Run |
|------|---------|---------|-----------|-----------------|
| 类型 | K8s 框架 | K8s 框架 | 云服务 | 云服务(基于 Knative) |
| 部署 | 自部署 K8s | 自部署 K8s | AWS 托管 | GCP 托管 |
| 缩到 0 | 支持(KPA) | 支持 | 支持 | 支持 |
| 冷启动 | 200ms-2s | 300ms-3s | 100-500ms | 200-800ms |
| 事件驱动 | Eventing(强) | 中 | 中(EventBridge) | 中(Eventarc) |
| 流量切分 | 支持(Revision) | 弱 | 支持(Alias) | 支持(Tag) |
| 计费 | 按资源(自部署) | 按资源(自部署) | 按请求/毫秒 | 按请求/毫秒 |
| 多云 | 支持(K8s) | 支持(K8s) | AWS only | GCP only(Anthos 多云) |
| 适合场景 | 自部署、多云、事件驱动 | 简单 FaaS | AWS 单云、低频 | GCP 单云、容器 |

**选型建议**:
- **自建 K8s、需要控制**:Knative
- **简单 FaaS、社区友好**:OpenFaaS
- **AWS 单云、低频访问**:Lambda
- **GCP 单云、容器化**:Cloud Run

### 28.9.4 Serverless 容器对比:ACK ASK vs AWS Fargate vs Cloud Run vs Azure ACI

| 方案 | 厂商 | 计费 | 冷启动 | 缩到 0 | 集成 |
|------|------|------|--------|--------|------|
| ACK ASK | 阿里 | 按秒 | 1-3s | 支持 | 阿里云生态 |
| AWS Fargate | AWS | 按秒 | 1-3s | 不支持(需 Lambda) | AWS 生态 |
| Google Cloud Run | GCP | 按毫秒 | 200-800ms | 支持 | GCP + Knative |
| Azure ACI | Azure | 按秒 | 2-5s | 不支持 | Azure 生态 |

**与 K8s 的关系**:
- ACK ASK / Fargate:K8s 节点级 Serverless(替代 EC2/ECS 节点)
- Cloud Run / ACI:容器级 Serverless(直接部署容器,不需 K8s 集群)

**与服务网格的协同**:
- Serverless 容器(ASK / Fargate)节点上可运行 Sidecar
- Cloud Run 内置 Istio 流量治理能力
- Knative on ASK:Serverless 容器 + Serverless 框架(极致弹性)

### 28.9.5 Argo Workflows vs Tekton vs Argo CD

| 维度 | Argo Workflows | Tekton | Argo CD |
|------|---------------|--------|---------|
| 定位 | 通用工作流引擎 | CD 标准框架 | GitOps 部署 |
| 模型 | Workflow + Template | Task + Pipeline + Run | Application + Sync |
| 场景 | ETL / ML / CI/CD | CI/CD Pipeline | 部署(Sync) |
| DAG | 原生支持 | runAfter | 不支持 |
| 触发 | 手动 / 定时 / Webhook | 手动 / Trigger | Git 变更 |
| GitOps | 弱 | 弱 | 强(原生) |
| 多步骤 | 强(Steps / DAG) | 强(Pipeline) | 弱(单 Sync) |
| 适合场景 | 复杂工作流 / ML Pipeline | 标准化 CI/CD | GitOps 部署 |

**典型组合**:
- CI/CD 流水线:Tekton(构建)+ Argo CD(部署)
- ML Pipeline:Argo Workflows(训练)+ Argo CD(部署)
- 通用 ETL:Argo Workflows

### 28.9.6 OpenFaaS vs OpenWhisk vs Kubeless

| 维度 | OpenFaaS | OpenWhisk | Kubeless |
|------|---------|-----------|---------|
| 类型 | K8s 框架 | K8s / 独立 | K8s 框架 |
| 函数模型 | Function + Stack | Action + Trigger | Function |
| 语言 | 多语言(模板) | 多语言 | 多语言 |
| 事件 | Connector | Trigger | Trigger |
| 缩到 0 | 支持 | 支持 | 支持 |
| 生态 | CNCF 沙箱 | Apache | CNCF 停止维护 |
| 状态 | 活跃 | 活跃 | 停滞 |
| 适合场景 | K8s 原生 FaaS | 复杂事件 | 已弃用 |

**推荐**:OpenFaaS(K8s 原生,生态活跃)

------

## 28.10 面试速答

**Q1:服务网格是什么?解决什么问题?**

A:服务网格是微服务通信基础设施,通过 Sidecar(或节点级代理)接管服务的所有进出流量,提供流量管理、安全(mTLS/JWT/授权)、可观测性三大能力,实现语言无关、无侵入的统一治理。解决微服务规模化后通信逻辑重复实现、治理策略难以统一、安全加密复杂等问题。

**Q2:Sidecar 模式和 Ambient Mesh 的区别?**

A:Sidecar 模式每个 Pod 注入一个 Envoy,资源开销大(~100MB/Pod)但功能全(L7 治理)。Ambient Mesh(Istio 1.18+)采用 ztunnel(节点级 L4 mTLS)+ waypoint(按需 L7 治理)两层架构,资源开销低(~10MB/节点),升级无感,但 L7 治理需额外 waypoint。大规模集群推荐 Ambient,复杂 L7 治理推荐 Sidecar。

**Q3:Envoy xDS 协议有哪些?**

A:xDS 是 Envoy 的动态配置发现协议,包括:
- LDS(Listener):监听器配置
- RDS(Route):路由配置
- CDS(Cluster):集群配置
- EDS(Endpoint):端点配置
- SDS(Secret):密钥配置
- ADS(Aggregated):聚合 xDS,保证顺序(LDS→RDS→CDS→EDS)

Istiod 通过 ADS gRPC 流式推送配置到 Envoy,支持增量推送和去抖动优化。

**Q4:Istio 的 mTLS 模式有哪些?**

A:三种模式:
- STRICT:强制 mTLS,非 mTLS 请求被拒绝
- PERMISSIVE:同时接受 mTLS 和明文(迁移用)
- DISABLE:禁用 mTLS

通过 PeerAuthentication CRD 配置,支持命名空间级和端口级覆盖。

**Q5:Istio 流量切分如何实现?**

A:通过 VirtualService 的 http.route.weight 字段实现,例如:
```yaml
http:
- route:
  - destination: {host: reviews, subset: v1}
    weight: 90
  - destination: {host: reviews, subset: v3}
    weight: 10
```
配合 DestinationRule 定义 subset(按 version 标签),实现精确的百分比切流。支持按 header/path 匹配 + 镜像 + 故障注入。

**Q6:Knative 如何实现 scale-to-zero?**

A:Knative 通过 KPA(Knative Pod Autoscaler)实现:
1. 每个 Pod 注入 Queue Proxy,统计 RPS/并发
2. KPA 收集 metrics,60s 窗口(stable)无流量时 replicas=0
3. 请求到达时,Activator 缓冲请求,触发 KPA 扩容
4. Pod ready 后,Activator 转发请求

关键参数:minScale(最小副本)、target(目标并发)、scale-to-zero-pod-retention-period(保留 Pod 时间)。

**Q7:Knative 冷启动慢的原因和优化?**

A:原因:Pod 调度 + 镜像拉取 + 应用启动 + Queue Proxy 启动。
优化:
1. 镜像优化(distroless/scratch,<50MB)
2. Go/Rust 替代 JVM(启动快)
3. JVM 用 GraalVM native image(50-200ms)
4. minScale=1(避免缩到 0)
5. 镜像预拉(daemonset)
6. 节点预留资源

工业级目标:Go + distroless,冷启动 200-400ms。

**Q8:Argo Workflows 和 Tekton 的区别?**

A:Argo Workflows 是通用工作流引擎,偏 DAG 编排,适合 ML/ETL/CI/CD 复杂场景;Tekton 是 CD 标准框架(Tekton CD 底座),偏标准化 CI/CD Pipeline,Task/Pipeline 重用性强。Argo 用 Workflow+Template,Tekton 用 Task+Pipeline+Run。典型组合:Tekton(构建)+ Argo CD(部署)。

**Q9:Istio 升级如何做?**

A:revision-based 灰度升级:
1. istioctl install --revision 1-21(安装新版本 Istiod)
2. 标记 namespace 迁移:kubectl label ns canary istio.io/rev=1-21
3. 重启 Pod:kubectl rollout restart -n canary
4. 验证灰度正常
5. 全量迁移
6. 卸载旧版本:istioctl uninstall --revision 1-20

避免直接原地升级(全集群 Sidecar 不兼容)。

**Q10:服务网格的 Sidecar 资源开销如何优化?**

A:
1. Sidecar CRD 限制配置范围(只看本 namespace)
2. 调小 resources.requests(50m/64Mi)
3. 关闭不需要的 Access Log
4. 限制 Envoy worker 线程数(concurrency: 2)
5. 大规模集群切换 Ambient Mesh(10MB/节点)

1万 Pod 集群,优化后内存从 1TB 降到 500GB 甚至 50GB(Ambient)。

**Q11:xDS 推送风暴如何解决?**

A:
1. 增加去抖动时间(PILOT_DEBOUNCE_AFTER: 500ms)
2. 启用增量 xDS(PILOT_ENABLE_INCREMENTAL_XDS)
3. 使用 EndpointSlice 代替 Endpoints
4. Sidecar CRD 限制配置范围
5. 控制面分片(多 Istiod 实例)

1万 Pod 集群,优化后 EDS 推送延迟 P99 从 5s 降到 500ms。

**Q12:Knative Eventing 的核心概念?**

A:
- Source:事件源(Kafka/GitHub/S3)
- Broker:事件总线(命名空间级)
- Trigger:事件过滤 + 订阅
- Channel:事件存储后端
- CloudEvents:事件格式标准

工作流:Source → Broker → Trigger(过滤)→ Knative Service(消费者)。实现事件源与消费者的解耦,支持 scale-to-zero。

**Q13:Istio 的 AuthorizationPolicy 如何工作?**

A:AuthorizationPolicy 是 L7 授权策略,基于 source(命名空间/IP/主体)、operation(路径/方法/端口)、when(条件,如 JWT claims)匹配请求。action 包括 ALLOW/DENY/AUDIT/CUSTOM。默认 DENY(白名单模式)。例如:允许 namespace istio-system 访问、允许 JWT role=admin 访问 /admin。

**Q14:Tekton 的 Workspace 是什么?**

A:Workspace 是 Tekton 的存储抽象,用于在 Step / Task 间共享数据。类型:
- PVC(volumeClaimTemplate)
- ConfigMap
- Secret
- EmptyDir
- VolumeClaimTemplate

Pipeline 级 Workspace 可被多个 Task 共享,Task 级 Workspace 可被多个 Step 共享。

**Q15:服务网格和 API Gateway 的区别?**

A:
- API Gateway:南北向流量(外部 → 内部),L7 路由,认证,限流
- 服务网格:东西向流量(内部 ↔ 内部),mTLS,熔断,追踪

两者互补:Gateway 处理外部入口,Mesh 处理内部通信。Istio Gateway 实际上也能做 API Gateway 的事,但通常搭配独立 API Gateway(如 Kong/APISIX)使用。

------

## 28.11 综合面试题

### 28.11.1 设计题:1万微服务规模的服务网格选型与落地

**题目**:公司有 1万+ 微服务(Java/Go/Python 混合),需要统一流量治理、安全、可观测,如何选型和落地服务网格?

**答题要点**:

1. **选型**:
   - Istio(功能全、生态大,但资源开销高)
   - Linkerd(轻量,但功能弱)
   - Ambient Mesh(Istio 1.21+,资源低,但生产验证少)
   - 推荐:Istio + Ambient Mesh(ztunnel L4 + waypoint L7 按需)

2. **架构**:
   - 控制面分片(5 个 Istiod,按 namespace hash)
   - 数据面 Ambient(ztunnel DaemonSet + waypoint Deployment)
   - 管理面 Kiali + Prometheus + Jaeger
   - 多集群网格(3 集群,跨 region)

3. **落地路径**:
   - 阶段 1:PERMISSIVE 模式(兼容现有明文流量)
   - 阶段 2:逐步迁移到 STRICT(强制 mTLS)
   - 阶段 3:流量切分 + 金丝雀
   - 阶段 4:AuthorizationPolicy(L7 授权)
   - 阶段 5:Ambient Mesh 切换(资源优化)

4. **性能优化**:
   - Sidecar CRD 限制配置范围
   - 增量 xDS + 去抖动
   - EndpointSlice
   - Sidecar 资源调优(50m/64Mi)

5. **容量规划**:
   - Sidecar 内存:1万 Pod × 50MB = 500GB(优化后)
   - Istiod 内存:5 实例 × 4GB = 20GB
   - xDS 推送延迟 P99 < 500ms

6. **风险**:
   - 升级风险(revision-based 灰度)
   - NetworkPolicy 冲突(允许 15006/15012/15017)
   - Envoy OOM(内存监控 + 告警)

### 28.11.2 原理题:Sidecar 注入与流量拦截机制

**题目**:详细描述 Istio Sidecar 的注入过程和流量拦截机制。

**答题要点**:

1. **注入机制**(Mutating Webhook):
   - namespace 标记 istio-injection=enabled
   - Pod 创建时触发 MutatingWebhookConfiguration
   - istio-sidecar-injector 接收请求
   - 注入 init 容器(配 iptables)+ istio-proxy 容器(Envoy)
   - 返回 JSON Patch,API Server 持久化

2. **流量拦截**(iptables redirect):
   - init 容器配置 iptables 规则
   - 入站:PREROUTING → REDIRECT 15006(Envoy 入站监听)
   - 出站:OUTPUT → REDIRECT 15001(Envoy 出站监听)
   - 排除:15006/15001/15010/15012/15017 等 Sidecar 自身端口

3. **流量路径**:
   - 入站:外部 → Pod IP:8080 → iptables → 15006 → Envoy → 应用 8080
   - 出站:应用 → 外部:3306 → iptables → 15001 → Envoy → mTLS → 外部

4. **Ambient Mesh 区别**:
   - 无 iptables,用 HBONE(HTTP-Based Overlay)
   - ztunnel 节点级代理(L4 mTLS)
   - Pod 流量通过 HBONE 隧道到 ztunnel

### 28.11.3 故障题:Knative 冷启动雪崩

**题目**:Knative 服务在流量突增时,冷启动雪崩,响应时间飙升到 30s,如何排查和优化?

**答题要点**:

1. **排查**:
   - 查 Activator 缓冲队列长度(knative_activator_request_count)
   - 查 Pod 创建延迟(kube_pod_start_time)
   - 查镜像拉取时间(container_image_pull_duration)
   - 查应用启动时间(application_start_time)
   - 查节点资源(是否调度等待)

2. **根因**:
   - 流量突增,Activator 缓冲堆积
   - 同时触发大量 Pod 创建,节点资源不足
   - 镜像未缓存,拉取慢
   - JVM 应用启动慢

3. **优化**:
   - **预热**:minScale=1(避免缩到 0)
   - **保留 Pod**:scale-to-zero-pod-retention-period=10m
   - **镜像预拉**:daemonset 预拉镜像
   - **运行时**:Go 替代 JVM,或 GraalVM native image
   - **节点预留**:节点预留资源,避免调度等待
   - **限流**:Activator 并发限制(container-concurrency)
   - **panic 模式**:调整 panic-window(10s)和 panic-threshold(200%)

4. **监控**:
   - 冷启动延迟分布(P50/P90/P99)
   - Activator 队列长度
   - Pod 创建延迟
   - 镜像拉取延迟

### 28.11.4 设计题:基于 Knative + Eventing 构建事件驱动架构

**题目**:设计一个订单系统,订单创建后触发库存扣减、支付、通知等异步流程,使用 Knative Eventing 实现。

**答题要点**:

1. **架构**:
   ```
   订单服务 → Broker → Trigger 1 → 库存服务(Knative Service)
                      → Trigger 2 → 支付服务(Knative Service)
                      → Trigger 3 → 通知服务(Knative Service)
   ```

2. **事件源**:
   - 订单服务通过 HTTP POST 发送 CloudEvents 到 Broker

3. **Broker**:
   - 默认 Broker(Channel 后端:Kafka)
   - 命名空间级

4. **Trigger**:
   - 库存:filter type=com.example.order.created
   - 支付:filter type=com.example.order.created
   - 通知:filter type=com.example.order.paid(支付完成后触发)

5. **服务**:
   - 库存服务:Knative Service,scale-to-zero,处理完发送 com.example.inventory.updated
   - 支付服务:Knative Service,处理完发送 com.example.order.paid
   - 通知服务:Knative Service,监听 com.example.order.paid

6. **关键设计**:
   - 事件幂等(避免重复处理)
   - 事件顺序(Sequence / Parallel)
   - 错误处理(死信队列)
   - 事件溯源(所有事件存档)

7. **优势**:
   - 解耦:订单服务不关心下游
   - 弹性:scale-to-zero,按需起
   - 可观测:全链路追踪
   - 扩展:新增消费者只需加 Trigger

### 28.11.5 原理题:Envoy xDS 推送优化

**题目**:1万 Pod 集群,Istiod xDS 推送延迟 P99 飙到 5s,如何优化?

**答题要点**:

1. **根因分析**:
   - Endpoints 频繁变化(滚动更新/HPA 扩缩)
   - 1次 Endpoints 变化 → 1万次 EDS 推送
   - Istiod CPU 飙升,推送队列堆积

2. **优化手段**:
   - **去抖动**:PILOT_DEBOUNCE_AFTER=500ms(默认 100ms)
   - **增量 xDS**:PILOT_ENABLE_INCREMENTAL_XDS=true(只推变化)
   - **EndpointSlice**:PILOT_USE_ENDPOINT_SLICE=true(降低 watch 压力)
   - **Sidecar CRD**:限制配置范围(只看本 namespace)
   - **控制面分片**:5 个 Istiod 实例,按 namespace hash
   - **推送 QPS 限制**:PILOT_MAX_PUSHES_PER_SECOND=1000

3. **监控**:
   - istio_pilot_xds_pushes(Prometheus 指标)
   - istio_pilot_xds_push_time
   - istio_pilot_xds_config_size

4. **效果**:
   - 优化前:EDS 推送 P99 5s,Istiod 内存 16GB
   - 优化后:EDS 推送 P99 500ms,Istiod 内存 4GB

### 28.11.6 对比题:Istio Sidecar vs Ambient Mesh vs Cilium Service Mesh

**题目**:对比三种服务网格数据面方案,分析各自适合的场景。

**答题要点**:

| 维度 | Istio Sidecar | Istio Ambient | Cilium Service Mesh |
|------|--------------|---------------|---------------------|
| 数据面 | Envoy(C++) | ztunnel(Rust) + waypoint(Envoy) | Envoy / eBPF |
| 部署 | 每 Pod 一个 | 每节点 ztunnel + 按需 waypoint | 每节点 eBPF + 按需 Envoy |
| 资源开销 | 100MB/Pod | 10MB/节点 | 5MB/节点(eBPF) |
| L4 mTLS | 支持 | 支持(ztunnel) | 支持(eBPF) |
| L7 治理 | 支持(Envoy) | 支持(waypoint) | 支持(Envoy,按需) |
| 升级 | Pod 重启 | 无感(ztunnel 独立) | 无感(eBPF) |
| 故障域 | 单 Pod | 单节点 | 单节点 |
| 成熟度 | 生产成熟 | GA(1.21+),验证中 | 较新,生产验证少 |
| 适合场景 | 通用、复杂 L7 | 大规模、低资源 | eBPF 原生、性能优先 |

**选型建议**:
- 中小规模(<1000 Pod)+ 复杂 L7:Sidecar
- 大规模(>1000 Pod)+ L7 策略少:Ambient
- 已用 Cilium + 性能优先:Cilium Service Mesh

------

## 28.12 故障复盘

### 28.12.1 案例 1:Istio 升级全集群断网

**现象**:某公司从 Istio 1.18 升级到 1.20 后,全集群微服务间通信中断,所有请求 503。

**根因**:
1. 直接原地升级(未用 revision-based)
2. Istiod 1.20 默认 mTLS 模式从 PERMISSIVE 改为 STRICT
3. 旧版本 Sidecar(1.18)与新 Istiod(1.20)xDS 协议不兼容
4. Sidecar 收不到配置,所有流量被拒绝

**修复**:
1. 紧急回滚 Istiod 到 1.18
2. 全集群重启 Pod(让 Sidecar 重新连接)
3. 切换到 revision-based 灰度升级

**防范**:
1. 升级前用 istioctl experimental precheck 检查
2. 使用 revision-based 升级(灰度)
3. 升级前备份 CRD 配置
4. 测试环境先升级,验证后再生产

### 28.12.2 案例 2:Sidecar 内存爆炸导致节点 OOM

**现象**:某 5000 Pod 集群,Sidecar 内存持续增长,从 100MB 涨到 1GB,导致节点 OOM,Pod 被批量 Evict。

**根因**:
1. Sidecar 默认配置全量(所有 namespace 的 Service/Endpoint)
2. 5000 Pod 的 Endpoints 配置膨胀
3. Envoy 配置缓存未限制,内存泄漏式增长
4. Sidecar 内存 limit 设为 256MB,频繁 OOM

**修复**:
1. 紧急调大 Sidecar memory limit 到 512MB
2. 部署 Sidecar CRD,限制配置范围(只看本 namespace)
3. 重启所有 Pod(清理 Envoy 内存)
4. 长期:切换 Ambient Mesh(资源开销低 10x)

**防范**:
1. Sidecar 内存监控 + 告警(>500MB 告警)
2. 大集群必须用 Sidecar CRD 限制配置范围
3. 压测验证(5000 Pod 容量评估)
4. 定期清理无用的 Service/Endpoint

### 28.12.3 案例 3:Knative 冷启动雪崩

**现象**:某 Knative 服务在早高峰流量突增,冷启动延迟从 1s 飙到 30s,大量请求超时。

**根因**:
1. 服务 minScale=0(完全 scale-to-zero)
2. 早高峰流量突增,Activator 缓冲堆积
3. 同时触发 100+ Pod 创建,节点资源不足
4. 镜像未缓存(500MB JVM 镜像),拉取 5-10s
5. JVM 应用启动 5-10s
6. 总冷启动 30s+,请求超时

**修复**:
1. minScale=3(保持最小副本,避免冷启动)
2. scale-to-zero-pod-retention-period=10m(保留 Pod)
3. 镜像预拉(daemonset)
4. JVM 用 GraalVM native image(启动 200ms)
5. 节点预留资源(避免调度等待)
6. 限流(Activator container-concurrency=10)

**效果**:冷启动从 30s 降到 200ms。

**防范**:
1. 早高峰预热(定时触发请求)
2. 冷启动监控 + 告警
3. 压测验证流量突增场景

### 28.12.4 案例 4:Envoy 配置错误导致 5xx

**现象**:某团队修改 VirtualService 后,10% 请求返回 500。

**根因**:
1. VirtualService 配置了 fault.abort(故障注入)
2. 配置 percentage: 10(httpStatus: 500)
3. 本意是测试,但应用到生产 namespace
4. 10% 请求被注入 500 错误

**修复**:
1. 紧急删除 fault.abort 配置
2. 流量恢复正常

**防范**:
1. 故障注入只在测试 namespace 用
2. istioctl analyze 检查配置
3. GitOps + Code Review(防止误配)
4. 生产 namespace 限制 fault 配置(OPA/Kyverno 策略)

### 28.12.5 案例 5:Argo Workflows 大规模卡死

**现象**:某 ML 平台提交 10000 个并行任务的 Workflow,Argo Controller 卡死,Pod 创建缓慢,Workflow 状态不更新。

**根因**:
1. Workflow 未限制 parallelism(10000 个 Step 全部并行)
2. Controller 单实例,Reconcile 队列堆积
3. 大量 Pod 创建压垮 APIServer(限流)
4. Workflow 状态巨大(数 MB),etcd 写入慢

**修复**:
1. 紧急暂停 Workflow(argo stop)
2. 清理已创建 Pod
3. Workflow 加 parallelism: 50(限制并行)
4. Controller 加资源(CPU 4 核,内存 8GB)
5. 启用 Pod GC(OnPodCompletion)
6. 长期:Controller 分片(3 副本,按 namespace hash)

**防范**:
1. Workflow 必须设 parallelism
2. Controller 资源规划(根据并发量)
3. 大规模用分片(多 Controller 实例)
4. 监控 Controller 队列长度

------

## 28.13 参考与延伸

### 28.13.1 官方文档

- Istio Docs — https://istio.io/latest/docs/
- Istio Best Practices — https://istio.io/latest/docs/ops/best-practices/
- Linkerd Docs — https://linkerd.io/2.14/overview/
- Knative Docs — https://knative.dev/docs/
- Argo Workflows Docs — https://argoproj.github.io/argo-workflows/
- Tekton Docs — https://tekton.dev/docs/
- Envoy Docs — https://www.envoyproxy.io/docs/
- OpenFaaS Docs — https://docs.openfaas.com/
- Apache OpenWhisk — https://openwhisk.apache.org/
- Cilium Service Mesh — https://docs.cilium.io/en/stable/network/servicemesh/
- Consul Connect — https://developer.hashicorp.com/consul/docs/connect

### 28.13.2 源码链接

- Istio — https://github.com/istio/istio
- Envoy — https://github.com/envoyproxy/envoy
- Linkerd2-proxy(Rust)— https://github.com/linkerd/linkerd2-proxy
- Knative Serving — https://github.com/knative/serving
- Knative Eventing — https://github.com/knative/eventing
- Argo Workflows — https://github.com/argoproj/argo-workflows
- Tekton Pipeline — https://github.com/tektoncd/pipeline
- OpenFaaS — https://github.com/openfaas/faas
- ztunnel(Rust)— https://github.com/istio/ztunnel

### 28.13.3 关键博客与演讲

- *Istio Ambient Mesh: The Next Generation of Service Mesh* — Istio Blog 2022
- *Scaling Istio at Alibaba* — KubeCon China 2021
- *ByteDance Service Mesh Practice* — KubeCon China 2022
- *Knative Cold Start Optimization* — Knative Blog 2023
- *Argo Workflows at Scale* — ArgoCon 2023
- *Tekton: The CI/CD Standard* — TektonCon 2022
- *Envoy xDS Performance Tuning* — EnvoyCon 2023

### 28.13.4 书籍

- 《Istio in Action》—— Christian Posta / Rinor Maloku
- 《Service Mesh in Practice》—— 杨舵
- 《Kubernetes Patterns》—— Bilgin Ibryam / Roland Huss(含 Sidecar 模式)
- 《Cloud Native Patterns》—— Cornelia Davis
- 《Building Microservices》—— Sam Newman

### 28.13.5 跨文件链接

- [05-Service与网络](./05-Service与网络.md) — K8s 原生 Service / Endpoint / Ingress / DNS
- [14-kube-proxy与服务转发](./14-kube-proxy与服务转发.md) — iptables / IPVS / eBPF 转发原理
- [15-CNI与网络模型](./15-CNI与网络模型.md) — Calico / Cilium / Flannel 网络模型
- [18-NetworkPolicy与流量管控](./18-NetworkPolicy与流量管控.md) — L3/L4 隔离,与 Istio AuthorizationPolicy 互补
- [25-大厂K8s平台](./25-大厂K8s平台.md) — 阿里 ACK / 字节 TCE / GKE / EKS 平台能力
- [26-大规模集群优化](./26-大规模集群优化.md) — 1万节点集群调优,服务网格 xDS 推送优化
- [27-CRD与Operator生态](./27-CRD与Operator生态.md) — CRD / Operator / Helm / Argo CD
- [11-滚动更新与发布策略](./11-滚动更新与发布策略.md) — 金丝雀 / 蓝绿 / Argo Rollouts(与 Istio 流量切分互补)
- [10-HPA-VPA-CA](./10-HPA-VPA-CA.md) — K8s 原生自动扩缩(Knative KPA 的对比)
- [21-监控与指标](./21-监控与指标.md) — Prometheus + Grafana(服务网格可观测性)
- [22-日志与追踪](./22-日志与追踪.md) — Jaeger / OpenTelemetry(分布式追踪)
- [17-RBAC与认证授权](./17-RBAC与认证授权.md) — K8s RBAC(与 Istio AuthorizationPolicy 互补)
- [19-Pod安全](./19-Pod安全.md) — PSA / seccomp / gVisor(Pod 安全,与 mTLS 互补)
- [20-策略与治理](./20-策略与治理.md) — OPA Gatekeeper / Kyverno(策略治理)

### 28.13.6 相关模块

- [Docker](../Docker/) — 容器基础,Sidecar 镜像构建
- [分布式系统](../分布式系统/) — 一致性、共识、CAP(mTLS 证书分发)
- [云计算安全](../云计算安全/) — 零信任、mTLS、JWT
- [infra开发](../infra开发/) — 网关、服务网格、可观测性

### 28.13.7 TODO 与延伸

- [ ] Cilium Service Mesh(eBPF 原生网格)深度实践
- [ ] 多集群服务网格(跨 region / 跨云)
- [ ] WasmPlugin 自定义扩展开发
- [ ] Knative Eventing 与 Kafka 深度集成
- [ ] Argo Workflows + Volcano 批调度
- [ ] Tekton + Argo CD GitOps 流水线
- [ ] 服务网格性能压测(Envoy benchmark)
- [ ] Ambient Mesh 生产落地经验

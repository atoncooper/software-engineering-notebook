# 20 - 无 Sidecar 服务网格

> 传统 Sidecar 服务网格 (Istio < 1.22, Linkerd) 的代价: 每 Pod 一个代理, 资源开销大, 启动延迟, 版本耦合。无 Sidecar 架构 (Istio Ambient / Cilium Mesh / Linkerd2 多线程) 用节点级代理 + eBPF 把网格开销降到最低。本章梳理无 Sidecar 架构演进、ztunnel + waypoint 模型、性能对比、迁移路径, 以及大厂 LLM 推理集群的网格实践。

---

## 一、思维导图

```
              无 Sidecar 服务网格
                      │
        ┌─────────────┼─────────────┐
        ▼             ▼             ▼
   ┌─────────┐   ┌─────────┐   ┌─────────┐
   │ Istio   │   │ Cilium  │   │ Linkerd │
   │ Ambient │   │ Mesh    │   │ 2       │
   │ ztunnel │   │ eBPF    │   │ Rust    │
   │ waypoint│   │ 无代理  │   │ 轻量    │
   └─────────┘   └─────────┘   └─────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **降低 Sidecar 开销**: CPU/内存/启动延迟
- **解耦网格与应用**: 网格升级不影响应用 Pod
- **保留 L7 能力**: 金丝雀/重试/熔断仍可用
- **简化运维**: 节点级而非 Pod 级

### 2.2 不解决什么

- 不覆盖 eBPF 基础（19 章）
- 不覆盖网络 CNI（k8s 模块）
- 不深入服务网格理论（infra开发 模块）

---

## 三、直觉解释

### 3.1 Sidecar 的代价

```
传统 Sidecar (Istio < 1.22):
  每 Pod 注入 Envoy 容器
  ↓
  资源开销:
    - CPU: ~0.5 核/Pod (Envoy)
    - 内存: ~100MB/Pod
    - 1000 Pod: 500 核 CPU, 100GB 内存
  
  启动延迟:
    - Pod 启动需等 Envoy ready
    - 额外 1-3s
  
  版本耦合:
    - 网格升级需重启所有 Pod
    - 影响 1000+ 服务
  
  交叉版本问题:
    - Envoy v1 与 v2 兼容性
    - 滚动升级期间流量异常

无 Sidecar (Ambient):
  节点级 ztunnel (L4) + waypoint (L7)
  ↓
  资源开销:
    - 节点级, 不按 Pod 计
    - 100 节点: 100 个 ztunnel
    - 总开销降低 5-10x
  
  启动: 无需等 sidecar
  
  升级: ztunnel 滚动重启, 不影响 Pod
```

### 3.2 网格谱系

| 方案 | 部署模型 | L7 能力 | 性能开销 | 复杂度 |
|------|---------|---------|---------|--------|
| Sidecar (Istio <1.22) | Pod 级 | 完整 | 高 (每 Pod) | 中 |
| Istio Ambient | 节点级 ztunnel + L7 waypoint | 完整 | 低 (节点级) | 中 |
| Cilium Mesh | 节点级 eBPF + 可选 Envoy | 中 (HTTP/gRPC) | 极低 | 低 |
| Linkerd2 | Pod 级 (Rust 轻量) | 完整 | 中 (轻量 sidecar) | 低 |
| Consul Connect | Pod 级 / 节点级 | 完整 | 中 | 中 |
| Kuma | Pod 级 / 节点级 | 完整 | 中 | 中 |

### 3.3 Istio Ambient 架构演进

```
传统 Sidecar:
  Pod
  ├── app 容器
  └── istio-proxy (Envoy)
        ↓
  所有流量经 Envoy (L4 + L7)

Ambient:
  节点
  ├── ztunnel (DaemonSet, L4 mTLS + 策略)
  ├── waypoint (按 namespace, 可选 L7)
  └── Pod
        └── app 容器 (无 sidecar)
        ↓
  L4: ztunnel 处理 (透明, HBONE 隧道)
  L7: 命名空间有 waypoint 才走 L7
```

---

## 四、核心概念与架构

### 4.1 Istio Ambient 架构

```
┌─────────────────────────────────────────────┐
│                Node                         │
│  ┌──────────────────────────────────────┐   │
│  │  ztunnel (DaemonSet)                 │   │
│  │  - Rust 实现, 轻量                   │   │
│  │  - L4 mTLS                           │   │
│  │  - HBONE (HTTP-Based Overlay)        │   │
│  │  - 不参与 L7                         │   │
│  └──────────────────────────────────────┘   │
│  ┌──────────────────────────────────────┐   │
│  │  Pod A    Pod B    Pod C             │   │
│  │  (无 sidecar)                        │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│  Namespace "vllm-prod"                      │
│  ┌──────────────────────────────────────┐   │
│  │  waypoint (Deployment)               │   │
│  │  - Envoy, 处理 L7                    │   │
│  │  - 金丝雀/重试/熔断                  │   │
│  │  - 仅命名空间有 L7 策略时部署        │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘

流量路径:
  Pod A → ztunnel (L4 mTLS) → ztunnel (Node B) → Pod B
  或
  Pod A → ztunnel → waypoint (L7) → ztunnel → Pod B (需 L7 策略)
```

### 4.2 ztunnel 详解

```
ztunnel:
  - Rust 实现
  - DaemonSet 部署 (每节点一个)
  - L4 代理: mTLS, 策略
  - HBONE 协议: HTTP/2 隧道封装 L4 流量
  - 不参与 L7 (无 HTTP 路由)

工作流:
  1. Pod A 发流量到 Pod B
  2. 节点 A 的 ztunnel 拦截 (iptables/cgroup)
  3. ztunnel A 与 ztunnel B 建立 HBONE 隧道 (mTLS)
  4. 流量经隧道到节点 B
  5. ztunnel B 解密, 转发到 Pod B
  
资源:
  - 内存: ~30MB/节点
  - CPU: 0.1 核/节点 (低负载)
  - 比 Sidecar 节省 5-10x
```

### 4.3 waypoint 详解

```
waypoint:
  - Envoy 实现
  - 命名空间级部署 (Gateway API)
  - L7 代理: HTTP 路由, 金丝雀, 重试, 熔断
  - 仅需 L7 策略的命名空间部署
  
部署方式:
  istioctl waypoint apply -n vllm-prod
  或 Gateway API:
  
apiVersion: gateway.networking.k8s.io/v1
kind: Gateway
metadata:
  name: waypoint
  namespace: vllm-prod
  labels:
    istio.io/waypoint-for: service
spec:
  gatewayClassName: istio-waypoint
  listeners:
    - name: mesh
      port: 15008
      protocol: HBONE
```

### 4.4 Cilium Mesh 架构

```
┌─────────────────────────────────────────────┐
│                Node                         │
│  ┌──────────────────────────────────────┐   │
│  │  Cilium Agent (DaemonSet)            │   │
│  │  - eBPF 实现 L4/L7                   │   │
│  │  - 无独立代理                         │   │
│  │  - L7: 可选 Envoy DaemonSet          │   │
│  └──────────────────────────────────────┘   │
│  ┌──────────────────────────────────────┐   │
│  │  eBPF Programs (内核态)              │   │
│  │  - socket hook                       │   │
│  │  - TC                                │   │
│  │  - XDP                               │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘

特点:
  - eBPF 内核态处理, 无用户态代理
  - L4: eBPF socket redirect
  - L7: 可选 Envoy (节点级, DaemonSet)
  - 性能: 比 Sidecar 快 2-5x
  - 兼容: 完整 K8s NetworkPolicy + Cilium 策略
```

### 4.5 Linkerd2 (Rust 轻量 Sidecar)

```
Linkerd2 设计:
  - 仍用 Sidecar 模式
  - 但用 Rust 实现 (Linkerd2-proxy)
  - 比 Envoy 轻 5-10x
  
资源:
  - 内存: ~10MB/Pod (vs Envoy 100MB)
  - CPU: 0.05 核/Pod (vs Envoy 0.5)

适用:
  - 不愿引入 eBPF
  - 接受 Sidecar 模式但要求轻量
  - Linkerd2 已有生产验证
```

---

## 五、操作流程与配置

### 5.1 安装 Istio Ambient

```bash
# 1. 安装 Istio 1.22+ (Ambient 模式)
istioctl install --set profile=ambient \
  --set values.global.istioNamespace=istio-system

# 2. 启用 Ambient 模式 (命名空间级)
kubectl label namespace vllm-prod istio.io/dataplane-mode=ambient

# 3. 验证 Pod 无 sidecar
kubectl get pods -n vllm-prod
# NAME      READY   STATUS    (无 istio-proxy)

# 4. 验证 ztunnel
kubectl get pods -n istio-system -l app=ztunnel
# ztunnel-xxx   1/1   Running

# 5. (可选) 部署 L7 waypoint
istioctl waypoint apply -n vllm-prod --enroll-namespace
kubectl get gateway -n vllm-prod
# waypoint   istio-waypoint   35d
```

### 5.2 Ambient L4 策略（mTLS + 授权）

```yaml
# PeerAuthentication: 强制 mTLS
apiVersion: security.istio.io/v1beta1
kind: PeerAuthentication
metadata:
  name: default
  namespace: vllm-prod
spec:
  mtls:
    mode: STRICT
---
# AuthorizationPolicy: L4 授权
apiVersion: security.istio.io/v1beta1
kind: AuthorizationPolicy
metadata:
  name: vllm-policy
  namespace: vllm-prod
spec:
  selector:
    matchLabels:
      app: vllm
  action: ALLOW
  rules:
    - from:
        - source:
            namespaces: ["api-gateway"]
      to:
        - operation:
            ports: ["8000"]
```

### 5.3 Ambient L7 策略（需 waypoint）

```yaml
# 部署 waypoint
apiVersion: gateway.networking.k8s.io/v1beta1
kind: Gateway
metadata:
  name: waypoint
  namespace: vllm-prod
  labels:
    istio.io/waypoint-for: service
spec:
  gatewayClassName: istio-waypoint
  listeners:
    - name: mesh
      port: 15008
      protocol: HBONE
---
# L7 金丝雀策略
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: vllm-canary
  namespace: vllm-prod
spec:
  hosts:
    - vllm
  http:
    - match:
        - headers:
            x-canary:
              exact: "true"
      route:
        - destination:
            host: vllm
            subset: canary
          weight: 100
    - route:
        - destination:
            host: vllm
            subset: stable
          weight: 95
        - destination:
            host: vllm
            subset: canary
          weight: 5
---
# DestinationRule 定义 subset
apiVersion: networking.istio.io/v1beta1
kind: DestinationRule
metadata:
  name: vllm
  namespace: vllm-prod
spec:
  host: vllm
  subsets:
    - name: stable
      labels:
        version: v1
    - name: canary
      labels:
        version: v2
```

### 5.4 Cilium Mesh 安装

```bash
# 1. 安装 Cilium (替代 CNI + Service Mesh)
helm install cilium cilium/cilium --version 1.15.0 \
  --namespace kube-system \
  --set kubeProxyReplacement=true \
  --set ingressController.enabled=true \
  --set gatewayAPI.enabled=true \
  --set gatewayAPI.enableAlpn=true \
  --set l7Proxy.enabled=true \
  --set envoy.enabled=true

# 2. 启用服务网格 (CiliumNetworkPolicy + Gateway API)
kubectl apply -f https://raw.githubusercontent.com/cilium/cilium/v1.15/examples/grpc/manifest.yaml

# 3. 启用 L7 观测
hubble enable
```

```yaml
# Cilium L7 HTTP 策略
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata:
  name: vllm-l7
  namespace: vllm-prod
spec:
  endpointSelector:
    matchLabels:
      app: vllm
  ingress:
    - fromEndpoints:
        - matchLabels:
            app: api-gateway
      toPorts:
        - ports:
            - port: "8000"
              protocol: TCP
          rules:
            http:
              - method: POST
                path: /v1/chat/completions
              - method: GET
                path: /health
```

### 5.5 Linkerd2 安装

```bash
# 1. 安装 Linkerd CLI
curl -sL https://run.linkerd.io/install | sh

# 2. 验证集群
linkerd check --pre

# 3. 安装控制面
linkerd install | kubectl apply -f -

# 4. 注入 sidecar (命名空间级)
kubectl annotate namespace vllm-prod linkerd.io/inject=enabled
kubectl rollout restart deployment -n vllm-prod

# 5. 验证
linkerd check
linkerd viz dashboard
```

---

## 六、底层原理

### 6.1 HBONE 协议

```
HBONE (HTTP-Based Overlay Network):
  - Istio Ambient 的 L4 隧道协议
  - HTTP/2 CONNECT 隧道封装 TCP
  - mTLS 加密
  - 复用 HTTP/2 多路复用

数据包路径:
  Pod A → ztunnel A
        ↓ (HBONE: HTTP/2 CONNECT + mTLS)
        ztunnel B
        ↓ (解密, 解封装)
        Pod B

vs 传统 Sidecar:
  Pod A → Envoy A → IPsec/wireguard → Envoy B → Pod B
  
  HBONE 优势:
    - 复用 HTTP/2 基础设施 (LB, CDN)
    - 兼容现有网络设备
    - 多路复用降低延迟
```

### 6.2 ztunnel 流量拦截

```
ztunnel 流量拦截 (节点级):

入站 (Pod 接收):
  1. 数据包到达节点
  2. iptables/TPROXY 重定向到 ztunnel
  3. ztunnel 解 HBONE (mTLS)
  4. ztunnel 转发到 Pod (本地)

出站 (Pod 发送):
  1. Pod 发出数据包
  2. cgroup BPF 拦截
  3. 重定向到 ztunnel
  4. ztunnel 封装 HBONE
  5. 发往目标节点的 ztunnel

关键: 透明, 应用无感
  - Pod 内应用不知道有 ztunnel
  - 不修改应用代码
  - 不需要重启 Pod
```

### 6.3 Ambient vs Sidecar 性能

```
延迟对比 (P99, 同节点):
  无网格 (基线):        1ms
  Sidecar (Envoy):      3ms (+2ms)
  Ambient (ztunnel):    1.5ms (+0.5ms)
  Ambient (waypoint):   4ms (+3ms, L7 经 waypoint)

资源对比 (1000 Pod, 100 节点):
  Sidecar:
    - CPU: 500 核 (Envoy 0.5/Pod)
    - 内存: 100GB (100MB/Pod)
  
  Ambient (L4 only):
    - CPU: 10 核 (ztunnel 0.1/节点)
    - 内存: 3GB (30MB/节点)
  
  Ambient (L7 waypoint):
    - CPU: 50 核 (waypoint 5/namespace, 10 namespace)
    - 内存: 10GB

  节省: L4 模式 5-10x, L7 模式 2-3x

启动对比:
  Sidecar: Pod ready 需等 istio-proxy ready, 额外 1-3s
  Ambient: 无 sidecar, Pod ready 即可服务
```

### 6.4 Cilium eBPF 数据路径

```
Cilium Mesh 数据路径 (eBPF):

Pod A 发送:
  1. 应用调用 sendmsg
  2. cgroup BPF 拦截
  3. BPF 程序查找目标 Service
  4. 直接 socket redirect 到 Pod B (同节点)
     或封装 VXLAN/Geneve 跨节点

特点:
  - 内核态处理, 无用户态切换
  - 无独立代理进程
  - 延迟: +0.1ms (vs Sidecar +2ms)

L7 模式 (Envoy DaemonSet):
  - 命名空间启用 L7 策略时, 流量经 Envoy
  - Envoy 节点级 (DaemonSet), 而非 Pod 级
  - 资源: 1 Envoy/节点 (vs 1/Pod)
```

---

## 七、代码与配置示例

### 7.1 LLM 推理集群 Ambient 配置

```yaml
# 1. 命名空间启用 Ambient
apiVersion: v1
kind: Namespace
metadata:
  name: vllm-prod
  labels:
    istio.io/dataplane-mode: ambient
---
# 2. mTLS 严格模式
apiVersion: security.istio.io/v1beta1
kind: PeerAuthentication
metadata:
  name: default
  namespace: vllm-prod
spec:
  mtls:
    mode: STRICT
---
# 3. 授权: 仅 API gateway 可访问
apiVersion: security.istio.io/v1beta1
kind: AuthorizationPolicy
metadata:
  name: allow-api-gateway
  namespace: vllm-prod
spec:
  action: ALLOW
  rules:
    - from:
        - source:
            namespaces: ["api-gateway"]
---
# 4. (可选) waypoint 用于 L7 策略
apiVersion: gateway.networking.k8s.io/v1beta1
kind: Gateway
metadata:
  name: waypoint
  namespace: vllm-prod
  labels:
    istio.io/waypoint-for: service
spec:
  gatewayClassName: istio-waypoint
  listeners:
    - name: mesh
      port: 15008
      protocol: HBONE
---
# 5. L7 金丝雀 (需 waypoint)
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: vllm-canary
  namespace: vllm-prod
spec:
  hosts: ["vllm"]
  http:
    - route:
        - destination:
            host: vllm
            subset: stable
          weight: 95
        - destination:
            host: vllm
            subset: canary
          weight: 5
---
apiVersion: networking.istio.io/v1beta1
kind: DestinationRule
metadata:
  name: vllm
  namespace: vllm-prod
spec:
  host: vllm
  subsets:
    - name: stable
      labels: {version: v1}
    - name: canary
      labels: {version: v2}
```

### 7.2 Ambient 模式 + Argo Rollouts 集成

```yaml
# Rollout 配合 Ambient 金丝雀
apiVersion: argoproj.io/v1alpha1
kind: Rollout
metadata:
  name: vllm
  namespace: vllm-prod
spec:
  replicas: 8
  strategy:
    canary:
      canaryService: vllm-canary
      stableService: vllm-stable
      trafficRouting:
        istio:
          virtualService:
            name: vllm-canary
            routes:
              - primary
      steps:
        - setWeight: 5
        - pause: { duration: 5m }
        - setWeight: 25
        - pause: { duration: 10m }
        - setWeight: 100
```

### 7.3 Cilium Mesh 完整配置

```yaml
# cilium-values.yaml
kubeProxyReplacement: true
ingressController:
  enabled: true
  loadBalancerMode: hybrid
gatewayAPI:
  enabled: true
  enableAlpn: true
l7Proxy:
  enabled: true
envoy:
  enabled: true
hubble:
  enabled: true
  relay:
    enabled: true
  ui:
    enabled: true
  metrics:
    enabled:
      - flow
      - http
      - port-distribution
prometheus:
  enabled: true
  serviceMonitor:
    enabled: true
```

### 7.4 监控仪表盘

```yaml
# Ambient 网格监控
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: istio-ambient
  namespace: monitoring
spec:
  selector:
    matchLabels:
      app: ztunnel
  endpoints:
    - port: http-monitoring
      path: /stats/prometheus
---
# Grafana 仪表盘
# 关键指标:
# - istio_requests_total (L7 请求, 仅 waypoint 模式)
# - istio_tcp_connections_opened_total (L4 连接)
# - ztunnel_total_connections (ztunnel 连接)
# - ztunnel_active_connections (活跃连接)
# - envoy_cluster_upstream_rq_xx (Envoy 上游请求)
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：L7 策略不生效

**症状**：配置 VirtualService 金丝雀, 但流量没切。

**根因**：未部署 waypoint, L7 策略需 waypoint 才生效。

**修复**：
```bash
istioctl waypoint apply -n vllm-prod --enroll-namespace
```
确认 namespace 启用 L7。

### 8.2 陷阱 2：从 Sidecar 迁移中断流量

**症状**：从 Sidecar 模式迁 Ambient, 部分流量中断。

**修复**：
- 逐步迁移: 命名空间级 label 切换
- 验证 mTLS 兼容 (Sidecar 与 Ambient 共存)
- 灰度: 非核心服务先迁

### 8.3 陷阱 3：ztunnel 资源不足

**症状**：高流量节点 ztunnel CPU 飙高, P99 延迟上升。

**修复**：
- ztunnel 资源调整 (requests/limits)
- 节点级 HPA (按流量)
- 监控 ztunnel 指标

### 8.4 陷阱 4：Cilium Mesh 与 Istio 共存

**症状**：Cilium Mesh 与 Istio Sidecar 共存, 流量异常。

**修复**：
- 二选一, 不要混用
- Cilium Mesh 兼容 Istio CRD (但功能有限)
- 评估后选合适方案

### 8.5 陷阱 5：waypoint 单点故障

**症状**：waypoint Pod 挂, 命名空间流量中断。

**修复**：
- waypoint 多副本 (HPA)
- PodDisruptionBudget
- 跨可用区调度

### 8.6 调优 Checklist

- [ ] L4 策略用 ztunnel (无需 waypoint)
- [ ] L7 策略才部署 waypoint (按命名空间)
- [ ] waypoint 多副本 + HPA + PDB
- [ ] ztunnel 资源按节点流量调整
- [ ] 监控 ztunnel/waypoint 指标
- [ ] 迁移灰度 (命名空间级)
- [ ] Cilium Mesh 与 Istio 二选一
- [ ] Hubble 流量观测

---

## 九、工业案例与基准数据

### 9.1 案例 1：Google Cloud Anthos Service Mesh

**背景**：Google 推出 Anthos Service Mesh (基于 Istio)。

**方案**：
- Istio Ambient 模式 (1.22+)
- 节点级 ztunnel + 命名空间级 waypoint
- 完整 L7 能力 + 低开销

**效果**：
- 资源开销降低 70%
- 启动延迟消除 (无 sidecar)
- 网格升级零影响

### 9.2 案例 2：阿里云 ASM (Ambient 模式)

**背景**：阿里云服务网格 ASM 支持 Ambient。

**方案**：
- 自研 ztunnel 优化
- 与阿里云 SLB 集成
- 中文文档与技术支持

**效果**：
- 大规模集群 (10000+ Pod) 资源开销下降 80%
- 网格升级时间从小时级到分钟级

### 9.3 案例 3：字节跳动 Cilium Mesh

**背景**：字节跳动内部 LLM 推理集群用 Cilium Mesh。

**方案**：
- Cilium eBPF 替代 kube-proxy + Service Mesh
- 节点级 Envoy DaemonSet (L7)
- Hubble 流量观测

**效果**：
- 网格开销降低 90%
- 延迟 P99 降低 50%
- 故障排查时间下降 (Hubble)

### 9.4 案例 4：Linkerd2 在 Buoyant

**背景**：Buoyant (Linkerd 母公司) 客户案例。

**方案**：
- Linkerd2 (Rust 轻量 sidecar)
- 比 Envoy 节省 5-10x 资源
- 简单易用

**适用**：中小团队, 不愿引入 eBPF。

### 9.5 性能基准

| 方案 | 延迟 P99 (L4) | 延迟 P99 (L7) | 资源/1000 Pod |
|------|--------------|--------------|---------------|
| 无网格 | 1ms | 1ms | 0 |
| Sidecar (Envoy) | 3ms | 4ms | 500 核, 100GB |
| Ambient (L4 only) | 1.5ms | N/A | 10 核, 3GB |
| Ambient (L7 waypoint) | 1.5ms | 4ms | 50 核, 10GB |
| Cilium Mesh (L4) | 1.1ms | N/A | 5 核, 1GB |
| Cilium Mesh (L7) | 1.1ms | 3ms | 30 核, 5GB |
| Linkerd2 | 2ms | 2.5ms | 50 核, 10GB |

---

## 十、与其他方案的关系

### 10.1 Ambient vs Sidecar

| 维度 | Sidecar | Ambient |
|------|---------|---------|
| 部署 | Pod 级 | 节点级 (ztunnel) + 命名空间级 (waypoint) |
| 资源 | 高 (每 Pod) | 低 (节点级) |
| 启动 | 等 sidecar ready | 即时 |
| 升级 | Pod 重启 | ztunnel 滚动 |
| L7 | 完整 | 需 waypoint |
| 复杂度 | 中 | 中 |
| 成熟度 | 高 | 中 (1.22+) |

### 10.2 Ambient vs Cilium Mesh

| 维度 | Ambient | Cilium Mesh |
|------|---------|-------------|
| 数据面 | ztunnel (Rust) + Envoy | eBPF + Envoy (可选) |
| 性能 | 良好 | 极佳 (eBPF 内核态) |
| L7 | waypoint (Envoy) | Envoy DaemonSet |
| 功能 | 完整 (Istio CRD) | 完整 (Cilium CRD + Gateway API) |
| 生态 | Istio 生态 | Cilium + Gateway API |
| 适合 | 已用 Istio | 全新集群, 追求极致性能 |

### 10.3 网格选型决策

```
决策树:
  1. 是否需要服务网格?
     - 否: K8s NetworkPolicy 足够
     - 是: 继续
  
  2. 是否已有 Istio Sidecar?
     - 是: 评估迁 Ambient (资源/启动优势)
     - 否: 继续
  
  3. 是否追求极致性能?
     - 是: Cilium Mesh (eBPF)
     - 否: 继续
  
  4. 团队熟悉度?
     - Istio: Ambient
     - Cilium: Cilium Mesh
     - 都不熟: Linkerd2 (简单)
  
  5. L7 策略需求?
     - 仅 L4: Ambient L4 only (最省)
     - L7 完整: Ambient + waypoint
```

---

## 十一、面试速答

**Q1: Sidecar 服务网格的代价?**

A: 1) 资源开销: 每 Pod 0.5 CPU + 100MB 内存 (Envoy); 2) 启动延迟: Pod 需等 sidecar ready, 额外 1-3s; 3) 版本耦合: 网格升级需重启所有 Pod; 4) 大集群 (1000+ Pod) 资源开销显著。

**Q2: Istio Ambient 怎么解决 Sidecar 问题?**

A: 节点级 ztunnel (Rust, L4 mTLS) + 命名空间级 waypoint (Envoy, L7)。1) 资源: 节点级, 不按 Pod 计, 节省 5-10x; 2) 启动: 无 sidecar, 即时; 3) 升级: ztunnel 滚动重启, 不影响 Pod; 4) L7: 仅需 L7 策略的命名空间部署 waypoint。

**Q3: ztunnel 与 waypoint 区别?**

A: ztunnel: 节点级 DaemonSet, Rust 实现, 仅 L4 (mTLS + 授权), ~30MB/节点。waypoint: 命名空间级 Deployment, Envoy 实现, L7 (HTTP 路由, 金丝雀, 重试), 仅需 L7 策略的命名空间部署。

**Q4: Cilium Mesh 比 Istio Ambient 好在哪?**

A: Cilium 用 eBPF 内核态处理, 无用户态代理 (ztunnel 仍是用户态)。延迟更低 (+0.1ms vs +0.5ms), 资源更省 (5 核 vs 10 核/1000 Pod)。但功能略弱 (L7 依赖 Envoy DaemonSet), 生态不如 Istio 成熟。

**Q5: 怎么从 Sidecar 迁 Ambient?**

A: 1) 安装 Istio 1.22+ (Ambient profile); 2) 命名空间级 label 启用 Ambient (`istio.io/dataplane-mode=ambient`); 3) 逐命名空间迁, 验证 mTLS 兼容; 4) 移除 sidecar 注入 (`istio.io/rev` annotation); 5) Pod 重启后无 sidecar; 6) 灰度: 非核心服务先迁。

---

## 十二、综合面试题

### 题 1（中级）：LLM 推理集群从 Sidecar 迁 Ambient

**答题要点**：

1. **现状分析**:
   - 1000+ Pod, Sidecar 模式
   - 资源开销: 500 核 CPU, 100GB 内存
   - 启动延迟: 部署慢

2. **迁移方案**:
   - 升级 Istio 到 1.22+
   - 安装 Ambient profile
   - 命名空间级灰度迁移

3. **迁移步骤**:
   - 阶段 1: 非核心命名空间 (test/dev) 启用 Ambient
   - 阶段 2: 核心命名空间 (vllm-prod) 灰度
   - 阶段 3: 移除所有 Sidecar
   - 验证: mTLS, 策略, 流量观测

4. **资源收益**:
   - L4 模式: 10 核 CPU, 3GB 内存 (节省 50x)
   - L7 模式: 50 核 CPU, 10GB 内存 (节省 10x)

5. **风险**:
   - mTLS 版本兼容 (Sidecar 与 Ambient 共存)
   - L7 策略需 waypoint, 验证金丝雀
   - 灰度期间监控流量

6. **回滚**:
   - 命名空间级回滚
   - 保留 Sidecar 配置 30 天

### 题 2（高级）：Cilium Mesh vs Istio Ambient 选型

**答题要点**：

1. **需求评估**:
   - 性能: LLM 推理延迟敏感
   - 功能: L7 金丝雀, 重试, 熔断
   - 团队: 熟悉 Istio, 不熟 Cilium
   - 生态: K8s NetworkPolicy + Gateway API

2. **Cilium Mesh 优势**:
   - 性能: eBPF 内核态, +0.1ms 延迟
   - 资源: 极低 (5 核/1000 Pod)
   - 整合: CNI + Service Mesh 一体
   - 可观测: Hubble

3. **Istio Ambient 优势**:
   - 成熟: 生态完整, 文档丰富
   - 兼容: 现有 Istio CRD 平滑迁移
   - 功能: 完整 L7 (waypoint)
   - 支持: 商业化 (Anthos/ASM)

4. **决策**:
   - 全新集群, 追求性能: Cilium Mesh
   - 已用 Istio, 平滑升级: Ambient
   - 团队熟悉度优先: 已有经验

5. **混合方案**:
   - 网络层: Cilium CNI (eBPF 替 kube-proxy)
   - 服务网格: Istio Ambient
   - 兼得两者优势

---

## 十三、故障复盘

### 13.1 案例 1：Ambient 迁移后 mTLS 失败

**背景**：2024 年某公司 Sidecar → Ambient 迁移, 部分流量 mTLS 失败。

**根因**：Sidecar 与 Ambient 共存期间, mTLS 版本不兼容。

**修复**：
- 启用 STRICT mTLS 前, 先 PERMISSIVE 模式
- 验证全 Pod 兼容后切 STRICT
- 监控 mTLS 失败率

**防范**：迁移期间用 PERMISSIVE 模式, 渐进切 STRICT。

### 13.2 案例 2：waypoint 单点故障

**背景**：2025 年某公司 waypoint 单副本, Pod 重启时命名空间流量中断。

**根因**：未配置多副本与 PDB。

**修复**：
```yaml
spec:
  replicas: 3
  template:
    spec:
      topologySpreadConstraints:
        - maxSkew: 1
          topologyKey: topology.kubernetes.io/zone
```
配置 PDB minAvailable: 2。

**防范**：waypoint 必须多副本 + 跨可用区 + PDB。

### 13.3 案例 3：ztunnel 资源不足

**背景**：2024 年某公司高流量节点 ztunnel CPU 100%, 延迟飙升。

**根因**：默认 resource requests 过低。

**修复**：
- ztunnel resources 调整: 2 CPU, 1GB 内存
- 节点级 HPA (按流量)
- 监控 ztunnel 指标

**防范**：ztunnel 资源按节点流量规划, 监控告警。

### 13.4 案例 4：Cilium Mesh 与 Istio 共存冲突

**背景**：2025 年某公司同时装 Cilium Mesh 与 Istio, 流量异常。

**根因**：两者都拦截流量, 冲突。

**修复**：
- 二选一, 移除其中一个
- 或: Cilium 仅作 CNI, 不启用 Mesh
- 或: Istio 用 Ambient, Cilium 仅 CNI

**防范**：网格方案不能混用, 评估后选其一。

### 13.5 案例 5：L7 策略不生效

**背景**：2024 年某公司配置 VirtualService 金丝雀, 流量没切分。

**根因**：未部署 waypoint, Ambient 默认仅 L4。

**修复**：
- 部署 waypoint
- 命名空间标 `istio.io/dataplane-mode=ambient`
- 命名空间 enroll waypoint

**防范**：Ambient L7 策略必须配 waypoint, 文档明确说明。

---

## 十四、参考与延伸

### 14.1 工具与项目

- Istio Ambient — https://istio.io/latest/docs/ambient/
- ztunnel — https://github.com/istio/ztunnel
- Cilium Mesh — https://docs.cilium.io/en/stable/network/servicemesh/
- Linkerd2 — https://linkerd.io/
- Consul Connect — https://developer.hashicorp.com/consul/docs/connect
- Kuma — https://kuma.io/
- Open Service Mesh — https://openservicemesh.io/

### 14.2 文档与博客

- *Istio Ambient Mode* — https://istio.io/latest/blog/2022/introducing-ambient-mesh/
- *Cilium Service Mesh* — https://isovalent.com/blog/cilium-service-mesh/
- *Linkerd2 vs Istio* — https://linkerd.io/compare/

### 14.3 跨模块链接

- [19-eBPF与可编程数据面](./19-eBPF与可编程数据面.md) —— Cilium eBPF 基础
- [14-GitOps与现代发布](./14-GitOps与现代发布.md) —— 网格 CRD 的 GitOps
- [15-渐进式发布策略](./15-渐进式发布策略.md) —— 网格金丝雀
- [22-混沌工程与稳定性验证](./22-混沌工程与稳定性验证.md) —— 网格故障注入

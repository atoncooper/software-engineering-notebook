# 05 - Service 与网络

> Pod 是临时的,IP 会变;Service 是稳定的,流量入口恒定。本章从 Service 四种类型到 EndpointSlice / Ingress / DNS / kube-proxy,把 K8s 网络从入口到出栈讲透。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- Pod IP 不稳定,客户端怎么找到服务? → **Service**
- Service 类型(ClusterIP / NodePort / LoadBalancer / ExternalName)何时选哪个?
- kube-proxy 怎么把流量从 Service IP 转到 Pod IP?
- 跨集群 / 外部服务怎么暴露? → **Ingress / Gateway API**
- DNS 在 K8s 里怎么工作?为什么 `nslookup` 找不到服务?

### 1.2 不解决什么

- 不讲 CNI 细节(见第 15 章)
- 不讲 NetworkPolicy(见第 18 章)
- 不讲 Service Mesh(见第 29 章)

---

## 2. 直觉解释

### 2.1 "公司前台"类比

```
   客户端
      │
      ▼
   ┌──────────┐
   │  Service  │  ← 公司前台(稳定 IP + 名字)
   │ "请找销售部" │
   └────┬─────┘
        │
   ┌────▼────┬────────┬────────┐
   ▼         ▼        ▼        ▼
Pod-1     Pod-2    Pod-3    Pod-4   ← 后端员工
```

- **Service**:前台,稳定入口,客户只认前台
- **EndpointSlice**:前台手里的"销售部员工名单"
- **kube-proxy**:前台到员工的路由器(决定具体找谁)
- **DNS**:电话簿,`sales.example.com → 前台 IP`
- **Ingress**:大楼门卫,根据路径(`/api` → 销售部,`/hr` → 人事)分流

### 2.2 为什么需要 Service

| 问题 | 解决 |
|------|------|
| Pod IP 变(重建、滚动) | Service 提供稳定 VIP |
| 多 Pod 负载均衡 | Service 自动分发 |
| 服务发现(怎么找) | DNS + 名称解析 |
| 跨 namespace / 集群 | Service + DNS 命名规则 |

---

## 3. 核心概念

### 3.1 Service 四种类型

#### 3.1.1 ClusterIP(默认)

**用途**:集群内访问,外部不可见。

```yaml
apiVersion: v1
kind: Service
metadata:
  name: web-app
  namespace: production
spec:
  type: ClusterIP                # 默认
  selector:
    app: web-app
  ports:
  - name: http
    port: 80                    # Service 端口
    targetPort: 8080            # Pod 端口(可用名称)
    protocol: TCP
```

**特性**:
- VIP 在 `serviceSubnet`(默认 10.96.0.0/12)内分配
- 仅集群内可达(`.svc.cluster.local` DNS)
- kube-proxy 在每个 Node 配置 iptables/IPVS 规则,把 VIP DNAT 到 Pod IP
- 默认命中 Pod IP 后由 kube-proxy 做负载均衡

#### 3.1.2 NodePort

**用途**:集群外访问,通过 Node IP:Port 暴露。

```yaml
apiVersion: v1
kind: Service
metadata:
  name: web-app-np
spec:
  type: NodePort
  selector:
    app: web-app
  ports:
  - port: 80
    targetPort: 8080
    nodePort: 30080             # 范围 30000-32767,不写则随机
```

**特性**:
- 在每个 Node 上开 30080 端口
- 任何 Node IP:30080 都能访问
- 流量路径:Client → Node:30080 → kube-proxy → Pod IP
- **生产慎用**:端口管理混乱、暴露面大、无 TLS 卸载

#### 3.1.3 LoadBalancer

**用途**:云厂商 LB 集成,自动创建外部 LB。

```yaml
apiVersion: v1
kind: Service
metadata:
  name: web-app-lb
  annotations:
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-spec: "slb.s2.medium"
spec:
  type: LoadBalancer
  selector:
    app: web-app
  ports:
  - port: 80
    targetPort: 8080
  loadBalancerSourceRanges:     # 白名单
  - 10.0.0.0/8
  - 192.168.0.0/16
```

**特性**:
- 云厂商 CCM 自动创建 LB(阿里 CLB / AWS ELB / GCP LB)
- LB 后端是 NodePort,流量路径:LB → Node:NodePort → Pod
- `loadBalancerSourceRanges` 限源(默认 0.0.0.0/0)
- `externalTrafficPolicy`:
  - Cluster(默认):所有 Node 都能转发,但 SNAT 隐藏源 IP
  - Local:仅本 Node Pod 接收,保留源 IP,但负载不均

#### 3.1.4 ExternalName

**用途**:把外部服务映射为集群内 Service 名。

```yaml
apiVersion: v1
kind: Service
metadata:
  name: external-db
spec:
  type: ExternalName
  externalName: db.example.com
```

**特性**:
- 不分配 VIP,仅 DNS CNAME
- `external-db.production.svc.cluster.local` → CNAME → `db.example.com`
- 适合把外部 DB 逐步迁到 K8s,业务代码不变

### 3.2 Headless Service

```yaml
apiVersion: v1
kind: Service
metadata:
  name: mysql
spec:
  clusterIP: None               # 关键!
  selector:
    app: mysql
  ports:
  - port: 3306
```

**用途**:
- 不分配 VIP,DNS 直接返回所有 Pod IP
- 客户端自己选 Pod IP(用于 StatefulSet 主从选择)
- 配合 StatefulSet:`mysql-0.mysql.default.svc.cluster.local` 直接解析到 mysql-0

**对比**:

| 类型 | DNS 返回 | 用途 |
|------|---------|------|
| ClusterIP | 单个 VIP(自动 LB) | 普通服务 |
| Headless | 所有 Pod IP | StatefulSet、客户端自选 |

### 3.3 Endpoint / EndpointSlice

#### 3.3.1 Endpoint(旧)

```yaml
apiVersion: v1
kind: Endpoints
metadata:
  name: web-app
subsets:
- addresses:
  - ip: 10.244.1.5
  - ip: 10.244.2.7
  ports:
  - port: 8080
```

**问题**:
- 单对象存所有 Pod IP,大服务(1000+ Pod)单对象过大
- 一次更新触发全量推送,Watch 风暴

#### 3.3.2 EndpointSlice(1.21+ 默认)

```yaml
apiVersion: discovery.k8s.io/v1
kind: EndpointSlice
metadata:
  name: web-app-abc12
  labels:
    kubernetes.io/service-name: web-app
addressType: IPv4
endpoints:
- addresses:
  - 10.244.1.5
  conditions:
    ready: true
    serving: true
    terminating: false
  targetRef:
    kind: Pod
    name: web-app-xxx
  nodeName: node-1
  zone: us-east-1a
ports:
- name: http
  port: 8080
```

**优势**:
- 每个 Slice 最多 100 个 Pod(可配),多个 Slice 组成大服务
- 增量更新,降低 Watch 压力
- 包含 topology 信息(zone / nodeName),支持拓扑感知路由

### 3.4 Service 转发底层(iptables 模式)

```
   ┌─────────────────────────────────────┐
   │  PREROUTING (入站) / OUTPUT (本机)   │
   └────────────┬────────────────────────┘
                │
                ▼
   ┌───────────────────────────┐
   │  KUBE-SERVICES            │  ← Service IP 匹配
   └────────────┬──────────────┘
                │
                ▼
   ┌───────────────────────────┐
   │  KUBE-SVC-XXXXXXXX        │  ← Service 链,随机到 SEP
   │  (随机选一个 SEP)         │
   └────────────┬──────────────┘
                │
                ▼
   ┌───────────────────────────┐
   │  KUBE-SEP-XXXXXXXX        │  ← Pod 链,DNAT 到 Pod IP
   │  DNAT to 10.244.1.5:8080  │
   └───────────────────────────┘
```

**规则示例**:
```bash
# KUBE-SERVICES 链:Service IP 匹配
-A KUBE-SERVICES -d 10.96.0.10/32 -p tcp --dport 80 -j KUBE-SVC-XXX

# KUBE-SVC-XXX 链:随机到 SEP(概率分配)
-A KUBE-SVC-XXX -m statistic --mode random --probability 0.333 -j KUBE-SEP-1
-A KUBE-SVC-XXX -m statistic --mode random --probability 0.5   -j KUBE-SEP-2
-A KUBE-SVC-XXX -j KUBE-SEP-3

# KUBE-SEP-XXX 链:DNAT 到 Pod IP
-A KUBE-SEP-1 -p tcp -j DNAT --to-destination 10.244.1.5:8080
```

### 3.5 Service 转发底层(IPVS 模式)

```bash
# IPVS 规则
ipvsadm -Ln
TCP  10.96.0.10:80 rr
  -> 10.244.1.5:8080      Masq    1      0          0
  -> 10.244.2.7:8080      Masq    1      0          0
  -> 10.244.3.9:8080      Masq    1      0          0
```

**对比**:

| 维度 | iptables | IPVS |
|------|---------|------|
| 数据结构 | 线性规则链 | 哈希表 |
| 查找复杂度 | O(n) | O(1) |
| 大集群性能 | 1k Service 退化 | 10k Service 仍快 |
| 调度算法 | 随机 | rr/lc/dh/sed/... |
| 会话保持 | service sessionAffinity | persistent scheduler |

### 3.6 DNS

#### 3.6.1 CoreDNS

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: coredns
  namespace: kube-system
data:
  Corefile: |
    .:53 {
        errors
        health {
           lameduck 5s
        }
        ready
        kubernetes cluster.local in-addr.arpa ip6.arpa {
           pods insecure
           fallthrough in-addr.arpa ip6.arpa
           ttl 30
        }
        prometheus :9153
        forward . /etc/resolv.conf {
           max_concurrent 1000
        }
        cache 30
        loop
        reload
        loadbalance
    }
```

#### 3.6.2 DNS 命名规则

| 名称 | 解析为 |
|------|--------|
| `kubernetes` | kubernetes Service IP(默认 namespace) |
| `kubernetes.default` | 同上 |
| `kubernetes.default.svc` | 同上 |
| `kubernetes.default.svc.cluster.local` | 同上(完整 FQDN) |
| `mysql-0.mysql` | mysql-0 Pod IP(mysql 是 Headless Service) |
| `web-app.production.svc.cluster.local` | web-app Service VIP |

#### 3.6.3 Pod 的 DNS 策略

```yaml
spec:
  dnsPolicy: ClusterFirst        # 默认,集群优先
  dnsConfig:
    nameservers:
    - 8.8.8.8
    searches:
    - ns1.svc.cluster.local
    - my.dns.search.suffix
    options:
    - name: ndots
      value: "5"
```

| 策略 | 含义 |
|------|------|
| ClusterFirst | 集群 DNS 优先,失败转上游 |
| ClusterFirstWithHostNet | hostNetwork 时仍用集群 DNS |
| Default | 用 Node 的 /etc/resolv.conf |
| None | 完全用 dnsConfig 自定义 |

### 3.7 Ingress

#### 3.7.1 用途

- 七层路由(HTTP/HTTPS),基于 host / path
- TLS 卸载
- 一个 LB 暴露多个 Service

#### 3.7.2 Ingress vs LoadBalancer

```
   方式 A:每个服务一个 LB
   Client → LB1 → Service1
   Client → LB2 → Service2
   Client → LB3 → Service3
   成本:3 个 LB

   方式 B:Ingress 共享 LB
   Client → LB → Ingress Controller → Service1/2/3(按 host/path)
   成本:1 个 LB
```

#### 3.7.3 Ingress 示例

```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: web-ingress
  annotations:
    nginx.ingress.kubernetes.io/ssl-redirect: "true"
    nginx.ingress.kubernetes.io/proxy-body-size: "100m"
    nginx.ingress.kubernetes.io/proxy-connect-timeout: "10"
    nginx.ingress.kubernetes.io/proxy-send-timeout: "300"
    nginx.ingress.kubernetes.io/canary: "true"           # 金丝雀
    nginx.ingress.kubernetes.io/canary-weight: "10"
spec:
  ingressClassName: nginx
  tls:
  - hosts:
    - api.example.com
    secretName: api-tls
  rules:
  - host: api.example.com
    http:
      paths:
      - path: /v1
        pathType: Prefix
        backend:
          service:
            name: api-v1
            port:
              number: 80
      - path: /v2
        pathType: Prefix
        backend:
          service:
            name: api-v2
            port:
              number: 80
```

#### 3.7.4 主流 Ingress Controller

| Controller | 特点 | 适合 |
|-----------|------|------|
| **nginx-ingress** | 社区主流,基于 nginx | 通用场景 |
| **Traefik** | Go 写,自动发现 | 简单 / 容器化 |
| **HAProxy Ingress** | HAProxy 内核 | 高性能 |
| **Istio IngressGateway** | Service Mesh 入口 | 已有 Istio |
| **Envoy Gateway** | 基于 Envoy | 新一代,API Gateway |
| **AWS ALB Ingress** | AWS ALB 集成 | AWS 上 |
| **阿里 SLB Ingress** | 阿里 CLB 集成 | 阿里云上 |

### 3.8 Gateway API(1.22+)

下一代 Ingress,更通用、更强大:

```yaml
apiVersion: gateway.networking.k8s.io/v1
kind: Gateway
metadata:
  name: my-gateway
spec:
  gatewayClassName: nginx
  listeners:
  - name: http
    protocol: HTTP
    port: 80
  - name: https
    protocol: HTTPS
    port: 443
    tls:
      certificateRefs:
      - name: api-tls
---
apiVersion: gateway.networking.k8s.io/v1beta1
kind: HTTPRoute
metadata:
  name: api-route
spec:
  parentRefs:
  - name: my-gateway
  hostnames:
  - api.example.com
  rules:
  - matches:
    - path:
        type: PathPrefix
        value: /v1
    backendRefs:
    - name: api-v1
      port: 80
  - matches:
    - path:
        type: PathPrefix
        value: /v2
    backendRefs:
    - name: api-v2
      port: 80
      weight: 10                # 灰度权重
    - name: api-v2-canary
      port: 80
      weight: 90
```

---

## 4. 操作流程与命令

### 4.1 Service 排查命令

```bash
# 查看 Service
kubectl get svc -n production
kubectl describe svc web-app

# 查看 Endpoints
kubectl get endpoints web-app
kubectl get endpointslice -l kubernetes.io/service-name=web-app

# 在 Pod 内测试 DNS
kubectl exec -it debug-pod -- nslookup web-app
kubectl exec -it debug-pod -- nslookup web-app.production
kubectl exec -it debug-pod -- nslookup web-app.production.svc.cluster.local

# 测试 Service 连通
kubectl exec -it debug-pod -- curl -v http://web-app:80
kubectl exec -it debug-pod -- curl -v http://10.96.0.10:80

# 查看 kube-proxy 模式
kubectl -n kube-system get cm kube-proxy -o yaml | grep mode

# 查看 iptables 规则
sudo iptables -t nat -L KUBE-SERVICES -n | grep web-app
sudo iptables -t nat -L KUBE-SVC-XXX -n

# 查看 IPVS 规则
sudo ipvsadm -Ln | grep -A 5 10.96.0.10
```

### 4.2 创建带 LB 的 Service

```bash
# 创建
kubectl apply -f lb-service.yaml

# 等 LB 分配外部 IP
kubectl get svc web-app-lb -w
# EXTERNAL-IP 从 <pending> 变为实际 IP

# 测试
curl http://<EXTERNAL-IP>:80

# 阿里云查看 CLB
aliyun slb DescribeLoadBalancers --LoadBalancerName k8s-default-web-app
```

### 4.3 调试 DNS

```bash
# 启动调试 Pod
kubectl run dnsutils --image=registry.k8s.io/e2e-test-images/jessie-dnsutils:1.3 -it --rm -- bash

# 查询 Service
nslookup kubernetes
nslookup web-app.production
nslookup web-app.production.svc.cluster.local

# 查询 Headless Service(返回所有 Pod IP)
nslookup mysql.production

# 查询 StatefulSet Pod(返回单个 Pod IP)
nslookup mysql-0.mysql.production

# 查询外部域名
nslookup www.google.com

# 查 CoreDNS Pod
kubectl get pods -n kube-system -l k8s-app=kube-dns -o wide

# 看 CoreDNS 日志
kubectl logs -n kube-system -l k8s-app=kube-dns --tail=50

# 看 CoreDNS ConfigMap
kubectl get cm coredns -n kube-system -o yaml
```

---

## 5. 底层原理

### 5.1 Service 转发的完整链路

#### 5.1.1 集群内 Pod 访问 Service

```
   Pod-A (10.244.1.5)
      │
      │ curl http://10.96.0.10:80
      ▼
   Container 网络命名空间
      │
      ▼
   veth pair
      │
      ▼
   Node-1 主机网络命名空间
      │
      ▼
   PREROUTING 链(iptables)
      │
      ▼
   KUBE-SERVICES 链:匹配 10.96.0.10:80
      │
      ▼
   KUBE-SVC-XXX 链:随机选 KUBE-SEP-XXX
      │
      ▼
   KUBE-SEP-XXX 链:DNAT to 10.244.2.7:8080
      │
      ▼
   路由判断:10.244.2.7 在 Node-2
      │
      ▼
   隧道(VXLAN / IPIP)/ 路由(BGP)
      │
      ▼
   Node-2 主机网络命名空间
      │
      ▼
   veth pair
      │
      ▼
   Pod-B (10.244.2.7) containerPort 8080
```

#### 5.1.2 集群外客户端访问 NodePort

```
   Client (外部)
      │
      │ http://Node-1:30080
      ▼
   Node-1 主机网络命名空间
      │
      ▼
   PREROUTING 链(iptables)
      │
      ▼
   KUBE-NODEPORTS 链:匹配 30080
      │
      ▼
   KUBE-SVC-XXX 链:随机选 KUBE-SEP-XXX
      │
      ▼
   KUBE-SEP-XXX 链:DNAT to Pod IP
      │
      ▼
   (Cluster 模式)SNAT:把源 IP 改成 Node IP(避免回包走丢)
   (Local 模式)无 SNAT:保留源 IP,但仅本 Node Pod 接收
      │
      ▼
   Pod 接收请求
```

### 5.2 EndpointSlice Controller 工作流程

```
   1. EndpointSlice Controller Watch Pod / Service 变化
   2. Pod 状态变化(Ready / NotReady / Terminating)
   3. 更新 EndpointSlice:
      - ready=true:可接收流量
      - serving=true:仍在响应(终止中也可能 serving)
      - terminating=true:正在终止
   4. kube-proxy Watch EndpointSlice
   5. 更新本机 iptables/IPVS 规则
   6. 新流量不再到 terminating Pod
```

**Endpoint Conditions 详解**:
- `ready`:Pod 通过 readiness probe
- `serving`:Pod 仍响应(终止中可能仍 serving,用于优雅关)
- `terminating`:正在终止

### 5.3 Service 的 sessionAffinity

```yaml
spec:
  sessionAffinity: ClientIP
  sessionAffinityConfig:
    clientIP:
      timeoutSeconds: 10800      # 默认 3 小时
```

**实现**:
- iptables 模式:用 `-m recent` 模块记录客户端 IP
- IPVS 模式:用 persistent scheduler(`-p` 参数)
- 适合:有状态会话(WebSocket / 数据库连接)

### 5.4 externalTrafficPolicy

#### 5.4.1 Cluster(默认)

```
   Client → Node-1:30080
            │
            ▼ (DNAT 到任意 Node 的 Pod)
            Pod on Node-2
            │
            ▼ (回包)
            Node-2 → Node-1 → Client
```

- 优势:负载均衡,任意 Pod 可接收
- 劣势:SNAT 隐藏源 IP,跨 Node 跳转

#### 5.4.2 Local

```
   Client → Node-1:30080
            │
            ▼ (DNAT 仅到本 Node Pod)
            Pod on Node-1
            │
            ▼ (回包)
            Node-1 → Client
```

- 优势:保留源 IP,无跨 Node 跳转
- 劣势:本 Node 无 Pod 时连接失败,负载不均

### 5.5 Ingress Controller 工作流程

```
   1. Ingress Controller (Pod) Watch Ingress 资源
   2. 解析 Ingress 规则(host / path → Service)
   3. 通过 EndpointSlice 拿到 Pod IP
   4. 生成 nginx/Envoy 配置
   5. reload nginx / hot reload Envoy
   6. 客户端 → LB → Ingress Controller Pod → Service → Pod
```

---

## 6. 配置示例

### 6.1 生产级 Service(完整模板)

```yaml
apiVersion: v1
kind: Service
metadata:
  name: web-app
  namespace: production
  labels:
    app: web-app
    tier: frontend
  annotations:
    # 阿里云 CLB 规格
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-spec: "slb.s2.medium"
    # 健康检查
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-health-check-type: "tcp"
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-health-check-connect-timeout: "5"
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-healthy-threshold: "2"
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-unhealthy-threshold: "2"
    # 计费方式
    service.beta.kubernetes.io/alibaba-cloud-loadbalancer-charge-type: "pay_by_traffic"
spec:
  type: LoadBalancer
  externalTrafficPolicy: Local
  selector:
    app: web-app
  ports:
  - name: http
    port: 80
    targetPort: 8080
    protocol: TCP
  - name: https
    port: 443
    targetPort: 8443
    protocol: TCP
  loadBalancerSourceRanges:
  - 10.0.0.0/8       # 内网
  - 203.0.113.0/24   # 办公网
```

### 6.2 完整 Ingress 生产模板

```yaml
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: web-ingress
  namespace: production
  annotations:
    # TLS 重定向
    nginx.ingress.kubernetes.io/ssl-redirect: "true"
    nginx.ingress.kubernetes.io/force-ssl-redirect: "true"
    
    # 上传大小
    nginx.ingress.kubernetes.io/proxy-body-size: "100m"
    
    # 超时
    nginx.ingress.kubernetes.io/proxy-connect-timeout: "10"
    nginx.ingress.kubernetes.io/proxy-send-timeout: "300"
    nginx.ingress.kubernetes.io/proxy-read-timeout: "300"
    
    # 限流
    nginx.ingress.kubernetes.io/limit-connections: "100"
    nginx.ingress.kubernetes.io/limit-rps: "50"
    nginx.ingress.kubernetes.io/limit-burst: "100"
    
    # CORS
    nginx.ingress.kubernetes.io/enable-cors: "true"
    nginx.ingress.kubernetes.io/cors-allow-origin: "https://example.com"
    nginx.ingress.kubernetes.io/cors-allow-methods: "GET, POST, PUT, DELETE, OPTIONS"
    nginx.ingress.kubernetes.io/cors-allow-credentials: "true"
    
    # WebSocket
    nginx.ingress.kubernetes.io/upstream-hash-by: "$request_uri"
    
    # 优雅关闭
    nginx.ingress.kubernetes.io/custom-http-errors: "502,503,504"
    nginx.ingress.kubernetes.io/default-backend: error-pages
    
    # 客户端 IP 透传
    nginx.ingress.kubernetes.io/use-forwarded-headers: "true"
    nginx.ingress.kubernetes.io/forwarded-for-header: "X-Forwarded-For"
spec:
  ingressClassName: nginx
  tls:
  - hosts:
    - api.example.com
    - www.example.com
    secretName: example-tls
  rules:
  - host: api.example.com
    http:
      paths:
      - path: /v1
        pathType: Prefix
        backend:
          service:
            name: api-v1
            port:
              number: 80
      - path: /v2
        pathType: Prefix
        backend:
          service:
            name: api-v2
            port:
              number: 80
  - host: www.example.com
    http:
      paths:
      - path: /
        pathType: Prefix
        backend:
          service:
            name: web-frontend
            port:
              number: 80
```

### 6.3 金丝雀发布 Ingress

```yaml
# 主 Ingress
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: web-app
  annotations:
    nginx.ingress.kubernetes.io/canary: "false"
spec:
  rules:
  - host: api.example.com
    http:
      paths:
      - path: /
        pathType: Prefix
        backend:
          service:
            name: web-app-stable
            port:
              number: 80
---
# 金丝雀 Ingress
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: web-app-canary
  annotations:
    nginx.ingress.kubernetes.io/canary: "true"
    nginx.ingress.kubernetes.io/canary-weight: "10"        # 10% 流量
    # nginx.ingress.kubernetes.io/canary-by-header: "X-Canary"
    # nginx.ingress.kubernetes.io/canary-by-header-value: "true"
    # nginx.ingress.kubernetes.io/canary-by-cookie: "canary"
spec:
  rules:
  - host: api.example.com
    http:
      paths:
      - path: /
        pathType: Prefix
        backend:
          service:
            name: web-app-canary
            port:
              number: 80
```

---

## 7. 常见陷阱与调优 ⚠️

### 7.1 Service 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Service 无 Endpoints** | 连接 refused | selector 不匹配 Pod | 检查 selector + label |
| **DNS 解析失败** | nslookup 失败 | CoreDNS Pod 不健康 | 重启 CoreDNS |
| **NodePort 不通** | 外部访问失败 | 防火墙 / 安全组 | 检查 Node 防火墙 + 云安全组 |
| **LB 一直 pending** | EXTERNAL-IP 不分配 | CCM 未安装 / 配额满 | 检查 CCM + 云配额 |
| **sessionAffinity 失效** | 同客户端被分到不同 Pod | IPVS 模式未配 persistent | 检查 ipvsadm -L |

### 7.2 Ingress 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Ingress 不生效** | 直接到默认后端 | IngressClass 未配 | `kubectl get ingressclass` |
| **TLS 证书不识别** | 浏览器警告 | secret 名字错 / 域名不匹配 | 检查 tls.secretName + 域名 |
| **配置 reload 慢** | Ingress 改后 10s+ 生效 | nginx reload 慢 | 用 Lua 动态配置,免 reload |
| **大流量 502** | Ingress 报 502 | upstream Pod 不健康 | 检查 readiness + 优雅终止 |
| **路径重写丢失** | /api/ → / 但后端拿到 /api/ | 未配 rewrite | `nginx.ingress.kubernetes.io/rewrite-target: /` |

### 7.3 DNS 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **DNS 解析慢** | 第一次解析 5s+ | ndots=5 导致多次查询 | 短名用 FQDN,或调整 ndots |
| **CoreDNS 卡死** | 所有 Pod DNS 失败 | CoreDNS 资源不足 | 调高 replicas + resources |
| **Pod DNS 失败但 Node 正常** | 仅 Pod 内解析失败 | dnsPolicy 错 | 改 ClusterFirst |
| **Headless 解析延迟** | Pod 启动后 30s 才解析 | CoreDNS 缓存 + TTL | 等 cache 失效或重启 CoreDNS |

### 7.4 kube-proxy 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **iptables 同步慢** | 新 Pod 30s+ 才接收流量 | iptables 模式 + 大集群 | 切 IPVS |
| **conntrack 残留** | Pod 删后流量仍到 | conntrack 缓存 5min | 缩短 conntrack timeout |
| **NodeLocal DNSCache 缺失** | DNS 延迟高 | CoreDNS 跨节点 | 启用 NodeLocal DNSCache |

---

## 8. 工业案例与基准数据

### 8.1 阿里:超大规模 Service 网络

**场景**:单集群 5000 Service + 30k Pod。

**架构**:
- kube-proxy IPVS 模式(必选)
- CNI:Terway(基于 ENI,无 overlay)
- Service 数:5000+,EndpointSlice 100k+
- DNS:CoreDNS 5 副本 + NodeLocal DNSCache

**性能数据**:
- Service 创建到生效:< 1s(IPVS)
- Pod 启动到流量接入:< 3s
- DNS 查询 P99:< 5ms(NodeLocal DNSCache)
- conntrack 表大小:1M

### 8.2 字节:eBPF 替代 kube-proxy

**场景**:8k 节点集群,kube-proxy 性能瓶颈。

**方案**:自研基于 eBPF 的 kube-proxy 替代。

**性能提升**:
- 转发延迟:200μs → 30μs(降 85%)
- 规则同步:5s → 500ms
- 内存:100MB → 80MB
- conntrack 表:1M(无需)

**实现**:
- XDP 程序在网卡层处理 Service 流量
- 直接 DNAT,绕过 iptables/IPVS
- 替代 conntrack:用 BPF map 维护连接状态

### 8.3 Netflix:Ingress 高可用

**场景**:全球流量入口,单 Ingress 控制器 100k QPS。

**架构**:
- nginx-ingress + NLB(AWS)
- 多副本(50+),跨 AZ
- 优雅关闭:preStop 等 30s,连接排空

**优化**:
- `worker-processes=auto`,`worker-connections=65535`
- 启用 keepalive(到 upstream)
- 用 Lua 动态配置(免 reload)
- 监控:QPS / 延迟 / 502 率

### 8.4 AWS EKS:ALB Ingress

**场景**:AWS 上 Ingress,深度集成 ALB。

**特点**:
- Ingress → ALB 自动创建
- ALB → Target Group → Pod IP(直接到 Pod,不经 NodePort)
- 支持 path-based routing / host-based routing
- IRSA 集成 IAM 权限

**陷阱**:
- ALB 创建慢(2-5min)
- ALB Ingress Controller 重启会重新 reconcile
- 大量 Ingress 时 controller 性能下降

---

## 9. 与其他方案的关系

### 9.1 Service vs Service Mesh

| 维度 | Service | Service Mesh |
|------|---------|-------------|
| 层级 | L4 | L7 |
| 流量管理 | 简单 LB | 精细路由(权重 / 重试 / 熔断) |
| 可观测性 | 弱 | 强(mTLS + 追踪) |
| 复杂度 | 低 | 高(需要 control plane) |
| 例子 | K8s Service | Istio / Linkerd |

### 9.2 Ingress vs Gateway API

| 维度 | Ingress | Gateway API |
|------|---------|-------------|
| API 版本 | v1(稳定) | v1(1.25+) |
| 角色 | 单一资源 | Gateway + Route 分离 |
| 多协议 | HTTP/HTTPS 为主 | HTTP / TCP / UDP / TLS |
| 跨命名空间 | 不支持 | 支持(ReferenceGrant) |
| 厂商扩展 | annotations | CRD 原生 |

### 9.3 K8s Service vs Docker Compose networks

| 维度 | K8s Service | Compose network |
|------|------------|----------------|
| 范围 | 集群 | 单机 |
| 服务发现 | DNS | DNS / links |
| 负载均衡 | kube-proxy | 内置 |
| 类型 | 4 种 | bridge / overlay |

---

## 10. 面试速答 ⭐

| 问题 | 一句话答案 |
|------|----------|
| Service 的 4 种类型? | ClusterIP(集群内)/ NodePort(节点端口)/ LoadBalancer(云 LB)/ ExternalName(DNS CNAME) |
| Headless Service 干什么? | clusterIP=None,DNS 返回所有 Pod IP,客户端自选(用于 StatefulSet) |
| Endpoint vs EndpointSlice? | EndpointSlice 增量更新 + 拓扑信息,大服务用 Slice 避免单对象过大 |
| kube-proxy 三种模式? | iptables(默认)/ IPVS(大集群优)/ eBPF(Cilium,最快) |
| Service 与 Ingress 区别? | Service 是 L4,Ingress 是 L7(host/path 路由 + TLS 卸载) |
| externalTrafficPolicy Local vs Cluster? | Local 保留源 IP 但仅本 Node Pod,Cluster 全集群但 SNAT |
| DNS 解析 web-app 在 Pod 内会查几次? | 取决于 ndots(默认 5),短名会尝试多次拼接 |
| Service sessionAffinity 怎么实现? | iptables 用 recent 模块,IPVS 用 persistent scheduler |
| Ingress Controller 干什么? | Watch Ingress 资源,生成 nginx/Envoy 配置,处理 L7 流量 |
| Gateway API vs Ingress? | Gateway API 是下一代,角色分离 + 多协议 + 跨命名空间 |

---

## 11. 综合面试题

### 11.1 基础题

**Q1**: Service 的 4 种类型分别用于什么场景?

**答题要点**:
- ClusterIP:集群内访问(默认,微服务间调用)
- NodePort:集群外访问(开发 / 小规模,端口范围 30000-32767)
- LoadBalancer:云厂商 LB 集成(生产入口)
- ExternalName:外部服务映射为集群 Service(迁移过渡)

**Q2**: 解释 Headless Service 与 ClusterIP Service 的区别。

**答题要点**:
- ClusterIP:分配 VIP,DNS 返回 VIP,kube-proxy 做负载均衡
- Headless(clusterIP=None):不分配 VIP,DNS 返回所有 Pod IP A 记录,客户端自选
- Headless 用途:StatefulSet(直接访问某 Pod)/ 客户端自实现 LB

### 11.2 进阶题

**Q3**: 描述一个 Pod 访问 Service 的完整流量路径。

**答题要点**:
1. Pod-A 发请求到 Service IP(10.96.0.10:80)
2. Pod 网络命名空间 → veth pair → Node 主机网络
3. PREROUTING 链 → KUBE-SERVICES 链 → KUBE-SVC-XXX 链(随机选 SEP)→ KUBE-SEP-XXX(DNAT 到 Pod IP)
4. 路由判断:Pod IP 在哪
5. 跨 Node:CNI 隧道(VXLAN)或 BGP 路由
6. 目标 Node → veth pair → Pod-B
7. 回包逆向

**Q4**: 为什么大集群必须切 IPVS?

**答题要点**:
- iptables:规则链线性,O(n) 查找;1k Service × 10 Pod = 30k 规则,每个包遍历查找
- IPVS:基于 hash,O(1) 查找;10k Service 仍快
- iptables 同步:5-10s(全量重建),IPVS:1-2s(增量)
- iptables 内存:每个规则 ~50 字节,30k 规则 = 1.5MB;IPVS 用 hash 表,小很多
- 阈值:Service > 1000 或 Pod > 5000 → 必切 IPVS

**Q5**: 一个 Service 没有Endpoints,怎么排查?

**答题要点**:
1. `kubectl get endpoints <svc>` 看 Endpoints
2. `kubectl describe svc <svc>` 看 selector
3. 检查 Pod 是否有对应 label:`kubectl get pods -l <selector>`
4. 检查 Pod 是否 Ready:NotReady Pod 不进 Endpoints
5. 检查 targetPort 是否正确(端口名 / 数字)
6. 检查 Pod readiness probe 是否过严

### 11.3 高级题

**Q6**: 设计一个高可用 Ingress 方案,支撑 100k QPS。

**答题要点**:
- 多副本 Ingress Controller(50+ Pod),跨 AZ
- 前置 LB(NLB / CLB),L4 模式
- 优雅关闭:preStop 30s + graceful shutdown
- 性能调优:
  - worker_processes auto
  - worker_connections 65535
  - keepalive to upstream
  - 用 Lua / Envoy 免 reload
- 监控:QPS / 延迟 / 502 率 / 连接数
- 灰度:多 Ingress + canary annotation
- TLS:OCSP stapling + session resumption
- 安全:WAF + 限流 + IP 黑名单

**Q7**: Pod 内 DNS 解析慢,怎么优化?

**答题要点**:
1. 启用 NodeLocal DNSCache:
   - DaemonSet 在每 Node 跑 DNSCache
   - Pod DNS 请求先到本地 cache
   - 命中:直接返回,< 1ms
   - 未命中:转 CoreDNS
2. 调整 ndots:
   - 默认 5,短名会多次拼接 suffix 查询
   - 用 FQDN(末尾 .)避免拼接
3. CoreDNS 优化:
   - 多副本(≥ 3)
   - cache 30s+
   - prometheus 插件监控
4. 应用层:
   - DNS 缓存(JVM / Go / Python)
   - 减少 DNS 查询(用 IP)

### 11.4 设计题

**Q8**: 设计一个 K8s 集群的入口流量方案,要求支持灰度 / A/B / 蓝绿发布。

**答题要点**:
- 多层入口:
  ```
  Global LB(Route53 / GTM)
       ↓
  Region LB(NLB / CLB)
       ↓
  Ingress Controller(nginx / Envoy)
       ↓
  Service
       ↓
  Pod
  ```
- 灰度:Ingress canary annotation(权重 / header / cookie)
- A/B:多 Ingress + 不同 host / path
- 蓝绿:两个 Deployment,Service 切换 selector
- 工具:Argo Rollouts + Ingress 联动
- 监控:Prometheus + Grafana,异常自动 rollback

---

## 12. 故障复盘

### 12.1 案例 1:CoreDNS 卡死导致全集群 DNS 失败

**业务影响**:2023 年某公司,CoreDNS 单副本 OOM,所有 Pod DNS 解析失败 5 分钟。

**根因**:
- CoreDNS 单副本,资源限制 100Mi
- 大量 Pod 启动 → DNS 查询峰值 → 内存超限 → OOM
- CoreDNS Pod 重启,但已 5min 服务降级

**修复过程**:
1. 紧急:重启 CoreDNS
2. 调高 replicas=5,resources.limits.memory=512Mi
3. 启用 NodeLocal DNSCache(每 Node 一个)
4. 加监控:CoreDNS 查询 QPS / 延迟 / 错误率

**防范**:
- CoreDNS ≥ 3 副本
- 启用 NodeLocal DNSCache
- 监控指标:coredns_dns_request_duration_seconds / coredns_dns_request_count_total

### 12.2 案例 2:LB 健康检查导致 Pod 频繁重启

**业务影响**:2022 年某公司,LB type=LoadBalancer,健康检查过严,Pod 频繁被摘流,业务 5xx 上升。

**根因**:
- LB 健康检查:HTTP /health,2s 间隔,2 次失败摘流
- Pod 启动时 readiness 未通过,但 LB 仍发请求
- 应用启动慢(30s),被 LB 摘流后 Pod 仍 Running,但流量打到其他 Pod,负载不均

**修复过程**:
1. 调整 LB 健康检查:间隔 5s,失败阈值 5
2. Pod 配 startup probe(避免慢启动被误杀)
3. Service externalTrafficPolicy=Local(避免跨 Node 摘流)
4. 应用启动加速(预热 / 缓存)

### 12.3 案例 3:Ingress 配置 reload 失败导致全站 502

**业务影响**:2024 年某公司,Ingress 频繁变更,nginx reload 失败,所有请求 502 持续 1 分钟。

**根因**:
- nginx reload 是 graceful,但短时间内大量 reload 会卡死
- 配置语法错(某个 Ingress 用了不存在的 Service)
- nginx 配置测试未通过,但 controller 仍 reload

**修复过程**:
1. 紧急:回滚最近 Ingress 变更
2. 启用 `nginx -t` 测试,失败不 reload
3. 切到 OpenResty / Envoy(支持热加载)
4. 加监控:Ingress controller reload 次数 + 失败率

**防范**:
- Ingress 变更走 GitOps(预校验)
- 控制变更频率(批量而非单个)
- 监控 reload 失败次数,异常告警

---

## 13. 参考与延伸

### 13.1 官方文档

- [Service](https://kubernetes.io/docs/concepts/services-networking/service/)
- [Service & Endpoints](https://kubernetes.io/docs/concepts/services-networking/endpoint-slices/)
- [Ingress](https://kubernetes.io/docs/concepts/services-networking/ingress/)
- [Gateway API](https://gateway-api.sigs.k8s.io/)
- [DNS for Services and Pods](https://kubernetes.io/docs/concepts/services-networking/dns-pod-service/)
- [kube-proxy](https://kubernetes.io/docs/reference/command-line-tools-reference/kube-proxy/)

### 13.2 经典文章与论文

- [Kubernetes IPVS](https://kubernetes.io/blog/2018/07/09/ipvs-long-running-services-load-balancing/)
- [NodeLocal DNSCache](https://kubernetes.io/docs/tasks/administer-cluster/nodelocaldns/)
- [Topological Aware Routing](https://kubernetes.io/docs/concepts/services-networking/topology-aware-routing/)

### 13.3 跨文件链接

- 上一章: [04 - Pod 与工作负载](./04-Pod与工作负载.md)
- 下一章: [06 - 存储](./存储.md)
- 详见: [14 - kube-proxy 与服务转发](./14-kube-proxy与服务转发.md) / [15 - CNI 与网络模型](./15-CNI与网络模型.md) / [18 - NetworkPolicy 与流量管控](./18-NetworkPolicy与流量管控.md) / [29 - 服务网格与 Serverless](./29-服务网格与Serverless.md)
- 参考平行模块: [分布式系统/05 - 复制](../分布式系统/05-复制.md)

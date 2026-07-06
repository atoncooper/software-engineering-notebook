# 18. NetworkPolicy 与流量管控

> 关键词:NetworkPolicy、零信任、Namespace 隔离、Ingress、Egress、CiliumNetworkPolicy、Service Mesh

------

## 18.1 问题定义

K8s 默认网络模型是 **"扁平、互通"** 的:

- 任何 Pod 可以访问任何 Pod(无限制)
- 任何 Pod 可以访问任何 Service
- 跨命名空间流量默认放行

这种模型在 **单一团队** 没问题,但在 **多租户、生产/测试隔离、合规要求** 场景下完全不安全。

**核心问题**:

> 如何在 Pod 级别实现 **"默认拒绝,显式允许"** 的网络访问控制,实现零信任网络?

------

## 18.2 直觉解释

把 NetworkPolicy 想象成 **办公楼的门禁系统**:

| 办公楼门禁 | NetworkPolicy |
|-----------|---------------|
| 默认所有门禁关闭 | 默认 deny-all |
| 员工工牌刷开特定门 | 命中规则放行 |
| 不同部门不同权限 | namespaceSelector |
| 个人访客权限 | podSelector |
| 上班时间限制 | 端口/协议限制 |
| 出门也要刷卡 | egress 策略 |
| 大楼总门 vs 部门门 | ClusterNetworkPolicy vs NetworkPolicy |

关键点:NetworkPolicy 是 **白名单模型**,声明策略后 **被选中的 Pod** 流量按规则匹配,未匹配的全部拒绝。

------

## 18.3 核心概念

### 18.3.1 NetworkPolicy 资源模型

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: my-policy
  namespace: production
spec:
  podSelector:          # 选中要控制的 Pod
    matchLabels:
      app: backend
  policyTypes:          # 策略方向
  - Ingress
  - Egress
  ingress:              # 入站规则(白名单)
  - from:
    - podSelector:      # 允许哪些 Pod
        matchLabels:
          app: frontend
    ports:
    - protocol: TCP
      port: 8080
  egress:               # 出站规则(白名单)
  - to:
    - podSelector:
        matchLabels:
          app: db
    ports:
    - protocol: TCP
      port: 5432
```

### 18.3.2 三大要素

```
1. podSelector: 策略作用对象
   - {} 表示命名空间内所有 Pod
   - matchLabels 精确匹配
   
2. policyTypes: 策略方向
   - Ingress: 控制入站
   - Egress: 控制出站
   - 默认: 有 ingress 字段则 Ingress,有 egress 字段则 Egress
   
3. ingress / egress: 规则列表
   - from/to: 来源/目的地
   - ports: 端口限制
   - 多个规则是 OR 关系
   - 规则内多个 from/to 也是 OR
   - ports 与 from/to 是 AND
```

### 18.3.3 隔离范围

```
Pod 被 NetworkPolicy 选中后:
  - Ingress: 默认全部拒绝,仅允许 ingress 规则
  - Egress: 默认全部拒绝,仅允许 egress 规则
  - 未选中: 默认全部允许

注意:NetworkPolicy 是 Pod 级别,不是 namespace 级别
     但通过 namespaceSelector 可实现 namespace 级别隔离
```

### 18.3.4 from/to 三种选择器

```yaml
ingress:
- from:
  # 1. 同命名空间 Pod(简化写法)
  - podSelector:
      matchLabels:
        app: frontend

  # 2. 跨命名空间 Pod(需同时指定 namespace + pod)
  - namespaceSelector:
      matchLabels:
        name: staging
    podSelector:
      matchLabels:
        app: frontend

  # 3. IP 段
  - ipBlock:
      cidr: 10.0.0.0/8
      except:
      - 10.0.0.0/24   # 排除内部

  # 4. 命名空间所有 Pod
  - namespaceSelector:
      matchLabels:
        name: monitoring
```

### 18.3.5 默认策略模板

```yaml
# 1. 默认拒绝所有入站(命名空间级)
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: default-deny-ingress
  namespace: production
spec:
  podSelector: {}    # 命名空间所有 Pod
  policyTypes: [Ingress]
---
# 2. 默认拒绝所有出站
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: default-deny-egress
  namespace: production
spec:
  podSelector: {}
  policyTypes: [Egress]
---
# 3. 默认拒绝所有
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: default-deny-all
  namespace: production
spec:
  podSelector: {}
  policyTypes: [Ingress, Egress]
---
# 4. 默认允许所有(慎用,等于不隔离)
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-all
  namespace: production
spec:
  podSelector: {}
  policyTypes: [Ingress, Egress]
  ingress: [{}]    # 空对象 = 允许所有
  egress: [{}]
```

### 18.3.6 CNI 实现差异

| CNI | K8s NetworkPolicy | 扩展策略 | L7 |
|-----|-------------------|---------|----|
| Calico | ✅ | CalicoNetworkPolicy | ❌ |
| Cilium | ✅ | CiliumNetworkPolicy | ✅(HTTP/gRPC) |
| Antrea | ✅ | AntreaNetworkPolicy | ✅ |
| Flannel | ❌ | - | - |
| Weave Net | ✅ | - | ❌ |

**注意**:Flannel 不支持 NetworkPolicy,需配合 Calico for policy。

------

## 18.4 操作流程

### 18.4.1 NetworkPolicy 实施流程

```
1. 创建 NetworkPolicy 对象 → APIServer
2. APIServer 校验格式
3. CNI Controller Watch 到 NetworkPolicy
4. CNI 转换为底层规则:
   - Calico: felix → iptables
   - Cilium: cilium-agent → eBPF map
5. 规则下发到各节点
6. 节点应用规则,Pod 流量开始受控
```

### 18.4.2 Pod 流量匹配流程

```
Pod A(10.244.1.5) → Pod B(10.244.2.5:8080)

1. Pod A 出包: src=10.244.1.5, dst=10.244.2.5
2. 节点 A veth → tc ingress(Cilium)/iptables PREROUTING(Calico)
3. 查 Pod A 的 egress 策略:
   - 命中允许 to:10.244.2.5:8080?放行
   - 未命中或默认 deny?丢弃
4. 包到 Pod B 节点
5. 节点 B 查 Pod B 的 ingress 策略:
   - 命中允许 from:10.244.1.5:8080?放行
   - 未命中?丢弃
6. Pod B 收到包
```

### 18.4.3 跨命名空间隔离典型流程

```
需求:production 与 staging 完全隔离,但都可访问 kube-system

1. 给命名空间打 label:
   kubectl label ns production name=production
   kubectl label ns staging name=staging
   kubectl label ns kube-system name=kube-system

2. production 默认 deny-all + 允许到 kube-system:
   apiVersion: networking.k8s.io/v1
   kind: NetworkPolicy
   metadata:
     name: production-isolation
     namespace: production
   spec:
     podSelector: {}
     policyTypes: [Ingress, Egress]
     ingress:
     - from:
       - namespaceSelector:
           matchLabels:
             name: production   # 同命名空间互通
     egress:
     - to:
       - namespaceSelector:
           matchLabels:
             name: production
     - to:
       - namespaceSelector:
           matchLabels:
             name: kube-system
       ports:
       - protocol: UDP
         port: 53   # DNS
```

------

## 18.5 底层原理

### 18.5.1 Calico 实现

```
Calico 架构:
  - calico-node(DaemonSet):每节点运行
    * felix:策略转换、iptables 写入
    * bird(BGP):路由同步
  - calico-kube-controllers:策略同步
  
NetworkPolicy → felix → iptables 规则:
  - KUBE-FORWARD 链入口
  - 按 namespace + Pod 计算规则
  - 写入 KUBE-NWPLCY-XXX 链
  - 默认 DROP,匹配则 ACCEPT
  
性能:
  - 规则数随 Pod × Policy 增长
  - 大集群(10000 Pod)iptables 规则爆炸
  - 解决:eBPF 模式
```

### 18.5.2 Cilium eBPF 实现

```
Cilium 架构:
  - cilium-agent(DaemonSet):每节点
  - cilium-operator:集群级
  
NetworkPolicy → cilium-agent → eBPF map:
  - from-container 程序查 policy map
  - 数据结构:
    {
      source_ip: {dest_ip: {port: action}}
    }
  - O(1) 查找
  
L7 策略(HTTP):
  - L7 proxy(Envoy)嵌入 eBPF
  - 解析 HTTP path/method
  - 例:仅允许 GET /api/v1/.*
  
优势:
  - 性能高(无 iptables)
  - L7 能力
  - Hubble 流量可视化
```

### 18.5.3 ipBlock 的陷阱

```
NetworkPolicy 中 ipBlock:
  - 仅匹配 IP 流量
  - 不区分 Pod 还是外部
  - Pod IP 也算 ipBlock

陷阱:
1. ipBlock.cidr 包含 Pod IP
   - 实际允许的是该 IP 段所有流量
   - 包括 Pod 重启后新 IP(若在 CIDR 内)
   
2. except 排除范围
   - 例:cidr=10.0.0.0/8 except=10.0.0.0/24
   - 允许 10.0.0.0/8 但排除 10.0.0.0/24
   
3. nodeIP 不等于 Pod IP
   - 限制 nodeIP 不限制 Pod
   - 需用 endpoints IP 或 service IP
```

### 18.5.4 与 kube-proxy 协同

```
kube-proxy 处理 Service IP → Pod IP 的 DNAT
NetworkPolicy 处理 Pod 间访问

执行顺序:
  1. PREROUTING(kube-proxy DNAT)
  2. 网络策略检查(NetworkPolicy)
  
所以 NetworkPolicy 看到的是 DNAT 后的 Pod IP,不是 Service IP。
这意味着:
  - 策略匹配 Pod IP 即可
  - 不需要为 Service IP 单独写策略
  - 但若 Service 后端 Pod 跨命名空间,需注意
```

### 18.5.5 零信任网络模型

```
传统边界安全:
  外网 → 防火墙 → 内网(全部信任)
  
零信任:
  - 永不信任,始终验证
  - 默认 deny
  - 显式允许
  - 最小权限
  - 加密传输
  - 持续监控

K8s 实现:
  1. NetworkPolicy:Pod 级访问控制
  2. mTLS(Istio/Linkerd):服务间加密
  3. RBAC:API 级控制
  4. Pod Security:容器级隔离
  5. Audit Log:全审计
  6. SPIFFE:工作负载身份
```

### 18.5.6 NetworkPolicy 缺失能力

```
NetworkPolicy v1 不能做:
  1. L7 策略(HTTP path/method)
  2. 基于域名的限制(DNS 解析后才能匹配 IP)
  3. 计费/限流
  4. 熔断/重试
  5. 跨集群策略
  6. 策略状态/审计

扩展方案:
  - CiliumNetworkPolicy:L7 + 域名
  - AntreaNetworkPolicy:集群级策略
  - Service Mesh(Istio):L7 + 流量治理
  - OPA:策略即代码
```

### 18.5.7 AdminNetworkPolicy(KEP-3726)

K8s 1.30+ 引入更强策略层:

```
三层策略优先级:
  1. AdminNetworkPolicy(集群管理员,最高优先级)
 2 NetworkPolicy(租户自管)
  3. BaselineAdminNetworkPolicy(默认基线)

特性:
  - 集群级(非命名空间)
  - 显式优先级
  - Pass/Allow/Deny 三态(可让流量继续匹配下一层)
```

------

## 18.6 配置示例

### 18.6.1 多层防御策略

```yaml
# 第 1 层:命名空间默认 deny-all
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: default-deny-all
  namespace: production
spec:
  podSelector: {}
  policyTypes: [Ingress, Egress]
---
# 第 2 层:允许同命名空间互通
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-same-namespace
  namespace: production
spec:
  podSelector: {}
  policyTypes: [Ingress, Egress]
  ingress:
  - from:
    - podSelector: {}
  egress:
  - to:
    - podSelector: {}
---
# 第 3 层:允许 DNS
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-dns
  namespace: production
spec:
  podSelector: {}
  policyTypes: [Egress]
  egress:
  - to:
    - namespaceSelector:
        matchLabels:
          kubernetes.io/metadata.name: kube-system
      podSelector:
        matchLabels:
          k8s-app: kube-dns
    ports:
    - protocol: UDP
      port: 53
    - protocol: TCP
      port: 53
---
# 第 4 层:应用级细粒度策略
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: backend-policy
  namespace: production
spec:
  podSelector:
    matchLabels:
      app: backend
  policyTypes: [Ingress]
  ingress:
  - from:
    - podSelector:
        matchLabels:
          app: frontend
    ports:
    - protocol: TCP
      port: 8080
```

### 18.6.2 外部访问控制

```yaml
# 允许特定外部 IP 访问
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-external
  namespace: production
spec:
  podSelector:
    matchLabels:
      app: web
  policyTypes: [Ingress]
  ingress:
  - from:
    - ipBlock:
        cidr: 0.0.0.0/0
        except:
        - 10.0.0.0/8        # 排除内网
        - 169.254.169.254/32 # 排除元数据
    ports:
    - protocol: TCP
      port: 443
```

### 18.6.3 出站限制(白名单外部 API)

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: restrict-egress
  namespace: production
spec:
  podSelector:
    matchLabels:
      app: api-client
  policyTypes: [Egress]
  egress:
  # DNS
  - to:
    - namespaceSelector:
        matchLabels:
          kubernetes.io/metadata.name: kube-system
    ports:
    - {protocol: UDP, port: 53}
  # HTTPS 出站
  - to:
    - ipBlock:
        cidr: 0.0.0.0/0
    ports:
    - {protocol: TCP, port: 443}
  # 内部数据库
  - to:
    - podSelector:
        matchLabels:
          app: postgres
    ports:
    - {protocol: TCP, port: 5432}
```

### 18.6.4 CiliumNetworkPolicy(L7)

```yaml
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata:
  name: l7-api-policy
  namespace: production
spec:
  endpointSelector:
    matchLabels:
      app: api
  ingress:
  - fromEndpoints:
    - matchLabels:
        app: frontend
    toPorts:
    - ports:
      - port: "8080"
        protocol: TCP
      rules:
        http:
        - method: GET
          path: "/api/v1/users/.*"
        - method: POST
          path: "/api/v1/orders"
```

### 18.6.5 Cilium 基于域名的策略

```yaml
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata:
  name: allow-external-api
  namespace: production
spec:
  endpointSelector:
    matchLabels:
      app: api-client
  egress:
  - toFQDNs:    # 基于域名
    - matchPattern: "*.amazonaws.com"
    - matchName: "api.stripe.com"
    toPorts:
    - ports:
      - port: "443"
        protocol: TCP
```

### 18.6.6 命名空间标签自动打

```yaml
# 用 Kyverno 自动打命名空间 label
apiVersion: kyverno.io/v1
kind: ClusterPolicy
metadata:
  name: add-namespace-label
spec:
  rules:
  - name: add-label
    match:
      resources:
        kinds: [Namespace]
    mutate:
      patchStrategicMerge:
        metadata:
          labels:
            name: "{{ request.object.metadata.name }}"
```

### 18.6.7 AntreaNetworkPolicy(集群级)

```yaml
apiVersion: crd.antrea.io/v1beta1
kind: ClusterNetworkPolicy
metadata:
  name: deny-prod-to-staging
spec:
  tier: securityops
  priority: 5
  appliedTo:
  - namespaceSelector:
      matchLabels:
        name: production
  egress:
  - action: Drop
    to:
    - namespaceSelector:
        matchLabels:
          name: staging
```

------

## 18.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | NetworkPolicy 写错 namespace | 不生效 | 检查 metadata.namespace |
| 2 | podSelector 写错 label | 不匹配 Pod | kubectl get pod --show-labels 校验 |
| 3 | namespaceSelector 无 label | 全部不匹配 | 给 namespace 打 label |
| 4 | 命名空间默认 deny 后忘放 DNS | Pod 无法解析域名 | 必须放通 kube-dns |
| 5 | egress 限制后忘放 metadata | 云元数据访问失败 | 显式允许或拒绝 |
| 6 | 用 ipBlock 限制 nodeIP | 不限制 Pod | 用 Pod IP 段 |
| 7 | Service IP 与 Pod IP 混淆 | 策略不生效 | NetworkPolicy 匹配 Pod IP |
| 8 | CNI 不支持 NetworkPolicy | 策略无效 | 用 Calico/Cilium |
| 9 | 多 NetworkPolicy 误以为 AND | 实际 OR | 设计策略时合并 |
| 10 | L7 需求用 L4 实现 | 不够细 | 用 Cilium L7 |
| 11 | 跨集群策略 NetworkPolicy 不支持 | 多集群失败 | 用 Service Mesh |
| 12 | Pod 重启 IP 变,ipBlock 失效 | 策略失效 | 用 podSelector |
| 13 | 策略变更后旧连接残留 | 短时间旧流量通过 | conntrack 失效(几分钟) |
| 14 | 默认策略覆盖过广 | 全集群断网 | 分批应用 + dry-run |
| 15 | 忘记放 Kubelet 探针流量 | 健康检查失败 | 允许 Node IP 访问 |
| 16 | 忘记放 Prometheus 抓取 | 监控数据缺失 | 允许 monitoring ns |
| 17 | HPA 伸缩时新 Pod 无策略 | 短期无隔离 | 策略用 label 匹配,自动覆盖 |

------

## 18.8 工业案例

### 18.8.1 阿里 ACK:Terway + NetworkPolicy

**场景**:多租户集群,要求严格网络隔离。

**方案**:
- Terway CNI(基于 Calico)
- 每命名空间默认 deny-all
- 应用级细粒度策略
- L7 策略用 Istio(网络层不够)

**经验**:
- 命名空间 label 是基石
- DNS 必须放通
- 监控流量(Prometheus)必须放通

### 18.8.2 字节跳动:Cilium L7 策略

**场景**:API 网关需要按 HTTP path 限流。

**方案**:
1. CiliumNetworkPolicy 实现 L7 策略
2. 仅允许 GET /api/v1/.* (公开接口)
3. POST /api/v1/orders 需要认证(由 Service Mesh 处理)
4. Hubble 监控流量,异常告警

**收益**:
- L4 + L7 双层防御
- 网络层拦截非法 path
- 减少应用层压力

### 18.8.3 Google GKE:零信任实践

**GKE Datapath V2** + Anthos Service Mesh:
1. NetworkPolicy:L4 隔离
2. ASM(Istio):mTLS + L7 策略
3. Anthos Identity Service:OIDC 集成
4. Cloud Audit Logs:全审计

**结果**:符合金融监管要求,零信任架构。

### 18.8.4 AWS EKS:VPC CNI + Calico

**场景**:EKS 默认 VPC CNI 不支持 NetworkPolicy。

**方案**:
1. 安装 Calico for policy(仅策略,不替代 CNI)
2. Calico Controller Watch NetworkPolicy
3. 转换为 iptables 规则
4. 应用到节点

**注意**:
- VPC CNI 与 Calico Policy 共存
- 性能略低于纯 Calico/Cilium
- 但 AWS VPC 集成优势大

### 18.8.5 Netflix:NetworkPolicy 自动生成

**场景**:数百微服务,手写策略不可行。

**方案**:
1. 服务依赖图(从 Istio 追踪数据)
2. 自动生成 NetworkPolicy 模板
3. GitOps 审核 + 应用
4. 持续校验实际流量与策略一致性

**收益**:
- 策略从手写到自动
- 错误率降低 90%
- 安全态势可视化

------

## 18.9 与其他方案关系

### 18.9.1 NetworkPolicy vs Security Group

| 维度 | NetworkPolicy | AWS SG |
|------|---------------|--------|
| 工作层 | L3/L4 | L3/L4 |
| 对象 | Pod | ENI/EC2 |
| 标识 | Label | SG ID |
| VPC 内 | ✅ | ✅ |
| 跨 VPC | 需 mesh | VPC peering |
| K8s 原生 | ✅ | ❌ |

### 18.9.2 NetworkPolicy vs Service Mesh 策略

| 维度 | NetworkPolicy | Service Mesh |
|------|---------------|--------------|
| 工作层 | L3/L4 | L4 + L7 |
| 部署 | 节点级 | Pod sidecar |
| 加密 | ❌ | mTLS |
| 路由 | ❌ | 复杂路由 |
| 限流 | ❌ | ✅ |
| 熔断 | ❌ | ✅ |

**关系**:互补,NetworkPolicy 防网络层,Mesh 防应用层。

### 18.9.3 NetworkPolicy vs iptables

```
iptables 是底层机制
NetworkPolicy 是 K8s 抽象

NetworkPolicy → CNI → iptables 规则

优势:
  - 声明式
  - 标签选择
  - 跨节点同步
  - K8s 原生
```

### 18.9.4 与 OPA/Gatekeeper

```
OPA:校验 K8s 资源是否符合策略
  - 例:NetworkPolicy 必须存在
  - 例:必须 deny-all 默认

NetworkPolicy:运行时网络策略
  - 控制实际流量

互补:
  - OPA 准入时校验策略存在
  - NetworkPolicy 运行时执行
```

------

## 18.10 面试速答

**Q1: NetworkPolicy 默认行为?**

默认允许所有流量。声明 NetworkPolicy 后,被选中的 Pod 流量按白名单匹配,未匹配的拒绝。未选中的 Pod 仍允许所有流量。

**Q2: K8s NetworkPolicy 与 CiliumNetworkPolicy 区别?**

K8s NetworkPolicy 是标准 API,仅 L3/L4。CiliumNetworkPolicy 是 CRD,扩展 L7、域名匹配、集群级策略等。

**Q3: 命名空间隔离怎么做?**

1. 给 namespace 打 label
2. 创建 deny-all 默认策略
3. 允许同命名空间互通
4. 显式放通 DNS/monitoring 等公共流量
5. 跨命名空间显式允许

**Q4: NetworkPolicy 不生效怎么排查?**

1. CNI 是否支持(Flannel 不支持)
2. podSelector/namespaceSelector 是否匹配
3. 命名空间是否有 label
4. CNI controller 日志
5. 底层规则(iptables/eBPF map)是否生成

**Q5: NetworkPolicy 能限制 Service IP 吗?**

不能直接限制 Service IP,但 Service 流量经 kube-proxy DNAT 后变成 Pod IP,NetworkPolicy 匹配 Pod IP 即可。

**Q6: egress 限制后 DNS 不通怎么办?**

必须显式允许到 kube-system/kube-dns 的 UDP/TCP 53 端口。否则 Pod 无法解析域名,所有外部访问失败。

**Q7: NetworkPolicy 与 Service Mesh 怎么协同?**

NetworkPolicy 在网络层(L3/L4)隔离,Service Mesh 在应用层(L7)做精细控制 + mTLS。两者互补,大型生产环境常同时使用。

**Q8: ipBlock 的陷阱?**

ipBlock.cidr 是 IP 段,匹配所有该段流量。Pod IP 也算 ipBlock。Pod 重启 IP 变化时,ipBlock 可能失效。建议用 podSelector 而非 ipBlock 限制 Pod 间流量。

**Q9: 零信任在 K8s 怎么实现?**

1. NetworkPolicy 默认 deny
2. mTLS(Service Mesh)
3. RBAC 最小权限
4. Pod Security(restricted)
5. 全审计日志
6. 持续监控

**Q10: NetworkPolicy 多个策略如何叠加?**

多个 NetworkPolicy 是 OR 关系。任一策略允许即放行。所以要实现严格隔离,默认 deny-all + 显式 allow 是必须的。

------

## 18.11 综合面试题

### 题 1:设计多租户 K8s 集群的网络隔离方案

```
需求:5 个 BU 共享集群,严格隔离,允许特定跨 BU 协作

设计:
1. 命名空间规划:
   - bu1-prod / bu1-staging / bu2-prod / ...
   - kube-system(共享)
   - monitoring(共享)
   - ingress-gateway(共享)
   
2. 命名空间 label:
   name: <namespace-name>
   bu: <bu1|bu2|...>
   env: <prod|staging>
   
3. 默认策略(每命名空间):
   - default-deny-all(Ingress + Egress)
   - allow-same-namespace
   - allow-dns(kube-system)
   - allow-monitoring(monitoring ns)
   - allow-ingress(ingress-gateway ns)
   
4. 跨 BU 协作(白名单):
   - 显式 NetworkPolicy 允许 bu1-frontend → bu2-backend
   - 双方命名空间都需要策略
   - 网络层 + 审批流程
   
5. 出站限制:
   - 仅允许 443 出站
   - 排除内网 IP
   - 排除元数据 IP(169.254.169.254)
   
6. L7 策略(Cilium):
   - HTTP path 限制
   - 域名白名单
   
7. 监控:
   - Hubble 流量可视化
   - 异常流量告警
   - 策略违反日志
   
8. 灾备:
   - NetworkPolicy GitOps
   - dry-run 模式应用
   - 紧急回滚预案
```

### 题 2:NetworkPolicy 部署后 Pod 间不通,排查步骤?

```
1. 策略状态:
   kubectl get networkpolicy -n <ns>
   kubectl describe networkpolicy <name> -n <ns>
   - 策略是否生效?
   - podSelector 是否匹配?
   
2. Pod label:
   kubectl get pod -n <ns> --show-labels
   - 期望的 label 是否存在?
   
3. namespace label:
   kubectl get ns --show-labels
   - 用到的 namespace 是否有 label?
   
4. CNI 状态:
   kubectl get pod -n kube-system | grep -E 'calico|cilium'
   - CNI pod 是否 Running?
   
5. 底层规则:
   iptables -L KUBE-NWPLCY-XXX -n -v(Calico)
   cilium policy get(Cilium)
   - 规则是否生成?
   
6. 流量测试:
   kubectl exec -it <pod-A> -- curl <pod-B-ip>
   - 实际是否通?
   
7. 流量监控:
   cilium monitor --type=drop(Cilium)
   - 哪条策略 drop 了包?
   
8. CNI 日志:
   kubectl logs -n kube-system <calico-node-xxx>
   - 是否有 sync 错误?
```

### 题 3:解释零信任网络的实现层次

```
零信任核心:永不信任,始终验证

K8s 实现层次:
1. 身份层:
   - Workload Identity(SPIFFE)
   - ServiceAccount(短期 token)
   - IRSA(云 IAM 集成)
   
2. 网络层:
   - NetworkPolicy(默认 deny)
   - 命名空间隔离
   - L7 策略(Cilium)
   
3. 传输层:
   - mTLS(Istio/Linkerd)
   - WireGuard(节点间)
   - HTTPS only
   
4. 应用层:
   - JWT 验证
   - API 网关
   - OAuth2
   
5. 数据层:
   - 存储加密(etcd kms)
   - Secret 加密
   - 传输加密
   
6. 审计层:
   - Audit Log
   - SIEM 集成
   - 异常检测
   
7. 治理层:
   - OPA/Gatekeeper
   - CIS Benchmark
   - 合规扫描
   
每层都默认拒绝,显式允许,最小权限。
```

### 题 4:NetworkPolicy 的局限与替代方案

```
NetworkPolicy v1 局限:
1. 仅 L3/L4,无 L7
2. 无域名匹配
3. 无限流/熔断
4. 单集群
5. 无状态(策略不知道流量状态)
6. 无审计/日志

替代/扩展:
1. CiliumNetworkPolicy:
   - L7 HTTP/gRPC
   - 域名匹配
   - 集群级策略
   - Hubble 可观测
   
2. Service Mesh(Istio):
   - L7 路由
   - mTLS
   - 限流/熔断
   - 跨集群
   
3. API Gateway:
   - 入口流量控制
   - JWT/OAuth
   - 限流
   - WAF
   
4. AdminNetworkPolicy(K8s 1.30+):
   - 集群级管理员策略
   - 优先级
   - Pass/Allow/Deny

5. OPA/Gatekeeper:
   - 准入时校验策略存在
   - 合规审计
   
最佳实践:多层叠加
- NetworkPolicy:基础隔离
- CiliumNetworkPolicy:L7 增强
- Service Mesh:流量治理
- OPA:合规校验
```

### 题 5:解释 Cilium L7 策略工作原理

```
Cilium L7 策略实现:
1. 数据路径:
   Pod A → veth → tc eBPF → L7 proxy(Envoy)→ Pod B
   
2. eBPF 程序:
   - from-container:Pod 出流量
   - 检测到 HTTP 流量,转到 L7 proxy
   - L7 proxy 解析 HTTP,匹配策略
   - 允许 → 转发,拒绝 → 丢弃
   
3. L7 策略示例:
   rules:
     http:
     - method: GET
       path: "/api/v1/.*"
     - method: POST
       path: "/api/v1/orders"
   
4. 性能:
   - L7 处理慢于 L4
   - 仅对需要 L7 的流量启用
   - L4 流量绕过 L7 proxy
   
5. 可观测:
   - Hubble 记录 HTTP 元数据
   - 流量拓扑可视化
   - 异常流量告警

对比 Service Mesh:
  - Cilium 无 sidecar,资源消耗低
  - 但能力不如 Istio 完整
  - 适合简单 L7 场景
```

### 题 6:如何实现 Pod 出站流量白名单?

```
需求:仅允许 Pod 访问 *.amazonaws.com + 内部 db

方案 1:NetworkPolicy + IP 白名单
  - 缺点:AWS IP 段动态变化,维护成本高
  
方案 2:Cilium FQDN 策略
  apiVersion: cilium.io/v2
  kind: CiliumNetworkPolicy
  spec:
    endpointSelector:
      matchLabels: {app: api-client}
    egress:
    - toFQDNs:
      - matchPattern: "*.amazonaws.com"
      - matchName: "api.stripe.com"
      toPorts:
      - ports: [{port: "443", protocol: TCP}]
    - toEndpoints:
      - matchLabels: {app: db}
      toPorts:
      - ports: [{port: "5432", protocol: TCP}]
  
  Cilium 通过 DNS proxy 解析域名,缓存 IP,动态更新策略
  
方案 3:Service Mesh + 外部服务代理
  - Istio ServiceEntry 声明外部服务
  - Envoy 代理出站流量
  - L7 策略 + mTLS
  
方案 4:Egress Gateway
  - 所有出站流量经 Egress Gateway
  - Gateway 集中管控
  - IP 白名单固定(网关 IP)
  - 适合合规要求严格场景
```

------

## 18.12 故障复盘

### 案例 1:NetworkPolicy 误封 DNS

**故障时间**:2023-11-15

**故障现象**:
- 部署 NetworkPolicy 后,Pod 无法解析域名
- 应用启动失败,所有外部 API 不可达

**根因**:
- 默认 deny-all egress
- 未显式放通 kube-system DNS
- DNS 查询被 drop

**修复**:
```yaml
egress:
- to:
  - namespaceSelector:
      matchLabels:
        kubernetes.io/metadata.name: kube-system
    podSelector:
      matchLabels:
        k8s-app: kube-dns
  ports:
  - {protocol: UDP, port: 53}
  - {protocol: TCP, port: 53}
```

**经验**:NetworkPolicy 部署前必须列出所有必需出站,DNS 是最易漏的。

### 案例 2:NetworkPolicy 覆盖过广断网

**故障时间**:2024-02-28

**故障现象**:
- 全集群 Pod 间通信中断
- 业务大规模告警

**根因**:
- 误用 `podSelector: {}` + 默认 deny-all
- 覆盖所有命名空间
- 未放通同命名空间

**修复**:
1. 紧急删除误配策略
2. 重新分批应用
3. 加 dry-run 模式

**经验**:大规模策略变更必须分批 + dry-run。

### 案例 3:calico-node 卡死策略不生效

**故障时间**:2024-04-12

**故障现象**:
- 新建 NetworkPolicy 不生效
- Pod 间仍可互通

**根因**:
- calico-node pod OOM
- 与 etcd 连接失败
- 策略无法同步

**修复**:
1. 重启 calico-node
2. 调大资源 limit
3. 监控 felix 状态

**经验**:CNI 控制器健康必须监控。

### 案例 4:Cilium L7 策略误杀健康检查

**故障时间**:2024-06-08

**故障现象**:
- 部署 L7 策略后,Pod 健康检查失败
- Pod 反复重启

**根因**:
- L7 策略仅允许 GET /api/v1/.*
- kubelet 健康检查 /healthz 被拒绝
- L7 proxy 拦截了健康检查流量

**修复**:
1. 显式放通健康检查路径:
   ```yaml
   http:
   - method: GET
     path: "/healthz"
   - method: GET
     path: "/api/v1/.*"
   ```
2. 或用 cilium 例外规则

**经验**:L7 策略要考虑所有流量,包括健康检查。

### 案例 5:跨集群策略不一致

**故障时间**:2024-09-20

**故障现象**:
- 多集群 Pod 通信偶发失败
- 不同集群策略不同步

**根因**:
- 多集群手动维护 NetworkPolicy
- 配置漂移

**修复**:
1. GitOps 统一管理
2. Cluster API 自动应用
3. Cilium ClusterMesh 跨集群策略

**经验**:多集群策略必须用工具统一管理。

------

## 18.13 参考与延伸

### 官方文档
- [Network Policies](https://kubernetes.io/docs/concepts/services-networking/network-policies/)
- [Declare Network Policy](https://kubernetes.io/docs/tasks/administer-cluster/declare-network-policy/)

### KEP
- [KEP-3726: AdminNetworkPolicy](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/3726-admin-network-policy)
- [KEP-3482: NetworkPolicy status](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/3482-network-policy-status)

### CNI 扩展策略
- [Calico NetworkPolicy](https://docs.tigera.io/calico/latest/reference/resources/networkpolicy)
- [CiliumNetworkPolicy](https://docs.cilium.io/en/stable/concepts/kubernetes/policy/)
- [AntreaNetworkPolicy](https://antrea.io/docs/main/docs/network-policies/)
- [Kube-router NetworkPolicy](https://www.kube-router.io/docs/network-policies/)

### 源码导航
- `kubernetes/pkg/controller/networkpolicy/` - 网络策略控制器
- `kubernetes/pkg/kubelet/network/` - kubelet 网络相关

### 相关章节
- [15-CNI与网络模型.md](./15-CNI与网络模型.md) - CNI 实现细节
- [17-RBAC与认证授权.md](./17-RBAC与认证授权.md) - API 层授权
- [19-Pod安全.md](./19-Pod安全.md) - Pod 级安全
- [20-策略与治理.md](./20-策略与治理.md) - 策略即代码
- [28-服务网格与Serverless.md](./28-服务网格与Serverless.md) - Service Mesh

### 推荐阅读
- [Zero Trust Architecture](https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-207.pdf)
- [Cilium: Network Policy](https://docs.cilium.io/en/stable/security/policy/)
- [Calico Default Deny](https://docs.tigera.io/calico/latest/getting-started/kubernetes/quickstart)
- [Kubernetes Network Policy Recipes](https://github.com/ahmetb/kubernetes-network-policy-recipes)

### 工具
- `kubectl get networkpolicy`
- `cilium policy get` - Cilium 策略
- `calicoctl get networkpolicy` - Calico 策略
- `hubble observe` - 流量观测
- `netshoot` - 网络调试容器

### 进阶主题
- **AdminNetworkPolicy**:集群级管理员策略(K8s 1.30+)
- **BaselineAdminNetworkPolicy**:默认基线策略
- **Egress Gateway**:出站流量集中管控
- **Cilium ClusterMesh**:跨集群网络策略
- **SPIFFE/SPIRE**:工作负载身份
- **WireGuard**:节点间加密
- **Service Mesh mTLS**:服务间加密

# 15. CNI 与网络模型

> 关键词：CNI、Calico、Cilium、Flannel、VXLAN、BGP、eBPF、NetworkPolicy、双栈、SR-IOV

------

## 15.1 问题定义

K8s 网络模型要求：

1. **Pod 间直接通信**：无需 NAT
2. **Pod 与节点间直接通信**：无需 NAT
3. **Pod 自己看到的 IP 与外界看到的一致**

实现这三条规则的底层机制就是 **CNI（Container Network Interface）**。

**核心问题**：

> 每个 Pod 都需要独立 IP，跨节点 Pod 怎么通信？如何高效、可扩展、可观测地实现 K8s 网络模型？

------

## 15.2 直觉解释

把 CNI 想象成 **快递公司的物流网络设计**：

| 物流 | CNI |
|------|-----|
| 每个客户分配地址 | 每个 Pod 分配 IP |
| 同楼内直接送达 | 同节点：veth pair 直连 |
| 跨城市用货运飞机 | 跨节点：VXLAN/Geneve 隧道 |
| 或各地分公司直发 | 跨节点：BGP 路由 |
| 不同快递公司不同方案 | 不同 CNI 实现方案不同 |
| 物流策略（如冷藏专线） | NetworkPolicy 网络策略 |

关键点：CNI 是 **规范（spec）+ 实现（plugin）**，规范定义接口，实现各显神通。

------

## 15.3 核心概念

### 15.3.1 CNI 接口规范

```
CNI 是个简单规范：
  输入：容器 ID、netns 路径、网络配置 JSON
  输出：分配的 IP、路由、接口列表
  
四个核心操作：
  ADD:    容器创建时调用，分配 IP、配置接口
  DEL:    容器删除时调用，释放 IP、清理接口
  CHECK:  检查容器网络是否健康
  VERSION: 返回支持的 CNI 版本

调用方：
  - kubelet（通过 CRI 间接调用）
  - containerd/CRI-O（实际调用者）
  - 调用时机：RunPodSandbox 前
```

### 15.3.2 Pod 网络基础结构

```
┌──────────────────────── 主机 ────────────────────────┐
│                                                      │
│  ┌── Pod A ──┐         ┌── Pod B ──┐                │
│  │  eth0     │         │  eth0     │                │
│  │ 10.244.1.5│         │ 10.244.1.6│                │
│  └─────┬─────┘         └─────┬─────┘                │
│        │ veth                │ veth                 │
│        ▼                     ▼                      │
│  ┌─────┴─────┐         ┌─────┴─────┐                │
│  │ veth-A    │         │ veth-B    │                │
│  └─────┬─────┘         └─────┬─────┘                │
│        │                     │                      │
│        └──────────┬──────────┘                      │
│                   │                                 │
│             ┌─────┴─────┐                           │
│             │  cni0     │  Linux bridge             │
│             │ 10.244.1.1│                           │
│             └─────┬─────┘                           │
│                   │                                 │
└───────────────────┼─────────────────────────────────┘
                    │
                    │ 路由/隧道
                    ▼
                其他节点
```

**关键组件**：
- **veth pair**：一对虚拟网卡，一端在容器 netns，一端在主机
- **cni0**（Bridge 模式）：Linux 网桥，同节点 Pod 互通
- **vxlan/calico.gw/cilium_host**：跨节点通信接口

### 15.3.3 三种跨节点通信模型

```
模型 1：Overlay（覆盖网络）
  ┌────────┐    VXLAN/Geneve     ┌────────┐
  │ Node A │ ◄─────────────────► │ Node B │
  │ Pod IP │   封装到 UDP 8472   │ Pod IP │
  └────────┘                     └────────┘
  特点：简单，不依赖底层网络
  代表：Flannel VXLAN、Calico VXLAN、Cilium VXLAN

模型 2：BGP（边界网关协议）
  ┌────────┐      BGP 路由       ┌────────┐
  │ Node A │ ◄─────────────────► │ Node B │
  │ Pod IP │   路由表直接指向    │ Pod IP │
  └────────┘                     └────────┘
  特点：性能好，需底层网络支持 BGP
  代表：Calico BGP、Cilium BGP、Kube-router

模型 3：Underlay（底层网络）
  ┌────────┐   物理网络 MAC/IP   ┌────────┐
  │ Pod A  │ ◄─────────────────► │ Pod B  │
  │ 物理IP │   直接路由          │ 物理IP │
  └────────┘                     └────────┘
  特点：性能最优，需底层网络配置
  代表：MACVLAN、SR-IOV、IPvlan
```

### 15.3.4 主流 CNI 对比

| CNI | 模式 | NetworkPolicy | 性能 | 复杂度 | 推荐场景 |
|-----|------|---------------|------|--------|---------|
| **Flannel** | Overlay (VXLAN) | ❌ | 中 | 低 | 入门、简单场景 |
| **Calico** | BGP/VXLAN | ✅ | 高 | 中 | 通用、生产首选 |
| **Cilium** | eBPF/VXLAN/BGP | ✅ | 最高 | 中 | 大规模、可观测 |
| **Weave Net** | Overlay | ✅ | 中 | 低 | 中小集群 |
| **Kube-router** | BGP | ✅ | 高 | 中 | BGP 偏好场景 |
| **Antrea** | OVS | ✅ | 高 | 中 | VMware 环境 |
| **OVN-Kubernetes** | OVS | ✅ | 高 | 高 | OpenStack 系 |
| **MACVLAN/SR-IOV** | Underlay | 部分 | 最高 | 高 | 高性能、低延迟 |

### 15.3.5 NetworkPolicy（网络策略）

```
NetworkPolicy 是 K8s 的网络 ACL 机制：
  - 默认允许所有流量
  - 显式声明允许规则（白名单）
  - 命名空间隔离

支持能力：
  - 按 Label 选择 Pod
  - 按 IP/CIDR 限制
  - 按命名空间限制
  - 按 端口/协议限制
  - ingress + egress 双向

实现依赖 CNI：
  - Calico/Cilium/Antrea: 完整支持
  - Flannel: 不支持（需 Calico for policy）
```

### 15.3.6 Service 与 CNI 的关系

```
Service (ClusterIP) 是虚拟的，由 kube-proxy 实现
CNI 处理的是 Pod IP 间通信

数据流：
  Pod A → Service IP (10.96.0.1)
    → kube-proxy DNAT → Pod B IP (10.244.2.5)
    → CNI 处理 10.244.2.5 的路由
    → 跨节点或同节点送达
```

------

## 15.4 操作流程

### 15.4.1 Pod 创建时 CNI 调用流程

```
T0: kubelet 收到新 Pod
T1: kubelet 调用 CRI RunPodSandbox
T2: containerd 创建 netns（network namespace）
T3: containerd 调用 CNI plugin（如 /opt/cni/bin/calico）
    输入：
      CNI_COMMAND=ADD
      CNI_CONTAINERID=xxx
      CNI_NETNS=/var/run/netns/cni-xxx
      CNI_IFNAME=eth0
      CNI_PATH=/opt/cni/bin
      CNI_ARGS=...
    配置 JSON（stdin）：
      {
        "name": "k8s-pod-network",
        "cniVersion": "0.4.0",
        "plugins": [...]
      }
T4: CNI plugin 执行：
    1. IPAM（IP 地址管理）：从 IP 池分配 IP
    2. 创建 veth pair：一端放容器（eth0），一端留主机
    3. 配置容器内路由：默认路由走 eth0
    4. 主机端连接到 cni0 网桥 / BGP / eBPF map
T5: CNI 返回：
    {
      "ips": [{"address": "10.244.1.5/24"}],
      "routes": [{"dst": "0.0.0.0/0", "gw": "10.244.1.1"}],
      "interfaces": [{"name": "eth0", ...}]
    }
T6: containerd 继续 RunPodSandbox（pause 容器）
T7: kubelet 创建业务容器（共享 netns）
```

### 15.4.2 跨节点 Pod 通信（VXLAN 模式）

```
Pod A (NodeA, 10.244.1.5) → Pod B (NodeB, 10.244.2.5)

1. Pod A 发出包：src=10.244.1.5, dst=10.244.2.5
2. veth → cni0
3. 路由表：10.244.2.0/24 → flannel.1/vxlan.calico
4. VXLAN 封装：
   外层：src=NodeA_IP, dst=NodeB_IP, UDP dport=8472
   内层：原 IP 包
5. 物理网络转发到 NodeB
6. NodeB vxlan 接口解封装
7. 路由到 cni0
8. veth → Pod B
9. Pod B 响应反向路径
```

### 15.4.3 跨节点 Pod 通信（BGP 模式）

```
Pod A (NodeA, 10.244.1.5) → Pod B (NodeB, 10.244.2.5)

1. Pod A 发出包：src=10.244.1.5, dst=10.244.2.5
2. veth → 主机内核
3. 路由表：10.244.2.0/24 via NodeB_IP（BGP 学到）
4. 直接转发到 NodeB_IP（无需封装）
5. NodeB 内核：dst=10.244.2.5 → 本机 Pod
6. veth → Pod B
```

**BGP 优势**：无封装开销，性能更接近物理网络。

**BGP 劣势**：要求底层网络能路由 Pod CIDR（企业数据中心常见，公有云不支持）。

------

## 15.5 底层原理

### 15.5.1 Linux 网络命名空间与 veth

```bash
# 创建 netns
ip netns add podns

# 创建 veth pair
ip link add veth-host type veth peer name veth-pod

# 一端放入 netns
ip link set veth-pod netns podns

# 配置 IP
ip addr add 10.244.1.5/24 dev veth-host
ip netns exec podns ip addr add 10.244.1.5/24 dev veth-pod

# 启用
ip link set veth-host up
ip netns exec podns ip link set veth-pod up
ip netns exec podns ip link set lo up

# 默认路由
ip netns exec podns ip route add default via 10.244.1.1
```

### 15.5.2 VXLAN 协议细节

```
VXLAN 帧：
┌─────────────────────────────────────────────┐
│ 外层 Ethernet (源/目 MAC = 物理 NIC)        │
├─────────────────────────────────────────────┤
│ 外层 IP (源/目 = 物理 IP)                   │
├─────────────────────────────────────────────┤
│ UDP (目的端口 8472)                         │
├─────────────────────────────────────────────┤
│ VXLAN Header (VNI = 24 位网络标识)          │
├─────────────────────────────────────────────┤
│ 内层 Ethernet (源/目 MAC = 虚拟)            │
├─────────────────────────────────────────────┤
│ 内层 IP (源/目 = Pod IP)                    │
├─────────────────────────────────────────────┤
│ 内层 TCP/UDP                                │
└─────────────────────────────────────────────┘

关键概念：
  - VNI（VXLAN Network Identifier）：24 位，1600 万独立网络
  - VTEP（VXLAN Tunnel Endpoint）：vxlan 接口
  - FDB（Forwarding DataBase）：MAC → 远端 VTEP IP 映射
```

### 15.5.3 BGP 协议基础

```
BGP（Border Gateway Protocol）是互联网主流路由协议：
  - AS（Autonomous System）：自治系统，每个集群一个 AS 号
  - eBGP：跨 AS
  - iBGP：AS 内
  
K8s 中 BGP 用法：
  - 每个节点跑 BGP daemon（如 Calico 的 bird/frr）
  - 节点间建立 BGP 邻居
  - 通告本节点的 Pod CIDR 路由
  - 学习其他节点的 Pod CIDR 路由

路由表：
  10.244.1.0/24 via 192.168.1.1  # NodeA 的 Pod CIDR
  10.244.2.0/24 via 192.168.1.2  # NodeB 的 Pod CIDR
  10.244.3.0/24 via 192.168.1.3  # NodeC 的 Pod CIDR

BGP 拓扑：
  - 全互联（mesh）：节点两两建邻，规模 <100 节点
  - RR（Route Reflector）：中心节点反射路由，规模 >100
  - Top-of-Rack：ToR 交换机作 BGP 邻居
```

### 15.5.4 eBPF 数据路径（Cilium）

```
传统路径（Calico/Flannel）：
  Pod A → veth → tc ingress → iptables → veth → Pod B
                              ↑ 多个 hook
  
eBPF 路径（Cilium）：
  Pod A → veth → tc eBPF → veth → Pod B
              ↑ 程序直接处理
              
关键 eBPF 程序：
  1. from-container：Pod 出流量
     - DNAT（Service → Pod）
     - NetworkPolicy 检查
     - 转发决策
  2. to-container：Pod 入流量
     - 反向 NAT
     - 策略检查
     - 路由

性能优势：
  - 绕过 iptables 全链
  - 无 conntrack（自维护）
  - 内核态直接处理，无上下文切换
```

### 15.5.5 IPAM（IP 地址管理）

```
方案 1：节点级 CIDR 分配（HostScope）
  - 集群 CIDR: 10.244.0.0/16
  - 节点 1: 10.244.1.0/24（256 IP）
  - 节点 2: 10.244.2.0/24
  - 节点 N: 10.244.N.0/24
  
  优点：路由聚合简单
  缺点：节点 Pod 数受 CIDR 大小限制

方案 2：全局 IP 池（ClusterScope）
  - 所有 Pod 从一个 IP 池分配
  - 节点不固定 CIDR
  - 需要中心化 IPAM（etcd/分布式存储）
  
  优点：IP 利用率高
  缺点：路由表庞大

方案 3：CIDR 池（Calico Block）
  - 每节点分配多个 /26 块
  - 块用完再申请新块
  - 兼顾聚合与利用率
```

### 15.5.6 NetworkPolicy 实现机制

```
K8s NetworkPolicy 资源 → CNI controller → 转换为底层规则

Calico 实现：
  NetworkPolicy → felix → iptables 规则
  
  KUBE-FORWARD 链 → KUBE-NWPLCY-XXX 链
  - 按 Label 选择 Pod
  - 按 Source/Destination 限制
  - 默认 DROP

Cilium 实现：
  NetworkPolicy → cilium-agent → eBPF map
  - from-container 程序查 policy map
  - 命中规则 ALLOW，否则 DROP
  
  优势：L7 策略（HTTP path/method 等）
```

### 15.5.7 双栈（IPv4 + IPv6）

```
K8s 1.21+ 双栈 GA：
  - Pod 同时分配 IPv4 + IPv6
  - Service 同时分配 IPv4 + IPv6 ClusterIP
  - 路由表双栈配置

配置：
  --cluster-cidr=10.244.0.0/16,fd00:10:244::/56
  --service-cluster-ip-range=10.96.0.0/16,fd00:10:96::/112
  
CNI 支持：
  - Calico: 完整支持
  - Cilium: 完整支持
  - Flannel: 部分支持
```

------

## 15.6 配置示例

### 15.6.1 Calico BGP 模式部署

```yaml
# calico-bgp.yaml
apiVersion: operator.tigera.io/v1
kind: Installation
metadata:
  name: default
spec:
  calicoNetwork:
    ipPools:
    - cidr: 10.244.0.0/16
      encapsulation: None       # BGP 模式不封装
      natOutgoing: false
      nodeSelector: all()
    nodeAddressAutodetectionV4:
      firstFound: true
    bgp: Enabled
---
# IPPool
apiVersion: crd.projectcalico.org/v1
kind: IPPool
metadata:
  name: default-ipv4-ippool
spec:
  cidr: 10.244.0.0/16
  ipipMode: Never
  vxlanMode: Never
  natOutgoing: false
  nodeSelector: all()
  blockSize: 26   # 每节点 /26 块（64 IP）
---
# BGP Configuration
apiVersion: crd.projectcalico.org/v1
kind: BGPConfiguration
metadata:
  name: default
spec:
  asNumber: 64512              # 私有 AS 号
  serviceClusterIPs:
  - cidr: 10.96.0.0/16
  serviceExternalIPs:
  - cidr: 192.168.100.0/24
```

### 15.6.2 Cilium eBPF 模式部署

```bash
# Helm 安装
helm install cilium cilium/cilium --version 1.15.0 \
  --namespace kube-system \
  --set kubeProxyReplacement=true \
  --set k8sServiceHost=APISERVER_IP \
  --set k8sServicePort=6443 \
  --set ipam.mode=cluster-pool \
  --set ipam.clusterPoolIPv4PodCIDR=10.244.0.0/16 \
  --set ipam.clusterPoolIPv4MaskSize=24 \
  --set hubble.enabled=true \
  --set hubble.relay.enabled=true \
  --set hubble.ui.enabled=true \
  --set bgp.enabled=true \
  --set bgp.announce.loadbalancerIP=true \
  --set bgp.announce.podCIDR=true
```

### 15.6.3 NetworkPolicy 示例

```yaml
# 1. 默认拒绝所有入站
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: default-deny-ingress
  namespace: production
spec:
  podSelector: {}    # 命名空间内所有 Pod
  policyTypes:
  - Ingress
---
# 2. 仅允许特定命名空间访问
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-from-frontend
  namespace: production
spec:
  podSelector:
    matchLabels:
      tier: backend
  policyTypes:
  - Ingress
  ingress:
  - from:
    - namespaceSelector:
        matchLabels:
          name: frontend
      podSelector:
        matchLabels:
          tier: frontend
    ports:
    - protocol: TCP
      port: 8080
---
# 3. 限制出站
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: restrict-egress
  namespace: production
spec:
  podSelector:
    matchLabels:
      app: web
  policyTypes:
  - Egress
  egress:
  - to:
    - namespaceSelector: {}
      podSelector:
        matchLabels:
          app: db
    ports:
    - protocol: TCP
      port: 5432
  - to:
    - ipBlock:
        cidr: 0.0.0.0/0
        exceptions:
        - 10.0.0.0/8
    ports:
    - protocol: TCP
      port: 443   # 仅允许 HTTPS
---
# 4. Cilium L7 策略
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata:
  name: l7-policy
  namespace: production
spec:
  endpointSelector:
    matchLabels:
      app: api
  egress:
  - toEndpoints:
    - matchLabels:
        app: backend
    toPorts:
    - ports:
      - port: "8080"
        protocol: TCP
      rules:
        http:
        - method: GET
          path: "/api/v1/.*"   # 仅允许 GET /api/v1
```

### 15.6.4 Flannel 简单部署

```bash
# 一行部署（学习/测试用）
kubectl apply -f https://raw.githubusercontent.com/flannel-io/flannel/master/Documentation/kube-flannel.yml

# 配置：
# - VXLAN 模式（默认）
# - 集群 CIDR: 10.244.0.0/16
# - 不支持 NetworkPolicy（需 Calico 配合）
```

### 15.6.5 多网卡（Multus）

```yaml
# Multus CNI 配置：Pod 多网卡
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata:
  name: macvlan-conf
spec:
  config: |
    {
      "cniVersion": "0.4.0",
      "name": "macvlan-network",
      "plugins": [
        {
          "type": "macvlan",
          "master": "eth0",
          "mode": "bridge",
          "ipam": {
            "type": "static",
            "addresses": [{"address": "192.168.100.10/24"}]
          }
        }
      ]
    }
---
apiVersion: v1
kind: Pod
metadata:
  name: multi-nic-pod
  annotations:
    k8s.v1.cni.cncf.io/networks: macvlan-conf
spec:
  containers:
  - name: app
    image: nginx
```

### 15.6.6 SR-IOV 高性能网络

```yaml
# SR-IOV Network
apiVersion: sriovnetwork.openshift.io/v1
kind: SriovNetwork
metadata:
  name: sriov-net
spec:
  resourceName: intel_nic
  networkNamespace: default
  ipam: |
    {"type": "host-local","subnet":"10.56.0.0/24"}
---
# Pod 使用 SR-IOV
apiVersion: v1
kind: Pod
metadata:
  name: sriov-pod
  annotations:
    k8s.v1.cni.cncf.io/networks: sriov-net
spec:
  containers:
  - name: dpdk-app
    image: dpdk-app:v1
    resources:
      requests:
        openshift.io/intel_nic: "1"
      limits:
        openshift.io/intel_nic: "1"
```

------

## 15.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | Flannel 用作生产 | 无 NetworkPolicy | 改用 Calico/Cilium |
| 2 | VXLAN MTU 未调小 | 包分片，性能差 | MTU=物理MTU-50 |
| 3 | BGP 模式底层不支持 | Pod 跨节点不通 | 公有云用 VXLAN |
| 4 | Pod CIDR 与节点网络冲突 | 路由混乱 | 严格规划 CIDR |
| 5 | 双栈配置不完整 | IPv6 Pod 无法访问 Service | CNI/Service 双栈同步 |
| 6 | NetworkPolicy 命名空间标签缺失 | 策略失效 | 命名空间打 label |
| 7 | NetworkPolicy 默认允许 | 安全风险 | 加默认 deny-all |
| 8 | CNI 升级时不滚节点 | 跨版本兼容问题 | 滚动升级 + 监控 |
| 9 | IPAM 池耗尽 | Pod 卡在 ContainerCreating | 提前规划 IP 容量 |
| 10 | conntrack 表满 | 连接失败 | 调大 max |
| 11 | CNI 与 kube-proxy 模式冲突 | 流量黑洞 | Cilium eBPF 替代 kube-proxy |
| 12 | 节点 Pod 数超 blockSize | 新 Pod 无 IP | 调大 blockSize 或加节点 |
| 13 | BGP 邻居未建立 | 跨节点不通 | 防火墙放通 BGP TCP 179 |
| 14 | Multus 配置错误 | 主网卡失效 | 严格按文档配置 |
| 15 | SR-IOV PF/VF 配置错 | 节点网络崩 | 充分测试 |
| 16 | 跨 zone 通信延迟高 | 应用性能差 | Topology Aware Hints |
| 17 | DNS 跨节点解析慢 | 业务启动慢 | NodeLocal DNSCache |

------

## 15.8 工业案例

### 15.8.1 阿里 ACK Terway

**Terway** 是阿里云自研 CNI：
- 基于 Calico + 自研 IPAM
- 共享 ENI（Elastic Network Interface）模式：Pod 直接获得 VPC IP
- 性能接近物理网络
- NetworkPolicy 完整支持

**性能数据**：
- Pod 间延迟：VXLAN 1.2ms → Terway 0.3ms
- 吞吐：VXLAN 5Gbps → Terway 9Gbps

### 15.8.2 字节跳动：Cilium 替代 Calico

**背景**：
- 原用 Calico BGP，5000 节点
- iptables 规则爆炸（与 kube-proxy 叠加）
- NetworkPolicy 性能差

**迁移到 Cilium**：
1. eBPF 替代 kube-proxy
2. eBPF 替代 iptables NetworkPolicy
3. Hubble 全链路可观测
4. L7 策略（HTTP/gRPC）

**收益**：
- 转发延迟降低 70%
- 规则同步从秒级到毫秒级
- L7 故障定位时间从小时到分钟

### 15.8.3 Google GKE Datapath V2

**Datapath V2** 是 GKE 自研 CNI：
- 基于 Cilium
- 与 Google SDN 集成
- eBPF 数据平面
- 默认启用 NetworkPolicy

**特色**：
- Andromeda（Google SDN）原生集成
- Pod 跨 zone 延迟低于 1ms
- 自动 BGP 路由学习

### 15.8.4 AWS VPC CNI

**AWS VPC CNI** 特点：
- Pod 直接获得 VPC IP（共享 ENI）
- 无 VXLAN 封装
- 性能最优
- 但 IP 消耗大（每个 Pod 占 VPC IP）

**陷阱**：
- VPC IP 池耗尽
- 子网容量限制
- 跨可用区流量费

**优化**：
- WARM_ENI_TARGET=1（预分配 ENI）
- PREFIX_DELEGATION=true（IPv6 /80 前缀委派）

### 15.8.5 Netflix：Calico BGP 全互联扩展

**场景**：从 100 节点扩展到 800 节点，BGP mesh 性能下降。

**问题**：
- 全互联 BGP，每节点 800 个邻居
- 收敛时间 30s+
- 路由表庞大

**方案**：
1. 引入 BGP Route Reflector
2. 选 5 个节点作 RR
3. 其他节点仅与 RR 建邻
4. 收敛时间降到 5s

**经验**：>100 节点必须用 RR 或 ToR 拓扑。

------

## 15.9 与其他方案关系

### 15.9.1 CNI vs Docker Bridge

| 维度 | Docker Bridge | CNI |
|------|---------------|-----|
| 部署 | 单机 | 集群 |
| IP 分配 | NAT 后私有 IP | 真实可路由 IP |
| 跨主机 | 端口映射 | 隧道/BGP/Underlay |
| 规范 | Docker 私有 | CNCF 标准 |
| 生态 | Docker | K8s/Mesos/Nomad |

### 15.9.2 CNI vs VMware NSX

| 维度 | CNI | NSX |
|------|-----|-----|
| 部署 | K8s 原生 | vSphere 集成 |
| 网络 | Overlay/BGP | Overlay（Geneve） |
| 管理 | kubectl | vCenter |
| 适合 | K8s 集群 | vSphere 虚拟化 |
| 复杂度 | 中 | 高 |

### 15.9.3 CNI vs Service Mesh

| 维度 | CNI | Service Mesh |
|------|-----|--------------|
| 工作层 | L3/L4 | L4 + L7 |
| 部署 | 节点级 | Pod sidecar |
| 关注点 | 连通性 | 流量治理 |
| 例子 | Calico/Cilium | Istio/Linkerd |

**关系**：CNI 提供 Pod 间连通，Service Mesh 在其上提供 L7 治理。

### 15.9.4 Multus：多 CNI 共存

```
Multus 是 CNI meta-plugin：
  - 主 CNI（如 Calico）提供默认网络
  - 附加 CNI（如 SR-IOV/MACVLAN）提供高性能网络
  - Pod 可同时有多个网卡

应用场景：
  - 控制平面用 Calico
  - 数据平面用 SR-IOV
  - 管理网络用 hostNetwork
```

------

## 15.10 面试速答

**Q1: K8s 网络模型三大要求？**

1. Pod 间直接通信，无需 NAT
2. Pod 与节点直接通信，无需 NAT
3. Pod 看到的 IP 与外界一致

**Q2: 主流 CNI 有哪些？区别？**

- Flannel：简单 Overlay，无 Policy
- Calico：BGP/VXLAN，Policy 完善
- Cilium：eBPF，性能最优
- Antrea：OVS，VMware 系

**Q3: VXLAN 与 BGP 区别？**

- VXLAN：封装模式，UDP 8472，不依赖底层，性能稍差
- BGP：路由模式，无封装，性能好，但需底层支持

**Q4: NetworkPolicy 默认行为？**

默认允许所有流量。声明 NetworkPolicy 后，被选中的 Pod 流量按白名单匹配，未匹配的拒绝。命名空间隔离需显式配置。

**Q5: Calico 的 IPPool blockSize 作用？**

每节点按 blockSize（默认 26，64 IP）分配 IP 块。块用完才能申请新块。blockSize 影响路由聚合与 IP 利用率。

**Q6: eBPF 模式为什么快？**

绕过 iptables 全链匹配，eBPF 程序在 TC/XDP hook 直接处理数据包，无上下文切换，自带 conntrack，O(1) 查找。

**Q7: Pod 跨节点不通怎么排查？**

1. 检查 CNI pod 状态
2. 检查节点路由表（ip route）
3. 检查 BGP 邻居（calicoctl node status）
4. 抓包确认包是否发出/到达
5. 检查 VXLAN FDB / ARP 表

**Q8: 双栈 K8s 怎么配置？**

集群 CIDR、Service CIDR 都用双栈（v4+v6）。kube-proxy、CNI、kubelet 全部启用双栈。Pod 同时分配 v4/v6 IP。

**Q9: SR-IOV 适合什么场景？**

需要极低延迟、高吞吐的场景：DPDK、网络功能虚拟化（NFV）、金融交易。普通 Web 服务无需。

**Q10: Cilium Hubble 是什么？**

Cilium 的可观测组件，基于 eBPF 提供 L3-L7 流量监控、服务依赖图、流量日志，无需 sidecar。

------

## 15.11 综合面试题

### 题 1：设计一个跨多可用区的高性能 K8s 网络方案

```
需求：3 个 AZ，每 AZ 50 节点，单 AZ 5000 Pod，跨 AZ 延迟敏感

设计：
1. CNI：Cilium eBPF（绕过 kube-proxy）
2. IPAM：每 AZ 独立 CIDR 池
   - AZ-a: 10.244.0.0/18
   - AZ-b: 10.244.64.0/18
   - AZ-c: 10.244.128.0/18
3. 跨 AZ：VXLAN（公有云）或 BGP（自建）
4. 同 AZ：直连路由（无封装）
5. Service：拓扑感知路由，优先同 AZ
6. NetworkPolicy：CiliumNetworkPolicy（L4 + L7）
7. 监控：Hubble 全链路追踪
8. 容灾：单 AZ 故障时降级到全集群

性能目标：
  - 同 AZ Pod 间延迟 < 0.5ms
  - 跨 AZ Pod 间延迟 < 2ms
  - NetworkPolicy 同步 < 100ms
```

### 题 2：Pod 跨节点不通，详细排查步骤？

```
1. 基础检查：
   kubectl get pod -o wide  # 都 Running？
   kubectl get nodes        # 节点 Ready？
   
2. 路由检查（在两节点）：
   ip route get <对端 Pod IP>
   - 路由表是否正确？
   - 下一跳是否正确？
   
3. CNI 状态：
   kubectl get pod -n kube-system | grep calico
   kubectl get pod -n kube-system | grep cilium
   - CNI pod 是否 Running？
   
4. 跨节点连通性：
   ping <对端节点 IP>
   - 物理网络通吗？
   
5. VXLAN 检查（如适用）：
   ip -d link show flannel.1
   bridge fdb show dev flannel.1
   - FDB 表是否完整？
   
6. BGP 检查（如适用）：
   calicoctl node status
   birdcl show protocols
   - BGP 邻居是否建立？
   
7. 抓包：
   tcpdump -i any host <对端 Pod IP> -nn
   - 包发出去了吗？
   - 对端节点收到吗？
   
8. conntrack：
   conntrack -L | grep <Pod IP>
   - 是否有异常连接条目？
   
9. 防火墙：
   iptables -L -n
   - 是否有 DROP 规则？
   
10. CNI 日志：
    kubectl logs -n kube-system <calico-pod>
```

### 题 3：解释 K8s 数据包从 Pod A 到 Pod B 的完整路径（VXLAN 模式）

```
1. Pod A 应用发送：dst=10.244.2.5
2. Pod A netns 内：eth0 → veth pair
3. 主机 veth 端 → tc ingress（Cilium eBPF 在此）
4. 路由决策：10.244.2.0/24 via flannel.1
5. flannel.1（VXLAN 接口）封装：
   外层：src=NodeA_IP, dst=NodeB_IP, UDP 8472
   内层：原包（10.244.1.5 → 10.244.2.5）
6. 物理网卡发出
7. 物理网络转发到 NodeB
8. NodeB 物理网卡收到
9. 内核解 UDP 8472 → flannel.1 解封装
10. 路由决策：10.244.2.5 是本节点 Pod
11. cni0 → veth pair → Pod B eth0
12. Pod B 应用接收
13. 响应反向路径（conntrack 记录反向 NAT）
```

### 题 4：NetworkPolicy 不生效，怎么排查？

```
1. CNI 是否支持：
   kubectl get pod -n kube-system
   - Flannel 不支持，需加 Calico
   
2. NetworkPolicy 是否正确：
   kubectl describe networkpolicy <name>
   - podSelector 命中目标 Pod？
   - namespaceSelector 是否需要命名空间 label？
   
3. 命名空间 label：
   kubectl get ns --show-labels
   - 用到的 namespace 是否有 label？
   
4. CNI controller 日志：
   kubectl logs -n kube-system <calico-node-xxx>
   - 是否有 sync 错误？
   
5. 底层规则：
   iptables -L KUBE-NWPLCY-XXX -n -v
   - 规则是否生成？
   
6. 流量测试：
   kubectl exec -it <pod-A> -- curl <pod-B-ip>
   - 期望：拒绝，实际：放行？
   
7. Cilium 特有：
   cilium policy get
   cilium monitor --type=drop
```

### 题 5：设计一个支持 NetworkPolicy + 高性能 + 多租户的 CNI 方案

```
1. CNI 选型：Cilium
   - eBPF 数据平面：最高性能
   - CiliumNetworkPolicy：L4 + L7
   - Hubble：多租户可观测

2. 多租户隔离：
   - 命名空间级别 NetworkPolicy
   - 默认 deny-all
   - 跨命名空间显式允许
   - CiliumClusterNetworkPolicy：集群级策略
   
3. IPAM：
   - 每租户独立 IP 池（cluster-pool）
   - 路由聚合便于审计
   
4. 性能优化：
   - eBPF 替代 kube-proxy
   - eBPF 替代 iptables
   - Maglev 一致性哈希
   - DSR 模式
   
5. 可观测：
   - Hubble UI：流量拓扑
   - Hubble metrics：黄金信号
   - 流量日志：全 L7 元数据
   
6. 安全：
   - mTLS（Cilium + SPIRE）
   - 数据加密（WireGuard/IPsec）
   - 审计日志
```

### 题 6：解释 BGP 在 K8s 中的工作原理

```
1. BGP 基础：
   - AS（自治系统）：每个集群一个 AS 号（私有 64512-65534）
   - 邻居：节点间建立 TCP 179 连接
   - 路由：通告本节点 Pod CIDR
   
2. K8s 中的 BGP 拓扑：
   - 全互联（mesh）：N×(N-1)/2 个邻居，N<100
   - RR（Route Reflector）：中心节点反射，N>100
   - ToR：机顶交换机作 BGP 邻居
   
3. 工作流程：
   a. 节点启动，CNI daemon 运行 bird/frr
   b. 与配置的邻居建立 BGP 会话
   c. 通告本节点 Pod CIDR（如 10.244.1.0/24）
   d. 学习其他节点的 Pod CIDR
   e. 写入本机路由表
   
4. 数据路径：
   Pod A → Pod B（远端）
   - 路由表：10.244.2.0/24 via NodeB_IP
   - 直接转发到 NodeB_IP（物理网络）
   - NodeB 收到 → 路由到 Pod B
   
5. 优势 vs 劣势：
   - 优势：无封装，性能最优
   - 劣势：要求底层网络能路由 Pod CIDR
   
6. 适用场景：
   - 自建数据中心
   - 公有云裸金属
   - 不适用：AWS VPC 等不支持自定义路由的环境
```

------

## 15.12 故障复盘

### 案例 1：VXLAN MTU 设置错误导致包分片

**故障时间**：2023-11-12

**故障现象**：
- 跨节点 Pod 通信慢
- 大文件传输经常超时
- 小包正常

**根因**：
- 物理网络 MTU 1500
- VXLAN 模式默认 MTU 1500
- VXLAN 头 50 字节，导致包超长被分片
- 分片重组消耗大量 CPU

**修复**：
```bash
# 调小 VXLAN 接口 MTU
ip link set flannel.1 mtu 1450
# 或 CNI 配置中：
# mtu: 1450
```

**经验**：VXLAN 模式 MTU = 物理 MTU - 50；Geneve - 60；WireGuard - 80。

### 案例 2：Calico BGP 邻居未建立

**故障时间**：2024-02-08

**故障现象**：
- 新节点加入集群后，Pod 跨节点不通
- calicoctl node status 显示邻居 Idle

**根因**：
- 安全组未放通 TCP 179
- BGP 邻居无法建立

**修复**：
1. 在云控制台安全组入站规则添加：TCP 179 允许
2. 节点防火墙：`iptables -A INPUT -p tcp --dport 179 -j ACCEPT`

**经验**：BGP 部署前必须确认网络策略放通。

### 案例 3：Cilium eBPF 模式与 kube-proxy 冲突

**故障时间**：2024-04-15

**故障现象**：
- 集群从 Calico 迁移到 Cilium
- 部分 Service 不可达
- 流量时通时不通

**根因**：
- Cilium 启用了 kubeProxyReplacement=true
- 但 kube-proxy 未卸载
- 两者规则冲突

**修复**：
1. 立即卸载 kube-proxy：`kubectl delete ds kube-proxy -n kube-system`
2. 清理残留 iptables 规则：`iptables -F`（谨慎）
3. 重启 Cilium：`kubectl rollout restart ds cilium -n kube-system`

**经验**：Cilium eBPF 模式必须彻底替代 kube-proxy。

### 案例 4：NetworkPolicy 误封 DNS

**故障时间**：2024-06-30

**故障现象**：
- 部署 NetworkPolicy 后，Pod 无法解析域名
- 应用启动失败

**根因**：
- NetworkPolicy 默认 deny ingress + egress
- 未显式放通 kube-system DNS
- Pod 无法访问 CoreDNS

**修复**：
```yaml
# 允许 DNS 出站
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
```

**经验**：NetworkPolicy 部署前必须列出所有必需的出站规则。

### 案例 5：Pod CIDR 与节点网络冲突

**故障时间**：2024-09-20

**故障现象**：
- Pod 创建后无法访问外网
- 节点 SSH 偶发断连

**根因**：
- 集群 Pod CIDR: 10.0.0.0/16
- 节点所在 VPC: 10.0.0.0/8
- 路由冲突，部分流量走错路径

**修复**：
1. 重新规划 Pod CIDR（如 172.16.0.0/16）
2. 重新初始化 CNI

**经验**：Pod CIDR 必须与现有网络严格隔离。

------

## 15.13 参考与延伸

### 官方文档
- [CNI Specification](https://github.com/containernetworking/cni/blob/master/SPEC.md)
- [Cluster Networking](https://kubernetes.io/docs/concepts/cluster-administration/networking/)
- [Network Policies](https://kubernetes.io/docs/concepts/services-networking/network-policies/)
- [Dual-stack](https://kubernetes.io/docs/setup/production-environment/tools/kubeadm/dual-stack-support/)

### KEP
- [KEP-1645: Multi-Network](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/1645-multi-network)
- [KEP-3705: NetworkPolicy status](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/3705-network-policy-status)
- [KEP-3726: adminnetworkpolicy](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/3726-admin-network-policy)

### CNI 项目
- [Calico](https://docs.tigera.io/calico/latest/about)
- [Cilium](https://docs.cilium.io/)
- [Flannel](https://github.com/flannel-io/flannel)
- [Antrea](https://antrea.io/)
- [Weave Net](https://www.weave.works/docs/net/latest/overview/)
- [kube-router](https://www.kube-router.io/)
- [Multus](https://github.com/k8snetworkplumbingwg/multus-cni)
- [SR-IOV Network Device Plugin](https://github.com/k8snetworkplumbingwg/sriov-network-device-plugin)

### 源码导航
- `kubernetes/pkg/kubelet/dockershim/network/cni/` - kubelet CNI 调用
- `vendor/github.com/containernetworking/cni/` - CNI 库

### 相关章节
- [05-Service与网络.md](./05-Service与网络.md) - Service 模型
- [13-kubelet与Pod生命周期.md](./13-kubelet与Pod生命周期.md) - kubelet 调 CNI
- [14-kube-proxy与服务转发.md](./14-kube-proxy与服务转发.md) - Service 转发
- [16-CSI与存储编排.md](./16-CSI与存储编排.md) - 节点组件协同

### 推荐阅读
- [Kubernetes Networking Deep Dive](https://learnk8s.io/kubernetes-networking)
- [Cilium: The eBPF-based Networking](https://cilium.io/blog/)
- [Calico Architecture](https://docs.tigera.io/calico/latest/reference/architecture/overview)
- [BGP in Data Centers](https://docs.nvidia.com/networking-communication-devices/cumulus-linux-43/Network-Protocols/Border-Gateway-Protocol-BGP/)

### 工具
- `calicoctl` - Calico 命令行
- `cilium status` - Cilium 状态
- `hubble observe` - 流量观测
- `ip route` - 路由表
- `bridge fdb` - FDB 表
- `ip netns` - 网络命名空间
- `tcpdump` - 抓包

### 进阶主题
- **WireGuard**：现代 VPN，性能优于 IPsec
- **IPsec**：传统加密隧道
- **eBPF 程序开发**：自研数据平面
- **Multus + SR-IOV**：电信级网络
- **Cluster API + CNI**：集群生命周期
- **Submariner**：多集群 Pod 互通

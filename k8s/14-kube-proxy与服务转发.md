# 14. kube-proxy 与服务转发

> 关键词：kube-proxy、iptables、IPVS、eBPF、Conntrack、Session Affinity、DSR、Service Mesh

------

## 14.1 问题定义

K8s 中 Pod IP 是易变的（重启、扩缩容、节点故障），如何让上游稳定地访问一组 Pod？

**Service** 提供 **稳定的虚 IP（ClusterIP/NodePort/LoadBalancer）**，而 **kube-proxy** 负责把流量从 Service IP 转发到具体的 Pod IP。

**核心问题**：

> kube-proxy 如何用 **节点本地的转发规则** 实现"Service IP → Pod IP"的负载均衡，并在 Pod 变化时快速收敛？

------

## 14.2 直觉解释

把 kube-proxy 想象成每个节点的 **邮政分拣员**：

| 邮政分拣员 | kube-proxy |
|-----------|-----------|
| 收到信件看邮编 | 数据包到达看目的 IP |
| 按邮编分到具体邮箱 | 按规则改写目的 IP → Pod IP |
| 邮箱变更要更新分拣表 | Pod 增减要更新转发规则 |
| 不同分拣员有不同方式 | iptables / IPVS / eBPF |
| 信件一旦送出就不再管 | 连接建立后 conntrack 接管 |

关键点：**kube-proxy 不在数据路径上**（eBPF 模式除外），它只 **写转发规则**，真正的转发由内核 netfilter/iptables/IPVS 完成。

------

## 14.3 核心概念

### 14.3.1 kube-proxy 在数据路径中的位置

```
传统模式（iptables/IPVS）：
  Pod A → 内核协议栈 → iptables PREROUTING → DNAT → Pod B
                              ↑
                       规则由 kube-proxy 写入
                       
  kube-proxy 自己不参与转发！只控制规则

eBPF 模式（Cilium 等）：
  Pod A → TC eBPF → 直接改写 → Pod B
            ↑
     kube-proxy 替换为 eBPF 程序
```

### 14.3.2 三种模式对比

| 模式 | 实现机制 | 复杂度 | 性能 | 推荐场景 |
|------|---------|--------|------|---------|
| **iptables** | netfilter 规则链 | O(n) 链式匹配 | 中（千条规则后下降） | 小集群默认 |
| **IPVS** | 内核 L4 LB | O(1) 哈希 | 高 | 大集群（>1000 Service） |
| **eBPF** | TC/XDP 程序 | O(1) 哈希 | 最高 | Cilium/Calico eBPF |

### 14.3.3 iptables 模式工作流

```
1. kube-proxy Watch Service + Endpoints
2. Pod 变化时，更新 iptables 规则：
   - KUBE-SERVICES 链：匹配 ClusterIP:Port
   - KUBE-SVC-XXX 链：随机 DNAT 到 Endpoints
   - KUBE-SEP-XXX 链：具体 Endpoint DNAT
   - KUBE-NODEPORTS 链：NodePort 入口

3. 数据包流程：
   Pod A 访问 10.96.0.1:80
   → PREROUTING
   → KUBE-SERVICES（匹配 10.96.0.1:80）
   → KUBE-SVC-XXX（统计随机）
   → KUBE-SEP-XXX（DNAT 到 10.244.1.5:8080）
   → 转发到 Pod B
```

**iptables 规则示例**：

```bash
# Service 10.96.0.1:80 → 3 个 Endpoints
-A KUBE-SERVICES -d 10.96.0.1/32 -p tcp --dport 80 -j KUBE-SVC-NGINX

# KUBE-SVC-NGINX 链：随机到 3 个 SEP
-A KUBE-SVC-NGINX -m statistic --mode random --probability 0.333 -j KUBE-SEP-EP1
-A KUBE-SVC-NGINX -m statistic --mode random --probability 0.500 -j KUBE-SEP-EP2
-A KUBE-SVC-NGINX -j KUBE-SEP-EP3

# KUBE-SEP-EP1 链：DNAT
-A KUBE-SEP-EP1 -p tcp -j DNAT --to-destination 10.244.1.5:8080
```

### 14.3.4 IPVS 模式工作流

```
1. kube-proxy Watch Service + Endpoints
2. 调用 netlink 创建 IPVS 虚拟服务：
   - IPVS Service: 10.96.0.1:80 TCP
   - 添加 3 个 destination（10.244.1.5:8080 等）
   - 调度算法：rr / wrr / lc / sh（源地址哈希）

3. 数据包流程：
   Pod A → 10.96.0.1:80
   → PREROUTING（仅 1 条规则：跳到 IPVS）
   → IPVS 内核模块查找 service
   → 按 rr/sh 等算法选 destination
   → DNAT → Pod B
```

**IPVS 优势**：
- O(1) 查找（哈希表）
- 内置多种调度算法
- 支持 Session Affinity（sh 算法）
- 不需要为每个 Endpoint 写规则

### 14.3.5 eBPF 模式（Cilium）

```
1. kube-proxy 不再运行
2. Cilium 加载 eBPF 程序到 TC（Traffic Control）hook
3. 数据包流程：
   Pod A 出 → TC eBPF（egress）
   → 查 eBPF map：{service_ip → endpoint_list}
   → 直接 DNAT 到 Pod B
   → 绕过 iptables/qdisc
   
4. 优势：
   - 完全绕过 netfilter，性能最高
   - L7 可观测（HTTP/gRPC 元数据）
   - 替代 kube-proxy 整个组件
```

### 14.3.6 Conntrack（连接跟踪）

无论哪种模式，**连接建立后** 都由 conntrack 接管：

```
首次包：10.244.1.10 → 10.96.0.1:80
  → DNAT: 10.244.1.10 → 10.244.1.5:8080
  → conntrack 记录：
    src=10.244.1.10:54321
    dst=10.96.0.1:80
    new_dst=10.244.1.5:8080
    
后续包（同一连接）：
  → conntrack 直接 DNAT（不重走 iptables 全链）
  → 直到连接结束（FIN/RST）+ 时间窗口过后清理

conntrack 表大小：
  - 默认 65536（小集群）
  - 生产推荐 262144~1048576
  - /proc/sys/net/netfilter/nf_conntrack_max
```

### 14.3.7 Session Affinity

```
spec.sessionAffinity: ClientIP
spec.sessionAffinityConfig.clientIP.timeoutSeconds: 10800（默认 3 小时）

实现：
  iptables: statistic 模块 + recent 模块
  IPVS:     sh（source hashing）调度算法
  eBPF:     客户端 IP 哈希到固定 Endpoint

用途：
  - 有状态应用（如 Redis 集群、会话保持）
  - 不推荐用于微服务，应让应用无状态
```

### 14.3.8 内部流量策略

```yaml
spec:
  internalTrafficPolicy: Cluster  # 默认（全集群转发）
                              # Local（仅本节点 Pod）
  
spec:
  externalTrafficPolicy: Cluster  # 默认（SNAT 转发）
                              # Local（保留源 IP）
```

**Cluster vs Local**：

| 模式 | 内部流量 | 外部流量 | 源 IP | 性能 |
|------|---------|---------|-------|------|
| Cluster | 全集群 | 全集群+SNAT | 丢失（SNAT） | 中 |
| Local（内部） | 仅本节点 | - | 保留 | 高 |
| Local（外部） | - | 仅本节点 Pod | 保留 | 高（无 SNAT） |

------

## 14.4 操作流程

### 14.4.1 Endpoints 更新时序

```
T0: Pod 添加/删除 → APIServer 更新 Endpoints
T1: kube-proxy Watch 到 Endpoints 变化
T2: kube-proxy 计算新规则
T3: 调用 iptables-restore / ipvsadm / netlink 更新内核
T4: 新规则立即生效
T5: 后续连接按新规则转发
   - 已有连接保持原状（conntrack 维持）
   - 新连接按新规则
```

### 14.4.2 数据包从 Pod A 到 Pod B 的完整路径

```
Pod A (10.244.1.10:54321) → Service (10.96.0.1:80) → Pod B (10.244.2.5:8080)

1. Pod A 发出包：src=10.244.1.10, dst=10.96.0.1
2. veth → 主机内核
3. PREROUTING（iptables）
   → KUBE-SERVICES 命中
   → KUBE-SVC-XXX
   → KUBE-SEP-XXX DNAT: dst=10.244.2.5:8080
4. 路由决策：10.244.2.0/24 → CNI 接口
5. POSTROUTING
   → MASQUERADE（如果跨节点且未启用保留源 IP）
6. CNI 隧道（VXLAN/Geneve）或 BGP 路由
7. Pod B 节点收到 → 解封装 → veth → Pod B
8. Pod B 响应：src=10.244.2.5, dst=10.244.1.10
9. conntrack 反向 NAT：dst=10.96.0.1 → dst=10.244.1.10
10. 回到 Pod A
```

### 14.4.3 NodePort 流量路径

```
外部客户端 → NodeIP:30080

1. 入口：Node 网卡收到 dst=NodeIP,dport=30080
2. PREROUTING → KUBE-NODEPORTS 命中
3. DNAT 到 Pod IP（Cluster 模式可能跨节点）
4. POSTROUTING: MASQUERADE（保留 NodeIP 源）
5. 转发到 Pod

externalTrafficPolicy=Local 模式：
  - 仅 DNAT 到本节点 Pod
  - 不 SNAT，保留客户端真实 IP
  - 若本节点无 Pod → 流量被丢弃
```

------

## 14.5 底层原理

### 14.5.1 iptables 模式性能特性

**规则数量爆炸**：

```
每个 Service 需要的规则数：
  - 1 条 KUBE-SERVICES 入口
  - 1 条 KUBE-SVC 链
  - N 条 KUBE-SEP 链（N = Endpoints 数）
  - 每条 SEP 包含 DNAT + MASQUERADE
  
总规则数 ≈ 4 × Service 数 × Endpoints 数

例：5000 Service × 平均 5 Endpoints = 100000 条规则
```

**性能问题**：
- iptables 规则 **链式顺序匹配**，O(n)
- 大量规则导致首个包延迟显著
- 规则更新（iptables-restore）需重写全表，秒级阻塞

**优化**：
- KUBE-SERVICES 链按 Service IP 排序（二分查找？不，仍是顺序）
- 实际靠 conntrack 后续包不重走规则

### 14.5.2 IPVS 模式优势

| 维度 | iptables | IPVS |
|------|---------|------|
| 数据结构 | 顺序链表 | 哈希表 |
| 查找复杂度 | O(n) | O(1) |
| 调度算法 | random（statistic） | rr/wrr/lc/sh/sed/dh 等 |
| 规则数量 | 线性增长 | 与 Endpoints 数无关 |
| 规则更新 | 重写全表 | 增删单条 |
| Session Affinity | recent 模块（弱） | sh 算法（强） |

**IPVS 调度算法**：
- `rr`（round-robin）：轮询
- `wrr`（weighted rr）：按权重轮询
- `lc`（least-connection）：最少连接
- `sh`（source hashing）：源 IP 哈希，会话保持
- `sed`（shortest expected delay）：最短期望延迟
- `dh`（destination hashing）：目标哈希

### 14.5.3 Conntrack 表深入

```
conntrack 条目结构：
  src=10.244.1.10:54321  dst=10.96.0.1:80
  src=10.244.1.10:54321  dst=10.244.2.5:8080  [NAT 后]
  state=ESTABLISHED
  timeout=432000s（TCP established 默认 5 天）

表满后果：
  - 新连接无法建立
  - 日志：nf_conntrack: table full, dropping packet
  - 现象：Pod 间歇性连接失败

调优：
  sysctl net.netfilter.nf_conntrack_max=1048576
  sysctl net.netfilter.nf_conntrack_tcp_timeout_established=86400
  sysctl net.netfilter.nf_conntrack_buckets=262144（hash 桶数）
```

### 14.5.4 eBPF 模式核心思想

```c
// 简化版 eBPF 数据路径
SEC("tc/egress")
int handle_egress(struct __sk_buff *skb) {
    // 解析包头
    struct iphdr iph;
    read_iphdr(skb, &iph);
    
    // 查 Service map
    __u32 svc_id = bpf_map_lookup_elem(&svc_map, &iph.daddr);
    if (!svc_id) return TC_ACT_OK;
    
    // 查 Endpoints map（按一致性哈希）
    __u32 ep = bpf_map_lookup_elem(&ep_map, &svc_id);
    
    // DNAT
    iph.daddr = ep_addr;
    rewrite_skb(skb);
    
    // 直接转发（绕过 iptables）
    return TC_ACT_REDIRECT;
}
```

**eBPF 优势**：
1. **绕过 netfilter**：跳过 iptables 全链匹配
2. **L7 可观测**：解析 HTTP/gRPC，提取元数据
3. **无 kube-proxy**：少一个组件，故障域更小
4. **DSR（Direct Server Return）**：响应包不经节点

### 14.5.5 拓扑感知路由（Topology Aware Hints）

K8s 1.21+ 引入，1.23 默认开启：

```
传统：ClusterIP → 全集群 Endpoints
拓扑感知：ClusterIP → 优先同区域 Endpoints

Endpoints Slice 添加 hints：
  endpoints:
  - addresses: [10.244.1.5]
    zone: us-east-1a
    hints:
      forZones: [{name: us-east-1a}]
  - addresses: [10.244.2.5]
    zone: us-east-1b
    hints:
      forZones: [{name: us-east-1b}]

kube-proxy 只把流量转发到本 zone Endpoint
  - 节省跨 zone 流量费
  - 降低延迟（跨 zone 可能 1-5ms）
  - 故障时降级到全集群
```

### 14.5.6 Session Affinity 的复杂性

```
iptables 模式：
  - 用 recent 模块记录客户端 IP
  - 一定时间窗口内固定到同一 SEP
  - 节点本地状态，跨节点不一致
  
IPVS 模式：
  - sh 算法：源 IP 哈希
  - 全集群一致（数学哈希）
  - 但 Endpoint 增减时哈希结果变化
  
eBPF 模式：
  - 一致性哈希（maglev）
  - Endpoint 增减影响最小
  - 全集群一致
```

------

## 14.6 配置示例

### 14.6.1 kube-proxy 启动配置（IPVS 模式）

```bash
/usr/bin/kube-proxy \
  --config=/var/lib/kube-proxy/config.conf \
  --v=2
```

```yaml
# /var/lib/kube-proxy/config.conf
apiVersion: kubeproxy.config.k8s.io/v1alpha1
kind: KubeProxyConfiguration
mode: "ipvs"   # iptables / ipvs / eBPF

# IPVS 调度算法
ipvs:
  scheduler: "rr"   # rr/wrr/lc/sh/sed/dh
  excludeCIDRs: []
  minSyncPeriod: 1s
  syncPeriod: 30s
  strictARP: true   # MetalLB 必须开启
  tcpTimeout: 0s
  tcpFinTimeout: 0s
  udpTimeout: 0s

# 连接规范
conntrack:
  maxPerCore: 32768        # 每核 conntrack 数
  min: 131072              # 全节点最小
  tcpCloseWaitTimeout: 1h0m0s
  tcpEstablishedTimeout: 8h0m0s

# 集群 CIDR
clusterCIDR: "10.244.0.0/16"

# 客户端配置
clientConnection:
  kubeconfig: /var/lib/kube-proxy/kubeconfig.conf
  qps: 100
  burst: 200

# 节点端口范围
nodePortAddresses: []
portRange: 30000-32767

# 拓扑感知
featureGates:
  TopologyAwareHints: true
```

### 14.6.2 IPVS 模式内核模块加载

```bash
# 必须加载的内核模块
modprobe -- ip_vs
modprobe -- ip_vs_rr
modprobe -- ip_vs_wrr
modprobe -- ip_vs_sh
modprobe -- ip_vs_sed
modprobe -- ip_vs_lc
modprobe -- nf_conntrack

# /etc/modules-load.d/ipvs.conf
cat > /etc/modules-load.d/ipvs.conf <<EOF
ip_vs
ip_vs_rr
ip_vs_wrr
ip_vs_sh
nf_conntrack
EOF

# 验证
lsmod | grep -e ip_vs -e nf_conntrack
```

### 14.6.3 iptables 模式调优

```bash
# 1. 调大 conntrack 表
sysctl -w net.netfilter.nf_conntrack_max=1048576
sysctl -w net.netfilter.nf_conntrack_buckets=262144
sysctl -w net.netfilter.nf_conntrack_tcp_timeout_established=86400

# 2. 调大 iptables 链表
sysctl -w net.netfilter.nf_conntrack_acct=0  # 关闭统计省内存

# 3. /etc/sysctl.d/99-k8s.conf
cat > /etc/sysctl.d/99-k8s.conf <<EOF
net.netfilter.nf_conntrack_max = 1048576
net.netfilter.nf_conntrack_buckets = 262144
net.netfilter.nf_conntrack_tcp_timeout_established = 86400
net.netfilter.nf_conntrack_tcp_timeout_close_wait = 3600
net.ipv4.ip_forward = 1
net.bridge.bridge-nf-call-iptables = 1
EOF

sysctl --system
```

### 14.6.4 Service 配置示例

```yaml
# 1. 普通 ClusterIP
apiVersion: v1
kind: Service
metadata:
  name: nginx-clusterip
spec:
  type: ClusterIP
  selector:
    app: nginx
  ports:
  - port: 80
    targetPort: 8080

---
# 2. Session Affinity
apiVersion: v1
kind: Service
metadata:
  name: nginx-affinity
spec:
  type: ClusterIP
  sessionAffinity: ClientIP
  sessionAffinityConfig:
    clientIP:
      timeoutSeconds: 3600
  selector:
    app: nginx
  ports:
  - port: 80
    targetPort: 8080

---
# 3. NodePort 保留源 IP
apiVersion: v1
kind: Service
metadata:
  name: nginx-nodeport-local
spec:
  type: NodePort
  externalTrafficPolicy: Local   # 保留客户端 IP
  selector:
    app: nginx
  ports:
  - port: 80
    targetPort: 8080
    nodePort: 30080

---
# 4. Topology Aware
apiVersion: v1
kind: Service
metadata:
  name: nginx-topology
spec:
  type: ClusterIP
  trafficDistribution: PreferClose   # K8s 1.30+
  selector:
    app: nginx
  ports:
  - port: 80
    targetPort: 8080
```

### 14.6.5 Cilium eBPF 模式（替代 kube-proxy）

```yaml
# Cilium Helm values
kubeProxyReplacement: true   # 完全替代 kube-proxy
enableIPv4: true
enableIPv6: false

# L7 可观测
hubble:
  enabled: true
  relay:
    enabled: true
  ui:
    enabled: true

# DSR 模式
loadBalancer:
  algorithm: maglev
  mode: dsr          # direct server return
  dsrDispatch: ipip  # ipip / geneve

# 加速
bpf:
  masquerade: true
  clock_probe: true
  tproxy: true
```

------

## 14.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | 大集群用 iptables 模式 | 规则数万，性能急剧下降 | 切换 IPVS |
| 2 | IPVS 模式未加载内核模块 | Service 不通 | modprobe ip_vs 等 |
| 3 | conntrack 表满 | 间歇性连接失败 | 调大 nf_conntrack_max |
| 4 | externalTrafficPolicy=Local 但本节点无 Pod | 流量被丢弃 | 确保 NodePort Pod 多副本分布 |
| 5 | strictARP 未开启 | MetalLB 不工作 | kube-proxy ipvs.strictARP=true |
| 6 | Session Affinity 期望跨节点一致 | iptables 模式仅节点本地 | 用 IPVS sh 算法 |
| 7 | 删除 Service 后规则残留 | 旧 ClusterIP 仍可访问 | 重启 kube-proxy 或等下次 sync |
| 8 | NodePort 范围配置错误 | 端口冲突 | port-range 与 nodePortAddresses 一致 |
| 9 | UDP Service 不释放端口 | conntrack 长时间保留 UDP | UDP timeout 调小 |
| 10 | 跨节点 SNAT 后源 IP 丢失 | 安全审计困难 | externalTrafficPolicy=Local |
| 11 | Service Mesh 与 kube-proxy 双重 NAT | 性能损失 | mesh 模式透明接管 |
| 12 | Endpoints 大量抖动 | kube-proxy CPU 高 | 用 readinessProbe 减少 EP 变化 |
| 13 | iptables-restore 阻塞 | 转发延迟尖刺 | 切 IPVS 或 eBPF |
| 14 | Topology Aware 跨 zone 降级时无感知 | 突发跨 zone 流量 | 监控 topology aware 命中率 |
| 15 | Pod 重启太快 conntrack 残留 | 旧连接复用到新 Pod | conntrack 同步清理（很难，需重启） |

------

## 14.8 工业案例

### 14.8.1 阿里 ACK：从 iptables 到 IPVS 到 eBPF

**阶段 1（2017-2019）**：iptables
- 集群 1000 节点，3000 Service，规则数 30 万+
- kube-proxy CPU 30%+
- Pod 首包延迟 P99 50ms

**阶段 2（2019-2021）**：IPVS
- 切换 IPVS 模式
- kube-proxy CPU 降至 5%
- 首包延迟 P99 5ms

**阶段 3（2021-至今）**：eBPF（Cilium）
- 完全替代 kube-proxy
- kube-proxy 进程消失
- 首包延迟 P99 1ms
- L7 可观测能力增强

**关键收益**：每节点 kube-proxy 与 conntrack 维护成本降低 80%。

### 14.8.2 字节跳动：大规模 Service 性能优化

**场景**：5000 节点，20000 Service，单 Service 平均 50 Endpoints。

**问题**：
- iptables 规则总数 400 万
- Endpoints 变化时 iptables-restore 耗时 2s
- 期间转发规则不生效，连接失败

**方案**：
1. 切换 IPVS + rr 算法
2. 端点切片（EndpointSlice）替代 Endpoints，减少 Watch 数据量
3. 开发自研 eBPF 加速器（基于 Cilium）
4. 拓扑感知路由，降低跨可用区流量 60%

**结果**：Service 转发性能提升 10 倍，规则更新从秒级到毫秒级。

### 14.8.3 Google GKE：拓扑感知路由部署

**场景**：多 region 集群，跨 region 流量费高。

**方案**：
1. 升级到 K8s 1.23，默认开启 Topology Aware Hints
2. EndpointSlice Controller 自动添加 hints
3. kube-proxy 仅转发到本 region Endpoints
4. region 故障时降级到全集群

**结果**：跨 region 流量降低 70%，月度网络费用节省 200 万美元。

### 14.8.4 Netflix：conntrack 表满事故

**故障时间**：2023-08-15 02:00

**故障现象**：
- 大量 Pod 间歇性连接失败
- 日志：`nf_conntrack: table full, dropping packet`
- 业务监控大量 5xx

**根因**：
- 集群规模翻倍，conntrack 表未调优
- 默认 65536 远不够（实际需要 500000+）
- 连接短时大量建立（每秒 10 万+）

**修复**：
```bash
sysctl -w net.netfilter.nf_conntrack_max=1048576
sysctl -w net.netfilter.nf_conntrack_buckets=262144
```

**经验**：
1. 生产集群 conntrack max 必须按节点 Pod 数 × 平均连接数 × 2 估算
2. 监控 conntrack 使用率（>70% 告警）

### 14.8.5 AWS EKS：strictARP 与 MetalLB 冲突

**场景**：自建 K8s 集群用 MetalLB 实现 LoadBalancer，IPVS 模式。

**故障**：
- LoadBalancer IP 不通
- ARP 表学习失败
- 客户端无法访问 LB IP

**根因**：
- IPVS 模式下，默认 strictARP=false
- 节点对 Service IP 的 ARP 请求不响应
- MetalLB 依赖 ARP 响应

**修复**：
```yaml
ipvs:
  strictARP: true
```

------

## 14.9 与其他方案关系

### 14.9.1 kube-proxy vs LVS（Linux Virtual Server）

| 维度 | kube-proxy | LVS |
|------|-----------|-----|
| 底层 | IPVS（可选） | IPVS（原生） |
| 部署 | 每节点 DaemonSet | 集中 LB 节点 |
| 配置来源 | APIServer Watch | 手动/Keepalived |
| 后端变化 | 自动同步 | Keepalived 同步 |
| 入口 | ClusterIP/NodePort | VIP |
| 适用 | 集群内部 | 集群入口 |

**关系**：kube-proxy 用 IPVS 时与 LVS 同源，但场景不同（内部 vs 入口）。

### 14.9.2 kube-proxy vs Envoy/Istio

| 维度 | kube-proxy | Envoy (Istio) |
|------|-----------|---------------|
| 工作层 | L4 | L4 + L7 |
| 部署 | 节点级 | Pod 级（sidecar） |
| 配置 | iptables/IPVS 规则 | xDS API |
| 协议 | TCP/UDP/SCTP | HTTP/gRPC/TCP 等 |
| 路由 | 简单 RR | 复杂路由规则 |
| 可观测 | 弱 | 强（黄金信号） |

**关系**：Service Mesh 在 kube-proxy 之上提供 L7 能力。Istio 用 iptables 把 Pod 流量劫持到 Envoy，绕过 kube-proxy。

### 14.9.3 kube-proxy 与云厂商 LB

```
用户 → 云 LB（外网 IP） → Node:NodePort → kube-proxy → Pod

云 LB 后端：
  - 各节点 IP:NodePort
  - 健康检查端口
  
kube-proxy 在节点上负责：
  - NodePort 到 Pod 的转发
  - externalTrafficPolicy 决定是否 SNAT

云厂商优化：
  - AWS: NLB 直连 Pod（绕过 kube-proxy）
  - GCP: ILB 直连 Pod
  - 阿里: CLB 直连 Pod（CLB + Pod EIP）
```

### 14.9.4 与 Calico/Cilium eBPF 数据平面

| 模式 | kube-proxy | CNI 数据平面 |
|------|-----------|-------------|
| 传统 | iptables/IPVS | Calico iptables / Cilium iptables |
| eBPF | 仍可用 | Cilium eBPF（替代 kube-proxy） |
| 混合 | iptables | Calico eBPF（部分功能） |

**趋势**：Cilium eBPF 一统天下，kube-proxy 在 eBPF 集群中消失。

------

## 14.10 面试速答

**Q1: kube-proxy 三种模式？怎么选？**

- iptables：小集群默认，简单稳定
- IPVS：大集群（>1000 Service），性能好
- eBPF（Cilium）：现代方案，性能最优，绕过 netfilter

**Q2: 为什么 IPVS 比 iptables 快？**

iptables 规则链式顺序匹配 O(n)，IPVS 哈希表查找 O(1)。iptables 规则数随 Endpoints 线性增长，IPVS 与 Endpoints 数无关。

**Q3: conntrack 表满会怎样？**

新连接被丢弃，日志 `nf_conntrack: table full, dropping packet`。解决：调大 nf_conntrack_max，监控使用率。

**Q4: externalTrafficPolicy=Cluster vs Local 区别？**

- Cluster：流量可跨节点，SNAT 后源 IP 丢失
- Local：仅本节点 Pod，保留客户端 IP，本节点无 Pod 时流量丢弃

**Q5: 删除 Service 后 ClusterIP 还能访问一会，为什么？**

conntrack 表保留已有连接，直到 timeout 才清理。新连接无法建立，但已建连接维持。

**Q6: Topology Aware Hints 是什么？**

K8s 1.21+ 特性，EndpointSlice 添加 zone hints，kube-proxy 优先转发到本 zone Endpoints，降低跨 zone 流量。

**Q7: kube-proxy 自己会转发流量吗？**

iptables/IPVS 模式不会，它只写内核规则，转发由 netfilter/IPVS 内核模块完成。eBPF 模式直接在 TC hook 处理，也不经过 kube-proxy 进程。

**Q8: Session Affinity 怎么实现？**

- iptables: recent 模块记录客户端 IP
- IPVS: sh（source hashing）调度算法
- eBPF: 一致性哈希（maglev）

**Q9: UDP Service 有什么坑？**

UDP 无连接，conntrack 难以判断结束，超时时间默认 30s。大量 UDP 流量可能撑爆 conntrack 表。建议调小 udp_timeout。

**Q10: 切换 iptables 到 IPVS 需要做什么？**

1. 加载 IPVS 内核模块（ip_vs / ip_vs_rr / nf_conntrack）
2. kube-proxy mode 改为 ipvs
3. 重启 kube-proxy DaemonSet
4. 验证：ipvsadm -Ln

------

## 14.11 综合面试题

### 题 1：设计一个 10000 Service + 50000 Endpoints 的集群网络方案

```
1. 数据平面：
   - Cilium eBPF 模式（绕过 kube-proxy）
   - 或 IPVS 模式（次优）
   
2. 控制平面：
   - EndpointSlice 替代 Endpoints（必需）
   - kube-proxy kubeAPIQPS 提升到 100+
   - 拓扑感知路由

3. 调优：
   - conntrack max: 1048576
   - conntrack buckets: 262144
   - kube-proxy syncPeriod: 30s（不要太频繁）

4. 监控：
   - kube-proxy 规则同步延迟
   - conntrack 使用率
   - EndpointSlice 数量
   - Service 转发延迟 P99

5. 故障预案：
   - 单节点 kube-proxy 卡死：Pod 化部署 + 健康检查
   - conntrack 满：紧急 sysctl 调大
   - APIServer 慢：kube-proxy 用本地 cache
```

### 题 2：Pod A 访问 Pod B 不通，怎么排查？

```
分层排查：
1. Pod 状态：
   kubectl get pod -o wide
   - 都 Running 吗？
   - 在哪个节点？
   
2. Service 状态：
   kubectl get svc
   kubectl get endpoints <svc>
   - Endpoints 是否有 Pod IP？
   - readinessProbe 是否通过？
   
3. 网络规则：
   iptables -L KUBE-SERVICES -n | grep <clusterIP>
   ipvsadm -Ln | grep <clusterIP>
   - 规则是否生成？
   
4. 节点路由：
   ip route get <podB_ip>
   - 路由表是否正确？
   
5. CNI 状态：
   - Calico: calicoctl node status
   - Cilium: cilium status
   
6. 抓包：
   tcpdump -i any host <podB_ip> -nn
   - 包发出去了吗？
   - 包回来了吗？
   
7. conntrack：
   conntrack -L | grep <podB_ip>
   - 是否有连接条目？
```

### 题 3：解释 Pod 间通信的完整数据路径（同节点 + 跨节点）

```
同节点：
1. Pod A veth → 主机内核
2. PREROUTING → KUBE-SERVICES（如果访问 Service）
3. DNAT → Pod B IP
4. 路由：直连（同节点 cni0）
5. veth → Pod B

跨节点（VXLAN 模式）：
1-3. 同上
4. 路由：远端 Pod IP → vxlan 接口
5. 封装 VXLAN（外层 UDP 目的 8472）
6. 物理网络转发到目标节点
7. 目标节点 vxlan 解封装
8. veth → Pod B

跨节点（BGP 模式，Calico）：
1-4. 同上但路由是物理网关
5. 主机路由 → 网关
6. 网关按 BGP 路由转发到目标节点
7. veth → Pod B
```

### 题 4：如何保留客户端真实 IP？

```
方案 1：externalTrafficPolicy=Local
  - 优点：简单
  - 缺点：本节点无 Pod 时流量丢弃
  
方案 2：PROXY Protocol
  - LB（HAProxy/Cloud LB）发送 PROXY Protocol 头
  - 应用读取真实 IP
  - 需要应用支持
  
方案 3：HTTP X-Forwarded-For
  - Ingress/网关添加 XFF 头
  - 应用读 XFF
  - 仅 HTTP

方案 4：DSR（Direct Server Return）
  - 响应包不经 LB，从 Pod 直接发到客户端
  - LB 仅处理请求方向
  - Cilium/Calico 支持

生产推荐：
  - 对外服务：externalTrafficPolicy=Local + 多节点 Pod
  - 内部服务：无需保留（默认 SNAT）
```

### 题 5：解释 conntrack 的 timeout 机制

```
conntrack 状态机：
  NEW          → SYN 包，新建条目，timeout 30s
  ESTABLISHED  → 双向通信，timeout 5 天（默认 432000s）
  RELATED      → FTP 数据连接等，timeout 300s
  FIN_WAIT     → FIN 包，timeout 120s
  CLOSE_WAIT   → 单向关闭，timeout 60s
  TIME_WAIT    → 双向关闭，timeout 120s

UDP：
  默认 30s（流量结束后）

ICMP：
  默认 30s

调优：
  - 长连接应用（DB）：established timeout 调大
  - 短连接 Web：close wait 调小
  - UDP：udp_timeout 调小避免表爆

监控：
  /proc/net/nf_conntrack_count
  /proc/net/nf_conntrack_max
  Prometheus: nf_conntrack_entries_limit
```

### 题 6：为什么 Service Mesh 会绕过 kube-proxy？

```
传统 kube-proxy 模式：
  Pod A → Service IP → kube-proxy 规则 → Pod B
  缺点：仅 L4，无 HTTP 路由、重试、熔断

Service Mesh（Istio）：
  Pod A → iptables REDIRECT → Envoy(sidecar) → Pod B
  Envoy 直接做 L7 路由，不查 kube-proxy 规则

优势：
  - HTTP 路由（按 path/header）
  - 重试、超时、熔断
  - 金丝雀发布
  - mTLS
  - 全链路追踪

代价：
  - 每个 Pod 一个 Envoy（资源消耗）
  - 多一跳（Pod → Envoy → Pod）
  - 复杂度高
```

------

## 14.12 故障复盘

### 案例 1：iptables 模式下规则爆炸

**故障时间**：2023-09-10

**故障现象**：
- 集群 800 Service，4000 Endpoints
- iptables 规则数 12 万
- 新 Service 创建后 30s 才生效
- Pod 首包延迟 P99 200ms

**根因**：
- 大集群仍用 iptables 模式
- iptables-restore 全表重写
- 规则匹配 O(n) 性能差

**修复**：
1. 切换 IPVS 模式（modprobe + kube-proxy 配置）
2. 规则数从 12 万降到 2000（仅 KUBE-SERVICES 链）
3. 首包延迟 P99 降到 5ms

**经验**：集群 >500 Service 即应考虑 IPVS。

### 案例 2：跨节点连接偶发失败

**故障时间**：2024-03-15

**故障现象**：
- 跨节点 Pod 通信偶发超时
- 同节点正常
- conntrack 表使用率 95%

**根因**：
- 节点 conntrack max 65536（默认）
- 实际连接数 60000+
- 表满后丢包

**修复**：
```bash
sysctl -w net.netfilter.nf_conntrack_max=524288
sysctl -w net.netfilter.nf_conntrack_buckets=131072
```

**经验**：conntrack 容量按节点 Pod 数 × 平均连接数 × 2 规划。

### 案例 3：externalTrafficPolicy=Local 流量黑洞

**故障时间**：2024-05-20

**故障现象**：
- NodePort 30080 偶发不可达
- 部分节点返回 RST

**根因**：
- externalTrafficPolicy=Local
- 部分 NodePort 节点上没有 Pod（调度不均）
- 流量到这些节点后被丢弃

**修复**：
1. 短期：调 DaemonSet 模式确保所有节点有 Pod
2. 长期：改用 LoadBalancer + externalTrafficPolicy=Cluster（牺牲源 IP）

**经验**：externalTrafficPolicy=Local 必须确保 Pod 跨节点分布。

### 案例 4：UDP Service conntrack 残留

**故障时间**：2024-07-01

**故障现象**：
- DNS（UDP）查询偶发失败
- 偶发解析到旧 Pod IP

**根因**：
- CoreDNS 滚动更新，Pod IP 变化
- conntrack UDP 条目 30s 才过期
- 期间 DNS 查询仍转发到旧 IP

**修复**：
1. CoreDNS 滚动更新前先缩容到 0
2. 调小 UDP conntrack timeout
3. NodeLocal DNSCache 减少跨节点 UDP

**经验**：UDP Service 滚动更新需特别处理。

------

## 14.13 参考与延伸

### 官方文档
- [kube-proxy](https://kubernetes.io/docs/reference/command-line-tools-reference/kube-proxy/)
- [Service](https://kubernetes.io/docs/concepts/services-networking/service/)
- [EndpointSlices](https://kubernetes.io/docs/concepts/services-networking/endpoint-slices/)
- [Topology Aware Hints](https://kubernetes.io/docs/concepts/services-networking/topology-aware-hints/)

### KEP
- [KEP-1669: Proxy Termination Endpoints](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/1669-proxy-protocol-endpoint)
- [KEP-2433: Topology Aware Hints](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/2433-topology-aware-hints)
- [KEP-3135: kubeproxy eBPF Support](https://github.com/kubernetes/enhancements/tree/master/keps/sig-network/3135-kubeproxy-ebpf-support)

### 源码导航
- `kubernetes/pkg/proxy/iptables/` - iptables 模式
- `kubernetes/pkg/proxy/ipvs/` - IPVS 模式
- `kubernetes/pkg/proxy/winuserspace/` - Windows 模式
- `kubernetes/pkg/proxy/endpointslicemapper.go` - EndpointSlice 转换

### 相关章节
- [05-Service与网络.md](./05-Service与网络.md) - Service 模型
- [13-kubelet与Pod生命周期.md](./13-kubelet与Pod生命周期.md) - 节点搭档
- [15-CNI与网络模型.md](./15-CNI与网络模型.md) - Pod 网络底层
- [11-滚动更新与发布策略.md](./11-滚动更新与发布策略.md) - Service 配合滚动
- [16-CSI与存储编排.md](./16-CSI与存储编排.md) - 节点组件协同

### 推荐阅读
- [Cilium: kube-proxy replacement](https://docs.cilium.io/en/stable/network/kubernetes/kubeproxy-free/)
- [IPVS: How it works](https://github.com/kubernetes/kubernetes/blob/master/pkg/proxy/ipvs/README.md)
- [Kubernetes networking deep dive](https://learnk8s.io/kubernetes-networking)
- [kube-proxy modes comparison](https://www.tkng.io/services/kube-proxy/)

### 工具
- `iptables -L -n -v` - 查看规则
- `ipvsadm -Ln --stats` - 查看 IPVS 服务
- `conntrack -L` - 查看连接跟踪
- `crictl` - 容器运行时调试
- `cilium status` - Cilium 状态

### 进阶主题
- **DSR（Direct Server Return）**：响应绕过 LB
- **Maglev 一致性哈希**：Google 内部 LB 算法
- **PROXY Protocol v2**：传递客户端信息
- **Kubernetes Gateway API**：下一代 Ingress
- **Dual-stack Service**：IPv4 + IPv6 同时支持
- **SCTP Service**：信令传输协议支持

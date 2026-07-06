# 19 - eBPF 与可编程数据面

> eBPF 是 Linux 内核的可编程层: 无需修改内核即可注入逻辑, 在网络/可观测/安全/性能四大领域实现"内核级"定制。本章梳理 eBPF 原理、Cilium 网络、Tetragon 安全、Hubble 可观测、XDP 高性能网络, 以及在 LLM 推理集群的 GPU 调度追踪与网络瓶颈定位实践。

---

## 一、思维导图

```
                eBPF 与可编程数据面
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   ┌─────────┐     ┌─────────┐     ┌─────────┐
   │ 网络    │     │ 可观测  │     │ 安全    │
   │ Cilium  │     │ Hubble  │     │ Tetragon│
   │ XDP     │     │ Pixie   │     │ Falco   │
   │ TC      │     │ bpftrace│     │ Tracee  │
   └─────────┘ └─────────┘     └─────────┘
```

---

## 二、问题定义与边界

### 2.1 解决什么

- **网络可编程**: 内核数据包处理, 无需修改内核
- **可观测性**: 内核级追踪, 系统调用/网络/文件
- **安全审计**: 实时检测异常, 系统调用拦截
- **性能分析**: 函数级追踪, 火焰图

### 2.2 不解决什么

- 不覆盖 Service Mesh 全景（20 章）
- 不深入 Linux 内核（操作系统课程）
- 不覆盖可观测性架构（infra开发 模块）

---

## 三、直觉解释

### 3.1 为什么需要 eBPF

```
传统方式修改内核行为:
  - 修改内核源码 + 编译 + 重启: 不可行
  - 写内核模块 (kmod): 危险 (bug 直接 panic)
  - 内核静态 tracepoint: 不灵活

eBPF:
  - 用户写 BPF 程序 (C/Go/Rust)
  - JIT 编译为本地代码
  - 注入内核 hook 点 (kprobe/tracepoint/XDP)
  - 安全验证 (verifier), 不会 panic
  - 动态加载/卸载, 无需重启

类比:
  - JavaScript 之于浏览器
  - eBPF 之于 Linux 内核
```

### 3.2 eBPF Hook 点

```
eBPF 程序可挂载的内核点:

网络 (XDP/TC):
  - XDP: 网卡驱动层, 数据包最早入口
  - TC (Traffic Control): 流量控制
  - socket: 应用层 socket

追踪 (kprobes/tracepoints):
  - kprobes: 任意内核函数入口
  - tracepoints: 内核预定义追踪点
  - perf events: 性能事件

安全 (LSM):
  - Linux Security Module hook
  - 文件/网络/进程操作前拦截

cgroup:
  - cgroup 级别资源控制
```

### 3.3 eBPF 应用领域

| 领域 | 工具 | 场景 |
|------|------|------|
| 网络 | Cilium, Calico-eBPF | K8s CNI, Service Mesh |
| 可观测 | Hubble, Pixie, bpftrace | 流量追踪, 性能分析 |
| 安全 | Tetragon, Falco, Tracee | 异常检测, 系统调用审计 |
| 性能 | bcc, bpftrace | 函数级追踪, 火焰图 |
| 调度 | BPF scheduler | 自定义调度策略 |

---

## 四、核心概念与架构

### 4.1 eBPF 工作流

```
1. 编写 BPF 程序 (C/Rust)
   ↓
2. 编译为 BPF 字节码 (LLVM)
   ↓
3. 用户态程序加载 (libbpf/ cilium-ebpf-go)
   ↓
4. 内核 Verifier 验证 (安全检查)
   - 无限循环
   - 越界访问
   - 资源消耗
   ↓
5. JIT 编译为本地机器码
   ↓
6. 挂载到 hook 点 (kprobe/XDP/TC)
   ↓
7. 事件触发时执行 BPF 程序
   ↓
8. 通过 map 与用户态通信
   (ring buffer / hash map / array)
```

### 4.2 eBPF 程序示例（C）

```c
// trace_open.c - 追踪所有 open 系统调用
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// 事件结构
struct event {
    u32 pid;
    u32 uid;
    char comm[16];
    char filename[256];
};

// ring buffer 与用户态通信
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_openat")
int trace_openat(struct trace_event_raw_sys_enter *ctx)
{
    struct event *e;
    
    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) return 0;
    
    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->uid = bpf_get_current_uid_gid();
    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    bpf_probe_read_user_str(&e->filename, sizeof(e->filename), ctx->args[1]);
    
    bpf_ringbuf_submit(e, 0);
    return 0;
}
```

```python
# user.py - 用户态程序, 读取事件
from bcc import BPF

bpf_text = open('trace_open.c').read()
b = BPF(text=bpf_text)

def print_event(cpu, data, size):
    event = b['events'].event(data)
    print(f"PID={event.pid} UID={event.uid} COMM={event.comm.decode()} FILE={event.filename.decode()}")

b['events'].open_ring_buffer(print_event)

print("Tracing openat... Ctrl-C to end.")
while True:
    b.ring_buffer_poll()
```

### 4.3 Cilium 架构

```
┌─────────────────────────────────────────────┐
│              Kubernetes Pod                 │
│  ┌──────────────────────────────────────┐   │
│  │  Application                         │   │
│  └─────────────────▲────────────────────┘   │
│                    │                         │
│  ┌─────────────────┴────────────────────┐   │
│  │  eBPF Programs (in kernel)           │   │
│  │  - socket (cgroup)                   │   │
│  │  - TC (traffic control)              │   │
│  │  - XDP (driver)                      │   │
│  └─────────────────▲────────────────────┘   │
└────────────────────│────────────────────────┘
                     │
┌────────────────────┴────────────────────────┐
│              Node Kernel                     │
│  ┌──────────────────────────────────────┐   │
│  │  Cilium Agent (用户态)               │   │
│  │  - 配置 eBPF 程序                    │   │
│  │  - 实现 K8s NetworkPolicy            │   │
│  │  - 实现 Service LoadBalancer         │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘

特点:
  - 替代 kube-proxy (eBPF 实现 Service)
  - 替代 iptables (eBPF 实现 NetworkPolicy)
  - 性能: 比 iptables 快 10x (大集群)
  - 可观测: Hubble 实时流量
```

### 4.4 Hubble 可观测

```yaml
# Hubble 流量观测
apiVersion: v1
kind: ConfigMap
metadata:
  name: hubble-config
  namespace: kube-system
data:
  config.yaml: |
    metrics:
      enabled:
        - flow
        - port-distribution
        - http
        - dns
      port: 9965
    observeCronJob:
      enabled: true
---
# Prometheus 抓取 Hubble 指标
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: hubble
spec:
  selector:
    matchLabels:
      k8s-app: hubble
  endpoints:
    - port: metrics
      path: /metrics
```

```bash
# hubble CLI 查看实时流量
hubble observe -f

# 输出
Jul 6 10:00:01.234 node-1 flow DEBUG/DEFAULT ...
  Jul 6 10:00:01.234 node-1:K8S_NET flow type=Response ...
    src=10.0.1.5:443 dst=10.0.2.10:38412
    verdict=FORWARDED protocol=TCP
    identity=1→2 policy=none
```

### 4.5 Tetragon 安全审计

```yaml
# Tetragon 策略: 检测容器逃逸
apiVersion: cilium.io/v1alpha1
kind: TracingPolicy
metadata:
  name: detect-container-escape
spec:
  kprobes:
    - call: "security_bprm_check"
      syscall: false
      args:
        - index: 0
          type: "linux_binprm"
      selectors:
        - matchPIDs:
            - operator: In
              values:
                - 0  # 容器内执行宿主二进制
          matchActions:
            - action: Sigkill  # 直接杀进程
              rateLimit: "1m"
---
# 检测敏感文件读取
apiVersion: cilium.io/v1alpha1
kind: TracingPolicy
metadata:
  name: detect-sensitive-file-access
spec:
  kprobes:
    - call: "security_file_open"
      syscall: false
      args:
        - index: 0
          type: "file"
      selectors:
        - matchBinaries:
            - operator: "In"
              values:
                - "/usr/bin/cat"
                - "/usr/bin/less"
          matchArgs:
            - index: 0
              operator: "Prefix"
              values:
                - "/etc/shadow"
                - "/etc/passwd"
                - "/root/.ssh/"
          matchActions:
            - action: Post
              auditMessage: "敏感文件访问"
```

---

## 五、操作流程与配置

### 5.1 安装 Cilium（替换 kube-proxy）

```bash
# 1. 集群准备 (内核 5.10+)
uname -r  # 确认内核版本

# 2. 移除 kube-proxy
kubectl delete daemonset kube-proxy -n kube-system

# 3. 安装 Cilium
helm repo add cilium https://helm.cilium.io/
helm install cilium cilium/cilium --version 1.15.0 \
  --namespace kube-system \
  --set kubeProxyReplacement=true \
  --set k8sServiceHost=API_SERVER_IP \
  --set k8sServicePort=6443 \
  --set hubble.enabled=true \
  --set hubble.relay.enabled=true \
  --set hubble.ui.enabled=true \
  --set prometheus.enabled=true

# 4. 验证
cilium status
cilium connectivity test
```

### 5.2 NetworkPolicy（Cilium CRD）

```yaml
# LLM 推理服务网络策略
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: vllm-network-policy
  namespace: vllm-prod
spec:
  podSelector:
    matchLabels:
      app: vllm
  policyTypes:
    - Ingress
    - Egress
  ingress:
    # 仅允许 API gateway 命名空间访问
    - from:
        - namespaceSelector:
            matchLabels:
              name: api-gateway
      ports:
        - protocol: TCP
          port: 8000
  egress:
    # 允许 DNS
    - to:
        - namespaceSelector: {}
      ports:
        - protocol: UDP
          port: 53
    # 允许访问模型仓库
    - to:
        - namespaceSelector:
            matchLabels:
              name: model-registry
      ports:
        - protocol: TCP
          port: 443
    # 默认拒绝其他
---
# Cilium L7 策略 (HTTP 级别)
apiVersion: cilium.io/v2
kind: CiliumNetworkPolicy
metadata:
  name: vllm-l7-policy
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
              - method: "POST"
                path: "/v1/chat/completions"
              - method: "GET"
                path: "/health"
```

### 5.3 L7 流量观测（Hubble）

```bash
# 1. 启用 Hubble
hubble enable

# 2. 查看实时流量
hubble observe -f --namespace vllm-prod

# 3. 过滤 L7 HTTP
hubble observe -f --protocol http

# 4. 查看特定服务
hubble observe -f --from-service vllm

# 5. 查看被拒绝的流量
hubble observe --verdict DROPPED

# 6. 导出指标到 Grafana
# Hubble UI: http://hubble-ui.kube-system:80
```

### 5.4 GPU 调度追踪（eBPF）

```c
// trace_gpu.c - 追踪 GPU 调度
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct gpu_event {
    u32 pid;
    u32 gpu_id;
    u64 start_ns;
    u64 duration_ns;
    char comm[16];
    char op[32];
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1024 * 1024);
} gpu_events SEC(".maps");

SEC("uprobe/nvidia_driver_submit")
int trace_gpu_submit(struct pt_regs *ctx) {
    struct gpu_event *e;
    
    e = bpf_ringbuf_reserve(&gpu_events, sizeof(*e), 0);
    if (!e) return 0;
    
    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->gpu_id = PT_REGS_PARM1(ctx);
    e->start_ns = bpf_ktime_get_ns();
    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    bpf_probe_read_user_str(&e->op, sizeof(e->op), (void *)PT_REGS_PARM2(ctx));
    
    bpf_ringbuf_submit(e, 0);
    return 0;
}
```

```python
# gpu_monitor.py - 分析 GPU 调度
from bcc import BPF
import time

bpf_text = open('trace_gpu.c').read()
b = BPF(text=bpf_text)

events = []
def on_event(cpu, data, size):
    event = b['gpu_events'].event(data)
    events.append({
        'pid': event.pid,
        'gpu_id': event.gpu_id,
        'time': event.start_ns,
        'comm': event.comm.decode(),
        'op': event.op.decode()
    })

b['gpu_events'].open_ring_buffer(on_event)

print("Tracing GPU submissions...")
while True:
    b.ring_buffer_poll()
    if len(events) > 1000:
        analyze(events)
        events = []
```

### 5.5 XDP 高性能 DDoS 防护

```c
// xdp_ddos.c - XDP 层 DDoS 防护
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>

// 限速表
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 100000);
    __type(key, u32);  // src IP
    __type(value, u64); // 包数
} rate_limit SEC(".maps");

SEC("xdp")
int xdp_ddos_filter(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    
    struct ethhdr *eth = data;
    if ((void*)(eth + 1) > data_end) return XDP_PASS;
    if (eth->h_proto != __constant_htons(ETH_P_IP)) return XDP_PASS;
    
    struct iphdr *ip = (void *)(eth + 1);
    if ((void*)(ip + 1) > data_end) return XDP_PASS;
    
    u32 src_ip = ip->saddr;
    u64 *count = bpf_map_lookup_elem(&rate_limit, &src_ip);
    u64 now = bpf_ktime_get_ns();
    
    if (count) {
        if (*count > 1000) {  // 超过 1000 包/秒
            return XDP_DROP;
        }
        (*count)++;
    } else {
        u64 init = 1;
        bpf_map_update_elem(&rate_limit, &src_ip, &init, BPF_ANY);
    }
    
    return XDP_PASS;
}
```

---

## 六、底层原理

### 6.1 BPF Verifier（验证器）

```
eBPF 程序加载前的验证 (确保安全):

1. 控制流图 (CFG) 分析
   - 无限循环检测 (有界循环)
   - 所有路径可达

2. 寄存器状态跟踪
   - 类型检查 (pointer/scalar)
   - 范围检查 (min/max)
   - 越界访问检测

3. 资源限制
   - 指令数上限 (4096 → 100万, 内核版本相关)
   - 栈大小 (512 字节)
   - map 大小限制

4. 权限检查
   - CAP_SYS_ADMIN 或 unprivileged bpf
   - 内核版本相关限制

失败模式:
  - "invalid bpf_context access"
  - "unbounded loop"
  - "stack offset out of range"
```

### 6.2 eBPF Map 类型

```
BPF Map: 内核态与用户态通信的数据结构

主要类型:
  - BPF_MAP_TYPE_HASH: 哈希表 (KV)
  - BPF_MAP_TYPE_ARRAY: 数组
  - BPF_MAP_TYPE_LRU_HASH: LRU 哈希 (自动淘汰)
  - BPF_MAP_TYPE_RINGBUF: 环形缓冲区 (事件)
  - BPF_MAP_TYPE_PERCPU_HASH: 每 CPU 哈希 (无锁)
  - BPF_MAP_TYPE_PERF_EVENT_ARRAY: 性能事件 (旧)
  - BPF_MAP_TYPE_SOCKHASH: socket 关联
  - BPF_MAP_TYPE_DEVMAP: 网卡映射 (XDP redirect)

LLM 推理场景应用:
  - HASH: PID → 推理会话
  - LRU_HASH: IP → 限流计数
  - RINGBUF: GPU 调度事件 → 用户态
  - PERCPU_HASH: 每卡 token 计数 (无锁)
```

### 6.3 XDP 高性能原理

```
传统网络包路径:
  NIC → driver → skb 分配 → netfilter → TC → socket → app
  开销: skb 分配, 协议栈处理, 拷贝

XDP (eXpress Data Path):
  NIC → driver → XDP 程序 → (DROP/REDIRECT/PASS)
  
  - 在驱动层执行, 不分配 skb
  - DROP: 直接丢弃, 不进协议栈 (DDoS 防护)
  - REDIRECT: 转发到其他网卡 (负载均衡)
  - PASS: 进协议栈 (正常处理)

性能:
  - 单核 XDP 可处理 24M pps (packets per second)
  - 传统 iptables: 1-3M pps
  - 提升 10-20x

应用:
  - Cloudflare DDoS 防护 (XDP)
  - Cilium LoadBalancer (XDP redirect)
  - L4 LB (Katran, Facebook)
```

### 6.4 Cilium vs kube-proxy

```
kube-proxy (iptables):
  - 每个 Service 一组 iptables 规则
  - 规则数: O(Services × Endpoints)
  - 大集群 (10K Service): 数十万规则
  - 查找: 线性遍历 (慢)
  - 更新: 全表替换 (秒级)

Cilium (eBPF):
  - eBPF 程序在 socket hook 拦截
  - 表大小: O(Endpoints)
  - 查找: 哈希表 O(1)
  - 更新: 增量 (毫秒级)
  - 性能: 比 iptables 快 10x (大集群)

实测 (5000 Service):
  - iptables: 新连接延迟 P99 50ms
  - Cilium: P99 5ms
```

---

## 七、代码与配置示例

### 7.1 LLM 推理集群网络监控

```yaml
# cilium-values.yaml
hubble:
  enabled: true
  relay:
    enabled: true
  ui:
    enabled: true
  metrics:
    enabled:
      - flow
      - port-distribution
      - http
      - dns
      - tcp
      - icmp
    dashboards:
      enabled: true
      namespace: monitoring

prometheus:
  enabled: true
  serviceMonitor:
    enabled: true
    namespace: monitoring

operator:
  enabled: true
  prometheus:
    enabled: true

bgp:
  enabled: false  # 单集群不需要

loadBalancer:
  mode: hybrid  # L4 + L7

ipv6:
  enabled: false
```

### 7.2 LLM 流量观测仪表盘

```yaml
# Hubble L7 流量仪表盘 (Grafana)
apiVersion: monitoring.coreos.com/v1
kind: GrafanaDashboard
metadata:
  name: llm-traffic-dashboard
  namespace: monitoring
spec:
  json: |
    {
      "dashboard": {
        "title": "LLM 推理流量",
        "panels": [
          {
            "title": "请求 QPS",
            "targets": [{
              "expr": "sum(rate(hubble_http_requests_total{namespace=\"vllm-prod\"}[1m])) by (method, status)"
            }]
          },
          {
            "title": "P99 延迟",
            "targets": [{
              "expr": "histogram_quantile(0.99, sum(rate(hubble_http_request_duration_seconds_bucket{namespace=\"vllm-prod\"}[1m])) by (le, path))"
            }]
          },
          {
            "title": "错误率",
            "targets": [{
              "expr": "sum(rate(hubble_http_requests_total{namespace=\"vllm-prod\", status=~\"5..\"}[1m])) / sum(rate(hubble_http_requests_total{namespace=\"vllm-prod\"}[1m]))"
            }]
          },
          {
            "title": "被拒绝流量",
            "targets": [{
              "expr": "sum(rate(hubble_flows_processed_total{namespace=\"vllm-prod\", verdict=\"DROPPED\"}[1m])) by (source_pod)"
            }]
          }
        ]
      }
    }
```

### 7.3 Tetragon 安全策略

```yaml
# 策略: 禁止容器内执行非预期二进制
apiVersion: cilium.io/v1alpha1
kind: TracingPolicy
metadata:
  name: restrict-binary-exec
  namespace: vllm-prod
spec:
  kprobes:
    - call: "security_bprm_check"
      syscall: false
      args:
        - index: 0
          type: "linux_binprm"
      selectors:
        - matchBinaries:
            - operator: "NotIn"
              values:
                - "/usr/bin/python3"
                - "/usr/local/bin/vllm"
                - "/usr/bin/sh"
          matchActions:
            - action: Sigkill
              auditMessage: "禁止执行非预期二进制"
---
# 策略: 检测 KV Cache 异常读取
apiVersion: cilium.io/v1alpha1
kind: TracingPolicy
metadata:
  name: detect-kv-cache-exfil
  namespace: vllm-prod
spec:
  kprobes:
    - call: "tcp_sendmsg"
      syscall: false
      args:
        - index: 0
          type: "socket"
        - index: 2
          type: "size_t"
      selectors:
        - matchArgs:
            - index: 2
              operator: "GT"
              values:
                - "10485760"  # > 10MB
          matchActions:
            - action: Post
              auditMessage: "大流量外发, 可能是 KV Cache 异常读取"
```

### 7.4 bpftrace 性能分析

```bash
# 1. 追踪 vLLM 函数调用
bpftrace -e '
  uprobe:/usr/local/lib/python3.10/dist-packages/vllm/_C.so:vllm::forward {
    @start[tid] = nsecs;
    printf("vllm forward start: pid=%d\n", tid);
  }
  uretprobe:/usr/local/lib/python3.10/dist-packages/vllm/_C.so:vllm::forward /@start[tid]/ {
    $dur = (nsecs - @start[tid]) / 1000000;
    printf("vllm forward dur: %dms\n", $dur);
    delete(@start[tid]);
  }
'

# 2. 系统调用统计
bpftrace -e 'tracepoint:raw_syscalls:sys_enter { @[comm] = count(); }'

# 3. 火焰图
bpftrace -e 'profile:hz:99 { @[ustack] = count(); }' > stacks.folds
# 用 FlameGraph 生成 SVG
git clone https://github.com/brendangregg/FlameGraph
./FlameGraph/flamegraph.pl stacks.folds > flame.svg

# 4. GPU 调度延迟
bpftrace -e '
  uprobe:libcuda.so:cuLaunchKernel { @start[tid] = nsecs; }
  uretprobe:libcuda.so:cuLaunchKernel /@start[tid]/ {
    @launch_latency_us = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
  }
'
```

---

## 八、常见陷阱与调优

### 8.1 陷阱 1：内核版本不兼容

**症状**：eBPF 程序在旧内核 (4.x) 不工作。

**修复**：
- 升级内核到 5.10+ (推荐 5.15+)
- 或用 CO-RE (Compile Once Run Everywhere)
- 部分 BPF 程序需 5.15+ (如 Tetragon)

### 8.2 陷阱 2：Verifier 拒绝

**症状**：BPF 程序加载失败, "invalid access" 错误。

**修复**：
- 检查指针解引用 (verifier 跟踪指针)
- 边界检查 (`if (offset < size)`)
- 避免复杂循环 (verifier 难以证明有界)
- 用 `bpf_probe_read` 读取用户态内存

### 8.3 陷阱 3：性能反退化

**症状**：eBPF 程序反而拖慢系统。

**修复**：
- 限制 BPF 程序执行频率 (过滤条件)
- 用 PERCPU map 避免锁竞争
- 避免在 hot path 做复杂计算
- 测试: 基线 vs BPF 加载后

### 8.4 陷阱 4：Hubble 数据洪流

**症状**：Hubble 流量日志量过大, 占满磁盘。

**修复**：
- 启用抽样 (`hubble.flow.flows-per-second`)
- 过滤无关命名空间
- 仅记录 L7 HTTP (非 L4 TCP)
- 短期保留 (1h-1d)

### 8.5 陷阱 5：Tetragon 误杀进程

**症状**：安全策略过严, 误杀正常进程。

**修复**：
- 先 `action: Post` (审计模式), 收集日志
- 调整策略后切 `action: Sigkill`
- 白名单匹配二进制路径而非 PID

### 8.6 调优 Checklist

- [ ] 内核 5.10+ (推荐 5.15+)
- [ ] BPF 程序用 CO-RE (跨内核)
- [ ] Cilium 替代 kube-proxy (性能)
- [ ] Hubble 抽样 + L7 (避免洪流)
- [ ] Tetragon 先审计后阻断
- [ ] PERCPU map 避免锁竞争
- [ ] XDP 用于 DDoS/LB (高性能)
- [ ] 监控 BPF 程序自身开销

---

## 九、工业案例与基准数据

### 9.1 案例 1：Cilium 在大厂生产

**背景**：阿里云/字节跳动/腾讯云等大厂采用 Cilium 替代 kube-proxy。

**方案**:
- Cilium eBPF 实现 Service + NetworkPolicy
- Hubble 流量观测
- 替代 iptables (大集群性能瓶颈)

**效果**:
- 5000 Service 集群: iptables P99 50ms → Cilium 5ms
- 网络策略更新: iptables 30s → Cilium 100ms
- 资源占用: iptables 100MB → Cilium 30MB

### 9.2 案例 2：Cloudflare DDoS 防护

**背景**：Cloudflare 用 eBPF/XDP 实现 DDoS 防护。

**方案**：
- XDP 在驱动层丢包 (不进协议栈)
- 单机 24M pps 处理能力
- 全球边缘节点部署

**效果**:
- 实测拦截 1Tbps DDoS
- 单机性能 10x 传统方案
- 节省带宽成本

### 9.3 案例 3：Tetragon 在金融场景

**背景**：某银行用 Tetragon 实时检测容器异常。

**方案**：
- 策略: 容器内执行非预期二进制 → 阻断
- 策略: 敏感文件读取 → 审计
- 策略: 异常网络外联 → 告警

**效果**：
- 容器逃逸检测延迟: < 1ms
- 误报率: < 0.1%
- 替代 Falco (Tetragon 内核态, 更快)

### 9.4 案例 4：Facebook Katran L4 LB

**背景**：Facebook 用 eBPF 实现四层负载均衡。

**方案**：
- XDP 在驱动层做 L4 转发
- 一致性哈希 (Maglev)
- DSR (Direct Server Return)

**规模**：
- 单机 40M pps
- 全球部署

### 9.5 案例 5：Pixie 自动可观测

**背景**：Pixie (New Relic 收购) 用 eBPF 实现零侵入可观测。

**方案**：
- eBPF 追踪 HTTP/gRPC/MySQL/Redis
- 无需修改应用代码
- 自动捕获请求/响应

**效果**：
- 零侵入接入
- 资源开销 < 1% CPU

### 9.6 性能基准

| 方案 | 网络吞吐 | 延迟 | 资源开销 |
|------|---------|------|---------|
| iptables | 1-3M pps | 50ms (P99) | 100MB |
| Cilium (eBPF) | 10M pps | 5ms (P99) | 30MB |
| XDP | 24M pps | < 1ms | 5MB |
| Calico eBPF | 10M pps | 5ms | 30MB |

---

## 十、与其他方案的关系

### 10.1 eBPF vs Sidecar

| 维度 | eBPF (Cilium) | Sidecar (Istio) |
|------|--------------|-----------------|
| 部署 | 节点级 | Pod 级 |
| 资源开销 | 低 (节点级) | 高 (每 Pod) |
| 启动 | 无需等 sidecar | 等 sidecar ready |
| L7 路由 | 部分 (HTTP) | 完整 (HTTP/gRPC/...) |
| 灰度 | 较弱 | 强 (流量切分) |
| 升级 | 节点滚动 | Pod 重启 |

详见 [20-无Sidecar服务网格](./20-无Sidecar服务网格.md)。

### 10.2 eBPF vs 内核模块

| 维度 | eBPF | 内核模块 (kmod) |
|------|------|----------------|
| 安全 | Verifier 验证 | 危险 (bug 直接 panic) |
| 开发 | C + 用户态工具 | C + 内核 API |
| 加载 | 动态 (无需重启) | 通常需重启 |
| 跨内核 | CO-RE | 重新编译 |
| 性能 | 接近原生 | 原生 |

---

## 十一、面试速答

**Q1: eBPF 是什么? 解决什么问题?**

A: eBPF 是 Linux 内核的可编程层, 用户写 BPF 程序 (C/Rust), JIT 编译注入内核 hook 点 (kprobe/XDP/TC)。解决: 1) 网络可编程 (Cilium 替代 iptables); 2) 可观测 (Hubble 流量追踪); 3) 安全 (Tetragon 异常检测); 4) 性能 (bpftrace 函数级追踪)。无需修改内核, 安全 (Verifier 验证)。

**Q2: eBPF 比 iptables 好在哪?**

A: 1) 性能: 哈希表 O(1) vs iptables 线性; 2) 规模: O(Endpoints) vs O(Services×Endpoints); 3) 更新: 增量 (毫秒) vs 全表 (秒级); 4) 可观测: Hubble 实时流量。5000 Service 集群: iptables P99 50ms → Cilium 5ms。

**Q3: XDP 为什么快?**

A: XDP 在网卡驱动层执行, 不分配 skb (套接字缓冲区), 不进协议栈。DROP 直接丢包 (DDoS 防护), REDIRECT 转发 (LB), PASS 进协议栈。单核 24M pps, 比 iptables 快 10-20x。Cloudflare/Dropbox/Cloudflare 用 XDP 做 DDoS 防护。

**Q4: Cilium 怎么实现 K8s Service?**

A: Cilium 用 eBPF 程序在 socket hook 拦截连接, 哈希表查找 Service → Endpoints, 直接转发到 Pod IP, 跳过 kube-proxy/iptables。优点: 1) 性能好 (O(1) 哈希); 2) 更新快 (增量); 3) 可观测 (Hubble); 4) 功能丰富 (L7 策略, 金丝雀)。

**Q5: Tetragon 与 Falco 区别?**

A: Tetragon (Cilium 系) 在内核态执行策略, 直接阻断 (Sigkill), 延迟 < 1ms。Falco 在用户态分析系统调用, 仅告警, 延迟 10-100ms。Tetragon 更快更安全 (内核态), Falco 生态更成熟 (规则多)。生产高安全场景用 Tetragon, 一般审计用 Falco。

---

## 十二、综合面试题

### 题 1（中级）：用 eBPF 优化 K8s 网络

**答题要点**：

1. **替换 kube-proxy**:
   - Cilium eBPF 实现 Service
   - 哈希表查找, O(1)
   - 性能提升 10x

2. **NetworkPolicy**:
   - CiliumNetworkPolicy (L7)
   - 比 K8s 原生 L4 更细粒度

3. **可观测**:
   - Hubble 实时流量
   - Prometheus 指标
   - Grafana 仪表盘

4. **性能优化**:
   - XDP LB (Katran 思路)
   - PERCPU map 避免锁竞争
   - 大集群 (5000+ Service) 性能优势

5. **迁移步骤**:
   - 评估内核版本 (5.10+)
   - 测试 Cilium connectivity
   - 逐步替换 (按命名空间)
   - 监控对比

### 题 2（高级）：LLM 推理集群的安全审计

**答题要点**：

1. **威胁模型**:
   - 容器逃逸: 攻击者获取宿主权限
   - KV Cache 窃取: 旁路获取其他用户 prompt
   - 模型外泄: 推理服务被滥用, 大量调用
   - 横向移动: 攻陷 Pod 后攻击其他服务

2. **eBPF 策略**:
   - Tetragon 检测非预期二进制执行
   - 检测敏感文件读取 (/etc/shadow, /root/.ssh)
   - 检测异常网络外联 (>10MB 流量)
   - 检测容器内提权 (setuid)

3. **网络策略**:
   - CiliumNetworkPolicy L7 (HTTP 方法 + 路径)
   - 默认拒绝, 仅允许必要 endpoint
   - DNS 白名单

4. **可观测**:
   - Hubble 流量 + Tetragon 事件
   - 异常检测 (UEBA)
   - SIEM 集成 (Splunk/ELK)

5. **响应**:
   - Sigkill 阻断危险进程
   - 自动隔离 Pod (NetworkPolicy)
   - 告警 PagerDuty

---

## 十三、故障复盘

### 13.1 案例 1：Cilium 升级导致网络中断

**背景**：2024 年某公司 Cilium 1.13→1.15 升级, 部分节点网络中断。

**根因**：旧版 BPF 程序未清理, 与新版冲突。

**修复**：
- 回滚到 1.13
- 逐节点升级 (rolling)
- 升级前 `cilium cleanup --force`

**防范**：Cilium 升级必须 rolling + 验证, 不要全量升级。

### 13.2 案例 2：Tetragon 误杀 vLLM 进程

**背景**：2025 年某公司 Tetragon 策略限制二进制, vLLM 启动时执行 `ld-linux` 被杀。

**根因**：白名单未包含 `ld-linux.so` (动态链接器)。

**修复**：
- 白名单加 `ld-linux.so` / `ld-musl.so`
- 策略先 `action: Post` 审计, 调整后再阻断
- 灰度: 按命名空间逐个启用

**防范**：Tetragon 策略上线必须先审计模式, 收集日志调整后再阻断。

### 13.3 案例 3：Hubble 占满磁盘

**背景**：2024 年某公司 Hubble 日志 100GB/天, 占满节点磁盘。

**根因**：未配置抽样, 全量记录 L4 TCP。

**修复**：
- 启用抽样 (`flows-per-second: 100`)
- 仅记录 L7 HTTP
- 日志保留 1h (而非永久)
- 持久化到对象存储 (S3)

**防范**：Hubble 必须配置抽样与保留策略。

### 13.4 案例 4：BPF Verifier 拒绝加载

**背景**：2025 年某公司自研 BPF 程序, 加载失败 "unbounded loop"。

**根因**：BPF 程序含复杂循环, Verifier 无法证明有界。

**修复**：
- 改写循环为有界 (`#pragma unroll`)
- 用 `bpf_loop` helper (5.17+)
- 拆分循环为多个 BPF 程序

**防范**：BPF 程序避免复杂循环, 用 verifier-friendly 模式。

### 13.5 案例 5：XDP 程序导致网卡不可达

**背景**：2024 年某公司 XDP DDoS 防护策略 bug, 误丢正常流量。

**根因**：限速计数器实现错误, 所有流量被丢。

**修复**：
- 紧急卸载 XDP 程序 (`ip link set dev eth0 xdpgeneric off`)
- 修复 BPF 程序后重新加载
- 灰度: 先在测试节点验证

**防范**：XDP 程序影响网络, 必须灰度 + 紧急卸载预案。

---

## 十四、参考与延伸

### 14.1 工具与项目

- Cilium — https://cilium.io/
- Hubble — https://github.com/cilium/hubble
- Tetragon — https://tetragon.io/
- Pixie — https://pixielabs.ai/
- Falco — https://falco.org/
- bcc — https://github.com/iovisor/bcc
- bpftrace — https://bpftrace.org/
- Katran — https://github.com/facebookincubator/katran
- Calico eBPF — https://docs.tigera.io/calib/ebpf

### 14.2 论文与文档

- *BPF: Tracing and More* — Brendan Gregg
- *Cilium: Network and Application Security with BPF* — Thomas Graf et al.
- *eBPF for Cloud Native* — LPC 2023

### 14.3 跨模块链接

- [17-微虚拟机与沙箱运行时](./17-微虚拟机与沙箱运行时.md) —— 沙箱运行时安全
- [20-无Sidecar服务网格](./20-无Sidecar服务网格.md) —— Cilium Mesh
- [22-混沌工程与稳定性验证](./22-混沌工程与稳定性验证.md) —— eBPF 故障注入
- [09-Agent系统部署与沙箱](./09-Agent系统部署与沙箱.md) —— Agent 沙箱安全审计

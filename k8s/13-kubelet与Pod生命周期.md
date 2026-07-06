# 13. kubelet 与 Pod 生命周期

> 关键词：kubelet、syncLoop、PLEG、CRI、CNI、CSI、Probe、Eviction、Graceful Termination

------

## 13.1 问题定义

在 K8s 集群中，**APIServer 是大脑，kubelet 是手脚**。

一个 Pod 从 `kubectl apply` 到真正跑起来，中间的"最后一公里"完全由 **kubelet** 完成：

- 怎么知道本节点该跑哪些 Pod？
- 怎么调用容器运行时（containerd/CRI-O）创建容器？
- 怎么执行 liveness/readiness 探针？
- 怎么处理 Pod 失败、重启、终止？
- 节点资源不足时怎么驱逐？
- 怎么上报状态给 APIServer？

**核心问题**：

> kubelet 如何把一个 **声明式 PodSpec** 转化为 **真实运行的容器进程**，并在整个生命周期内维持期望状态？

------

## 13.2 直觉解释

把 kubelet 想象成一个 **小区物业管家**：

| 物业管家 | kubelet |
|---------|---------|
| 接收业主（APIServer）下发的住户名单 | List-Watch 本节点的 Pod 列表 |
| 给新住户分配房间、开通水电气 | 调 CRI 创建容器、调 CNI 配网络、调 CSI 挂存储 |
| 定期巡检房间状态（人在不在、有没有故障） | syncLoop + PLEG 检测容器状态 |
| 发现住户口渴了给水、病了叫医生 | 执行 liveness/readiness probe |
| 房间着火/住户欠费 → 赶走 | Eviction Manager 驱逐 Pod |
| 业主说让住户搬走 → 通知、清理、腾房 | Graceful Termination |
| 定期向业主汇报房间状态 | NodeStatus 上报 |

关键点：**kubelet 是一个永不停止的 reconcile 循环**，它不断对比"期望状态"和"实际状态"，发现差异就行动。

------

## 13.3 核心概念

### 13.3.1 kubelet 在架构中的位置

```
                 ┌──────────────────────────┐
                 │       APIServer          │
                 └────────────┬─────────────┘
                              │ List-Watch
                              ▼
┌──────────────────────────────────────────────────────────┐
│                       kubelet                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │ syncLoop │→ │PLEG      │→ │Probe Mgr │→ │Eviction  │  │
│  │(主循环)  │  │(事件生成)│  │(探针)    │  │Manager   │  │
│  └────┬─────┘  └──────────┘  └──────────┘  └──────────┘  │
│       │                                                  │
│       ├──── CRI ──→ containerd/CRI-O ──→ runc/runsc      │
│       ├──── CNI ──→ Calico/Cilium/Flannel                │
│       ├──── CSI ──→ Node Plugin (mount/attach)           │
│       └──── Status ──→ APIServer (.status)               │
└──────────────────────────────────────────────────────────┘
```

### 13.3.2 syncLoop（核心协调循环）

kubelet 主循环，**每秒触发一次**（默认）或由事件触发：

```
syncLoop:
  while True:
    1. 从 PodConfig channel 收集事件（Update/Add/Delete/Sync）
    2. 对每个 Pod 调用 syncPod()
    3. syncPod 内部按顺序执行：
       a. 计算 Pod 状态变化
       b. 杀掉不该存在的容器（孤儿容器）
       c. 创建网络 namespace（CNI）
       d. 挂载卷（CSI/Volume）
       e. 拉取镜像（如果不存在）
       f. 调 CRI 创建 Sandbox（pause 容器）
       g. 启动 init 容器（按顺序）
       h. 启动业务容器（按 spec.containers 顺序）
       i. 启动 sidecar 容器（K8s 1.28+ native sidecar）
```

### 13.3.3 PLEG（Pod Lifecycle Event Generator）

PLEG 负责 **检测容器状态变化**，是 kubelet 的"眼睛"：

```
PLEG 工作流：
  1. 每秒调用 CRI ListContainers 获取所有容器状态
  2. 与上一次状态对比，生成 PodLifecycleEvent
  3. 事件类型：
     - ContainerStarted
     - ContainerDied
     - ContainerRemoved
  4. 事件写入 channel，触发 syncLoop
```

**关键问题**：PLEG Not Healthy 是 kubelet 最常见的故障之一（后文详述）。

### 13.3.4 CRI（Container Runtime Interface）

CRI 把 kubelet 与容器运行时 **解耦**：

```
kubelet  ──gRPC──→  CRI Server（containerd/CRI-O）
   │                  │
   │                  ├── ImageService: PullImage/ListImages/RemoveImage
   │                  └── RuntimeService:
   │                       ├── RunPodSandbox（创建 pause 容器 + netns）
   │                       ├── CreateContainer
   │                       ├── StartContainer
   │                       ├── StopContainer
   │                       ├── RemoveContainer
   │                       ├── ListContainers
   │                       └── ContainerStatus
   └── CNI（网络）/ CSI（存储）走另一套接口
```

主流 CRI 实现：

| 运行时 | 维护方 | 特点 |
|--------|--------|------|
| **containerd** | CNCF | 主流，K8s 默认推荐，轻量 |
| **CRI-O** | Red Hat | OpenShift 默认，专为 K8s |
| **docker-shim** | 已废弃 | K8s 1.24 移除 |
| **gVisor (runsc)** | Google | 沙箱隔离，强安全 |
| **Kata Containers** | OpenStack | VM 级隔离 |

### 13.3.5 Probe（探针）

| 探针 | 作用 | 失败后果 |
|------|------|---------|
| **startupProbe** | 慢启动应用保护，成功前禁用其他探针 | 重启容器 |
| **livenessProbe** | 容器是否"活着" | 重启容器（按 restartPolicy） |
| **readinessProbe** | 容器是否"准备好接收流量" | 从 Service Endpoints 移除 |

探针检查方式：
- `httpGet`：HTTP 请求，2xx/3xx 视为成功
- `tcpSocket`：TCP 连接成功即视为成功
- `exec`：执行命令，退出码 0 视为成功
- `gRPC`（1.24+）：gRPC 健康检查协议

### 13.3.6 QoS 与 Eviction

```
QoS 分类（基于 requests/limits）：
  Guaranteed   → request == limit（所有容器都设置且相等）
  Burstable    → 至少一个容器有 request
  BestEffort   → 没有任何 request/limit

节点资源不足时驱逐顺序（默认）：
  1. BestEffort
  2. Burstable（按优先级排序）
  3. Guaranteed（通常不驱逐，除非系统濒临崩溃）

驱逐信号：
  - memory.available < 100Mi（硬驱逐）
  - nodefs.available < 10%
  - imagefs.available < 15%
  - pid.available < 1000
```

### 13.3.7 Graceful Termination

```
Pod 删除流程：
  1. kubectl delete pod → APIServer 更新 .metadata.deletionTimestamp
  2. kubelet 监听到 deletionTimestamp
  3. 发送 SIGTERM 给容器 PID 1
  4. 等待 terminationGracePeriodSeconds（默认 30s）
  5. 超时后发送 SIGKILL
  6. 清理卷、网络、Sandbox
  7. APIServer 删除 Pod 对象
```

**注意**：`preStop` hook 在 SIGTERM **之前** 执行，用于注册中心反注册、清理连接。

------

## 13.4 操作流程

### 13.4.1 Pod 从创建到运行的完整时序

```
时间点  | 事件
--------|-----------------------------------------
T0      | kubectl apply → APIServer 写入 etcd
T1      | Scheduler Watch 到新 Pod（Pending）
T2      | Scheduler 执行 Filter/Score，绑定到 NodeA
T3      | APIServer 更新 pod.spec.nodeName = "nodeA"
T4      | kubelet 在 NodeA 上 Watch 到该 Pod
T5      | kubelet syncPod() 启动：
        |   - CNI 创建 netns
        |   - CSI 挂卷
        |   - CRI RunPodSandbox（pause 容器）
        |   - CRI CreateContainer + StartContainer（init→业务）
T6      | PLEG 检测到 ContainerStarted 事件
T7      | kubelet 启动探针 goroutine
T8      | readinessProbe 通过 → Endpoints 更新 → 流量进入
T9      | kubelet 上报 pod.status.phase = Running
```

### 13.4.2 Pod 终止时序

```
T0      | kubectl delete pod
T1      | APIServer 设置 deletionTimestamp
T2      | Endpoints Controller 从 Endpoints 移除该 Pod
T3      | kubelet Watch 到 deletionTimestamp
T4      | 执行 preStop hook（等待完成或超时）
T5      | 发送 SIGTERM 给容器 PID 1
T6      | 等待 terminationGracePeriodSeconds
T7      | 超时未退出 → SIGKILL
T8      | CRI StopPodSandbox + RemovePodSandbox
T9      | CNI 清理网络
T10     | CSI 卸载卷
T11     | APIServer 删除 Pod 对象
```

------

## 13.5 底层原理

### 13.5.1 syncPod 内部状态机

```
syncPod 入口：
  desired = pod.spec（期望）
  current = pod.status（实际，由 PLEG + probe 提供）

  if current == nil:  # Pod 新建
    return createPod()
  if current.phase == Failed:
    return killAndRecreate()
  if pod 应该被终止:
    return killPod()

  # 增量同步
  for each container in desired:
    if container 不存在:
      createContainer()
    elif container 镜像变了:
      killContainer(old); createContainer(new)
    elif container 已死:
      restartContainer()（按 restartPolicy）
    else:
      continue  # 无变化
```

### 13.5.2 CRI gRPC 接口细节

```protobuf
// PodSandbox = pause 容器 + network namespace
service RuntimeService {
  rpc RunPodSandbox(RunPodSandboxRequest) returns (RunPodSandboxResponse) {}
  rpc StopPodSandbox(StopPodSandboxRequest) returns (StopPodSandboxResponse) {}
  rpc RemovePodSandbox(RemovePodSandboxRequest) returns (RemovePodSandboxResponse) {}
  
  rpc CreateContainer(CreateContainerRequest) returns (CreateContainerResponse) {}
  rpc StartContainer(StartContainerRequest) returns (StartContainerResponse) {}
  rpc StopContainer(StopContainerRequest) returns (StopContainerResponse) {}
  rpc RemoveContainer(RemoveContainerRequest) returns (RemoveContainerResponse) {}
  rpc ListContainers(ListContainersRequest) returns (ListContainersResponse) {}
  rpc ContainerStatus(ContainerStatusRequest) returns (ContainerStatusResponse) {}
}

service ImageService {
  rpc PullImage(PullImageRequest) returns (PullImageResponse) {}
  rpc ListImages(ListImagesRequest) returns (ListImagesResponse) {}
  rpc RemoveImage(RemoveImageRequest) returns (RemoveImageResponse) {}
}
```

**为什么需要 PodSandbox（pause 容器）？**

```
Pod 网络模型：所有容器共享同一个 netns
  ┌─────────── Pod Sandbox (pause) ──────────┐
  │  netns: eth0(10.244.1.5) + lo            │
  │  IPC: 共享                                │
  │  ┌──────────┐  ┌──────────┐  ┌────────┐ │
  │  │containerA│  │containerB│  │sidecar │ │
  │  │  nginx   │  │  app     │  │  log   │ │
  │  └──────────┘  └──────────┘  └────────┘ │
  └──────────────────────────────────────────┘
  
pause 容器作用：
  1. 持有 netns（即使业务容器死掉重启，netns 不变）
  2. 回收僵尸进程（PID 1 的 reap 能力）
  3. 共享 IPC namespace
```

### 13.5.3 PLEG 健康检查

```go
// kubelet 内部 PLEG 关键代码（简化）
func (m *GenericPLEG) relist() {
  containers, _ := runtime.ListContainers()
  for _, c := range containers {
    old := m.getOldStatus(c.ID)
    if old == nil {
      // ContainerStarted 事件
      m.events <- PodLifecycleEvent{Type: ContainerStarted}
    } else if old.State != c.State {
      // ContainerDied / ContainerRemoved 事件
      m.events <- PodLifecycleEvent{Type: ...}
    }
  }
  m.updateCache(containers)
}

// 健康检查：如果 relist 超过 3 分钟没成功 → PLEG Not Healthy
```

**PLEG Not Healthy 触发条件**：
1. CRI ListContainers 调用超时/失败
2. 容器运行时卡死
3. 节点负载过高导致 relist 协程饥饿
4. 大量容器（>500）导致 ListContainers 慢

### 13.5.4 Probe 执行机制

```
Probe Manager 内部：
  - 每个 Pod 启动时创建 worker goroutine
  - worker 按 periodSeconds 周期执行探测
  - 探测结果写入 cache（resultsMap）
  - 状态变化触发 syncLoop

执行细节：
  httpGet:  在容器 netns 内发请求
  tcpSocket: 在容器 netns 内建立 TCP 连接
  exec:     通过 CRI ExecSync 在容器内执行
  
关键参数：
  initialDelaySeconds: 启动后等多久开始探测（已废弃，推荐 startupProbe）
  periodSeconds:       周期（默认 10）
  timeoutSeconds:      超时（默认 1）
  successThreshold:    连续成功几次视为成功（默认 1）
  failureThreshold:    连续失败几次视为失败（默认 3）
```

### 13.5.5 Graceful Termination 的隐藏陷阱

**陷阱 1：Endpoint 更新慢于 SIGTERM**

```
时序问题：
  T0: kubectl delete pod
  T1: kubelet 立即收到 deletionTimestamp，发 SIGTERM
  T2: 应用收到 SIGTERM，开始关闭
  T3: Endpoints Controller 才异步更新（可能慢 1-2s）
  T4: 期间 kube-proxy 还在转发流量到该 Pod → 502/连接重置

解决方案：
  preStop hook 中 sleep 5-10s，等 Endpoints 同步
```

**陷阱 2：进程不响应 SIGTERM**

```
PID 1 陷阱：
  - 容器 PID 1 是 entrypoint 进程
  - 如果 entrypoint 是个 shell 脚本，shell 不会转发信号给子进程
  - 子进程收不到 SIGTERM，等 30s 后被 SIGKILL
  
解决：
  - 用 exec 真正接管 PID 1：exec java -jar app.jar
  - 或使用 tini/dumb-init 作为 PID 1
```

**陷阱 3：优雅终止与 readinessProbe**

```
deletionTimestamp 设置后：
  - kubelet 不再检查 readinessProbe
  - Pod 立即从 Endpoints 移除（外部视角）
  - 但应用还在跑（preStop + SIGTERM + grace period）
```

### 13.5.6 Eviction Manager 工作流

```
Eviction 触发：
  1. kubelet 每 10s 采集节点指标（memory/disk/pid）
  2. 与 eviction-hard 阈值对比
  3. 超过阈值 → 进入驱逐流程

驱逐策略：
  - 按 QoS 等级排序（BestEffort → Burstable → Guaranteed）
  - 同 QoS 内按 Pod 优先级排序（低优先级先驱逐）
  - 同优先级按内存使用率排序（用得多先驱逐）

驱逐执行：
  1. 选定 Pod → 设置 pod.status.phase = Failed
  2. 发送 SIGTERM（按 grace period）
  3. 超时 → SIGKILL
  4. 记录 Event: "Evicted"
```

**Soft Eviction vs Hard Eviction**：

| 类型 | 行为 | 配置示例 |
|------|------|---------|
| Hard | 立即驱逐 | `--eviction-hard=memory.available<100Mi` |
| Soft | 等 grace period 后驱逐 | `--eviction-soft=memory.available<500Mi --eviction-soft-grace-period=memory.available=1m30s` |

### 13.5.7 Image GC（垃圾回收）

```
kubelet 维护两个阈值：
  - imageGCHighThresholdPercent: 85%（触发 GC）
  - imageGCLowThresholdPercent:  80%（停止 GC）

GC 流程：
  1. 监测 imagefs 使用率
  2. 超过 High → 按 LRU 删除未被任何容器使用的镜像
  3. 删除到 Low 停止

容器 GC：
  - 历史死容器保留 --terminated-pod-gc-threshold 个（默认 1250）
  - 超过则按创建时间清理最老的
```

### 13.5.8 证书轮转

```
kubelet 客户端证书轮转（K8s 1.0+ 特性）：
  1. kubelet 启动时用 bootstrap-kubelet.conf 拿到第一张证书
  2. 证书有效期 1 年
  3. 到 70% 有效期时，kubelet 自动申请新证书
  4. 通过 csrsigningcontroller 签发
  5. 新证书写入 /var/lib/kubelet/pki/

配置：
  featureGate: RotateKubeletClientCertificate=true（默认开启）
  featureGate: RotateKubeletServerCertificate=true（需显式开启）
```

------

## 13.6 配置示例

### 13.6.1 kubelet 关键启动参数

```bash
/usr/bin/kubelet \
  --bootstrap-kubeconfig=/etc/kubernetes/bootstrap-kubelet.conf \
  --kubeconfig=/etc/kubernetes/kubelet.conf \
  --config=/var/lib/kubelet/config.yaml \
  --container-runtime=remote \
  --container-runtime-endpoint=unix:///run/containerd/containerd.sock \
  --node-ip=10.0.0.5 \
  --node-labels=node.kubernetes.io/worker= \
  --register-node=true \
  --v=2
```

### 13.6.2 kubelet 配置文件（推荐方式）

```yaml
# /var/lib/kubelet/config.yaml
kind: KubeletConfiguration
apiVersion: kubelet.config.k8s.io/v1beta1

# 地址
address: 0.0.0.0
port: 10250
readOnlyPort: 10255  # 生产关闭

# 静态 Pod
staticPodPath: /etc/kubernetes/manifests

# Pod 限制
maxPods: 110
podPidsLimit: 4096

# CRI
containerRuntime: remote
containerRuntimeEndpoint: unix:///run/containerd/containerd.sock

# 驱逐配置
evictionHard:
  memory.available:  "100Mi"
  nodefs.available:  "10%"
  nodefs.inodesFree: "5%"
  imagefs.available: "15%"
  pid.available:     "10%"
evictionSoft:
  memory.available:  "500Mi"
  nodefs.available:  "15%"
evictionSoftGracePeriod:
  memory.available: "1m30s"
  nodefs.available: "2m"
evictionMaxPodGracePeriod: 30

# 镜像 GC
imageGCHighThresholdPercent: 85
imageGCLowThresholdPercent:  80

# 证书轮转
rotateCertificates: true
serverTLSBootstrap: true

# 探针
enableControllerAttachDetach: true

# 拓扑管理（CPU/NUMA 亲和）
topologyManagerPolicy: best-effort  # none/best-effort/restricted/single-numa-node

# 保护
protectKernelDefaults: true

# 日志
logging:
  format: json
  verbosity: 2
```

### 13.6.3 Pod 配置：探针与优雅终止

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: web-app
spec:
  terminationGracePeriodSeconds: 60
  
  containers:
  - name: app
    image: nginx:1.25
    
    # 启动探针：保护慢启动应用
    startupProbe:
      httpGet:
        path: /healthz
        port: 8080
      failureThreshold: 30    # 30*10s = 5min 启动窗口
      periodSeconds: 10
    
    # 存活探针
    livenessProbe:
      httpGet:
        path: /healthz
        port: 8080
      periodSeconds: 10
      timeoutSeconds: 3
      failureThreshold: 3
      successThreshold: 1
    
    # 就绪探针
    readinessProbe:
      httpGet:
        path: /ready
        port: 8080
      initialDelaySeconds: 5
      periodSeconds: 5
      failureThreshold: 2
    
    # 优雅终止钩子
    lifecycle:
      preStop:
        exec:
          command:
          - /bin/sh
          - -c
          - "nginx -s quit; sleep 10"
    
    # QoS: Guaranteed
    resources:
      requests:
        cpu: 500m
        memory: 512Mi
      limits:
        cpu: 500m
        memory: 512Mi
```

### 13.6.4 containerd 配置（生产推荐）

```toml
# /etc/containerd/config.toml
version = 2

[plugins."io.containerd.grpc.v1.cri"]
  # 沙箱镜像
  sandbox_image = "registry.k8s.io/pause:3.9"
  
  # 容器运行时（runc 默认，gVisor/Kata 可选）
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc]
    runtime_type = "io.containerd.runc.v2"
  
  # Systemd Cgroup（生产必须）
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc.options]
    SystemdCgroup = true
  
  # 镜像拉取
  [plugins."io.containerd.grpc.v1.cri".registry]
    config_path = "/etc/containerd/certs.d"
  
  # 镜像 GC
  [plugins."io.containerd.grpc.v1.cri".containerd]
    default_runtime_name = "runc"
    snapshotter = "overlayfs"

# NRI（Node Resource Interface）插件
[plugins."io.containerd.nri.v1.nri"]
  disable = false

# 镜像拉取性能优化
[plugins."io.containerd.grpc.v1.cri".containerd]
  max_concurrent_downloads = 3
```

### 13.6.5 SystemdCgroup 为什么必须

```bash
# 错误（cgroupfs 驱动，K8s 1.25+ 不推荐）
SystemdCgroup = false

# 正确
SystemdCgroup = true

# 原因：
# 1. systemd 是现代 Linux 发行版默认 init
# 2. cgroupfs 与 systemd 同时操作 cgroup 会冲突
# 3. kubelet 也用 cgroupfs 的话，会有两个 cgroup 管理者
# 4. 导致资源统计错乱、Pod 无法正确驱逐
```

------

## 13.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | `preStop` 缺失或太短 | 流量未摘除就 SIGTERM，502 | preStop 加 `sleep 5-10s` |
| 2 | `terminationGracePeriodSeconds` 过短 | 应用没释放完连接被 SIGKILL | 设为 60-120s |
| 3 | 容器 PID 1 是 shell | 信号不转发，子进程 30s 后才被杀 | 用 `exec` 或 `tini` |
| 4 | readinessProbe 与 livenessProbe 用同一接口 | 服务不健康立即重启而非摘流 | readiness 用更宽松判断 |
| 5 | livenessProbe 失败阈值过低 | 偶发抖动导致重启风暴 | failureThreshold≥3 |
| 6 | startupProbe 缺失但应用慢启动 | 还没起好就被 liveness 杀掉 | 加 startupProbe |
| 7 | resource requests 不设 | QoS=BestEffort，优先被驱逐 | 至少设 request |
| 8 | containerd 用 cgroupfs 驱动 | 与 systemd 冲突 | SystemdCgroup=true |
| 9 | maxPods 调太大（如 500） | PLEG 不健康、性能下降 | IPVS+高性能节点才考虑 |
| 10 | evictionHard 阈值过低 | 节点 OOM 前 kubelet 没机会驱逐 | memory.available≥100Mi |
| 11 | podPidsLimit 不限制 | 进程泄漏拖垮节点 | 设 4096 等合理上限 |
| 12 | imageGC 阈值过高 | 节点磁盘爆满 | High≤85% Low≤80% |
| 13 | 大量镜像同时拉取 | 网络/磁盘 IO 打满 | max_concurrent_downloads=3 |
| 14 | 静态 Pod 路径误放文件 | 误创建非预期 Pod | 严格控制 staticPodPath |
| 15 | kubelet 证书不轮转 | 1 年后节点 NotReady | rotateCertificates=true |
| 16 | exec 探针启动新进程 | 高频探测导致僵尸进程 | 改用 httpGet/tcpSocket |
| 17 | 挂载卷在 Pod 终止前没卸载 | 卷泄漏，下次挂载失败 | 修复应用释放 fd |
| 18 | HugePages 不配置 | 大内存 JVM 性能差 | 启用 2Mi HugePages |

------

## 13.8 工业案例

### 13.8.1 阿里 ACK：kubelet 性能优化

**场景**：单节点 250+ Pod，kubelet CPU 占用 30%+，PLEG 延迟高。

**优化项**：
1. **PLEG relist 并发化**：改造 GenericPLEG，ListContainers 后并发获取 ContainerStatus
2. **Evented PLEG**（K8s 1.26+）：CRI 主动推送容器事件，取代轮询
3. **Pod 缓存优化**：syncLoop 减少 List 操作，多用本地 cache
4. **Probe 并发限制**：单 Pod 探针 worker 串行，多 Pod 探针并发上限 256
5. **CSI Attach 并发**：从串行改并发，挂载速度提升 5x

**结果**：250 Pod 节点 kubelet CPU 降至 8%，PLEG 延迟从 2s 降到 200ms。

### 13.8.2 字节跳动：Pod 启动加速

**场景**：边缘计算场景需要 Pod 秒级启动，原 8s 太慢。

**优化项**：
1. **镜像预热**：DaemonSet 预拉常用镜像到所有节点
2. **镜像快照**：containerd snapshotter + overlayfs，跳过 layer 解压
3. **CNI 缓存**：IPAM 分配 IP 池，避免每次 RPC 调用
4. **CSI Skip Attach**：对本地卷跳过 Attach 阶段
5. **并发启动**：sidecar 容器与主容器并行启动（K8s 1.28 native sidecar）

**结果**：Pod 启动时间从 8s 降到 1.2s。

### 13.8.3 Google GKE：PLEG Not Healthy 故障

**故障**：GKE 节点大量出现 `PLEG is not healthy` 警告，节点 NotReady。

**根因**：
- 节点运行 400+ Pod
- containerd 1.6.x ListContainers 在大量容器时性能下降
- relist 超时 3 分钟触发 PLEG Not Healthy
- kubelet 自动重启容器，引发雪崩

**修复**：
1. 升级 containerd 到 1.7.x（ListContainers 性能优化）
2. 启用 Evented PLEG（K8s 1.27+）
3. 节点 Pod 数限制为 250
4. 监控 PLEG relist 延迟，>1s 告警

### 13.8.4 Netflix：优雅终止实践

**场景**：视频流服务 Pod 终止时大量 502 错误。

**排查**：
- 应用收到 SIGTERM 后立即关闭
- 但 Endpoints 还在向该 Pod 转发流量
- 5-10s 窗口期内所有请求失败

**方案**：
```yaml
lifecycle:
  preStop:
    exec:
      command:
      - /bin/sh
      - -c
      - "sleep 15 && /app/deregister.sh"
```

**关键点**：preStop 总时间（15s）+ grace period（30s）要让 Endpoints 同步（通常 <5s）+ 应用清理（10s）。

### 13.8.5 AWS EKS：容器运行时从 dockerd 到 containerd 迁移

**背景**：K8s 1.24 移除 docker-shim，EKS 强制迁移到 containerd。

**迁移坑**：
1. **镜像构建依赖**：Dockerfile 中的 `docker` 命令在构建期还能用，运行期换 containerd
2. **日志驱动差异**：dockerd 默认 json-file，containerd 也是，但路径不同
3. **API 兼容**：调用 Docker API 的应用需改用 containerd API 或 CRI
4. **GPU 支持**：nvidia-container-toolkit 配置路径不同

**结果**：迁移后节点资源占用降低 15%（containerd 比 dockerd 轻），启动速度提升 20%。

------

## 13.9 与其他方案关系

### 13.9.1 kubelet vs Docker Daemon

| 维度 | Docker Daemon | kubelet |
|------|---------------|---------|
| 角色 | 容器生命周期管理 | Pod 生命周期管理 |
| 单位 | Container | Pod（多容器组） |
| 接口 | Docker API | CRI + CNI + CSI |
| 编排 | 无（需 Swarm/Mesos） | 声明式 + Reconcile |
| 网络 | 单容器 bridge | Pod 共享 netns |
| 状态上报 | 无 | NodeStatus + PodStatus |
| 适配范围 | 单机 | 集群 |

### 13.9.2 kubelet vs systemd

| 维度 | systemd | kubelet |
|------|---------|---------|
| 管理对象 | 系统服务 | Pod |
| 启动顺序 | After/Wants | init container + 主容器 |
| 重启策略 | Restart=always | restartPolicy=Always/OnFailure/Never |
| 资源限制 | CGroup slices | requests/limits |
| 健康检查 | 无（或 Watchdog） | liveness/readiness probe |
| 网络 | 共享主机 | Pod 独立 netns |
| 日志 | journald | stdout→日志驱动 |

**关系**：kubelet 自身作为 systemd service 运行，又管理 Pod（不通过 systemd）。

### 13.9.3 与 Nomad/Apache Mesos Agent 对比

| 维度 | kubelet | Nomad Client | Mesos Agent |
|------|---------|--------------|-------------|
| 工作单元 | Pod | Task | Task/Executor |
| 编排协议 | K8s API | Nomad API | Mesos Framework |
| 容器运行时 | CRI（containerd/CRI-O） | Docker/containerd | Docker/Mesos containerizer |
| 网络 | CNI | host/bridge | Mesos CNI |
| 存储 | CSI/CSI | host/volume | Mesos isolated |
| 声明式 | 是（强） | 是（弱） | 否（任务式） |
| 生态 | 巨大 | 中等 | 衰退 |

### 13.9.4 kubelet 与 Serverless（Fargate/Lambda）

```
传统 kubelet:
  - 长期运行 Pod
  - 资源预占用
  - 节点固定

Serverless（Fargate 等）:
  - 任务级启动
  - 按需资源
  - 无节点概念

混合模式：
  - AWS Fargate: 跳过 kubelet，直接 EKS Pod 启动到 Fargate
  - Virtual Kubelet: 把 Pod 调度到云 Serverless（ACI/Cloud Run）
```

------

## 13.10 面试速答

**Q1: kubelet 的核心职责是什么？**

管理本节点 Pod 的全生命周期：监听 APIServer 获取本节点 Pod，调 CRI/CNI/CSI 创建资源，执行探针，处理驱逐与终止，上报 Node/Pod 状态。

**Q2: PLEG 是什么？为什么会 Not Healthy？**

PLEG = Pod Lifecycle Event Generator，每秒调用 CRI ListContainers 检测容器状态变化并生成事件。Not Healthy 通常是 ListContainers 超时（容器运行时卡死、节点负载过高、容器数量过多），超过 3 分钟未恢复即标记 unhealthy，可能导致节点 NotReady。

**Q3: liveness vs readiness vs startup 探针区别？**

- startup：慢启动应用保护，成功前禁用其他探针，失败重启
- liveness：判断容器是否健康，失败重启
- readiness：判断是否可接收流量，失败从 Endpoints 摘除但不重启

**Q4: Pod 终止时为什么会有 502 错误？怎么解决？**

SIGTERM 发送后应用立即关闭，但 Endpoints 异步同步有 1-2s 窗口期，期间流量仍转发到该 Pod。解决：preStop 加 `sleep 5-10s` 等待 Endpoints 同步。

**Q5: 为什么需要 pause 容器？**

Pod 多容器共享 netns，需要一个稳定持有 netns 的容器，避免业务容器重启导致 netns 销毁。pause 容器还负责回收僵尸进程（PID 1 能力）。

**Q6: containerd 与 docker 的区别？为什么 K8s 弃用 docker-shim？**

docker-shim 是 K8s 适配 Docker 的中间层，多一层转换性能差。containerd 直接实现 CRI，更轻量。K8s 1.24 移除 docker-shim 后，所有集群用 containerd 或 CRI-O。

**Q7: QoS 等级如何影响驱逐？**

- Guaranteed（request==limit）：基本不驱逐，除非系统濒临崩溃
- Burstable：有 request，按优先级驱逐
- BestEffort：最先被驱逐

**Q8: Graceful Termination 流程？**

deletionTimestamp → preStop hook → SIGTERM → 等 terminationGracePeriodSeconds（默认 30s）→ SIGKILL → 清理 sandbox/netns/volume → 删除 Pod 对象。

**Q9: Evented PLEG 是什么？为什么需要？**

传统 PLEG 轮询 ListContainers，大量容器时性能差。Evented PLEG（K8s 1.26+ GA）让 CRI 主动推送容器事件，类似 List-Watch，性能大幅提升。

**Q10: kubelet 证书如何轮转？**

启用 `rotateCertificates=true`，kubelet 在证书 70% 有效期时自动申请新证书，通过 csrsigningcontroller 签发。生产必须开启，否则 1 年后节点 NotReady。

------

## 13.11 综合面试题

### 题 1：Pod 一直 CrashLoopBackOff，如何排查？

```
排查路径：
1. kubectl describe pod → 看 Events、Last State
2. kubectl logs pod -c <container> --previous → 看上次崩溃日志
3. 常见原因：
   - 应用启动报错（配置缺失、依赖不可达）
   - livenessProbe 失败（接口返回非 2xx）
   - OOMKilled（kubectl describe 看 Reason）
   - exec 探针命令错误
   - 资源 limit 过低
4. 进阶：kubelet 日志（journalctl -u kubelet）、容器事件（crictl logs）
```

### 题 2：节点 NotReady，PLEG Not Healthy，如何处理？

```
1. 立即隔离：kubectl cordon <node>（停止调度）
2. 诊断：
   - kubectl describe node → 看 Conditions
   - journalctl -u kubelet | grep PLEG
   - crictl ps（看 CRI 是否响应）
   - top / iotop（节点负载）
3. 短期修复：重启 kubelet 或 containerd
4. 长期优化：
   - 减少节点 Pod 数（<250）
   - 升级 containerd 到 1.7+
   - 启用 Evented PLEG（K8s 1.27+）
   - 节点资源扩容
5. 不可恢复：kubectl drain <node> --ignore-daemonsets --delete-emptydir-data
```

### 题 3：设计一个支持 1000 Pod/节点的 kubelet 优化方案。

```
1. 调优参数：
   - maxPods: 1000
   - podPidsLimit: 1024（控制单 Pod 进程数）
   - kubeAPIQPS: 100, kubeAPIBurst: 200
   - serializeImagePulls: false（并发拉镜像）
   - maxConcurrentDownloads: 5

2. 必须升级：
   - K8s 1.27+（Evented PLEG GA）
   - containerd 1.7+（ListContainers 优化）
   - 内核 5.10+（cgroup v2 性能）

3. CRI 选型：
   - containerd + runc（默认）
   - 或 containerd + crun（C 实现，更轻量）

4. 网络优化：
   - Cilium + eBPF（绕过 iptables）
   - NodeLocal DNSCache

5. 监控：
   - PLEG relist 延迟 < 500ms
   - kubelet CPU < 15%
   - containerd RPC 延迟 P99 < 100ms

6. 警告：1000 Pod/节点是极限，常用 250-500。
```

### 题 4：Pod 删除卡在 Terminating，怎么办？

```
排查：
1. kubectl get pod → 看 STATUS
2. kubectl describe pod → 看 finalizers
3. 常见原因：
   - finalizer 未移除（自定义 controller 卡住）
   - PVC 未释放（Pod 持有挂载）
   - kubelet 卡死（无法清理 sandbox）
   - 节点 NotReady（kubelet 不响应）

强制删除（危险，最后手段）：
  kubectl delete pod <name> --grace-period=0 --force
  
注意：可能留下脏数据（卷挂载、IP 泄漏）。生产建议：
  1. 先恢复 controller/kubelet
  2. 仍卡住再 force delete
  3. 检查节点上残留容器（crictl rm -f）
```

### 题 5：kubelet 与 APIServer 通信断开会怎样？

```
1. 已运行 Pod：
   - 继续运行（kubelet 本地 cache 维持）
   - 状态无法上报（APIServer 显示旧状态）
   - 探针继续执行，本地重启仍可
   
2. 新 Pod：
   - 无法 Watch 到，不调度
   - 节点上 Pod 不会被驱逐
   
3. 删除 Pod：
   - APIServer 端 Pod 对象残留
   - 实际容器可能继续运行
   
4. 恢复后：
   - kubelet 重新 List-Watch
   - 对比本地状态与 APIServer 状态
   - reconcile 到期望状态

5. 影响：
   - 短时间（<5min）：基本无感
   - 长时间（>10min）：状态漂移，监控告警
```

### 题 6：为什么生产必须用 SystemdCgroup？

```
1. Linux init 系统已统一为 systemd
2. systemd 与 cgroupfs 同时操作 cgroup 会冲突
3. kubelet 也管理 cgroup（执行驱逐、资源统计）
4. 双驱动 → cgroup 层级不一致 → 资源统计错乱

错误现象：
  - Pod 内存使用统计不准确
  - 驱逐阈值失效
  - 节点 OOM 但 kubelet 未及时驱逐

正确配置：
  containerd: SystemdCgroup = true
  kubelet:    cgroupDriver: systemd（K8s 1.0+ 自动检测）
```

### 题 7：sidecar 容器与主容器启动顺序问题，怎么解决？

```
K8s 1.28 之前：
  - 容器按 spec.containers 顺序启动
  - sidecar（如 log/istio-proxy）在主容器后启动
  - 主容器先死，sidecar 还在 → Pod 不终止
  - Job 类 Pod 永远不完成

K8s 1.28+ native sidecar：
  spec.initContainers:
  - name: log-sidecar
    restartPolicy: Always   # ← 关键
    image: fluent-bit
  spec.containers:
  - name: app
  
特性：
  - init 容器声明 restartPolicy=Always 即变成 sidecar
  - 先于主容器启动
  - 主容器全部退出后立即终止
  - 与主容器共享 lifecycle
```

------

## 13.12 故障复盘

### 案例 1：preStop 缺失导致电商大促 502

**故障时间**：2024-11-11 00:30（大促开始 30 分钟）

**故障现象**：
- 滚动更新过程中大量 502 错误
- 影响订单创建接口，持续 5 分钟
- 损失：约 200 万元订单

**根因分析**：
```yaml
# 原始配置（无 preStop）
spec:
  terminationGracePeriodSeconds: 30
  containers:
  - name: order-service
    image: order:v2.1.0
```

时序问题：
1. kubelet 收到 deletionTimestamp，立即发 SIGTERM
2. 应用收到 SIGTERM，开始关闭 HTTP 服务（<1s）
3. 此时 Endpoints Controller 还在异步更新（耗时 2-3s）
4. kube-proxy 还在按旧 iptables 规则转发流量
5. 流量持续进入 → 连接被拒 → 502

**修复方案**：
```yaml
spec:
  terminationGracePeriodSeconds: 60
  containers:
  - name: order-service
    lifecycle:
      preStop:
        exec:
          command:
          - /bin/sh
          - -c
          - "curl -X POST http://localhost:8080/deregister && sleep 15"
```

**经验教训**：
1. 任何对外服务的 Pod 都必须有 preStop
2. preStop 总时间 ≥ Endpoints 同步时间（5-10s）
3. grace period ≥ preStop + 应用清理时间

### 案例 2：PLEG Not Healthy 引发节点雪崩

**故障时间**：2024-08-15 14:20

**故障现象**：
- 200+ 节点同时报 PLEG Not Healthy
- 业务 Pod 大量重启
- 集群扩容无法跟上 Pod 重新调度

**根因分析**：
1. 集群升级到 K8s 1.26，未启用 Evented PLEG
2. 节点 Pod 数平均 300+
3. containerd 1.6.0 ListContainers 在大量容器时性能下降
4. PLEG relist 超时 3 分钟
5. kubelet 重启所有容器（认为是孤儿）
6. 大量 Pod 同时启动，进一步打满节点

**修复过程**：
1. 紧急 cordon 故障节点，停止调度
2. 重启 containerd（临时缓解）
3. 一周内滚动升级 containerd 到 1.7.0
4. 启用 Evented PLEG
5. 节点 Pod 上限调为 250

**经验教训**：
1. 升级前必须看 release notes 的已知问题
2. PLEG relist 延迟必须监控
3. 节点 Pod 数不要超过 250（除非专门优化）

### 案例 3：证书过期导致全集群不可用

**故障时间**：2024-06-01 09:00（集群运行满 1 年）

**故障现象**：
- 所有节点同时 NotReady
- kubectl 命令报证书错误
- 业务 Pod 无法调度

**根因分析**：
- 集群搭建时未启用证书轮转
- kubelet 客户端证书 1 年有效期到期
- 所有节点同时失效

**修复过程**：
1. 紧急手动续期（kubeadm alpha certs renew all）
2. 重启所有 kubelet
3. 启用 rotateCertificates=true
4. 配置 cert-manager + 自建 CA 自动轮转

**经验教训**：
1. 生产必须开启证书轮转
2. 监控证书过期时间（Prometheus + kubeadm certs check-expiration）
3. 关键证书提前 30 天告警

### 案例 4：cgroup 驱动不一致导致 OOM 风暴

**故障时间**：2023-12-20

**故障现象**：
- 节点频繁 OOM
- kubelet 驱逐不及时
- Pod 内存统计与实际不符

**根因分析**：
```
kubelet:     cgroupDriver=cgroupfs
containerd:  SystemdCgroup=false
systemd:     默认 systemd cgroup
→ 三方管理 cgroup，层级混乱
```

**修复**：
```
containerd:  SystemdCgroup=true
kubelet:     cgroupDriver=systemd
→ 重启所有节点
```

**经验教训**：
1. 全集群必须统一 cgroup 驱动为 systemd
2. kubeadm init 时通过 config 指定，避免后续不一致

------

## 13.13 参考与延伸

### 官方文档
- [kubelet](https://kubernetes.io/docs/reference/command-line-tools-reference/kubelet/)
- [CRI (Container Runtime Interface)](https://kubernetes.io/docs/concepts/architecture/cri/)
- [Configure Probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/)
- [Pod Lifecycle](https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/)
- [Node Pressure Eviction](https://kubernetes.io/docs/concepts/scheduling-eviction/node-pressure-eviction/)

### KEP（Kubernetes Enhancement Proposals）
- [KEP-3329: Evented PLEG](https://github.com/kubernetes/enhancements/tree/master/keps/sig-node/3329-evented-pleg)
- [KEP-753: Native Sidecar Containers](https://github.com/kubernetes/enhancements/tree/master/keps/sig-node/753-sidecar-containers)
- [KEP-1287: In-place Pod Vertical Scaling](https://github.com/kubernetes/enhancements/tree/master/keps/sig-node/1287-in-place-update-pod-resources)
- [KEP-2535: Resizing Pod Resources](https://github.com/kubernetes/enhancements/tree/master/keps/sig-node/2535-reserve-resources-for-phase-one)

### 源码导航
- `kubernetes/pkg/kubelet/kubelet.go` - kubelet 主结构
- `kubernetes/pkg/kubelet/kubelet_getters.go` - 状态获取
- `kubernetes/pkg/kubelet/pleg/` - PLEG 实现
- `kubernetes/pkg/kubelet/prober/` - Probe 实现
- `kubernetes/pkg/kubelet/eviction/` - 驱逐管理
- `kubernetes/pkg/kubelet/images/` - 镜像 GC
- `kubernetes/pkg/kubelet/kuberuntime/` - CRI 调用封装
- `kubernetes/pkg/kubelet/lifecycle/` - 生命周期处理

### 相关章节
- [12-APIServer与etcd.md](./12-APIServer与etcd.md) - kubelet 的对端
- [04-Pod与工作负载.md](./04-Pod与工作负载.md) - Pod 模型
- [06-存储.md](./06-存储.md) - kubelet 调 CSI 挂卷
- [09-控制器模式.md](./09-控制器模式.md) - Reconcile Loop 同源
- [14-kube-proxy与服务转发.md](./14-kube-proxy与服务转发.md) - 节点网络搭档
- [15-CNI与网络模型.md](./15-CNI与网络模型.md) - kubelet 调 CNI
- [16-CSI与存储编排.md](./16-CSI与存储编排.md) - kubelet 调 CSI

### 推荐实践
- [Production Ready Kubelet](https://learnk8s.io/production-best-practices#kubelet)
- [Tuning kubelet for performance](https://kubernetes.io/blog/2023/11/17/kubelet-tenant-isolation/)
- [kubelet-eviction-zh](https://kubernetes.io/zh-cn/docs/concepts/scheduling-eviction/node-pressure-eviction/)

### 工具
- `crictl` - CRI 命令行客户端
- `kubectl node-shell` - 进入节点 shell
- `kubelet-debug` - kubelet 调试工具
- `nerdctl` - containerd 命令行（类似 docker）

### 进阶主题
- **Topology Manager**：CPU/NUMA 亲和性
- **Device Manager**：GPU/FPGA 设备分配
- **CPU Manager**：cpuset 静态分配
- **Memory Manager**：HugePages 预留
- **In-place Pod Resize**（KEP-1287）：不重启 Pod 调整资源
- **User Namespaces**（KEP-127）：Pod 内用户隔离

# 11 - OCI 规范与运行时

> Docker 不是孤岛。OCI 规范让容器生态百花齐放:runc、containerd、CRI-O、Kata、gVisor……

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 把 **OCI 规范**(Image Spec / Runtime Spec)讲到位
- 把 **runc / containerd / CRI-O** 的职责说清
- 把 **Docker Engine 与 containerd 的关系** 画清
- 把 **shim 架构** 讲到能排障
- 把 **大厂运行时选型** 讲到能落地

### 1.2 本章不解决什么

- 不讲 namespace / cgroup(见 [08](./08-底层原理-namespaces.md) / [09](./09-底层原理-cgroups.md))
- 不讲 UnionFS(见 [10-底层原理-UnionFS](./10-底层原理-UnionFS.md))
- 不讲 K8s 调度(见 [19-容器生态对比](./19-容器生态对比.md))
- 不讲容器安全(gVisor / Kata,见 [12-安全与隔离](./12-安全与隔离.md))

> **关键认知**:Docker 公司 2017 年把 containerd 捐给 CNCF,镜像与运行时规范捐给 OCI。容器生态从此不再由单一公司控制,形成开放标准。

---

## 2. 直觉解释

### 2.1 OCI 类比:USB 标准

```
   没有 USB 标准                有 USB 标准
   ────────────                  ──────────
   每个厂商自家接口              统一接口规范
   鼠标只能用 A 厂的              任何鼠标插任何电脑
   厂商锁定                      生态繁荣
   
   Docker 早期                    OCI 之后
   ───────────                    ────────
   Docker 镜像只能 Docker 跑      OCI 镜像任何运行时跑
   Docker 运行时封闭              runc / containerd / CRI-O 竞争
```

### 2.2 OCI 两大规范

```
┌─────────────────────────────────────┐
│   OCI(Open Container Initiative)   │
├─────────────────────────────────────┤
│                                     │
│   Image Spec(镜像规范)            │
│   - 镜像格式(manifest / config)   │
│   - 层(layer)格式                 │
│   - manifest list(多架构)         │
│                                     │
│   Runtime Spec(运行时规范)        │
│   - 容器配置(config.json)         │
│   - 生命周期(create/start/stop)   │
│   - namespace / cgroup / mounts    │
│                                     │
└─────────────────────────────────────┘
```

### 2.3 容器生态角色

```
   用户 / K8s
       │
       ▼
   ┌────────┐
   │  CRI   │ ← Container Runtime Interface(K8s 标准)
   └───┬────┘
       │
       ▼
   ┌────────────┐
   │containerd  │ ← 高级运行时(守护进程)
   │ CRI-O      │
   └───┬────────┘
       │ OCI Runtime Spec
       ▼
   ┌────────┐
   │  runc  │ ← 低级运行时(命令行工具)
   │ kata   │
   │ gVisor │
   └────────┘
       │
       ▼
   Linux Kernel
```

---

## 3. 核心概念与架构

### 3.1 OCI Runtime Spec

```json
// config.json(简化)
{
  "ociVersion": "1.0.2",
  "process": {
    "terminal": false,
    "user": {"uid": 0, "gid": 0},
    "args": ["nginx", "-g", "daemon off;"],
    "env": ["PATH=/usr/local/bin:/usr/bin:/bin"],
    "cwd": "/",
    "capabilities": {
      "bounding": ["CAP_NET_BIND_SERVICE"],
      "effective": ["CAP_NET_BIND_SERVICE"],
      "permitted": ["CAP_NET_BIND_SERVICE"]
    },
    "noNewPrivileges": true
  },
  "root": {
    "path": "rootfs",
    "readonly": false
  },
  "hostname": "container",
  "mounts": [
    {"destination": "/proc", "type": "proc", "source": "proc"},
    {"destination": "/dev", "type": "tmpfs", "source": "tmpfs"}
  ],
  "linux": {
    "namespaces": [
      {"type": "pid"},
      {"type": "network"},
      {"type": "mount"},
      {"type": "uts"},
      {"type": "ipc"}
    ],
    "cgroupsPath": "/docker/<container-id>",
    "resources": {
      "memory": {"limit": 1073741824},
      "cpu": {"shares": 1024, "quota": 200000, "period": 100000}
    }
  }
}
```

### 3.2 OCI Image Spec

```
镜像组成:
├── manifest.json        # 清单(有哪些层)
├── config.json          # 配置(环境变量、CMD)
├── <layer-1>.tar.gz     # 层 1
├── <layer-2>.tar.gz     # 层 2
└── <layer-3>.tar.gz     # 层 3
```

```json
// manifest.json
{
  "schemaVersion": 2,
  "mediaType": "application/vnd.oci.image.manifest.v1+json",
  "config": {
    "mediaType": "application/vnd.oci.image.config.v1+json",
    "digest": "sha256:abc123...",
    "size": 7023
  },
  "layers": [
    {"mediaType": "application/vnd.oci.image.layer.v1.tar+gzip", "digest": "sha256:l1...", "size": 31337184},
    {"mediaType": "application/vnd.oci.image.layer.v1.tar+gzip", "digest": "sha256:l2...", "size": 8255831}
  ]
}
```

### 3.3 Docker Engine 与 containerd 架构

```
┌─────────────────────────────────────────────┐
│              Docker Engine                  │
│                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │ dockerd  │  │  build   │  │ network  │ │
│  │ (API)    │  │ (BuildKit)│  │ plugin   │ │
│  └────┬─────┘  └──────────┘  └──────────┘ │
│       │                                     │
│       ▼                                     │
│  ┌──────────────────┐                       │
│  │   containerd     │ ← 高级运行时           │
│  │   ┌────────────┐ │                       │
│  │   │  runc      │ │ ← 低级运行时           │
│  │   └────────────┘ │                       │
│  │   ┌────────────┐ │                       │
│  │   │ containerd │ │ ← 每容器一个 shim      │
│  │   │   -shim    │ │                       │
│  │   └────────────┘ │                       │
│  └──────────────────┘                       │
└─────────────────────────────────────────────┘
       │
       ▼
   Linux Kernel
```

### 3.4 containerd-shim 的作用

```
没有 shim:
  dockerd ─── runc ─── 容器进程
  问题:dockerd 重启 → 容器失去父进程 → 容器退出

有 shim:
  dockerd ─── containerd ─── containerd-shim ─── 容器进程
  containerd 重启 → shim 还在 → 容器不受影响
  
  shim 是每容器一个独立进程,作为容器的"养父母"
```

### 3.5 K8s CRI 接口

```
┌─────────────┐
│  kubelet    │
└──────┬──────┘
       │ CRI gRPC
       ▼
┌─────────────────────┐
│  CRI 实现           │
│  - containerd       │
│  - CRI-O            │
│  - dockershim(废弃)│
└──────┬──────────────┘
       │
       ▼
   实际容器运行时
```

```protobuf
// CRI 接口(简化)
service RuntimeService {
    rpc RunPodSandbox(RunPodSandboxRequest) returns (RunPodSandboxResponse);
    rpc CreateContainer(CreateContainerRequest) returns (CreateContainerResponse);
    rpc StartContainer(StartContainerRequest) returns (StartContainerResponse);
    rpc StopContainer(StopContainerRequest) returns (StopContainerResponse);
    rpc RemoveContainer(RemoveContainerRequest) returns (RemoveContainerResponse);
    rpc ListContainers(ListContainersRequest) returns (ListContainersResponse);
    rpc ContainerStatus(ContainerStatusRequest) returns (ContainerStatusResponse);
}

service ImageService {
    rpc ListImages(ListImagesRequest) returns (ListImagesResponse);
    rpc PullImage(PullImageRequest) returns (PullImageResponse);
    rpc RemoveImage(RemoveImageRequest) returns (RemoveImageResponse);
}
```

---

## 4. 操作流程与命令

### 4.1 直接用 runc 创建容器

```bash
# 1. 创建 rootfs
mkdir -p rootfs
docker export $(docker create busybox) | tar -C rootfs -xf -

# 2. 生成 config.json
runc spec

# 3. 创建容器
runc create my-container

# 4. 启动
runc start my-container

# 5. 查看状态
runc list
# ID              PID         STATUS      BUNDLE              CREATED                          OWNER
# my-container    12345       running     /root/container     2024-01-01T00:00:00Z             root

# 6. 停止
runc kill my-container KILL

# 7. 删除
runc delete my-container
```

### 4.2 直接用 containerd(ctr)

```bash
# ctr 是 containerd 自带的 CLI(不友好,调试用)
# 拉镜像
ctr image pull docker.io/library/nginx:1.25

# 列镜像
ctr image ls

# 运行容器
ctr run -d docker.io/library/nginx:1.25 my-nginx

# 列容器
ctr container ls

# 查看任务(进程)
ctr task ls

# 进入容器
ctr task exec --exec-id 1 my-nginx sh

# 停止
ctr task kill my-nginx
ctr container delete my-nginx
```

### 4.3 用 nerdctl(更友好)

```bash
# nerdctl 是 docker CLI 的 containerd 版本
nerdctl run -d --name web -p 8080:80 nginx:1.25
nerdctl ps
nerdctl logs web
nerdctl compose up -d
```

### 4.4 查看 containerd 配置

```bash
# 配置文件
cat /etc/containerd/config.toml

# 关键配置
[plugins."io.containerd.grpc.v1.cri"]
  # 容器运行时
  sandbox_image = "k8s.gcr.io/pause:3.9"
  
  # cgroup driver
  systemd_cgroup = true
  
  # 镜像仓库
  [plugins."io.containerd.grpc.v1.cri".registry.mirrors."docker.io"]
    endpoint = ["https://mirror.ccs.tencentyun.com"]

# 重启 containerd
systemctl restart containerd
```

---

## 5. 底层原理

### 5.1 容器创建完整链路(Docker)

```
docker run -d --name web nginx:1.25

1. CLI → dockerd(REST API)
2. dockerd:
   a. 检查 / 拉取镜像
   b. 创建容器元数据(/var/lib/docker/containers/)
   c. 准备 rootfs(overlay mount)
3. dockerd → containerd(gRPC,task service)
4. containerd:
   a. 创建 containerd-shim 进程
   b. shim 调用 runc
5. runc:
   a. 读 OCI runtime spec(config.json)
   b. clone(CLONE_NEWPID | CLONE_NEWNET | ...)
   c. 设置 cgroup
   d. mount rootfs
   e. exec 应用进程
6. runc 退出,shim 接管为容器父进程
7. dockerd 返回容器 ID 给 CLI
```

### 5.2 K8s Pod 创建链路

```
kubectl run web --image=nginx

1. kubectl → kube-apiserver
2. apiserver 写 etcd
3. kubelet(本节点)watch 到 Pod
4. kubelet → CRI(containerd)
5. CRI:
   a. RunPodSandbox(创建 pause 容器,建立 namespace)
   b. CreateContainer(创建业务容器,共享 pause 的 namespace)
   c. StartContainer
6. kubelet → CNI(配置网络)
7. kubelet → CSI(挂载存储)
8. Pod 就绪
```

### 5.3 pause 容器的作用

```
K8s Pod = pause 容器 + 业务容器(s)

pause 容器:
  - 极小(C 语言,几百字节)
  - 永远睡眠
  - 持有 Pod 的 namespace(NET / IPC / UTS)
  - 是 Pod 内其他容器的"父"

业务容器:
  - 共享 pause 的 namespace
  - pause 死了,所有业务容器跟着死
  - 业务容器死了,pause 不死,Pod 不死

┌─────────────────────────────┐
│  Pod                        │
│  ┌──────────┐               │
│  │  pause   │ ← 持有 namespace│
│  └────┬─────┘               │
│       │共享 NET/IPC/UTS     │
│  ┌────┴────┐  ┌─────────┐   │
│  │  app    │  │ sidecar │   │
│  └─────────┘  └─────────┘   │
└─────────────────────────────┘
```

---

## 6. 代码与配置示例

### 6.1 containerd 生产配置

```toml
# /etc/containerd/config.toml
version = 2

[plugins."io.containerd.grpc.v1.cri"]
  # 镜像仓库
  sandbox_image = "registry.k8s.io/pause:3.9"
  
  # 容器运行时
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc]
    runtime_type = "io.containerd.runc.v2"
    [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc.options]
      SystemdCgroup = true   # 用 systemd cgroup driver
  
  # 镜像拉取
  [plugins."io.containerd.grpc.v1.cri".registry.mirrors]
    [plugins."io.containerd.grpc.v1.cri".registry.mirrors."docker.io"]
      endpoint = ["https://mirror.ccs.tencentyun.com"]
    [plugins."io.containerd.grpc.v1.cri".registry.mirrors."gcr.io"]
      endpoint = ["https://gcr.mirrors.corp.com"]
  
  # 镜像 GC
  [plugins."io.containerd.grpc.v1.cri".registry.configs]
    # 私有仓库认证
    [plugins."io.containerd.grpc.v1.cri".registry.configs."harbor.corp.com".auth]
      username = "robot$pull"
      password = "xxx"
  
  # 镜像最大并行下载
  [plugins."io.containerd.grpc.v1.cri".registry]
    max_concurrent_downloads = 20

# 镜像存储
[plugins."io.containerd.metadata.v1.bolt"]
  content_sharing_policy = "shared"

# 快照器
[plugins."io.containerd.snapshotter.v1.overlayfs"]
  rootfs_path = "/var/lib/containerd/io.containerd.snapshotter.v1.overlayfs"
```

### 6.2 K8s Pod with runtimeClassName

```yaml
# 强制用 Kata Containers(强隔离)
apiVersion: node.k8s.io/v1
kind: RuntimeClass
metadata:
  name: kata
handler: kata

---
apiVersion: v1
kind: Pod
spec:
  runtimeClassName: kata
  containers:
    - name: app
      image: myapp:v1
```

### 6.3 多运行时并存

```toml
# containerd 配置多个 runtime
[plugins."io.containerd.grpc.v1.cri".containerd.runtimes]
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc]
    runtime_type = "io.containerd.runc.v2"
  
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata]
    runtime_type = "io.containerd.kata.v2"
  
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.gvisor]
    runtime_type = "io.containerd.runsc.v1"
```

---

## 7. 常见陷阱与调优

### 7.1 陷阱:cgroup driver 不匹配

**症状**:K8s 节点 kubelet 拿不到容器指标。

**修复**:
```toml
# containerd
SystemdCgroup = true
```
```yaml
# kubelet
cgroupDriver: systemd
```

### 7.2 陷阱:K8s 1.24+ 移除 dockershim

**症状**:升级 K8s 到 1.24+,Docker 容器不工作。

**原因**:K8s 移除了 dockershim,不再支持 Docker 作为 CRI。

**修复**:
- 切换到 containerd(推荐)
- 或用 CRI-O
- 或装 cri-dockerd(社区维护的 dockershim 替代)

### 7.3 陷阱:镜像格式不兼容

**症状**:某些镜像在 containerd 能跑,在 Docker 跑不了(或反之)。

**原因**:镜像 manifest 格式差异(Docker v2 vs OCI)。

**修复**:
- 用 OCI 标准镜像
- buildx 构建时指定格式
- 升级 Docker / containerd

### 7.4 调优:镜像拉取并发

```toml
# containerd
[plugins."io.containerd.grpc.v1.cri".registry]
  max_concurrent_downloads = 20  # 默认 3
```

### 7.5 调优:镜像 GC

```toml
# K8s kubelet 配置
--image-gc-high-threshold=80   # 磁盘 80% 触发 GC
--image-minimum-ttl-duration=2h  # 镜像最少保留 2 小时
```

### 7.6 调优:sandbox_image

```toml
# 用国内镜像源
sandbox_image = "registry.cn-hangzhou.aliyuncs.com/google_containers/pause:3.9"
```

---

## 8. 工业案例与基准数据

### 8.1 运行时性能对比

**测试条件**:跑 nginx,1000 QPS,10 分钟。

| 运行时 | CPU | 内存 | P99 延迟 | 备注 |
|--------|-----|------|----------|------|
| runc(默认) | 1.0× | 1.0× | 50 ms | 基准 |
| containerd(直接) | 0.98× | 0.95× | 48 ms | 略快(无 dockerd 开销) |
| CRI-O | 0.99× | 0.96× | 48 ms | 与 containerd 相当 |
| gVisor | 2.5× | 1.2× | 180 ms | 用户态内核,慢 |
| Kata Containers | 1.3× | 1.5× | 70 ms | VM 级隔离 |

**结论**:
- runc / containerd / CRI-O 性能相近
- gVisor 慢但安全
- Kata 中等,平衡

### 8.2 Docker vs containerd 资源占用

| 维度 | Docker Engine | containerd |
|------|---------------|------------|
| 守护进程内存 | 200-400 MB | 50-100 MB |
| 二进制大小 | 150 MB | 50 MB |
| 启动时间 | 1-2 s | 0.5 s |
| 功能 | 全 | 核心运行时 |
| 适合 | 通用 | K8s 节点 |

### 8.3 大厂运行时选型

| 公司 | K8s 运行时 | 备注 |
|------|-----------|------|
| Google (GKE) | containerd | 默认 |
| AWS (EKS) | containerd | 1.24+ 默认 |
| Azure (AKS) | containerd | 默认 |
| 阿里 (ACK) | containerd | 自研优化 |
| 字节 | containerd | 自研 |
| 腾讯 (TKE) | containerd | 自研 |
| Netflix | Docker(自研 Titus) | 历史包袱 |

> **趋势**:K8s 节点全面切 containerd,Docker 仅在开发环境保留。

### 8.4 强隔离运行时选型

| 场景 | 推荐 | 原因 |
|------|------|------|
| 普通业务 | runc | 性能最优 |
| 多租户(不可信代码) | Kata / gVisor | 强隔离 |
| Serverless(短任务) | Firecracker | 启动快(< 125ms) |
| 金融 / 政企 | Kata | 合规要求 |
| AI 训练 | runc + GPU | 性能优先 |

---

## 9. 与其他方案的关系

### 9.1 containerd vs CRI-O

| 维度 | containerd | CRI-O |
|------|-----------|-------|
| 来源 | Docker 捐给 CNCF | Red Hat 主导 |
| 设计目标 | 通用容器运行时 | 专为 K8s |
| 功能 | 多(含 image pull) | 少(只 CRI) |
| K8s 集成 | 原生 | 原生 |
| 生态 | 大 | 中(OpenShift) |
| 默认 | 是(K8s 1.24+) | 否 |

### 9.2 runc vs crun vs runsc

| 运行时 | 语言 | 隔离 | 备注 |
|--------|------|------|------|
| runc | Go | namespace + cgroup | 默认,OCI 实现 |
| crun | C | namespace + cgroup | Red Hat,更快 |
| runsc | Go | gVisor(用户态内核) | 强隔离 |
| kata-runtime | Go | VM + namespace | VM 级隔离 |
| firecracker | Rust | microVM | AWS Lambda |

### 9.3 Docker vs containerd vs Podman

| 维度 | Docker | containerd | Podman |
|------|--------|-----------|--------|
| 架构 | C/S(dockerd) | C/S(containerd) | daemonless |
| Root | 默认 root | 默认 root | 默认 rootless |
| K8s CRI | 否(dockershim 废弃) | 是 | 否 |
| Compose | 原生 | nerdctl compose | podman-compose |
| 镜像构建 | BuildKit | BuildKit | Buildah |
| 适用 | 开发 / 单机 | K8s 节点 | 单机 / 安全敏感 |

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| OCI 是什么? | Open Container Initiative,容器开放标准,含 Image Spec 与 Runtime Spec。 |
| runc 与 containerd 区别? | runc 是低级运行时(单次创建容器),containerd 是高级运行时(守护进程,管理生命周期)。 |
| K8s 1.24 为什么移除 dockershim? | Docker 不符合 CRI 标准,需 shim 转换;containerd / CRI-O 原生 CRI,更高效。 |
| containerd-shim 干什么? | 每容器一个 shim,作为容器父进程,containerd 重启不影响容器。 |
| K8s Pod 里 pause 容器干什么? | 持有 Pod 的 namespace(NET/IPC/UTS),业务容器共享它,死了不带走 Pod。 |
| CRI 是什么? | Container Runtime Interface,K8s 与运行时之间的 gRPC 标准。 |
| gVisor 与 Kata 区别? | gVisor 是用户态内核(应用 → gVisor → Linux),Kata 是 microVM(独立内核)。 |
| Docker 镜像与 OCI 镜像区别? | 格式相近,Docker v2 是 OCI 的前身;现在多数镜像两者兼容。 |
| containerd 比 Docker 好在哪? | 更轻量(50MB vs 150MB)、K8s 原生 CRI、攻击面小、性能略优。 |
| 多租户场景选什么运行时? | Kata Containers(VM 级隔离)或 gVisor(用户态内核),看性能与安全权衡。 |

---

## 11. 综合面试题

### 题 1(原理)
**问**:解释 `docker run` 从 CLI 到容器进程的完整链路。

**答题要点**:
- CLI → dockerd(REST API)
- dockerd 检查/拉镜像,准备 rootfs(overlay)
- dockerd → containerd(gRPC)
- containerd 创建 shim
- shim → runc
- runc 读 OCI spec,clone + cgroup + mount + exec
- runc 退出,shim 接管
- dockerd 返回容器 ID

### 题 2(架构)
**问**:为什么 K8s 移除 dockershim?

**答题要点**:
- Docker 不符合 CRI 标准,需 dockershim 转换
- 维护成本高(K8s 团队要维护 shim)
- containerd / CRI-O 原生 CRI,更高效
- Docker Engine 臃肿(含构建、网络、卷),K8s 不需要
- 1.24 移除,推动生态统一

### 题 3(故障)
**问**:K8s 节点容器起不来,报 `Failed to create pod sandbox`,如何排查?

**答题要点**:
- 看 kubelet 日志:`journalctl -u kubelet`
- 看 containerd 日志:`journalctl -u containerd`
- 检查 sandbox_image 是否能拉取
- 检查 CRI 配置(/etc/containerd/config.toml)
- 检查 cgroup driver 是否匹配
- 检查内核版本是否支持

### 题 4(深度)
**问**:解释 containerd-shim 的作用与必要性。

**答题要点**:
- 每容器一个 shim 进程
- 作为容器的父进程(reap zombie)
- containerd 重启不影响容器(shim 还在)
- 收集容器退出码
- 转发信号
- 实现 live-restore

### 题 5(对比)
**问**:containerd 与 CRI-O 选哪个?

**答题要点**:
- containerd:Docker 捐给 CNCF,通用,生态大,K8s 默认
- CRI-O:Red Hat 主导,专为 K8s,精简,OpenShift 用
- 性能相近
- 选 containerd:通用、生态、文档多
- 选 CRI-O:OpenShift 环境、追求精简

### 题 6(工业)
**问**:大厂 K8s 集群如何选运行时?

**答题要点**:
- 默认 containerd(性能、生态、K8s 原生)
- 强隔离场景用 Kata(多租户、金融)
- Serverless 用 Firecracker(AWS Lambda)
- AI 训练用 runc + GPU(性能优先)
- 自研优化:阿里、字节、腾讯都有定制
- 监控:运行时指标采集

### 题 7(架构)
**问**:K8s Pod 为什么需要 pause 容器?

**答题要点**:
- Pod 内多容器共享 namespace
- 需要一个"父"持有 namespace
- pause 极小,只睡眠
- 业务容器共享 pause 的 NET/IPC/UTS
- pause 死,Pod 死
- 业务容器死,pause 不死,Pod 不死
- 实现 Pod 的"虚拟机"语义

### 题 8(性能)
**问**:gVisor 比 runc 慢 2-10 倍,慢在哪?

**答题要点**:
- gVisor 是用户态内核
- 每个 syscall 要从应用 → gVisor → 内核
- 网络包要经过 gVisor 的 netstack
- 文件 IO 要经过 gVisor 的 9p
- 适合:不可信代码、多租户
- 不适合:高性能、IO 密集

### 题 9(安全)
**问**:容器逃逸的根本原因是什么?如何彻底防御?

**答题要点**:
- 根本原因:共享内核,内核漏洞可绕过 namespace
- CVE 例子:Dirty COW、runc 逃逸
- 防御:
  - 最小权限(cap-drop ALL)
  - rootless(USER namespace)
  - seccomp / AppArmor
  - gVisor(用户态内核,阻断 syscall)
  - Kata(microVM,独立内核)
- 工业选型:普通用 runc + 加固,敏感用 Kata

### 题 10(综合)
**问**:设计一个支持多租户的 K8s 容器平台,运行时怎么选?

**答题要点**:
- 默认 runc(性能优先)
- 不可信租户用 Kata(VM 级隔离)
- 通过 RuntimeClass 切换
- 监控:每租户资源隔离
- 安全:NetworkPolicy + PSA + 镜像签名
- 调度:节点池隔离(高敏租户独占)
- 容器逃逸检测:Falco
- 案例:阿里云 ASK、AWS EKS Anywhere

---

## 12. 故障复盘

### 案例 1:K8s 1.24 升级后容器起不来

**现象**:某公司升级 K8s 到 1.24,所有 Pod 创建失败。

**根因**:
- 1.24 移除 dockershim
- 节点仍用 Docker 作为运行时

**修复**:
```bash
# 1. 安装 containerd
apt install containerd

# 2. 配置
containerd config default > /etc/containerd/config.toml
# 修改 SystemdCgroup = true

# 3. kubelet 配置
# /etc/kubernetes/kubelet.conf
environment="KUBELET_EXTRA_ARGS=--container-runtime=remote --container-runtime-endpoint=unix:///run/containerd/containerd.sock"

# 4. 重启
systemctl restart containerd kubelet
```

### 案例 2:cgroup driver 不匹配

**现象**:K8s 节点 NotReady,kubelet 报 cgroup 错误。

**根因**:
- kubelet 用 systemd cgroup driver
- containerd 用 cgroupfs driver
- 不一致

**修复**:
```toml
# containerd
[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc.options]
  SystemdCgroup = true
```

### 案例 3:镜像拉取失败

**现象**:K8s Pod 创建失败,报 `ImagePullBackOff`。

**根因**:
- 镜像在私有仓库
- containerd 没配认证

**修复**:
```toml
[plugins."io.containerd.grpc.v1.cri".registry.configs."harbor.corp.com".auth]
  username = "robot$pull"
  password = "xxx"
```

或用 imagePullSecret:
```yaml
spec:
  imagePullSecrets:
    - name: regcred
```

### 案例 4:sandbox_image 拉不下来

**现象**:K8s 节点容器起不来,报 pause 镜像拉取失败。

**根因**:
- 默认 `k8s.gcr.io/pause:3.9`
- 国内访问不了

**修复**:
```toml
sandbox_image = "registry.cn-hangzhou.aliyuncs.com/google_containers/pause:3.9"
```

### 案例 5:containerd 镜像 GC 误删

**现象**:某集群每隔几小时就有镜像被删,Pod 起不来。

**根因**:
- kubelet imageGC 阈值过高(high=80%)
- 节点磁盘紧张,频繁 GC
- 删了正在用的镜像

**修复**:
```yaml
# kubelet 配置
--image-gc-high-threshold=85
--image-gc-low-threshold=80
--image-minimum-ttl-duration=2h
```

---

## 13. 参考与延伸

### 官方文档

- OCI Runtime Spec — https://github.com/opencontainers/runtime-spec
- OCI Image Spec — https://github.com/opencontainers/image-spec
- containerd — https://containerd.io/
- CRI-O — https://cri-o.io/
- runc — https://github.com/opencontainers/runc
- K8s CRI — https://kubernetes.io/docs/concepts/architecture/cri/

### 工具

- ctr — containerd 自带 CLI
- nerdctl — containerd 友好 CLI
- crictl — K8s CRI 调试
- runc — 直接调用低级运行时
- dive — 镜像分层分析

### 论文 / 文章

- *The Containerd Architecture* — Docker Blog
- *Why Kubernetes Deprecated Docker* — CNCF Blog
- *Kata Containers: The Fastest Secure Container* — katacontainers.io

### 大厂实践

- Google gVisor — https://gvisor.dev/
- AWS Firecracker — https://firecracker-microvm.github.io/
- 阿里 Kata Containers — 强隔离场景

### 相关模块

- [10-底层原理-UnionFS](./10-底层原理-UnionFS.md) — 镜像存储
- [09-底层原理-cgroups](./09-底层原理-cgroups.md) — 资源限制
- [08-底层原理-namespaces](./08-底层原理-namespaces.md) — 隔离机制
- [12-安全与隔离](./12-安全与隔离.md) — gVisor / Kata
- [19-容器生态对比](./19-容器生态对比.md) — 运行时对比
- [25-工业实战-故障复盘集](./25-工业实战-故障复盘集.md) — 运行时故障

---

> **下一章**:[12-安全与隔离](./12-安全与隔离.md)

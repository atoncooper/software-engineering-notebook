# 19. Pod 安全

> 关键词:PSA、PSP、seccomp、AppArmor、SELinux、gVisor、Kata、Firecracker、privileged、capabilities

------

## 19.1 问题定义

Linux 容器 **不是安全边界**。容器内进程与主机内核共享:

- 系统调用接口(全 300+ syscalls)
- 内核 namespace(隔离不彻底)
- 内核漏洞(提权风险)
- 资源限制(cgroup 可被绕过)

K8s 默认允许 `privileged: true` 的 Pod,**等于把节点 root 权限交给容器**。

**核心问题**:

> 如何限制 Pod 的权限,防止容器逃逸、提权攻击、横向移动?PSP 弃用后,PSA(Pod Security Admission)如何接棒?

------

## 19.2 直觉解释

把 Pod 安全想象成 **机场安检级别**:

| 安检级别 | Pod 安全 |
|---------|---------|
| 贵宾通道(privileged) | 不设限,任意系统调用 |
| 商务舱(baseline) | 大部分限制,少量允许 |
| 经济舱(restricted) | 严格限制,生产推荐 |
| 拒绝登机(audit/warn) | 违规不阻断但记录 |
| 取消航班(deny) | 违规直接拒绝 |

关键点:**安全是分层** 的,从 namespace 到 Pod 到容器到内核,每层都要管。

------

## 19.3 核心概念

### 19.3.1 Pod Security Admission 三级

K8s 1.25+ 正式版,替代 PSP:

| 级别 | 严格度 | 用途 |
|------|--------|------|
| **privileged** | 不限制 | 系统组件、特殊场景 |
| **baseline** | 中等 | 防最危险提权 | 
| **restricted** | 严格 | 生产标准 |

**restricted 级别要求**:
- 必须 runAsNonRoot
- 必须 drop ALL capabilities
- seccomp = RuntimeDefault
- 不允许 privilege escalation
- 不允许 hostPath/hostNetwork/hostPID/hostIPC
- 必须 set runAsUser/runAsGroup

### 19.3.2 模式(mode)

PSA 提供四种执行模式:

```yaml
# 命名空间级 label
pod-security.kubernetes.io/enforce: restricted
pod-security.kubernetes.io/enforce-version: latest

pod-security.kubernetes.io/audit: restricted
pod-security.kubernetes.io/audit-version: latest

pod-security.kubernetes.io/warn: restricted
pod-security.kubernetes.io/warn-version: latest

pod-security.kubernetes.io/enforce-restricted: exempt  # 例外
```

| 模式 | 行为 |
|------|------|
| **enforce** | 违规拒绝创建 |
| **audit** | 创建成功,但写 audit annotation |
| **warn** | 创建成功,但返回 warning 给客户端 |
| **exempt** | 完全豁免(谨慎) |

可同时启用三种模式,如 `enforce: restricted + audit: restricted + warn: restricted`。

### 19.3.3 privileged 容器的危险

```yaml
spec:
  containers:
  - name: app
    image: app:v1
    securityContext:
      privileged: true   # ⚠️ 等同 root 主机访问
      # 可访问 /dev、/proc、/sys
      # 可加载内核模块
      # 可修改 cgroup
      # 可见所有进程(hostPID)
      # 可挂载主机文件系统
```

**逃逸路径**:
1. 容器内 root + privileged → 挂载主机磁盘 → chroot 逃逸
2. 加载内核模块 → 任意内核操作
3. 访问 /proc/1/root → 主机文件系统
4. 容器内 CAP_SYS_ADMIN → mount 主机根目录

### 19.3.4 Linux Capabilities

K8s 默认 drop 一部分能力,但仍保留:

```
默认保留:
  CAP_CHOWN, CAP_DAC_OVERRIDE, CAP_FSETID, CAP_FOWNER,
  CAP_MKNOD, CAP_NET_RAW, CAP_SETGID, CAP_SETUID,
  CAP_SETFCAP, CAP_SETPCAP, CAP_NET_BIND_SERVICE,
  CAP_SYS_CHROOT, CAP_KILL, CAP_AUDIT_WRITE

危险能力(应 drop):
  CAP_SYS_ADMIN       - 超级管理(最危险)
  CAP_SYS_MODULE      - 加载内核模块
  CAP_SYS_PTRACE      - ptrace 进程
  CAP_NET_ADMIN       - 网络配置
  CAP_NET_RAW         - 原始套接字(可伪造 IP)
  CAP_DAC_READ_SEARCH - 绕过文件权限
  CAP_LINUX_IMMUTABLE - 修改文件不可变属性

restricted 级别要求:
  drop: ALL   # 全部 drop,然后按需 add
```

### 19.3.5 seccomp(secure computing mode)

```
seccomp 限制进程可调用的系统调用集:
  - 默认 seccomp profile(RuntimeDefault):仅允许 ~300 个安全 syscall
  - 阻断:ptrace、mount、reboot、kexec_load 等
  - Docker/containerd 内置默认 profile

K8s 配置:
  securityContext:
    seccompProfile:
      type: RuntimeDefault   # 用运行时默认 profile
      # 或
      type: Localhost
      localhostProfile: profiles/my-profile.json

restricted 级别要求:必须设置 seccomp
```

### 19.3.6 AppArmor / SELinux

```
AppArmor(Ubuntu/Debian 默认):
  - 基于路径的 MAC
  - profile 文件定义可访问资源
  - K8s 注解绑定 profile:
    container.apparmor.security.beta.kubernetes.io/<container>: runtime/default
    或 localhost/profile-name

SELinux(RHEL/CentOS 默认):
  - 基于 label 的 MAC(更复杂)
  - type enforcement
  - K8s:
    securityContext:
      seLinuxOptions:
        level: "s0:c123,c456"
        type: "container_t"
        role: "container_r"
        user: "system_u"
```

### 19.3.7 沙箱运行时

```
传统 runc 容器:共享内核
  - 内核漏洞 = 容器逃逸
  - CVE-2019-5736(runc 逃逸)
  - CVE-2022-0185(cgroup 提权)

沙箱运行时:内核隔离
  ┌─ gVisor (runsc) ────────────────────┐
  │  应用进程                            │
  │  ↓                                   │
  │  Sentry(用户态内核,拦截 syscall)   │
  │  ↓                                   │
  │  KVM/Gvisor FS                       │
  │  ↓                                   │
  │  真实内核                            │
  └──────────────────────────────────────┘
  
  特点:
  - 用户态实现部分内核
  - 系统调用经 Sentry 拦截
  - 内核漏洞难逃逸
  - 性能损失 10-30%
  - 适合不可信代码

  ┌─ Kata Containers ───────────────────┐
  │  应用进程                            │
  │  ↓                                   │
  │  内嵌 VM(QEMU/Firecracker)         │
  │  ↓                                   │
  │  独立内核                            │
  │  ↓                                   │
  │  真实内核                            │
  └──────────────────────────────────────┘
  
  特点:
  - 每个容器/Pod 一个轻量 VM
  - 硬件虚拟化隔离
  - 性能损失 5-15%
  - 兼容性好(应用无感)
  
  ┌─ Firecracker (AWS) ─────────────────┐
  │  Rust 写的 microVM                   │
  │  - 极简:仅支持必要设备               │
  │  - 启动 125ms                        │
  │  - 内存占用 5MiB                     │
  │  - 适合 Serverless(Fargate/Lambda) │
  └──────────────────────────────────────┘
```

### 19.3.8 Pod Security 标准字段

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: secure-pod
spec:
  securityContext:          # Pod 级
    runAsNonRoot: true      # 必须 non-root
    runAsUser: 1000
    runAsGroup: 1000
    fsGroup: 1000
    supplementalGroups: [2000]
    seccompProfile:
      type: RuntimeDefault
    sysctls:                # 内核参数(白名单)
    - name: net.ipv4.ip_local_port_range
      value: "1024 65535"
  containers:
  - name: app
    image: app:v1
    securityContext:        # 容器级
      allowPrivilegeEscalation: false   # 不允许 sudo
      privileged: false
      readOnlyRootFilesystem: true       # 根文件系统只读
      runAsNonRoot: true
      runAsUser: 1000
      capabilities:
        drop: [ALL]
        add: [NET_BIND_SERVICE]
      seccompProfile:
        type: RuntimeDefault
    volumeMounts:
    - name: tmp
      mountPath: /tmp        # 写到 tmpfs
    - name: cache
      mountPath: /app/cache  # 持久化路径
  volumes:
  - name: tmp
    emptyDir: {medium: Memory}
  - name: cache
    emptyDir: {}
```

------

## 19.4 操作流程

### 19.4.1 Pod 创建时 PSA 检查流程

```
1. kubectl apply pod → APIServer
2. Authentication + Authorization
3. Admission 阶段:
   a. PodSecurityLabel admission controller
   b. 读取 namespace 的 PSA label
   c. 评估 Pod 是否符合级别(privileged/baseline/restricted)
   d. enforce 模式:不符合 → 拒绝
   e. audit 模式:不符合 → 记录 audit annotation
   f. warn 模式:不符合 → 返回 warning
4. 准入通过 → 写入 etcd
5. kubelet 调度执行
```

### 19.4.2 PSA 评估细节

```
PSA 评估级别:

restricted 检查项:
  1. Pod 级 securityContext.runAsNonRoot=true 或 runAsUser≠0
  2. 容器级 runAsNonRoot=true 或 runAsUser≠0
  3. allowPrivilegeEscalation=false
  4. capabilities.drop 包含 ALL
  5. seccompProfile 设置(RuntimeDefault 或 Localhost)
  6. 不允许 hostNetwork/hostPID/hostIPC
  7. 不允许 hostPath volumes
  8. 不允许 hostPort(部分版本)
  9. /proc mount 类型必须 Default 或 Unmasked(部分版本)

baseline 检查项(子集):
  1. 不允许 privileged
  2. 不允许 hostNetwork/hostPID/hostIPC
  3. 不允许 hostPath
  4. 不允许添加危险 capabilities(SYS_ADMIN 等)
  5. 不允许 hostPort

privileged:
  无任何限制
```

### 19.4.3 gVisor/Kata 部署流程

```
1. 安装运行时:
   gVisor: 安装 runsc
   Kata:   安装 kata-runtime
   
2. 配置 containerd:
   [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runsc]
     runtime_type = "io.containerd.runsc.v1"
   [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata]
     runtime_type = "io.containerd.kata.v2"
   
3. 创建 RuntimeClass:
   apiVersion: node.k8s.io/v1
   kind: RuntimeClass
   metadata:
     name: gvisor
   handler: runsc
   ---
   apiVersion: node.k8s.io/v1
   kind: RuntimeClass
   metadata:
     name: kata
   handler: kata
   
4. Pod 使用 RuntimeClass:
   spec:
     runtimeClassName: gvisor
```

------

## 19.5 底层原理

### 19.5.1 Linux Namespace 隔离

```
容器隔离基础 - Linux namespace:
  - PID:    进程 ID 隔离
  - NET:    网络栈隔离
  - MNT:    挂载点隔离
  - IPC:    信号量/共享内存隔离
  - UTS:    主机名隔离
  - USER:   UID/GID 映射
  - Cgroup: cgroup 视图隔离

容器内看到的"系统"实际是 namespace 内的视图,
但底层内核是同一个。

漏洞:
  - 内核漏洞 = 全部容器沦陷
  - CAP_SYS_ADMIN + privileged = 逃逸
  - namespace 不隔离:内核模块、设备
```

### 19.5.2 cgroup 限制

```
cgroup v1(传统):
  - 子系统独立(cpu、memory、blkio 等)
  - 容器间资源隔离
  - 但可被绕过(如 CAP_SYS_ADMIN 重设)

cgroup v2(现代,K8s 1.25+ 默认):
  - 统一层级
  - 更安全
  - 更精确的资源统计
  - 与 systemd 集成

Pod 资源限制:
  spec.containers.resources:
    requests: {cpu: 100m, memory: 128Mi}
    limits:   {cpu: 500m, memory: 256Mi}
    
  实现:
    cpu.shares/cpu.max(cpu)
    memory.max(memory)
    io.max(blkio)
```

### 19.5.3 seccomp-bpf

```
seccomp 工作原理:
  1. 进程启动时 attach seccomp filter(BPF 程序)
  2. 每次 syscall 进入内核前,BPF 程序执行
  3. BPF 决定:
     - ALLOW:放行
     - DENY:拒绝(返回 EPERM)
     - TRACE:记录
     - KILL:杀进程

profile JSON 示例:
  {
    "defaultAction": "SCMP_ACT_ERRNO",
    "syscalls": [
      {
        "names": ["read", "write", "open", "close", ...],
        "action": "SCMP_ACT_ALLOW"
      },
      {
        "names": ["ptrace", "mount", "reboot"],
        "action": "SCMP_ACT_ERRNO"
      }
    ]
  }

RuntimeDefault profile:
  - containerd/docker 内置
  - 阻断已知危险 syscall
  - K8s 1.25+ restricted 级别要求
```

### 19.5.4 AppArmor 工作机制

```
AppArmor profile:
  /etc/apparmor.d/usr.bin.example
  profile example {
    /etc/passwd r,             # 允许读
    /etc/shadow r,             # 允许读(若指定)
    /var/log/example/* rw,     # 允许读写
    /tmp/** rw,                # 递归
    
    deny /etc/shadow r,        # 显式拒绝
    
    network inet stream,       # 允许 IPv4 TCP
    network inet6 dgram,       # 允许 IPv6 UDP
    
    capability net_bind_service,
    deny capability sys_admin,
  }

K8s 绑定:
  metadata:
    annotations:
      container.apparmor.security.beta.kubernetes.io/<container>: localhost/example-profile
  # 或
      container.apparmor.security.beta.kubernetes.io/<container>: runtime/default
```

### 19.5.5 SELinux 工作机制

```
SELinux 类型强制(Type Enforcement):
  - 每个对象(subject/object)有 type label
  - 规则定义:subject_type → object_type : permissions
  
  例:
    allow container_t container_file_t : file { read write };
    allow container_t var_log_t : file { read };
    deny  container_t shadow_t : file { read };

容器默认 type:
  - 进程:container_t
  - 文件:container_file_t
  - 卷:container_file_t

K8s:
  securityContext:
    seLinuxOptions:
      user: system_u
      role: system_r
      type: container_t
      level: s0:c123,c456   # MLS/MCS 隔离
```

### 19.5.6 gVisor 工作机制

```
gVisor(runsc)架构:

传统容器(runc):
  应用 → 系统调用 → 内核 → 硬件
  
gVisor(runsc):
  应用 → 系统调用 → Sentry(用户态内核)
                      ↓
                    9p/KVM Gofer(文件系统)
                      ↓
                    KVM(部分系统调用)
                      ↓
                    内核 → 硬件

Sentry 实现:
  - 实现大部分 syscall(用户态)
  - 不进入内核,降低逃逸风险
  - 网络栈用户态实现(netstack)
  - 文件系统经 Gofer 进程访问

性能:
  - 系统调用慢(用户态处理)
  - 内存/CPU 开销 10-30%
  - 网络 I/O 慢
  - 适合不可信代码、低并发场景
```

### 19.5.7 Kata Containers 工作机制

```
Kata 架构:
  Pod → containerd-shim-kata-v2
          ↓
        hypervisor(QEMU/Firecracker)
          ↓
        VM(独立内核)
          ↓
        应用容器(runc 在 VM 内)
          ↓
        真实内核(主机)

特点:
  - 每个 Pod 一个轻量 VM
  - VM 内运行 runc 容器
  - 双层隔离:VM + 容器
  - 内核漏洞无法逃逸 VM
  - 应用完全无感(标准 OCI)

性能:
  - VM 启动开销(几百 ms)
  - 内存开销(50-100 MiB/VM)
  - CPU 接近原生(KVM 加速)
  - 网络/磁盘略慢(virtio)
```

### 19.5.8 Pod Security 与 PSP 的差异

```
PSP(PodSecurityPolicy,K8s 1.21 弃用,1.25 移除):
  - 集群级资源
  - 通过 RBAC 绑定
  - 复杂、易错
  - 一个 Pod 可能匹配多个 PSP(优先级混乱)

PSA(Pod Security Admission,K8s 1.25+):
  - namespace label 配置
  - 三级别(privileged/baseline/restricted)
  - 简单清晰
  - 内置 admission controller
  - 不依赖 RBAC

迁移建议:
  - 评估当前 PSP 配置
  - 转换为 PSA label
  - 复杂策略用 OPA/Gatekeeper 或 Kyverno
```

------

## 19.6 配置示例

### 19.6.1 PSA 命名空间配置

```yaml
# 生产命名空间:strict
apiVersion: v1
kind: Namespace
metadata:
  name: production
  labels:
    pod-security.kubernetes.io/enforce: restricted
    pod-security.kubernetes.io/enforce-version: latest
    pod-security.kubernetes.io/audit: restricted
    pod-security.kubernetes.io/audit-version: latest
    pod-security.kubernetes.io/warn: restricted
    pod-security.kubernetes.io/warn-version: latest
---
# 测试命名空间:中等
apiVersion: v1
kind: Namespace
metadata:
  name: staging
  labels:
    pod-security.kubernetes.io/enforce: baseline
    pod-security.kubernetes.io/audit: restricted
    pod-security.kubernetes.io/warn: restricted
---
# 系统命名空间:特权
apiVersion: v1
kind: Namespace
metadata:
  name: kube-system
  labels:
    pod-security.kubernetes.io/enforce: privileged
```

### 19.6.2 restricted 级别 Pod 模板

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: secure-app
  namespace: production
spec:
  securityContext:
    runAsNonRoot: true
    runAsUser: 10001
    runAsGroup: 10001
    fsGroup: 10001
    seccompProfile:
      type: RuntimeDefault
  containers:
  - name: app
    image: app:v1
    securityContext:
      allowPrivilegeEscalation: false
      readOnlyRootFilesystem: true
      runAsNonRoot: true
      runAsUser: 10001
      capabilities:
        drop: [ALL]
        add: []   # 不添加任何 capability
      seccompProfile:
        type: RuntimeDefault
    resources:
      requests: {cpu: 100m, memory: 128Mi}
      limits:   {cpu: 500m, memory: 256Mi}
    volumeMounts:
    - {name: tmp, mountPath: /tmp}
    - {name: cache, mountPath: /app/cache}
    - {name: config, mountPath: /etc/app, readOnly: true}
  volumes:
  - name: tmp
    emptyDir: {medium: Memory}
  - name: cache
    emptyDir: {}
  - name: config
    configMap:
      name: app-config
```

### 19.6.3 自定义 seccomp profile

```json
// /var/lib/kubelet/seccomp/profiles/app.json
{
  "defaultAction": "SCMP_ACT_ERRNO",
  "defaultErrnoRet": 1,
  "architectures": ["SCMP_ARCH_X86_64"],
  "syscalls": [
    {
      "names": [
        "read", "write", "open", "openat", "close", "lseek",
        "mmap", "mprotect", "munmap", "brk", "rt_sigaction",
        "rt_sigprocmask", "rt_sigreturn", "ioctl", "pread64",
        "pwrite64", "readv", "writev", "access", "pipe", "pipe2",
        "select", "poll", "epoll_create1", "epoll_ctl", "epoll_wait",
        "socket", "connect", "accept", "accept4", "bind", "listen",
        "getsockname", "getpeername", "sendto", "recvfrom",
        "setsockopt", "getsockopt", "shutdown",
        "fork", "vfork", "execve", "execveat", "wait4", "exit", "exit_group",
        "uname", "stat", "fstat", "lstat", "getcwd",
        "dup", "dup2", "dup3", "fcntl", "flock",
        "fsync", "fdatasync", "truncate", "ftruncate",
        "getdents", "getdents64", "getpid", "getppid",
        "getuid", "geteuid", "getgid", "getegid",
        "setuid", "setgid", "setreuid", "setregid",
        "clock_gettime", "nanosleep",
        "fcntl64"
      ],
      "action": "SCMP_ACT_ALLOW"
    },
    {
      "names": ["ptrace", "mount", "umount", "reboot", "kexec_load"],
      "action": "SCMP_ACT_ERRNO"
    }
  ]
}
```

```yaml
# Pod 使用
spec:
  securityContext:
    seccompProfile:
      type: Localhost
      localhostProfile: profiles/app.json
```

### 19.6.4 AppArmor 配置

```bash
# 1. 编写 profile
cat > /etc/apparmor.d/usr.bin.myapp <<EOF
#include <tunables/global>

profile myapp flags=(attach_disconnected) {
  #include <abstractions/base>
  
  /usr/bin/myapp mr,
  /etc/myapp/** r,
  /var/lib/myapp/** rw,
  /tmp/** rw,
  
  network inet stream,
  network inet dgram,
  
  deny /etc/shadow r,
  deny /etc/passwd w,
  deny /proc/*/mem rwklx,
  
  capability net_bind_service,
}
EOF

# 2. 加载 profile
apparmor_parser -r /etc/apparmor.d/usr.bin.myapp

# 3. 验证
aa-status | grep myapp
```

```yaml
# Pod 注解
metadata:
  annotations:
    container.apparmor.security.beta.kubernetes.io/app: localhost/myapp
```

### 19.6.5 RuntimeClass 配置

```yaml
# 1. 创建 RuntimeClass
apiVersion: node.k8s.io/v1
kind: RuntimeClass
metadata:
  name: gvisor
handler: runsc
scheduling:
  nodeSelector:
    kubernetes.io/os: linux
  tolerations:
  - key: sandbox-runtime
    operator: Equal
    value: gvisor
    effect: NoSchedule
---
apiVersion: node.k8s.io/v1
kind: RuntimeClass
metadata:
  name: kata
handler: kata-qemu
---
# 2. Pod 使用
apiVersion: v1
kind: Pod
metadata:
  name: untrusted-app
spec:
  runtimeClassName: gvisor
  containers:
  - name: app
    image: untrusted:v1
```

### 19.6.6 containerd 多运行时配置

```toml
# /etc/containerd/config.toml
version = 2

[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc]
  runtime_type = "io.containerd.runc.v2"

[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runsc]
  runtime_type = "io.containerd.runsc.v1"

[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata]
  runtime_type = "io.containerd.kata.v2"
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata.options]
    ConfigPath = "/etc/kata-containers/configuration.toml"
```

### 19.6.7 受限能力的 Pod(需要特定 capability)

```yaml
# 仅需 NET_BIND_SERVICE(绑定 80 端口)
apiVersion: v1
kind: Pod
metadata:
  name: web
spec:
  securityContext:
    runAsNonRoot: true
    runAsUser: 10001
    seccompProfile:
      type: RuntimeDefault
  containers:
  - name: nginx
    image: nginx:1.25
    securityContext:
      allowPrivilegeEscalation: false
      capabilities:
        drop: [ALL]
        add: [NET_BIND_SERVICE]   # 仅添加这一个
      seccompProfile:
        type: RuntimeDefault
    ports:
    - containerPort: 80
```

### 19.6.8 系统组件例外(exempt)

```yaml
# APIServer 配置
--admission-control-config-file=/etc/kubernetes/psa-config.yaml

# psa-config.yaml
apiVersion: apiserver.config.k8s.io/v1
kind: AdmissionConfiguration
plugins:
- name: PodSecurity
  configuration:
    apiVersion: pod-security.admission.config.k8s.io/v1
    kind: PodSecurityConfiguration
    defaults:
      enforce: "privileged"
      enforce-version: "latest"
      audit: "privileged"
      audit-version: "latest"
      warn: "privileged"
      warn-version: "latest"
    exemptions:
      usernames: []
      runtimeClasses: []
      namespaces: ["kube-system", "kube-public", "kube-node-lease", "ingress-nginx"]
```

------

## 19.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | privileged: true 滥用 | 容器逃逸风险 | 严格限制 |
| 2 | runAsUser: 0 | root 提权 | runAsNonRoot: true |
| 3 | 未 drop capabilities | 权限过大 | drop: [ALL] + 按需 add |
| 4 | 未设 seccomp | 系统调用全开 | RuntimeDefault |
| 5 | allowPrivilegeEscalation: true | sudo 提权 | false |
| 6 | readOnlyRootFilesystem: false | 写文件逃逸 | true + 挂 emptyDir |
| 7 | hostPath 挂载敏感路径 | 主机文件泄露 | 禁用或限定路径 |
| 8 | hostNetwork: true | 看到主机网络 | 禁用 |
| 9 | hostPID: true | 看到主机进程 | 禁用 |
| 10 | 用 root 镜像但不设 runAsNonRoot | 不生效 | 显式 runAsNonRoot: true |
| 11 | gVisor 性能损失低估 | 应用慢 | 评估后选型 |
| 12 | Kata 启动慢 | Pod 启动延迟 | 热池预热 |
| 13 | RuntimeClass 节点未配置 | Pod 创建失败 | 节点亲和性 |
| 14 | AppArmor profile 未加载 | Pod 创建失败 | 节点配置 |
| 15 | seccomp profile 路径错 | Pod 创建失败 | 标准 /var/lib/kubelet/seccomp |
| 16 | 系统组件也被 restricted | 集群故障 | namespace exempt |
| 17 | PSP 迁移漏项 | 升级失败 | 提前迁移到 PSA + Kyverno |

------

## 19.8 工业案例

### 19.8.1 阿里 ACK:沙箱容器

**场景**:Serverless 容器(ASK)需要强隔离,运行第三方代码。

**方案**:
1. Kata Containers(gVisor 备选)
2. Pod 级 VM 隔离
3. 每用户独立沙箱
4. 网络命名空间 + NetworkPolicy
5. 资源严格限制

**收益**:
- 多租户安全
- 不可信代码隔离
- 性能损失 <15%

### 19.8.2 字节跳动:Pod 安全基线

**场景**:全公司 10000+ Pod,需统一安全基线。

**方案**:
1. 所有生产 namespace 强制 restricted
2. 监控违规 Pod(audit 模式)
3. 渐进式 enforce
4. 自研工具检测 Pod 安全风险

**规则**:
- 必须 runAsNonRoot
- 必须 drop ALL capabilities
- 必须 seccomp=RuntimeDefault
- 禁止 hostPath/hostNetwork/hostPID
- 镜像必须来自可信仓库

**收益**:安全事件降低 80%。

### 19.8.3 Google GKE:Shielded GKE Nodes

**GKE 安全增强**:
1. Shielded Nodes(硬件信任根)
2. Confidential Computing(内存加密)
3. Workload Identity(IAM 集成)
4. Binary Authorization(镜像签名)
5. Policy Controller(ODIC + OPA)

**适合金融、医疗等高合规场景**。

### 19.8.4 AWS EKS:Bottlerocket + Firecracker

**场景**:Fargate Serverless 容器,需强隔离。

**方案**:
1. Bottlerocket(专用 OS,只读根)
2. Firecracker microVM(每个 Pod 一个 VM)
3. 不可变基础设施
4. 自动安全更新

**结果**:
- VM 级隔离
- 启动 <500ms
- 内存 <10MiB/VM
- 适合 Serverless

### 19.8.5 Netflix:不可信代码沙箱

**场景**:用户上传代码运行,需防逃逸。

**方案**:
1. gVisor 运行时
2. Pod 级 NetworkPolicy 限制
3. 资源限制(CPU/内存/磁盘)
4. 出站仅允许特定 API
5. 定期重启(短生命周期)
6. 审计日志全记录

**结果**:运行数百万次用户代码,零逃逸事件。

------

## 19.9 与其他方案关系

### 19.9.1 PSA vs PSP

| 维度 | PSP | PSA |
|------|-----|-----|
| K8s 版本 | 1.21 弃用,1.25 移除 | 1.25+ 默认 |
| 配置 | ClusterRole + RBAC | namespace label |
| 灵活度 | 高(细粒度) | 中(三级别) |
| 复杂度 | 高 | 低 |
| 推荐替代 | - | Kyverno/OPA |

### 19.9.2 容器 vs VM 隔离

| 维度 | 容器(runc) | 沙箱(gVisor/Kata) | VM |
|------|------------|---------------------|-----|
| 隔离 | namespace(弱) | 用户态内核/VM(强) | 硬件(最强) |
| 性能 | 最高 | 中(10-30% 损失) | 低 |
| 启动 | 毫秒 | 百毫秒 | 秒 |
| 资源 | 极少 | 中 | 多 |
| 适合 | 可信代码 | 不可信/多租户 | 强隔离 |

### 19.9.3 seccomp vs AppArmor vs SELinux

| 维度 | seccomp | AppArmor | SELinux |
|------|---------|----------|---------|
| 工作层 | syscall | 路径 | label |
| 配置 | JSON | profile 文件 | type 规则 |
| 复杂度 | 低 | 中 | 高 |
| 发行版 | 通用 | Ubuntu/Debian | RHEL/CentOS |
| K8s 集成 | securityContext | 注解 | securityContext |

**关系**:可叠加使用,纵深防御。

### 19.9.4 与镜像供应链安全

```
Pod 安全:运行时
镜像安全:构建时

互补:
  1. 构建时:镜像签名(Cosign)、SBOM、漏洞扫描(Trivy)
  2. 部署时:Binary Authorization(仅允许签名镜像)
  3. 运行时:PSA + seccomp + AppArmor
  
完整供应链:
  Source → Build(签名)→ Registry(扫描)→ Admission(验证)→ Runtime(限制)
```

------

## 19.10 面试速答

**Q1: K8s Pod 安全三级?**

privileged(无限制)、baseline(防最危险)、restricted(严格生产标准)。restricted 要求 non-root、drop ALL capabilities、seccomp=RuntimeDefault、不允许 hostPath 等。

**Q2: PSA 与 PSP 区别?**

PSP(K8s 1.25 移除)集群级资源,通过 RBAC 绑定,复杂易错。PSA(K8s 1.25+)namespace label 配置,三级别简单清晰,内置 admission controller。

**Q3: PSA 四种模式?**

enforce(拒绝)、audit(记录)、warn(警告)、exempt(豁免)。可同时启用三种模式。

**Q4: privileged 容器为什么危险?**

容器获得节点 root 权限,可访问 /dev、/proc、/sys,挂载主机磁盘,加载内核模块,完全逃逸。

**Q5: seccomp 作用?**

限制容器可调用的系统调用集,阻断 ptrace、mount、reboot 等危险 syscall。RuntimeDefault profile 由运行时提供。

**Q6: gVisor 与 Kata 区别?**

gVisor 用户态实现部分内核(Sentry),拦截 syscall。Kata 每 Pod 一个轻量 VM,独立内核。gVisor 性能损失 10-30%,Kata 5-15%。

**Q7: restricted 级别必须满足哪些?**

- runAsNonRoot=true(或 runAsUser≠0)
- allowPrivilegeEscalation=false
- capabilities.drop=[ALL]
- seccompProfile 设置
- 不允许 hostNetwork/hostPID/hostIPC/hostPath

**Q8: Pod 安全的多层防御?**

1. namespace 隔离
2. PSA(restricted)
3. seccomp + capabilities drop
4. AppArmor/SELinux
5. NetworkPolicy
6. 沙箱运行时(不可信代码)
7. 镜像签名(供应链)

**Q9: 容器为什么不是安全边界?**

容器与主机共享内核,内核漏洞=全部容器沦陷。namespace 隔离不彻底,capability 可被滥用。

**Q10: ReadOnlyRootFilesystem 有什么用?**

容器根文件系统只读,攻击者无法写入恶意脚本、持久化后门。需要写的路径用 emptyDir 挂载。

------

## 19.11 综合面试题

### 题 1:设计金融级 K8s 集群的 Pod 安全方案

```
需求:金融业务,合规严格,需防内部威胁

设计:
1. 镜像供应链:
   - 私有仓库 + 镜像签名(Cosign)
   - SBOM 生成 + 漏洞扫描(Trivy)
   - Binary Authorization(仅签名镜像)
   
2. Pod 安全:
   - 全集群 restricted 级别
   - 强制 runAsNonRoot
   - drop ALL capabilities + 按需 add
   - seccomp RuntimeDefault
   - AppArmor 限制路径
   
3. 网络隔离:
   - NetworkPolicy 默认 deny
   - 命名空间隔离
   - mTLS(Istio)
   
4. 沙箱运行时:
   - 不可信代码用 Kata
   - 业务容器用 runc(性能优先)
   
5. 资源限制:
   - requests/limits 严格
   - ResourceQuota 命名空间级
   - LimitRange 默认值
   
6. 审计:
   - audit log 接 SIEM
   - 异常 API 调用告警
   - Pod 行为基线监控
   
7. 合规:
   - CIS Benchmark 定期扫描
   - PCI-DSS 合规
   - 等保 2.0 三级
   
8. 应急:
   - 安全事件响应预案
   - 容器隔离/杀掉
   - 节点隔离
   - 镜像回滚
```

### 题 2:容器逃逸路径有哪些?如何防?

```
逃逸路径:
1. privileged 容器
   - 挂载主机磁盘 → chroot
   - 加载内核模块
   
2. CAP_SYS_ADMIN
   - mount 任意文件系统
   - 修改 cgroup
   
3. CAP_SYS_MODULE
   - 加载内核模块 → 任意操作
   
4. CAP_SYS_PTRACE
   - 注入进程
   
5. CAP_NET_RAW
   - ARP 欺骗
   - 网络扫描
   
6. 内核漏洞
   - CVE-2019-5736(runc 逃逸)
   - CVE-2022-0185(cgroup 提权)
   - CVE-2022-0492(cgroup escape)
   
7. 共享 namespace
   - hostPID/hostNetwork/hostIPC
   - 看到主机进程/网络
   
8. hostPath 挂载
   - /var/run/docker.sock
   - /etc /root /home

防御:
1. restricted PSA
2. drop ALL capabilities
3. seccomp RuntimeDefault
4. 禁 hostPath/hostPID/hostNetwork
5. 沙箱运行时(不可信代码)
6. 内核及时升级
7. 漏洞扫描 + 监控
8. 最小权限
```

### 题 3:PSA 部署后某些系统 Pod 无法启动,怎么办?

```
现象:
  - CNI/CSI/Ingress 等 DaemonSet Pod 创建失败
  - 错误:violates PodSecurity "restricted"

原因:
  - 系统 namespace 误配 restricted
  - 系统 Pod 需要 hostPath/privileged

解决:
1. 系统命名空间 exempt:
   kube-system/kube-public/kube-node-lease
   ingress-nginx/cilium-system/...
   
2. 配置:
   apiVersion: v1
   kind: Namespace
   metadata:
     name: kube-system
     labels:
       pod-security.kubernetes.io/enforce: privileged
   
3. 或在 APIServer 配置文件:
   exemptions:
     namespaces: ["kube-system", "ingress-nginx"]

4. 渐进式迁移:
   - 先 audit + warn 模式
   - 监控违规
   - 修复 Pod
   - 再 enforce
```

### 题 4:解释 gVisor 性能损失来源

```
gVisor 性能损失(10-30%)原因:

1. syscall 路径变长:
   - 传统:应用 → syscall → 内核
   - gVisor:应用 → syscall → Sentry → 内核(部分)
   
2. Sentry 用户态实现:
   - 系统调用在用户态处理
   - 上下文切换开销
   - 部分需陷入内核(KVM 加速)

3. 文件系统:
   - 经 9p 协议到 Gofer 进程
   - 跨进程通信开销
   - 缓存层级多

4. 网络栈:
   - 用户态 netstack
   - 不能用内核优化(GSO/GRO)
   - 包处理路径长

5. 内存:
   - Sentry 自身内存开销
   - page cache 双份

性能场景:
  - CPU 密集:损失 10-15%
  - 内存密集:损失 10-20%
  - I/O 密集:损失 20-30%
  - 网络 I/O:损失 30%+

适合场景:
  - 不可信代码
  - 低并发服务
  - 多租户隔离
  
不适合:
  - 高性能数据库
  - 大流量网关
  - 实时系统
```

### 题 5:如何实现 K8s 集群的"零信任容器"?

```
零信任容器架构:

1. 身份:
   - SPIFFE/SPIRE 工作负载身份
   - 短期证书(自动轮转)
   - mTLS 服务间认证

2. 镜像:
   - Cosign 签名
   - SBOM 物料清单
   - Trivy 漏洞扫描
   - Binary Authorization
   
3. 部署:
   - PSA restricted
   - seccomp + AppArmor
   - drop ALL capabilities
   - NetworkPolicy 默认 deny
   
4. 运行时:
   - 沙箱运行时(不可信代码)
   - Falco 行为监控
   - 异常进程告警
   
5. 网络:
   - mTLS(Istio)
   - NetworkPolicy
   - Egress Gateway
   - DNS 加密(DoH/DoT)
   
6. 数据:
   - etcd 加密(KMS)
   - Secret 加密
   - 卷加密(LUKS)
   
7. 审计:
   - 全 audit log
   - SIEM 集成
   - 行为基线
   - 异常检测
   
8. 治理:
   - OPA/Gatekeeper 策略
   - CIS Benchmark
   - 合规扫描
   - 渗透测试
```

### 题 6:解释 capabilities 与 root 的关系

```
Linux capabilities 把传统 root 权限拆分为细粒度能力:

传统 root(UID 0)拥有所有 capabilities
非 root 进程无 capabilities(默认)

容器内 root:
  - 默认拥有一部分 capabilities
  - 即使 drop 全部,仍是 UID 0
  - 仍可读取 /etc/passwd 等
  
容器内 non-root:
  - 无 capabilities
  - 文件权限按 UID 1000 等
  - 即使拥有 CAP_SYS_ADMIN,影响范围有限

最佳实践:
1. runAsNonRoot: true(runAsUser: 10001)
2. drop: [ALL]
3. 按需 add(如 NET_BIND_SERVICE)

陷阱:
1. 镜像用 root,即使 K8s 设 runAsUser
   - 镜像内进程可能 setuid 0
   - 必须看镜像 Dockerfile
   
2. capabilities add 误加 SYS_ADMIN
   - 等同 privileged
   - 严禁
   
3. SUID 二进制
   - 容器内 su/sudo
   - allowPrivilegeEscalation=false 阻止
```

------

## 19.12 故障复盘

### 案例 1:privileged 容器被逃逸

**故障时间**:2023-08-22

**故障现象**:
- 安全审计发现节点被入侵
- 攻击者从容器逃逸到主机

**根因**:
- 监控 Agent 用 privileged 方便采集
- 攻击者通过 Agent 漏洞逃逸
- 主机 root 权限沦陷

**修复**:
1. 移除 privileged,改用受限 capabilities
2. 必要权限通过专门的 DaemonSet 提供
3. 行为监控(Falco)检测异常

**经验**:privileged 等于把节点交给容器,严禁生产使用。

### 案例 2:runAsUser=0 仍能跑

**故障时间**:2023-12-10

**故障现象**:
- 配置 runAsUser: 1000 但容器内 id 显示 0

**根因**:
- 镜像内进程 setuid 0
- K8s runAsUser 仅初始 UID
- 进程可自行 setuid

**修复**:
1. runAsNonRoot: true(K8s 强制校验)
2. allowPrivilegeEscalation: false
3. 镜像改造,移除 setuid 二进制

**经验**:仅设 runAsUser 不够,必须 runAsNonRoot: true。

### 案例 3:seccomp profile 路径错误

**故障时间**:2024-03-08

**故障现象**:
- Pod 创建失败
- 错误:seccomp profile not found

**根因**:
- localhostProfile 路径错
- 必须相对 /var/lib/kubelet/seccomp

**修复**:
```yaml
seccompProfile:
  type: Localhost
  localhostProfile: profiles/app.json
  # 实际路径:/var/lib/kubelet/seccomp/profiles/app.json
```

**经验**:seccomp profile 必须放在所有节点固定路径。

### 案例 4:Kata 启动慢影响伸缩

**故障时间**:2024-05-30

**故障现象**:
- 高峰期扩容时 Pod 启动慢
- Kata VM 启动 800ms+
- 集群伸缩跟不上

**修复**:
1. Kata VM 预热池(保持 N 个空 VM)
2. 用 Firecracker 替代 QEMU(启动 125ms)
3. 优化 Pod 启动顺序

**经验**:沙箱运行时启动延迟需预热或换更轻方案。

### 案例 5:PSP 升级 PSA 漏项

**故障时间**:2024-07-15

**故障现象**:
- 集群升级到 K8s 1.25 后,部分 Pod 创建失败
- 错误:violates PodSecurity

**根因**:
- PSP 已移除
- 复杂 PSP 策略未完全迁移到 PSA
- 部分 namespace 无 PSA label

**修复**:
1. 紧急给系统 namespace 加 privileged label
2. 业务 namespace audit 模式
3. 复杂策略迁移到 Kyverno

**经验**:K8s 1.25 升级前必须完成 PSP→PSA 迁移评估。

------

## 19.13 参考与延伸

### 官方文档
- [Pod Security Admission](https://kubernetes.io/docs/concepts/security/pod-security-admission/)
- [Pod Security Standards](https://kubernetes.io/docs/concepts/security/pod-security-standards/)
- [Configure Service Account](https://kubernetes.io/docs/reference/access-authn-authz/service-accounts-admin/)
- [Seccomp](https://kubernetes.io/docs/tutorials/security/seccomp/)
- [AppArmor](https://kubernetes.io/docs/tutorials/security/apparmor/)

### KEP
- [KEP-2579: Pod Security Admission](https://github.com/kubernetes/enhancements/tree/master/keps/sig-auth/2579-psp-replacement)
- [KEP-1357: RuntimeClass](https://github.com/kubernetes/enhancements/tree/master/keps/sig-node/1357-runtime-class)

### 沙箱运行时
- [gVisor](https://gvisor.dev/)
- [Kata Containers](https://katacontainers.io/)
- [Firecracker](https://firecracker-microvm.github.io/)
- [Wasm Runtime](https://wasmer.io/)

### 安全工具
- [Falco](https://falco.org/) - 运行时安全
- [Trivy](https://trivy.dev/) - 漏洞扫描
- [Cosign](https://github.com/sigstore/cosign) - 镜像签名
- [kube-bench](https://github.com/aquasecurity/kube-bench) - CIS 基线
- [kube-hunter](https://github.com/aquasecurity/kube-hunter) - 渗透测试

### 源码导航
- `kubernetes/pkg/security/podsecurity/` - PSA 评估
- `kubernetes/plugin/pkg/admission/security/podsecurity/` - admission

### 相关章节
- [17-RBAC与认证授权.md](./17-RBAC与认证授权.md) - API 层安全
- [18-NetworkPolicy与流量管控.md](./18-NetworkPolicy与流量管控.md) - 网络层安全
- [20-策略与治理.md](./20-策略与治理.md) - 策略引擎
- [13-kubelet与Pod生命周期.md](./13-kubelet与Pod生命周期.md) - kubelet 调运行时

### 推荐阅读
- [Container Security](https://kubernetes.io/docs/concepts/security/)
- [Linux Capabilities Manual](https://man7.org/linux/man-pages/man7/capabilities.7.html)
- [seccomp user notification](https://man7.org/linux/man-pages/man2/seccomp.2.html)
- [OWASP Docker Security](https://owasp.org/www-project-docker-security/)

### 工具
- `kubectl label ns` - 配置 PSA
- `kubectl get runtimeclass`
- `crictl inspect` - 容器运行时检查
- `aa-status` - AppArmor 状态
- `semodule -l` - SELinux 模块
- `falco` - 运行时监控

### 进阶主题
- **Confidential Computing**:机密计算(Intel SGX/AMD SEV)
- **Trusted Execution Environment**:可信执行环境
- **Image Signature Verification**:镜像签名验证
- **Behavioral Monitoring**:行为基线监控
- **Pod Identity**:SPIFFE 工作负载身份
- **Wasm Runtime**:WebAssembly 容器

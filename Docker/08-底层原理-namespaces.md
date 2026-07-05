# 08 - 底层原理 - namespaces

> 容器隔离的内核基石。理解 7 种 namespace,就理解了"容器为什么能隔离"。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 把 Linux **7 种 namespace** 讲到源码级(PID / NET / MNT / UTS / IPC / USER / CGROUP)
- 把 **`unshare` / `nsenter`** 实操讲到能手动创建容器
- 把 **容器逃逸** 的攻击面与防御讲到安全工程师水平
- 把 **大厂内核调优** 经验讲到能落地(inotify / PID / conntrack 调优)
- 把 **namespace 与 cgroup 的协作** 讲清(隔离 + 限制 = 容器)

### 1.2 本章不解决什么

- 不讲 cgroup(见 [09-底层原理-cgroups](./09-底层原理-cgroups.md))
- 不讲 UnionFS(见 [10-底层原理-UnionFS](./10-底层原理-UnionFS.md))
- 不讲容器运行时(containerd / runc,见 [11-OCI规范与运行时](./11-OCI规范与运行时.md))
- 不讲 K8s pod 共享 namespace 设计(见 [19-容器生态对比](./19-容器生态对比.md))

> **关键认知**:namespace 不是 Docker 发明的,是 Linux 内核 2.6.29(2008)起逐步加入的特性。Docker 只是把这些特性组合起来,变成"容器"。

---

## 2. 直觉解释

### 2.1 namespace 类比:虚拟办公室

```
   没有 namespace                    有 namespace
   ──────────────                    ────────────
   所有员工在同一大办公室            每个团队独立小办公室
   能看到所有人(PID)                只看到本团队
   能听到所有对话(IPC)              只听到本团队
   共享一个电话号码(UTS hostname)  各自独立号码
   共享文件柜(MNT)                  各自独立文件柜
```

**核心思想**:给进程一副"眼镜",让它看到的系统视图与真实系统不同。

### 2.2 7 种 namespace

| namespace | 隔离对象 | 内核版本 | 类比 |
|-----------|----------|----------|------|
| **PID** | 进程 ID | 2.6.24(2008) | 看不到其他进程 |
| **NET** | 网络栈(网卡、路由、防火墙) | 2.6.29(2009) | 独立网卡 |
| **MNT** | 挂载点(文件系统视图) | 2.6.19(2006) | 独立文件柜 |
| **UTS** | hostname / domainname | 2.6.19(2006) | 独立名字 |
| **IPC** | System V IPC / POSIX 队列 | 2.6.19(2006) | 独立信箱 |
| **USER** | UID / GID 映射 | 3.8(2013) | 假装是 root |
| **CGROUP** | cgroup 视图 | 4.6(2016) | 独立 cgroup 树 |

---

## 3. 核心概念与架构

### 3.1 namespace 在内核中的表示

```c
// 内核源码:include/linux/nsproxy.h
struct nsproxy {
    atomic_t count;              // 引用计数
    struct uts_namespace *uts_ns;   // UTS
    struct ipc_namespace *ipc_ns;   // IPC
    struct mnt_namespace *mnt_ns;   // MNT
    struct pid_namespace *pid_ns;   // PID(对于 task)
    struct net *net_ns;             // NET
    struct cgroup_namespace *cgroup_ns;  // CGROUP
};

// 每个进程的 task_struct 里有:
struct task_struct {
    struct nsproxy *nsproxy;     // 指向 namespace 代理
    ...
};
```

**关键**:多个进程可共享同一个 `nsproxy`(即在同一 namespace)。fork 默认共享父进程的 namespace,`clone` 带 `CLONE_NEW*` 标志则创建新 namespace。

### 3.2 7 种 namespace 的 `CLONE_NEW*` 标志

```c
clone(flags)  // 系统调用

CLONE_NEWPID    // 新 PID namespace
CLONE_NEWNET    // 新 NET namespace
CLONE_NEWNS     // 新 MNT namespace
CLONE_NEWUTS    // 新 UTS namespace
CLONE_NEWIPC    // 新 IPC namespace
CLONE_NEWUSER   // 新 USER namespace
CLONE_NEWCGROUP // 新 CGROUP namespace
```

### 3.3 namespace 的层级关系

```
PID / USER namespace 可以嵌套(父子关系):

init(PID 1, root namespace)
  └─ bash(PID 100, root namespace)
     └─ unshare --pid --fork bash
        └─ bash(PID 1, child PID namespace)  ← 在子 ns 里是 PID 1
           └─ sleep 1000
              (子 ns 里 PID 14,父 ns 里 PID 1014)
```

**关键性质**:
- 子 namespace 看不到父 namespace 的进程
- 父 namespace 能看到子 namespace 的进程(但 PID 不同)
- PID namespace 严格嵌套,NET / MNT / UTS / IPC 可独立(不嵌套)

### 3.4 `/proc` 视图

```bash
# 宿主机
ls /proc | grep -E '^[0-9]+$' | head
# 1 2 3 ... 1000 ... 10000

# 容器内(PID namespace 隔离)
docker exec web ls /proc | grep -E '^[0-9]+$' | head
# 1 7 8 9 10   ← 只看到容器内进程
```

---

## 4. 操作流程与命令

### 4.1 查看 namespace

```bash
# 方式 1:ls -l /proc/<pid>/ns
ls -l /proc/$$/ns
# total 0
# lrwxrwxrwx ... cgroup -> cgroup:[4026531835]
# lrwxrwxrwx ... ipc -> ipc:[4026531839]
# lrwxrwxrwx ... mnt -> mnt:[4026531840]
# lrwxrwxrwx ... net -> net:[4026531992]
# lrwxrwxrwx ... pid -> pid:[4026531836]
# lrwxrwxrwx ... user -> user:[4026531837]
# lrwxrwxrwx ... uts -> uts:[4026531838]

# 数字是 namespace inode(全局唯一)

# 方式 2:lsns(列出所有 namespace)
lsns
# NS TYPE  NPROCS PID USER COMMAND
# 4026531835 cgroup 100 1 root /sbin/init
# 4026531836 pid    100 1 root /sbin/init
# ...

# 方式 3:nsenter(进入某 namespace)
nsenter --target <pid> --pid --mount --net bash
```

### 4.2 `unshare`:创建新 namespace

```bash
# 创建新 MNT namespace(隔离挂载)
unshare --mount bash
# 在新 shell 里 mount,宿主机看不到

# 创建新 UTS namespace(独立 hostname)
unshare --uts bash
hostname mycontainer
# 宿主机 hostname 不变

# 创建新 PID namespace
unshare --pid --fork bash
# 注意:必须 --fork,否则 bash 看不到自己作为 PID 1

# 创建新 NET namespace
unshare --net bash
# 新网络栈,只有 lo 接口

# 创建新 USER namespace(rootless)
unshare --user --map-root-user bash
# 容器内是 root,宿主机是普通用户
```

### 4.3 `nsenter`:进入已有 namespace

```bash
# 进入容器的 namespace
docker inspect web --format '{{.State.Pid}}'
# 12345

# 进入容器的 PID namespace
nsenter --target 12345 --pid bash

# 进入所有 namespace(完整容器环境)
nsenter --target 12345 --pid --mount --net --uts --ipc bash

# 在容器外调试容器内问题(不需要 docker exec)
nsenter --target 12345 --net ss -tlnp
```

### 4.4 手动创建一个"容器"

```bash
# 用 unshare + chroot 创建最小"容器"
unshare --pid --mount --net --uts --ipc --fork \
  /bin/bash -c "
    hostname mycontainer
    mount -t proc proc /proc
    chroot /alpine-rootfs /bin/sh
  "
```

> 这就是 runc 做的事——只是它更完整(加 cgroup、capabilities、seccomp 等)。

---

## 5. 底层原理

### 5.1 PID namespace 详解

```
父 PID namespace              子 PID namespace
─────────────────              ─────────────────
PID 1 (systemd)               
  └─ PID 1000 (bash)
     └─ PID 1001 (unshare --pid --fork bash)
        └─ PID 1002 (bash)  ←  PID 1 (子 ns 内)
           └─ PID 1003 (sleep)
                              ←  PID 14 (子 ns 内)
```

**关键性质**:
1. 子 ns 内,第一个进程是 PID 1
2. 子 ns 内的 PID 1 退出,**所有子 ns 进程被 SIGKILL**(类似容器)
3. 父 ns 能看到子 ns 进程,但 PID 不同(子 ns PID 1 = 父 ns PID 1002)
4. `/proc` 在子 ns 内只显示本 ns 进程

**特殊**:PID 1 在容器内承担:
- 接收 SIGTERM(优雅停止)
- 回收僵尸进程(reap zombie)
- 容器生命周期 = PID 1 生命周期

### 5.2 NET namespace 详解

```
NET namespace 隔离:
- 网络接口(eth0, lo, veth...)
- 路由表
- iptables 规则
- /proc/net
- 端口空间
- ARP 表
- socket 编号

不隔离:
- 物理网卡本身(但可在 ns 间移动)
- TCP/IP 协议栈实现(共享内核代码)
```

**veth pair 跨 namespace 连接**:
```
nsA                  nsB
┌──────────┐         ┌──────────┐
│  veth0   │ <=====> │  veth1   │
│ 172.17..│         │ 172.18..│
└──────────┘         └──────────┘
   ↑                    ↑
   一端在 nsA           一端在 nsB
   另一端在 nsB         另一端在 nsA
```

### 5.3 MNT namespace 详解

```
MNT namespace 隔离:
- 挂载点视图(/proc/mounts)
- 文件系统根(chroot / pivot_root)
- mount / umount 操作不影响其他 ns

不隔离:
- 磁盘本身(共享 inode)
- 文件内容(同一文件)
```

**pivot_root vs chroot**:
- `chroot`:改根目录视图,但原根目录仍可见(可被逃逸)
- `pivot_root`:换根目录,原根目录放到 put_old,完全隔离

容器用 `pivot_root`,更安全。

### 5.4 USER namespace 详解

```
USER namespace 实现 UID 映射:

宿主机(真实 UID)           容器内(虚拟 UID)
─────────────                ─────────────
UID 1000 (zhang)            UID 0 (root)
UID 1001 (li)               UID 1000

映射:/proc/<pid>/uid_map
"0 1000 1"   # 容器内 0 → 宿主 1000
```

**关键性质**:
- 容器内是 root,宿主机是普通用户(rootless)
- 容器内能 bind 80 端口(虚拟 root 权限)
- 容器逃逸后,宿主机权限受限(只是普通用户)
- 是 rootless 容器的基础

**风险**:
- 内核 bug 可绕过(USER namespace 历史 CVE 较多)
- 部分内核功能在 USER ns 内受限

### 5.5 容器创建的完整 clone 调用

```c
// runc 简化逻辑
int flags = CLONE_NEWPID   |  // 新 PID ns
            CLONE_NEWNET   |  // 新 NET ns
            CLONE_NEWNS    |  // 新 MNT ns
            CLONE_NEWUTS   |  // 新 UTS ns
            CLONE_NEWIPC   |  // 新 IPC ns
            CLONE_NEWUSER;    // 新 USER ns(rootless 时)

pid_t child = clone(child_func, stack, flags | SIGCHLD, arg);
```

---

## 6. 代码与配置示例

### 6.1 用 Go 实现最小容器

```go
package main

import (
    "os"
    "os/exec"
    "syscall"
)

func main() {
    switch os.Args[1] {
    case "run":
        run()
    case "child":
        child()
    default:
        panic("unknown command")
    }
}

func run() {
    cmd := exec.Command("/proc/self/exe", append([]string{"child"}, os.Args[2:]...)...)
    cmd.Stdin = os.Stdin
    cmd.Stdout = os.Stdout
    cmd.Stderr = os.Stderr
    cmd.SysProcAttr = &syscall.SysProcAttr{
        Cloneflags: syscall.CLONE_NEWUTS |
                    syscall.CLONE_NEWPID |
                    syscall.CLONE_NEWNS,
        Unshareflags: syscall.CLONE_NEWNS,
    }
    must(cmd.Run())
}

func child() {
    fmt.Printf("running %v as PID %d\n", os.Args[2:], os.Getpid())
    
    syscall.Sethostname([]byte("container"))
    syscall.Chroot("/alpine-rootfs")
    syscall.Chdir("/")
    syscall.Mount("proc", "proc", "proc", 0, "")
    
    cmd := exec.Command(os.Args[2], os.Args[3:]...)
    cmd.Stdin = os.Stdin
    cmd.Stdout = os.Stdout
    cmd.Stderr = os.Stderr
    must(cmd.Run())
}

func must(err error) {
    if err != nil {
        panic(err)
    }
}
```

```bash
# 编译并运行
go build -o mycontainer main.go
./mycontainer run /bin/sh
# 现在你在"容器"里,PID 1,hostname=container
```

> 来源:Liz Rice 的 *Building a container from scratch in Go* 演讲。

### 6.2 限制容器的 namespace

```bash
# 不创建 NET namespace(共享宿主网络)
docker run --network host ...

# 不创建 PID namespace(共享宿主 PID)
docker run --pid host ...

# 不创建 IPC namespace
docker run --ipc host ...

# 共享另一容器的 namespace
docker run --network container:web ...
docker run --pid container:web ...
```

### 6.3 K8s Pod 的 namespace 共享

```yaml
# Pod 内所有容器共享:
# - NET namespace(同 IP,同端口空间)
# - IPC namespace
# - UTS namespace
# 但 PID 隔离(默认)

apiVersion: v1
kind: Pod
spec:
  shareProcessNamespace: true   # 也共享 PID ns
  containers:
    - name: app
      ...
    - name: sidecar
      ...
```

> Pod = 一组共享部分 namespace 的容器。

---

## 7. 常见陷阱与调优

### 7.1 陷阱:容器内 `/proc` 不准

```bash
# 容器内
cat /proc/cpuinfo       # ✓ 看到的是宿主机 CPU(共享内核)
cat /proc/meminfo       # ❌ 看到的是宿主机内存(不是 cgroup limit)
cat /proc/loadavg       # ❌ 宿主机负载

# 容器内看自己的限制
cat /sys/fs/cgroup/memory.max          # cgroup v2
cat /sys/fs/cgroup/memory/memory.limit_in_bytes  # cgroup v1
```

**修复**:应用需感知 cgroup,不能读 `/proc/meminfo`。JDK 8u191+ 自动感知。

### 7.2 陷阱:PID 1 不收割僵尸

```bash
# 容器内
ps aux | grep defunct
# root  123  0.0  ... [defunct]
```

**原因**:PID 1 没有 `wait` 子进程,孤儿进程变 zombie。

**修复**:
```bash
docker run --init ...   # tini 作 PID 1
```

### 7.3 陷阱:USER namespace 与 capability 冲突

```bash
# rootless 容器内
docker run --cap-add SYS_ADMIN ...
# ❌ 可能失败,USER ns 内 capability 受限
```

### 7.4 陷阱:namespace 泄漏

```bash
# 容器删除,但 namespace 没释放
lsns | grep docker
# 还有残留的 NET namespace

# 排查
ls -l /proc/*/ns/net | grep <inode>
# 找到引用的进程,kill 掉
```

### 7.5 调优:inotify 上限

**问题**:单节点 200+ 容器,新容器启动报 `inotify instance limit reached`。

```bash
# 查看当前
cat /proc/sys/fs/inotify/max_user_instances
# 128(默认)

# 调大
echo 8192 | sudo tee /proc/sys/fs/inotify/max_user_instances
echo 1048576 | sudo tee /proc/sys/fs/inotify/max_user_watches

# 持久化
echo "fs.inotify.max_user_instances=8192" | sudo tee -a /etc/sysctl.conf
```

### 7.6 调优:PID 上限

```bash
# 容器内最大进程数
docker run --pids-limit 200 ...

# 宿主机
cat /proc/sys/kernel/pid_max
# 4194304(64 位默认)

# 大集群调大
echo 4194304 | sudo tee /proc/sys/kernel/pid_max
```

### 7.7 调优:conntrack(详见 [05-容器网络](./05-容器网络.md))

```bash
echo 2097152 | sudo tee /proc/sys/net/netfilter/nf_conntrack_max
```

---

## 8. 工业案例与基准数据

### 8.1 大厂内核调优基线

阿里 ACK 节点初始化脚本(公开资料):

```bash
# /etc/sysctl.d/99-docker.conf
fs.inotify.max_user_instances=8192
fs.inotify.max_user_watches=1048576
fs.file-max=2097152
kernel.pid_max=4194304
net.netfilter.nf_conntrack_max=2097152
net.netfilter.nf_conntrack_buckets=524288
net.core.somaxconn=65535
net.ipv4.tcp_max_syn_backlog=65535
vm.max_map_count=262144
vm.swappiness=0
```

> **背景**:默认内核参数为单机设计,不适合跑 200+ 容器。

### 8.2 namespace 性能开销

| 操作 | 无 namespace | 有 namespace | 开销 |
|------|-------------|--------------|------|
| fork() | 1.0 ms | 1.2 ms | +20% |
| 网络发包(本地) | 0.05 ms | 0.08 ms | +60% |
| 文件 IO(本地) | 0.1 ms | 0.1 ms | ~0% |
| 系统调用 | 0.001 ms | 0.001 ms | ~0% |

**结论**:namespace 创建有一次性开销,运行时开销极小。

### 8.3 容器逃逸案例(CVE)

| CVE | 漏洞 | 严重性 | 影响 |
|-----|------|--------|------|
| CVE-2019-5736 | runc 文件描述符泄漏 | 严重 | 容器内可覆盖宿主 runc,逃逸 |
| CVE-2019-14271 | docker cp 路径穿越 | 高 | 容器可读宿主文件 |
| CVE-2020-15257 | containerd-shim API 暴露 | 高 | 容器可控制 shim |
| CVE-2022-0185 | filesystem context 内核溢出 | 严重 | USER namespace 内触发,逃逸 |
| CVE-2022-0492 | cgroup release_agent 逃逸 | 高 | CAP_SYS_ADMIN 容器逃逸 |

**教训**:
- 容器不是安全沙箱
- 强隔离用 gVisor / Kata / Firecracker
- 最小权限:drop ALL capabilities
- 及时升级 runc / containerd / 内核

### 8.4 大厂容器安全方案

| 公司 | 方案 | 备注 |
|------|------|------|
| Google | gVisor(用户态内核) | GKE Sandbox |
| AWS | Firecracker(microVM) | Lambda / Fargate |
| 阿里 | Kata Containers(VM 级隔离) | 强隔离场景 |
| 字节 | 自研 + gVisor | 多租户场景 |
| Netflix | 标准 Docker + IAM | 单租户,信任边界外 |

---

## 9. 与其他方案的关系

### 9.1 namespace vs hypervisor

| 维度 | namespace(Docker) | hypervisor(VM) |
|------|---------------------|------------------|
| 隔离级别 | 进程级 | 硬件级 |
| 内核 | 共享 | 独立 |
| 启动 | 毫秒 | 秒 |
| 性能 | 接近原生 | 有虚拟化开销 |
| 安全 | 弱(共享内核) | 强(硬件隔离) |
| 密度 | 高 | 中 |

### 9.2 namespace vs Solaris Zones / BSD Jails

| 维度 | Linux namespace | Solaris Zones | BSD Jails |
|------|----------------|---------------|-----------|
| 出现 | 2008 | 2004 | 2000 |
| 隔离 | 7 种 namespace | 系统级 | 系统级 |
| 内核 | 共享 | 共享(可独立) | 共享 |
| 易用 | 复杂(7 种) | 简单(单一工具) | 中等 |

> namespace 是最细粒度但最复杂的方案。

### 9.3 namespace vs gVisor / Kata

| 维度 | namespace | gVisor | Kata |
|------|-----------|--------|------|
| 隔离 | 内核特性 | 用户态内核 | VM + namespace |
| 内核 | 共享 Linux | 应用 → gVisor → Linux | 独立 VM 内核 |
| 兼容 | 100% | 部分 syscall | 100% |
| 性能 | 原生 | 慢 2-10 倍 | 慢 1.5-3 倍 |
| 安全 | 中 | 强 | 强 |

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| Docker 用了哪几种 namespace? | 7 种:PID / NET / MNT / UTS / IPC / USER / CGROUP。 |
| namespace 与 cgroup 区别? | namespace 隔离视图,cgroup 限制资源;两者结合 = 容器。 |
| PID namespace 里 PID 1 退出会怎样? | 该 namespace 内所有进程被 SIGKILL(类似容器退出)。 |
| USER namespace 解决什么? | 容器内 root 映射到宿主普通用户,实现 rootless 容器。 |
| `unshare` 与 `nsenter` 区别? | unshare 创建新 namespace,nsenter 进入已有 namespace。 |
| 容器为什么不能跨 OS? | namespace 是 Linux 内核特性,Windows 没有(用 LCOW / WSL 模拟)。 |
| 容器逃逸的根本原因? | 共享内核,内核漏洞可绕过 namespace 边界。 |
| K8s Pod 共享哪些 namespace? | NET / IPC / UTS(默认),PID 可选(shareProcessNamespace)。 |
| `/proc/meminfo` 在容器内准吗? | 不准,显示的是宿主机内存;应用应读 cgroup。 |
| PID 1 不处理信号会怎样? | docker stop 等 10s 后 SIGKILL,无法优雅停止。 |

---

## 11. 综合面试题

### 题 1(原理)
**问**:解释 `clone(CLONE_NEWPID | CLONE_NEWNET)` 创建子进程后的视图。

**答题要点**:
- 子进程在新 PID namespace,自己是 PID 1
- 看不到父 namespace 进程
- 父 namespace 能看到子进程(但 PID 不同)
- 子进程在新 NET namespace,独立网卡(只有 lo)
- 看不到宿主 eth0、docker0
- 网络栈完全隔离

### 题 2(实战)
**问**:如何用 `nsenter` 调试一个无法 `docker exec` 的容器?

**答题要点**:
- `docker inspect <name> --format '{{.State.Pid}}'` 拿到容器主进程 PID
- `nsenter --target <pid> --pid --mount --net bash`
- 即使 docker daemon 挂了也能进
- 用于:容器内 docker exec 失败、daemon 不可达、紧急救援
- 注意:nsenter 不创建新进程,直接进 namespace

### 题 3(故障)
**问**:容器内 `df -h` 显示宿主机磁盘,而不是容器 rootfs,为什么?

**答题要点**:
- `df` 读 `/proc/mounts`
- 默认容器 MNT namespace 隔离,但 `/proc/mounts` 可能显示宿主挂载(部分内核版本)
- 容器内 `/proc` 是宿主的(没隔离 /proc)
- 修复:挂载 `/proc` 时用 `proc` 文件系统,或读 `/sys/fs/cgroup`
- 实际:容器内 df 显示的是宿主磁盘,但写入受 cgroup 限制

### 题 4(安全)
**问**:如何防止容器逃逸?

**答题要点**:
- 最小权限:`--cap-drop ALL --cap-add <needed>`
- `--security-opt no-new-privileges`
- seccomp profile(限制 syscall)
- AppArmor / SELinux
- rootless(USER namespace)
- 及时升级 runc / containerd / 内核
- 强隔离:gVisor / Kata / Firecracker
- 不挂载宿主敏感路径(`/`、`/var/run/docker.sock`)
- 详见 [12-安全与隔离](./12-安全与隔离.md)

### 题 5(架构)
**问**:K8s Pod 为什么让多容器共享 NET namespace?

**答题要点**:
- Sidecar 模式:Envoy / 日志收集器 / 监控 agent
- 共享 NET:同 IP,sidecar 可拦截流量
- 共享 IPC:本地高效通信
- 不共享 PID:容器隔离(可选开启)
- Pod = 一组紧耦合的容器,像一个"虚拟机"

### 题 6(工业)
**问**:大厂节点上跑 200+ 容器,内核需要调哪些参数?

**答题要点**:
- `fs.inotify.max_user_instances=8192`(容器多)
- `fs.inotify.max_user_watches=1048576`(文件监听)
- `kernel.pid_max=4194304`(进程数)
- `net.netfilter.nf_conntrack_max=2097152`(连接跟踪)
- `net.core.somaxconn=65535`(连接队列)
- `vm.max_map_count=262144`(Elasticsearch 等)
- `fs.file-max=2097152`(文件描述符)
- `vm.swappiness=0`(禁 swap)
- 工具:tuned / sysctl.d 持久化

### 题 7(深度)
**问**:为什么 USER namespace 是 rootless 容器的基础?

**答题要点**:
- 普通 namespace 需 root 创建(CLONE_NEWPID 等需 CAP_SYS_ADMIN)
- USER namespace 允许普通用户创建(无特权)
- USER ns 内 UID 0 → 宿主普通用户
- 在 USER ns 内可创建其他 namespace(PID / NET / MNT)
- 实现:普通用户跑完整容器,逃逸后无 root 权限
- Podman / rootless Docker 都基于此

### 题 8(性能)
**问**:namespace 的运行时性能开销有多大?

**答题要点**:
- 创建时一次性开销(clone 带 flag,几毫秒)
- 运行时几乎无开销(只是视图不同)
- 网络有 veth + bridge 转发,延迟 +0.05ms
- 文件 IO 无开销(共享 inode)
- 系统调用无开销(共享内核)
- 整体 < 5% 性能损失

### 题 9(故障)
**问**:容器频繁报 `fork: Resource temporarily unavailable`,如何排查?

**答题要点**:
- `--pids-limit` 设得过小
- 或宿主机 `kernel.pid_max` 不够
- 容器内 `ps aux | wc -l` 看进程数
- 修复:调大 pids-limit,或修复进程泄漏
- 监控:容器进程数告警

### 题 10(综合)
**问**:从内核视角解释 Docker 容器的本质。

**答题要点**:
- 容器 = 一组被 namespace 隔离 + 被 cgroup 限制 + 拥有独立 rootfs 的进程
- 7 种 namespace 提供视图隔离(PID / NET / MNT / UTS / IPC / USER / CGROUP)
- cgroup 提供资源限制(CPU / 内存 / IO / PID)
- UnionFS 提供分层 rootfs(overlay2)
- 没有虚拟化,共享内核,本质是普通进程
- 启动快、密度高,但隔离性弱于 VM
- 安全强隔离需 gVisor / Kata / Firecracker

---

## 12. 故障复盘

### 案例 1:容器内 `ps aux` 看到宿主进程

**现象**:某团队容器内 `ps aux` 看到宿主机所有进程,以为容器逃逸。

**根因**:
- 容器用 `--pid host` 启动(共享宿主 PID namespace)
- 不是逃逸,是配置错误

**修复**:
```bash
# 去掉 --pid host(默认隔离)
docker run --name web nginx
```

**防范**:
- 不用 `--pid host`(除非监控 agent 等特殊场景)
- Compose / K8s 默认隔离
- 镜像扫描检查启动参数

### 案例 2:inotify 耗尽导致 Pod 启动失败

**现象**:某集群新 Pod 起不来,报 `inotify instance limit reached`。

**根因**:
- 单节点跑 200+ 容器,每个容器多个文件监听
- 默认 `max_user_instances=128`,远不够

**修复**:
```bash
echo 8192 | sudo tee /proc/sys/fs/inotify/max_user_instances
echo 1048576 | sudo tee /proc/sys/fs/inotify/max_user_watches
```

**防范**:
- 节点初始化脚本调优
- 监控 inotify 使用率
- 应用减少不必要的文件监听

### 案例 3:容器逃逸事故(CVE-2019-5736)

**现象**:2019 年某公司被攻击,攻击者从容器逃逸到宿主机,获取 root。

**根因**:
- runc 版本旧(< 1.0-rc6)
- CVE-2019-5736:容器内可覆盖宿主 runc 二进制
- 攻击者通过恶意镜像触发

**修复**:
- 升级 runc 到 1.0-rc6+
- 升级 Docker 到 18.09.7+
- 镜像扫描,拒绝未知来源
- 最小权限(cap-drop ALL)

**防范**:
- 关注 runc / containerd CVE
- 及时升级
- 不跑不可信镜像
- 强隔离用 Kata

### 案例 4:USER namespace 与 K8s 不兼容

**现象**:某团队启用 USER namespace(rootless),K8s Pod 起不来。

**根因**:
- K8s 1.24 之前不支持 USER namespace
- 部分 CNI 插件不兼容
- volume 权限映射有问题

**修复**:
- 等待 K8s 1.25+(支持 USER namespace)
- 或继续用 root 容器 + 严格 RBAC
- 或用 gVisor 替代

### 案例 5:容器内时区不对

**现象**:容器内 `date` 显示 UTC,与本地差 8 小时。

**根因**:
- UTS namespace 隔离 hostname,但不隔离时区
- 时区文件 `/etc/localtime` 来自基础镜像(默认 UTC)

**修复**:
```dockerfile
ENV TZ=Asia/Shanghai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
```

**防范**:
- 基础镜像统一设时区
- 应用层用 UTC,展示时转本地

---

## 13. 参考与延伸

### 官方文档

- namespaces(7) man page — https://man7.org/linux/man-pages/man7/namespaces.7.html
- unshare(1) — https://man7.org/linux/man-pages/man1/unshare.1.html
- nsenter(1) — https://man7.org/linux/man-pages/man1/nsenter.1.html
- cgroup_namespaces(7)

### 内核源码

- `include/linux/nsproxy.h`
- `kernel/nsproxy.c`
- `include/linux/pid_namespace.h`

### 论文 / 文章

- *Namespaces in operation* — LWN.net 系列
- *Building a container from scratch in Go* — Liz Rice, GOTO 2018
- *Container Security* — Liz Rice(O'Reilly)

### 工具

- lsns — 列出所有 namespace
- nsenter — 进入 namespace
- unshare — 创建新 namespace
- CRIU — checkpoint/restore(可迁移容器)

### 相关模块

- [07-Docker-Compose](./07-Docker-Compose.md) — 上一章
- [09-底层原理-cgroups](./09-底层原理-cgroups.md) — 资源限制
- [10-底层原理-UnionFS](./10-底层原理-UnionFS.md) — 分层存储
- [11-OCI规范与运行时](./11-OCI规范与运行时.md) — runc 实现
- [12-安全与隔离](./12-安全与隔离.md) — 容器逃逸与防御
- [25-工业实战-故障复盘集](./25-工业实战-故障复盘集.md) — namespace 相关故障

---

> **下一章**:[09-底层原理-cgroups](./09-底层原理-cgroups.md)

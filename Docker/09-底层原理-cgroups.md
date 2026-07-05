# 09 - 底层原理 - cgroups

> namespace 隔离视图,cgroup 限制资源。两者结合,才是完整的"容器"。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 把 **cgroup v1 vs v2** 的差异讲到能选型与排障
- 把 **CPU / 内存 / IO / PID** 子系统讲到能读懂 `/sys/fs/cgroup`
- 把 **`docker run` 资源限制的底层映射** 讲到能手动调
- 把 **CPU throttling / OOM Killer** 讲到能避免生产事故
- 把 **大厂 cgroup 调优** 经验讲到能落地

### 1.2 本章不解决什么

- 不讲 namespace(见 [08-底层原理-namespaces](./08-底层原理-namespaces.md))
- 不讲 UnionFS(见 [10-底层原理-UnionFS](./10-底层原理-UnionFS.md))
- 不讲 Docker `--cpus` / `--memory` 参数(见 [04-容器运行与生命周期](./04-容器运行与生命周期.md))
- 不讲 K8s requests/limits 实现(见 [19-容器生态对比](./19-容器生态对比.md))

> **关键认知**:cgroup 不是 Docker 发明的,是 Linux 内核 2.6.24(2008)引入。cgroup v2 在 4.5(2016)稳定,Docker 20.10+ / K8s 1.22+ 默认 v2。

---

## 2. 直觉解释

### 2.1 cgroup 类比:公司预算

```
   没有 cgroup                    有 cgroup
   ────────────                    ──────────
   公司所有部门共用一个大账户      每个部门独立预算
   一个部门花超,全公司没钱        一个部门花超,只影响自己
   没有优先级                      可设优先级(权重)
```

**核心思想**:给一组进程设资源上限,超过则限制或杀掉。

### 2.2 cgroup 两层模型

```
   cgroup(core)                    cgroup(子系统)
   ────────────                    ──────────────
   进程分组                         资源控制器
   (一组进程)                       (cpu / memory / ...)
   
   ┌──────────────┐
   │ cgroup 组     │ ←── 挂载子系统
   │  - 进程 A    │     cpu:  limit=2 核
   │  - 进程 B    │     memory: limit=1g
   │  - 进程 C    │     pids: limit=200
   └──────────────┘
```

### 2.3 v1 vs v2 一图对比

```
   cgroup v1(进程在多个 cgroup)        cgroup v2(进程在单一层级)
   ────────────────────────────         ─────────────────────────
   
   /sys/fs/cgroup/                     /sys/fs/cgroup/
   ├── cpu/                            ├── cpu.max
   │   └── docker/<id>/                ├── memory.max
   │       ├── cpu.cfs_quota_us        ├── io.max
   │       └── tasks                   └── docker/<id>/
   ├── memory/                             ├── cpu.max
   │   └── docker/<id>/                    ├── memory.max
   │       ├── memory.limit_in_bytes       ├── io.max
   │       └── tasks                       └── cgroup.procs
   └── ...
   
   每个 subsystem 独立层级                统一层级
   进程在多个 cgroup                      进程在一个 cgroup
   语义复杂                                语义清晰
```

---

## 3. 核心概念与架构

### 3.1 cgroup v1 子系统

| 子系统 | 控制 | 关键文件 |
|--------|------|----------|
| **cpu** | CPU 配额(CFS) | `cpu.cfs_quota_us`, `cpu.cfs_period_us` |
| **cpuacct** | CPU 统计 | `cpuacct.usage` |
| **cpuset** | CPU 绑核 | `cpuset.cpus`, `cpuset.mems` |
| **memory** | 内存限制 | `memory.limit_in_bytes`, `memory.oom_control` |
| **blkio** | 块设备 IO | `blkio.throttle.read_bps_device` |
| **pids** | 进程数 | `pids.max` |
| **devices** | 设备访问 | `devices.allow`, `devices.deny` |
| **net_cls** | 网络分类(tc) | `net_cls.classid` |
| **net_prio** | 网络优先级 | `net_prio.ifpriomap` |
| **freezer** | 冻结进程 | `freezer.state` |
| **hugetlb** | 大页 | `hugetlb.<size>.limit_in_bytes` |
| **perf_event** | 性能计数 | `perf_event` |

### 3.2 cgroup v2 控制器

```
/sys/fs/cgroup/
├── cpu.max              # "quota period"(如 "200000 100000" = 2 核)
├── cpu.weight           # 1-10000,默认 100(类似 shares)
├── cpu.stat             # 统计(throttled 等)
├── cpuset.cpus          # 绑核
├── memory.max           # 内存硬限
├── memory.high          # 内存软限
├── memory.current       # 当前用量
├── memory.swap.max      # swap 限制
├── io.max               # IO 限制
├── io.weight            # IO 权重
├── pids.max             # 进程数限制
├── pids.current         # 当前进程数
├── cgroup.procs         # 进程列表(PID)
├── cgroup.threads       # 线程列表
└── cgroup.controllers   # 可用控制器
```

### 3.3 CFS 调度器(v1 cpu 子系统)

```
CFS(Completely Fair Scheduler)周期:100ms(default)

cpu.cfs_period_us = 100000  (100ms)
cpu.cfs_quota_us  = 200000  (200ms,即 2 核满载)

含义:每 100ms 周期内,该 cgroup 可用 200ms CPU 时间
     (因为多核,200ms / 100ms = 2 核)

容器内 4 个进程同时跑:
  - 100ms 周期开始,4 进程并行
  - 50ms 后用完 200ms quota(4×50=200)
  - 后 50ms 被 throttle,等下一周期
  - 表现:CPU 利用率 50%,但有 50ms 静默 → 延迟毛刺
```

### 3.4 内存限制语义

```
memory.max(硬限):
  超过 → OOM Killer 触发 → 杀进程

memory.high(软限):
  超过 → 内核回收页(cache),不杀进程
  适合:突发流量场景

memory.swap.max:
  swap 限制(0 = 禁用 swap)
  v2: memory.swap.max = memory.max + swap
  生产:通常设 0,避免 swap 拖慢
```

---

## 4. 操作流程与命令

### 4.1 查看 cgroup 版本

```bash
# 方法 1:看挂载
mount | grep cgroup
# cgroup2 on /sys/fs/cgroup type cgroup2 (v2)
# 或
# cgroup on /sys/fs/cgroup/cpu type cgroup (v1)

# 方法 2:看文件
stat -fc %T /sys/fs/cgroup/
# cgroup2fs(v2)
# tmpfs(v1)
```

### 4.2 查看容器 cgroup

```bash
# cgroup v2
docker run -d --name web --cpus=2 --memory=1g nginx
docker inspect web --format '{{.State.Pid}}'
# 12345

# 看 cgroup 路径
cat /proc/12345/cgroup
# 0::/system.slice/docker-<id>.scope

# 看 CPU 限制
cat /sys/fs/cgroup/system.slice/docker-<id>.scope/cpu.max
# 200000 100000(2 核)

# 看内存限制
cat /sys/fs/cgroup/system.slice/docker-<id>.scope/memory.max
# 1073741824(1 GB)

# 看当前内存
cat /sys/fs/cgroup/system.slice/docker-<id>.scope/memory.current
# 52428800(50 MB)
```

### 4.3 cgroup v1 查看资源限制

```bash
# CPU 配额
cat /sys/fs/cgroup/cpu/docker/<id>/cpu.cfs_quota_us
# 200000

cat /sys/fs/cgroup/cpu/docker/<id>/cpu.cfs_period_us
# 100000

# 内存限制
cat /sys/fs/cgroup/memory/docker/<id>/memory.limit_in_bytes
# 1073741824

# 当前内存
cat /sys/fs/cgroup/memory/docker/<id>/memory.usage_in_bytes
# 52428800

# OOM 计数
cat /sys/fs/cgroup/memory/docker/<id>/memory.failcnt
# 0
```

### 4.4 查看 CPU throttling

```bash
# cgroup v2
cat /sys/fs/cgroup/.../cpu.stat
# usage_usec 50000000
# user_usec 45000000
# system_usec 5000000
# nr_periods 1000
# nr_throttled 250     ← 关键:被 throttle 的周期数
# throttled_usec 5000000   ← throttle 总时间

# 如果 nr_throttled / nr_periods > 5%,说明 throttling 严重
```

### 4.5 手动设置 cgroup(无 Docker)

```bash
# cgroup v2
# 创建子 cgroup
mkdir /sys/fs/cgroup/mygroup

# 设置 CPU 限制(1 核)
echo "100000 100000" > /sys/fs/cgroup/mygroup/cpu.max

# 设置内存限制(500 MB)
echo "524288000" > /sys/fs/cgroup/mygroup/memory.max

# 加入进程
echo $$ > /sys/fs/cgroup/mygroup/cgroup.procs

# 现在这个 shell 受 1C500M 限制
```

---

## 5. 底层原理

### 5.1 cgroup v2 统一层级

```
/sys/fs/cgroup/(root cgroup)
├── cpu.max = "max 100000"(无限制)
├── memory.max = "max"
└── docker/
    ├── cpu.max = "200000 100000"(2 核)
    ├── memory.max = 1073741824(1g)
    └── <container-id>/
        ├── cpu.max = "200000 100000"
        ├── memory.max = 1073741824
        └── cgroup.procs = [12345, 12346, ...]
```

**关键性质**:
- 子 cgroup 继承父 cgroup 限制
- 子 cgroup 限制不能超过父(但父= max 时除外)
- 资源在兄弟 cgroup 间按 `cpu.weight` 分配

### 5.2 CFS quota 与 throttling

```
v1: cpu.cfs_quota_us / cpu.cfs_period_us
v2: cpu.max = "quota period"

例:cpu.max = "200000 100000"
   quota = 200000 us = 200 ms
   period = 100000 us = 100 ms
   200/100 = 2 核(每周期可用 200ms CPU 时间)

容器内 4 进程同时跑:
  T=0ms:   4 进程并行,每进程消耗 50ms
  T=50ms:  已用 200ms quota(4×50)
  T=50-100ms:  throttle,所有进程停
  T=100ms: 新周期,继续
  
表现:CPU 用量 50%,但 P99 延迟飙高(50ms 静默)
```

### 5.3 OOM Killer

```
内存超 limit 的处理流程:

v1:
  1. memory.limit_in_bytes 触发
  2. 内核尝试回收该 cgroup 的页
  3. 仍超 → cgroup OOM Killer
  4. 杀掉该 cgroup 内 oom_score 最高的进程
  5. 通常杀 PID 1 → 容器退出(137)

v2:
  1. memory.max 触发
  2. 同上,但语义更清晰
  3. memory.oom.group = 1 时,杀整个 cgroup(默认)

宿主机内存不足(系统级 OOM):
  1. 全局 OOM Killer 触发
  2. 按 oom_score 排序(综合 RSS、运行时间、nice)
  3. 容器进程通常得分高(占内存多)
  4. 杀进程,可能影响多个容器
```

### 5.4 IO 限制

```
v2: io.max
  例:io.max = "8:16 rbps=10485760 wbps=10485760 riops=1000 wiops=1000"
  
  8:16 = 主从设备号(如 /dev/sda)
  rbps:读字节/秒(10 MB/s)
  wbps:写字节/秒
  riops:读 IOPS
  wiops:写 IOPS

v1: blkio.throttle.read_bps_device
  echo "8:16 10485760" > blkio.throttle.read_bps_device
```

---

## 6. 代码与配置示例

### 6.1 容器资源限制全参数

```bash
docker run -d \
  --name web \
  --cpus="2.0" \                      # 2 核(v2: cpu.max = "200000 100000")
  --cpu-shares=512 \                  # 权重(默认 1024,空闲时可用全部)
  --cpuset-cpus="0,1" \              # 绑定 CPU 0 和 1
  --cpu-period=100000 \              # CFS 周期(默认 100ms)
  --cpu-quota=200000 \               # CFS 配额(等同 --cpus=2)
  \
  --memory="1g" \                    # 内存硬限
  --memory-reservation="512m" \      # 内存软限
  --memory-swap="2g" \               # memory + swap
  --memory-swappiness=0 \            # 禁 swap
  --oom-kill-disable \               # 禁 OOM Kill(慎用)
  --kernel-memory="100m" \           # 内核内存(v1,已废弃)
  \
  --pids-limit="200" \              # 进程数限制
  \
  --device-read-bps="/dev/sda:10mb" \  # 读 IO 限制
  --device-write-bps="/dev/sda:10mb" \ # 写 IO 限制
  --device-read-iops="/dev/sda:1000" \ # 读 IOPS
  \
  --ulimit nofile=65535:65535 \
  --ulimit nproc=65535:65535 \
  \
  nginx
```

### 6.2 cgroup v2 配置示例

```bash
# 创建 cgroup
mkdir /sys/fs/cgroup/myapp

# CPU:1 核
echo "100000 100000" > /sys/fs/cgroup/myapp/cpu.max

# 内存:500 MB
echo "524288000" > /sys/fs/cgroup/myapp/memory.max

# 软限:300 MB(超过时内核回收)
echo "314572800" > /sys/fs/cgroup/myapp/memory.high

# swap:0(禁用)
echo "0" > /sys/fs/cgroup/myapp/memory.swap.max

# 进程数:100
echo "100" > /sys/fs/cgroup/myapp/pids.max

# 加入进程
echo $$ > /sys/fs/cgroup/myapp/cgroup.procs
```

### 6.3 K8s resources 映射

```yaml
# K8s Pod
resources:
  requests:
    cpu: "1"        # 1 核请求(调度用)
    memory: "1Gi"
  limits:
    cpu: "2"        # 2 核上限(cgroup)
    memory: "2Gi"
```

映射到 cgroup v2:
```
cpu.max = "200000 100000"    # limit
cpu.weight = 100             # request(相对权重,范围 1-10000)
memory.max = 2147483648      # limit
```

> **关键**:K8s request 不直接映射到 cgroup limit,而是 cpu.weight(调度权重)。limit 才是硬限制。

---

## 7. 常见陷阱与调优

### 7.1 陷阱:CPU throttling 延迟毛刺

**症状**:P99 延迟周期性飙高,`nr_throttled` 增长。

**原因**:`--cpus` 设过低,突发流量在 100ms 周期内用完 quota。

**修复**:
```bash
# 调高 limit
docker run --cpus="2.5" ...   # 2 → 2.5

# 或 K8s 关键服务只设 request,不设 limit
resources:
  requests:
    cpu: "2"
  # 不设 limits
```

### 7.2 陷阱:`--memory` 设过小

**症状**:Java 应用 OOM,但宿主机内存充足。

**修复**:
- JDK 8u191+ 自动感知 cgroup(`-XX:+UseContainerSupport`,默认开)
- 显式限制:`-XX:MaxRAMPercentage=75`
- 不用 `-Xms` / `-Xmx` 写死

### 7.3 陷阱:cgroup v1 与 v2 混用

**症状**:升级到 v2 后部分工具(旧版 cadvisor)不工作。

**修复**:
- 升级工具到 v2 兼容版本
- 或继续用 v1(但 K8s 1.22+ 推荐 v2)

### 7.4 陷阱:OOM Kill 但容器没重启

**症状**:容器内某进程被 OOM Kill,但容器(PID 1)还在。

**原因**:`memory.oom.group=0`(v2)或 v1 默认,只杀单个进程。

**修复**:
```bash
# v2:设 oom.group = 1,OOM 时杀整个 cgroup
echo 1 > /sys/fs/cgroup/.../memory.oom.group
```

### 7.5 调优:CPU shares vs quota

```bash
# shares:相对权重,空闲时可用全部
docker run --cpu-shares=512 ...   # 默认 1024,权重一半

# quota:绝对限制,不能超
docker run --cpus=2 ...           # 最多 2 核

# 生产实践:
# - 关键服务:只设 request(shares),不设 limit(quota)
# - 离线任务:设 limit(quota),防止抢占
# - K8s:requests 用 shares,limits 用 quota
```

### 7.6 调优:内存软限

```bash
# 软限 < 硬限
docker run \
  --memory="2g" \              # 硬限:超过 OOM
  --memory-reservation="1g" \  # 软限:超过时内核回收
  ...
```

**作用**:内存紧张时,内核优先回收超过软限的容器,给没超的留空间。

### 7.7 调优:NUMA 绑定

```bash
# CPU 绑定 + NUMA
docker run \
  --cpuset-cpus="0,1,2,3" \
  --cpuset-mems="0" \         # 绑定 NUMA 节点 0
  ...
```

> **大厂实践**:数据库等高性能服务必须 NUMA 绑定,避免跨节点访问延迟。

---

## 8. 工业案例与基准数据

### 8.1 CPU throttling 对延迟的影响

**测试条件**:Python FastAPI,`--cpus=1`,QPS 500。

| 指标 | 无 throttling | throttling(nr_throttled/s=10) |
|------|--------------|------------------------------|
| P50 | 5 ms | 8 ms |
| P95 | 20 ms | **180 ms** |
| P99 | 50 ms | **450 ms** |
| 错误率 | 0% | 0.1% |

**结论**:throttling 对 P99 影响巨大,关键服务应避免。

### 8.2 cgroup v1 vs v2 性能

| 维度 | v1 | v2 |
|------|----|----|
| 创建 cgroup | 1.2 ms | 0.8 ms |
| 内存统计准确性 | 一般 | 精确(含 kernel memory) |
| CPU throttling 平滑度 | 突变 | 略好 |
| IO 限制 | 块设备 | 块设备 + cgroup v2 io |
| 维护 | 复杂(多层级) | 简单(单层级) |

### 8.3 大厂 cgroup 调优基线

**阿里 ACK(公开)**:
- cgroup v2(K8s 1.22+)
- 关键服务:不设 CPU limit,只设 request
- 内存:request = limit(避免 OOM 后重调度)
- Java:`-XX:+UseContainerSupport -XX:MaxRAMPercentage=75`

**Netflix Titus**:
- cgroup v2
- 离线任务:CPU limit 严格
- 在线服务:CPU limit = 1.5 × request

**Google Borg**:
- 自研 cgroup 类似机制
- CPU 配额 + 优先级(在线 > 离线)
- 内存 hard limit + soft limit

### 8.4 OOM 影响范围对比

| 场景 | v1 默认 | v2 + oom.group=1 |
|------|---------|-------------------|
| 单进程 OOM | 杀该进程 | 杀整个 cgroup |
| 容器内多进程 | 可能存活 | 容器退出 |
| PID 1 被杀 | 容器退出 | 容器退出 |
| Java 多线程 | 杀线程?杀进程 | 杀整个 JVM |

> **生产实践**:v2 + `oom.group=1`,OOM 即重启整个容器,语义清晰。

---

## 9. 与其他方案的关系

### 9.1 cgroup vs RSL(Red Hat Resource Limits)

RSL 是 Red Hat 的资源限制工具,基于 cgroup。功能类似,生态小众。

### 9.2 cgroup vs systemd slice

```bash
# systemd 也用 cgroup
systemctl status nginx.service
# └─ cgroup : /system.slice/nginx.service

# systemd slice 资源限制
systemctl set-property nginx.service CPUQuota=200% MemoryMax=1G
```

> Docker 容器在 systemd 体系下也在 system.slice。

### 9.3 cgroup v1 vs v2

| 维度 | v1 | v2 |
|------|----|----|
| 层级 | 多棵树(每子系统一棵) | 单棵树 |
| 进程归属 | 可在多个 cgroup | 只在一个 |
| 内存统计 | 不含 kernel memory | 含 |
| 嵌套 | 不支持 | 支持 |
| 控制器 | 12 个独立 | 统一接口 |
| 维护 | 复杂 | 简单 |
| 兼容性 | 广 | 旧工具不兼容 |

> **趋势**:K8s 1.25+ 默认 v2,旧集群迁移需测试。

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| cgroup 解决什么? | 限制一组进程的资源(CPU / 内存 / IO / PID),超过则限制或杀掉。 |
| cgroup v1 与 v2 区别? | v1 多层级、进程在多个 cgroup;v2 单层级、进程在一个 cgroup,语义清晰。 |
| `--cpus=2` 在 cgroup 里是什么? | v2:`cpu.max = "200000 100000"`,每 100ms 周期可用 200ms CPU 时间。 |
| CPU throttling 是什么? | CFS quota 用完后,容器被冻结到下一周期,表现为延迟毛刺。 |
| `--memory` 与 `--memory-reservation` 区别? | memory 是硬限(超过 OOM),reservation 是软限(超过内核回收)。 |
| OOM Kill 后容器会怎样? | 通常 PID 1 被杀,容器退出(137);restart 策略决定是否重启。 |
| K8s requests 与 limits 映射到 cgroup? | requests → cpu.weight(权重),limits → cpu.max(硬限)。 |
| 为什么 cgroup v2 比 v1 好? | 单层级语义清晰、内存统计精确、维护简单、原生支持嵌套。 |
| 容器内 `/proc/meminfo` 准吗? | 不准,显示宿主机内存;应读 `/sys/fs/cgroup/memory.max`。 |
| `--oom-kill-disable` 安全吗? | 不安全,内存超限不杀进程,可能导致宿主机 OOM。 |

---

## 11. 综合面试题

### 题 1(原理)
**问**:解释 `--cpus=2` 的实现机制。

**答题要点**:
- CFS 调度器,周期 100ms(`cpu.cfs_period_us`)
- 配额 200ms(`cpu.cfs_quota_us`)
- 每 100ms 周期可用 200ms CPU 时间(2 核满载)
- v2:`cpu.max = "200000 100000"`
- 超过 quota → throttle,等下一周期
- 副作用:P99 延迟毛刺

### 题 2(故障)
**问**:容器 P99 延迟飙高,如何排查是否 CPU throttling?

**答题要点**:
- 看 `cpu.stat` 的 `nr_throttled` 和 `throttled_usec`
- `nr_throttled / nr_periods > 5%` 说明严重
- 修复:调高 `--cpus`,或只设 request 不设 limit
- 监控:Prometheus + cAdvisor 采集 `container_cpu_cfs_throttled_seconds_total`

### 题 3(深度)
**问**:为什么 K8s 关键服务建议只设 CPU request,不设 limit?

**答题要点**:
- request 用于调度(保证有资源)
- limit 是硬限,突发流量被 throttle
- 关键服务(P99 敏感)不应被 throttle
- 不设 limit:可用宿主机空闲 CPU
- 风险:其他容器被挤占
- 缓解:节点超卖率控制(< 1.5×)

### 题 4(实战)
**问**:Java 应用容器化后 OOM,如何排查?

**答题要点**:
- 看 `docker inspect` 的 `OOMKilled` 字段
- JDK 8u191+ 自动感知 cgroup(`UseContainerSupport`)
- 用 `-XX:MaxRAMPercentage=75` 而非 `-Xmx`
- 检查 native memory(Metaspace、Direct ByteBuffer)
- 看 GC 日志(是否频繁 Full GC)
- 监控:Heap / Non-Heap / Native

### 题 5(架构)
**问**:设计一个混部集群(在线 + 离线)的 cgroup 策略。

**答题要点**:
- 在线:高优先级,只设 CPU request,不设 limit
- 离线:低优先级,设 CPU limit + 低 weight
- 内存:在线 hard limit,离线 soft limit
- IO:在线优先,离线 throttle
- 突发:离线可被驱逐(`best-effort` QoS)
- 监控:在线延迟毛刺报警
- 工具:K8s + Koordinator(阿里)/ Crane(腾讯)

### 题 6(工业)
**问**:大厂为什么从 cgroup v1 迁移到 v2?

**答题要点**:
- 单层级,语义清晰
- 内存统计精确(含 kernel memory)
- 嵌套支持(K8s pod 内容器)
- 维护简单
- 新特性(io.latency 等)
- 挑战:旧工具(cAdvisor 旧版)不兼容
- K8s 1.22+ GA,1.25+ 默认

### 题 7(故障)
**问**:容器频繁 OOM(退出码 137),但应用没内存泄漏,可能原因?

**答题要点**:
- `--memory` 设过小
- JVM / Node 不感知 cgroup(用旧版本)
- cgroup v1 不统计 kernel memory(kmem),被 slab 占满
- Native memory(Metaspace、Direct Buffer)超限
- 父 cgroup 限制(节点超卖)
- 排查:`docker stats`、`/proc/<pid>/status`、JVM NMT

### 题 8(性能)
**问**:容器 IO 慢,如何用 cgroup 限制与调优?

**答题要点**:
- 用本地 volume 而非 NFS
- `io.max` 限制 IO(避免离线任务抢占)
- IO 调度器:deadline / noop(SSD)
- 文件系统:ext4 / xfs(避免 overlay2 跑数据库)
- 监控:`iostat -x`、`io.stat`(cgroup v2)

### 题 9(安全)
**问**:如何防止容器 fork bomb?

**答题要点**:
- `--pids-limit=200`(限制进程数)
- `--ulimit nproc=65535:65535`
- 监控:容器进程数告警
- 应用:限制线程池大小
- 案例:`:(){ :|:& };:` 一秒千进程

### 题 10(综合)
**问**:从内核视角解释 Docker 资源限制的完整链路。

**答题要点**:
- `docker run --cpus=2 --memory=1g`
- dockerd 调用 containerd → runc
- runc 创建 cgroup(`/sys/fs/cgroup/docker/<id>/`)
- 设置 cpu.max、memory.max
- 把容器进程 PID 写入 cgroup.procs
- 内核调度器(CFS)按 cpu.max 限制 CPU
- 内核内存管理按 memory.max 限制内存
- 超限 → throttle / OOM Killer
- 容器退出 → cgroup 删除

---

## 12. 故障复盘

### 案例 1:CPU throttling 导致 P99 飙升 9 倍

**现象**:某互联网公司迁移到 Docker 后,P99 延迟从 50ms 飙到 450ms。

**根因**:
- `--cpus=2`,突发流量在 100ms 内用完 200ms quota
- 后 50ms throttle,延迟毛刺

**修复**:
- 调高 `--cpus=2.5`
- 升级 cgroup v2,throttling 更平滑
- 关键服务只设 CPU request,不设 limit

**防范**:
- 监控 `nr_throttled`,> 0 报警
- 压测确定合理 limit
- Prometheus + cAdvisor 采集

### 案例 2:Java OOM,JVM 不感知 cgroup

**现象**:某团队 Spring Boot 容器化后,启动 30 秒 OOM。

**根因**:
- 用 JDK 8u131(不支持 cgroup)
- JVM 看到宿主机 64 GB,堆设到 16 GB
- 容器 limit 2 GB,启动即 OOM

**修复**:
- 升级 JDK 到 8u191+
- `-XX:+UseContainerSupport -XX:MaxRAMPercentage=75`

**防范**:
- 基础镜像统一 JDK 版本
- CI 跑容器化启动测试
- JVM 监控:Heap / Native

### 案例 3:cgroup v1 内存统计不准

**现象**:某容器内存 1 GB,但 `docker stats` 显示 1.5 GB。

**根因**:
- cgroup v1 不统计 kernel memory(slab)
- 实际内核 slab 占 500 MB
- 容器看似没超 limit,但宿主机内存被占

**修复**:
- 升级 cgroup v2(精确统计)
- 或 v1 设 `memory.kmem.limit_in_bytes`

### 案例 4:OOM 后容器没重启

**现象**:某服务内存泄漏,容器内某 worker 进程被 OOM Kill,但容器(PID 1)还在,业务半瘫。

**根因**:
- cgroup v1 默认 `memory.oom_control`:只杀单个进程
- PID 1 没被杀,容器不退出,restart 不触发

**修复**:
```bash
# v2:设 oom.group = 1
echo 1 > /sys/fs/cgroup/.../memory.oom.group
```

**防范**:
- 升级 cgroup v2 + `oom.group=1`
- 应用层:进程崩溃即退出 PID 1
- K8s livenessProbe 检测

### 案例 5:fork bomb 拖垮节点

**现象**:某测试容器跑 fork bomb(`:(){ :|:& };:`),几秒内创建数千进程,节点僵死。

**根因**:
- 没设 `--pids-limit`
- 容器可创建无限进程
- 耗尽宿主机 PID 表

**修复**:
```bash
docker run --pids-limit=200 ...
```

**防范**:
- 所有容器必须设 pids-limit
- 节点调大 `kernel.pid_max`
- 监控容器进程数

---

## 13. 参考与延伸

### 官方文档

- cgroup v2 docs — https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html
- cgroup v1 docs — https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v1/
- CFS scheduler — https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html

### 论文

- *The cgroup v2 design* — Tejun Heo, LSFMM 2015
- *CPU Bandwidth Control for CFS* — LWN.net

### 工具

- cAdvisor — 容器资源监控
-Prometheus node-exporter — cgroup metrics
- `systemd-cgtop` — 实时 cgroup 资源占用
- `systemd-cgls` — cgroup 树视图

### 大厂实践

- 阿里 Koordinator — 混部调度
- 字节 Kamino — 容器资源管理
- Google Borg — 资源配额与优先级
- Netflix Titus — 容器资源隔离

### 相关模块

- [08-底层原理-namespaces](./08-底层原理-namespaces.md) — 隔离机制
- [10-底层原理-UnionFS](./10-底层原理-UnionFS.md) — 分层存储
- [11-OCI规范与运行时](./11-OCI规范与运行时.md) — runc 实现
- [04-容器运行与生命周期](./04-容器运行与生命周期.md) — 资源限制参数
- [12-安全与隔离](./12-安全与隔离.md) — cgroup 逃逸
- [17-生产最佳实践](./17-生产最佳实践.md) — 生产级调优
- [25-工业实战-故障复盘集](./25-工业实战-故障复盘集.md) — cgroup 相关故障

---

> **下一章**:[10-底层原理-UnionFS](./10-底层原理-UnionFS.md)

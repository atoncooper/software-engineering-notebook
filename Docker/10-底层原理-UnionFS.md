# 10 - 底层原理 - UnionFS

> 镜像分层、写时复制、共享底层——这些 Docker 核心特性都源自 UnionFS。本章讲透 overlay2。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 把 **OverlayFS** 的工作原理讲到能读懂 `/var/lib/docker/overlay2`
- 把 **lowerdir / upperdir / workdir / merged** 的关系说清
- 把 **写时复制(CoW)** 的实现讲到内核级
- 把 **overlay2 vs aufs vs devicemapper** 的演进讲到位
- 把 **镜像分层与存储优化** 讲到能调优

### 1.2 本章不解决什么

- 不讲 namespace(见 [08-底层原理-namespaces](./08-底层原理-namespaces.md))
- 不讲 cgroup(见 [09-底层原理-cgroups](./09-底层原理-cgroups.md))
- 不讲 Dockerfile 写法(见 [03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md))
- 不讲 K8s CSI(见 [11-OCI规范与运行时](./11-OCI规范与运行时.md))

> **关键认知**:UnionFS 是 Docker 镜像分层的实现基础。Docker 早期用 aufs,后改 overlayFS(overlay2),性能与稳定性都更好。

---

## 2. 直觉解释

### 2.1 UnionFS 类比:透明硫酸纸

```
   传统文件系统              UnionFS
   ──────────────            ────────
   一个目录一个文件系统       多个目录叠加成"一个"
   看到的是真实文件           看到的是叠加视图
   
                              ┌────────────┐  ← 第 3 层(只读)
                              │  app.py    │
                              ├────────────┤
                              │  nginx     │  ← 第 2 层(只读)
                              ├────────────┤
                              │  debian    │  ← 第 1 层(只读)
                              └─────┬──────┘
                                    │ 叠加(union mount)
                                    ▼
                              ┌────────────┐
                              │  /app      │  ← 统一视图
                              │  /usr/bin  │
                              │  /etc      │
                              └────────────┘
```

### 2.2 写时复制(CoW)类比

```
   读文件:                      写文件:
   ────────                      ────────
   从下层找,直接读              1. 从下层复制到上层
                                 2. 修改上层副本
                                 3. 下层不变
   
   ┌──────────┐                ┌──────────┐    ┌──────────┐
   │ upper    │                │ upper    │ ← │ modified │
   │ (空)     │                │ file.txt │    │  file    │
   ├──────────┤                ├──────────┤    ├──────────┤
   │ lower    │                │ lower    │    │ lower    │
   │ file.txt │                │ file.txt │    │ file.txt │
   └──────────┘                └──────────┘    └──────────┘
   读 lower                  写前(复制)      写后(修改 upper)
```

---

## 3. 核心概念与架构

### 3.1 OverlayFS 三层结构

```
┌─────────────────────────────────────────────────┐
│              merged(统一视图)                   │
│   容器内看到的文件系统                            │
│   /app /usr/bin /etc /var ...                   │
└────────────────────┬────────────────────────────┘
                     │
        ┌────────────┴────────────┐
        │                         │
        ▼                         ▼
┌──────────────┐          ┌──────────────────┐
│  upperdir    │          │  lowerdir        │
│  (可读写)    │          │  (只读,可多层)  │
│              │          │                  │
│  容器层       │          │  镜像层(从下到上)│
│  /var/lib/   │          │  /var/lib/docker/│
│  docker/     │          │  overlay2/l/...  │
│  overlay2/   │          │                  │
│  <id>/diff   │          │  多层叠加         │
└──────────────┘          └──────────────────┘
        │
        ▼
┌──────────────┐
│  workdir     │
│  (内部工作) │
│  overlay     │
│  内部用       │
└──────────────┘
```

### 3.2 overlay2 在磁盘上的布局

```
/var/lib/docker/overlay2/
├── <layer-hash-A>/        # 镜像第 1 层(debian)
│   ├── diff/              # 该层文件
│   │   ├── usr/
│   │   ├── etc/
│   │   └── ...
│   ├── work/
│   └── committed
│
├── <layer-hash-B>/        # 镜像第 2 层(nginx)
│   ├── diff/
│   │   ├── usr/sbin/nginx
│   │   └── etc/nginx/
│   ├── work/
│   ├── lower              # 指向下层(A)
│   └── committed
│
├── <layer-hash-C>/        # 镜像第 3 层(应用)
│   ├── diff/
│   │   └── app/
│   ├── work/
│   ├── lower              # 指向 B → A(链式)
│   └── committed
│
└── <container-id>/        # 容器层(可读写)
    ├── diff/              # 容器内修改的文件
    ├── work/
    ├── lower              # 指向 C → B → A
    ├── merged/            # overlay 挂载点(统一视图)
    └── link
```

### 3.3 overlay mount 命令

```bash
# 容器启动时,dockerd 执行(简化):
mount -t overlay overlay \
  -o lowerdir=/var/lib/docker/overlay2/l/A:/var/lib/docker/overlay2/l/B:/var/lib/docker/overlay2/l/C \
  -o upperdir=/var/lib/docker/overlay2/<container-id>/diff \
  -o workdir=/var/lib/docker/overlay2/<container-id>/work \
  /var/lib/docker/overlay2/<container-id>/merged

# 然后 chroot / pivot_root 到 merged
```

### 3.4 overlay2 vs aufs vs devicemapper

| 维度 | aufs | overlay(旧) | overlay2 | devicemapper |
|------|------|---------------|-----------|---------------|
| 内核支持 | out-of-tree | 3.18+ | 3.18+(推荐) | 2.6+ |
| 性能 | 中 | 中 | 高 | 中 |
| 层数限制 | 127 | 128 | 128 | 无 |
| 稳定性 | 好 | 一般 | 好 | 一般(曾丢数据) |
| 现状 | 废弃 | 废弃 | **默认** | 废弃 |

> Docker 18.06+ 默认 overlay2,其他驱动已不推荐。

---

## 4. 操作流程与命令

### 4.1 查看容器的 overlay 配置

```bash
docker run -d --name web nginx:1.25
docker inspect web --format '{{.GraphDriver.Data}}'

# 输出:
# MergedDir:/var/lib/docker/overlay2/<id>/merged
# UpperDir:/var/lib/docker/overlay2/<id>/diff
# WorkDir:/var/lib/docker/overlay2/<id>/work
# LowerDir:/var/lib/docker/overlay2/l/ABC:/var/lib/docker/overlay2/l/DEF:...
```

### 4.2 查看镜像分层

```bash
# 镜像历史(逻辑层)
docker history nginx:1.25
# IMAGE          CREATED       CREATED BY                    SIZE
# a1b2c3d4e5f6   2 days ago    CMD ["nginx" "-g" "daemon...  0B
# b2c3d4e5f6a1   2 days ago    STOPSIGNAL SIGQUIT             0B
# ...
# ff12ff12ff12   2 weeks ago   /bin/sh -c #(nop) ADD file...  31MB

# 镜像层(物理层)
docker inspect nginx:1.25 --format '{{.GraphDriver.Data.LowerDir}}'
# /var/lib/docker/overlay2/l/A:/var/lib/docker/overlay2/l/B:...

# 查看每层文件
ls /var/lib/docker/overlay2/l/A/
# usr/ etc/ var/ ...
```

### 4.3 实操:手动创建 overlay

```bash
# 创建目录
mkdir -p /tmp/overlay/{lower1,lower2,upper,work,merged}

# lower1 放文件
echo "from lower1" > /tmp/overlay/lower1/file1.txt
echo "shared" > /tmp/overlay/lower1/shared.txt

# lower2 放文件
echo "from lower2" > /tmp/overlay/lower2/file2.txt
echo "overridden" > /tmp/overlay/lower2/shared.txt

# 挂载 overlay
sudo mount -t overlay overlay \
  -o lowerdir=/tmp/overlay/lower2:/tmp/overlay/lower1 \
  -o upperdir=/tmp/overlay/upper \
  -o workdir=/tmp/overlay/work \
  /tmp/overlay/merged

# 查看 merged
ls /tmp/overlay/merged/
# file1.txt  file2.txt  shared.txt

cat /tmp/overlay/merged/file1.txt       # from lower1
cat /tmp/overlay/merged/file2.txt       # from lower2
cat /tmp/overlay/merged/shared.txt      # overridden(lower2 在上层)

# 修改文件(触发 CoW)
echo "modified" > /tmp/overlay/merged/file1.txt

# 查看上层
cat /tmp/overlay/upper/file1.txt        # modified
cat /tmp/overlay/lower1/file1.txt       # from lower1(不变)

# 删除文件(whiteout)
rm /tmp/overlay/merged/file2.txt

# 查看上层(whiteout 文件)
ls /tmp/overlay/upper/
# file1.txt  file2.txt(实际是 whiteout,字符设备 0/0)

ls -la /tmp/overlay/upper/file2.txt
# c--------- 1 root root 0, 0 ... file2.txt(whiteout)
```

---

## 5. 底层原理

### 5.1 whiteout 机制

```
删除文件:
  merged/file.txt(在 lower 中)
  → 在 upper 创建 whiteout 文件(字符设备 0/0)
  → overlay 看到 whiteout,隐藏 lower 的文件

whiteout 文件:
  mknod c 0 0 file.txt
  
  ls -la upper/file.txt
  c--------- 1 root root 0, 0 ... file.txt
```

### 5.2 opaque 目录

```
删除整个目录:
  merged/dir/(在 lower 中,有 100 个文件)
  → 在 upper 创建 dir/,加 xattr "trusted.overlay.opaque=y"
  → overlay 看到 opaque,完全忽略 lower 的 dir/

应用场景:
  RUN rm -rf /var/lib/apt/lists/*
  → 创建 opaque,而非 100 个 whiteout
  → 节省 inode 与 metadata
```

### 5.3 CoW 的实现

```
修改文件(已存在 lower):
  1. open(merged/file.txt, O_WRONLY)
  2. overlay 拦截:
     a. 复制 lower/file.txt → upper/file.txt
     b. 替换 file descriptor 指向 upper/file.txt
  3. write(fd, "modified")
  4. upper/file.txt 被修改
  5. lower/file.txt 不变

创建新文件:
  1. open(merged/new.txt, O_CREAT)
  2. overlay 直接在 upper/new.txt 创建
  3. lower 不变
```

### 5.4 overlay2 的链式 lower

```
旧 overlay:lower 是一个大目录(所有层合并)
  缺点:层数多时,合并开销大

overlay2:lower 是多个目录,链式查找
  lowerdir=A:B:C
  
  查找 /etc/nginx/nginx.conf:
    1. upper(找不到)
    2. C(找不到)
    3. B(找到,返回)
    4. A(不查)
  
  优点:无需合并,延迟查找
  缺点:层数多时,查找慢(深链)
```

### 5.5 内核实现(简化)

```c
// fs/overlayfs/super.c(简化)

struct ovl_fs {
    struct vfsmount *upper_mnt;        // upperdir
    struct vfsmount **lower_mnt;       // lowerdir 数组
    unsigned int numlower;             // lower 层数
    struct dentry *workdir;            // workdir
    ...
};

// 查找文件
static struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry) {
    // 1. 在 upper 找
    upper = ovl_lookup_upper(dentry);
    if (upper && !ovl_dentry_is_whiteout(upper))
        return upper;
    
    // 2. 在 lower 找(从上到下)
    for (i = 0; i < numlower; i++) {
        lower = ovl_lookup_lower(dentry, i);
        if (lower)
            break;
    }
    
    // 3. 合并 upper + lower
    return ovl_dentry_create(upper, lower);
}
```

---

## 6. 代码与配置示例

### 6.1 镜像分层优化

```dockerfile
# ❌ 每次改代码,后面所有层 cache miss
COPY . /app
RUN npm install

# ✓ requirements 很少变,放前面
COPY package.json package-lock.json ./
RUN npm ci
COPY . /app
```

### 6.2 删除文件不减小镜像

```dockerfile
# ❌ 删除下层文件,镜像不变小(whiteout 占空间)
FROM debian
RUN apt-get update && apt-get install -y curl
RUN rm -rf /var/lib/apt/lists/*   # whiteout,镜像没小

# ✓ 在同一层删除
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*
```

### 6.3 多阶段构建避免工具残留

```dockerfile
# ❌ 编译工具留在最终镜像
FROM golang:1.22
COPY . .
RUN go build -o /app .
# 最终镜像含 golang 工具链(1.2 GB)

# ✓ 多阶段
FROM golang:1.22 AS builder
COPY . .
RUN go build -o /app .

FROM scratch
COPY --from=builder /app /app
# 最终镜像只有二进制(15 MB)
```

### 6.4 查看镜像层细节

```bash
# 用 dive 工具
dive nginx:1.25

# 输出:
# Layer 1: ADD file:... (31 MB)
#   - /usr/bin/*
#   - /etc/*
# Layer 2: RUN apt-get install
#   - /usr/sbin/nginx
#   - /etc/nginx/
# Layer 3: COPY index.html
#   - /usr/share/nginx/html/index.html
```

---

## 7. 常见陷阱与调优

### 7.1 陷阱:镜像层数过多

```dockerfile
# ❌ 50 层
RUN apt-get update
RUN apt-get install -y curl
RUN apt-get install -y git
RUN apt-get install -y vim
RUN mkdir /app
RUN cd /app
... (50 个 RUN)

# ✓ 合并
RUN apt-get update \
    && apt-get install -y --no-install-recommends curl git vim \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir /app
```

### 7.2 陷阱:大文件修改触发整文件 CoW

```dockerfile
# ❌ 1 GB 日志文件,每次写都 CoW
RUN dd if=/dev/zero of=/var/log/big.log bs=1M count=1024
RUN echo "new line" >> /var/log/big.log
# 第二层复制整个 1 GB

# ✓ 不在镜像里放大文件
# 日志放 volume 或不写
```

### 7.3 陷阱:容器内删除文件不释放空间

```bash
# 容器内 rm 镜像文件
docker exec web rm /usr/share/nginx/html/index.html
# 创建 whiteout,容器层占空间
# 但镜像层文件还在,总空间不变

# 删除容器才释放
docker rm web
```

### 7.4 陷阱:overlay2 跑数据库性能差

**原因**:
- overlay2 有 CoW,数据库 fsync 性能下降
- 文件 metadata 操作有额外开销
- 大文件(数据文件)修改触发 CoW

**修复**:数据目录用 volume(本地 ext4/xfs),不用 overlay2。

### 7.5 调优:存储驱动选择

```bash
# 查看当前驱动
docker info | grep "Storage Driver"
# Storage Driver: overlay2

# 切换(需清空 /var/lib/docker)
# daemon.json:
{
  "storage-driver": "overlay2"
}
```

### 7.6 调优:inode 监控

```bash
# 镜像层多了会耗尽 inode
df -i /var/lib/docker

# 节点 inode 用尽
# 修复:清理悬空镜像
docker image prune -a
```

### 7.7 调优:备份 overlay2

```bash
# 备份单个容器层
tar czf container-layer.tar.gz \
  /var/lib/docker/overlay2/<id>/diff

# 备份整个 docker(停服务)
systemctl stop docker
tar czf docker-backup.tar.gz /var/lib/docker
systemctl start docker
```

---

## 8. 工业案例与基准数据

### 8.1 overlay2 vs 其他存储驱动性能

**测试条件**:fio 顺序写 4KB,容器内测试。

| 驱动 | IOPS | 吞吐 | 延迟 |
|------|------|------|------|
| overlay2(本地 ext4) | 9500 | 38 MB/s | 0.1 ms |
| overlay2(本地 xfs) | 9600 | 38 MB/s | 0.1 ms |
| aufs(旧) | 8500 | 34 MB/s | 0.12 ms |
| devicemapper(loop) | 3000 | 12 MB/s | 0.4 ms |
| 直接挂 ext4(无 overlay) | 10500 | 42 MB/s | 0.09 ms |

**结论**:overlay2 接近原生(差距 < 10%)。

### 8.2 overlay2 在大厂的优化

**阿里**:
- 基础镜像统一(共享底层)
- 镜像层 P2P 分发(Dragonfly)
- 按需加载(Nydus,只读层按需下载)
- 实测:1 GB 镜像启动从 30s → 3s

**字节**:
- 镜像层去重(相同层共享)
- overlay2 + 本地 SSD
- 单节点 200+ 容器,镜像存储 < 50 GB(共享底层)

### 8.3 镜像分层共享效率

```
1000 个微服务,都基于 python:3.12-slim(150 MB)

无共享(每个镜像独立):
  1000 × 150 MB = 150 GB

overlay2 共享底层:
  1 × 150 MB(基础层)+ 1000 × 50 MB(应用层)= 50.15 GB
  
节省:99.7%
```

### 8.4 数据库存储对比

**测试条件**:PostgreSQL 16,TPC-C 10 warehouse。

| 存储方式 | TPS | P99 延迟 | 备注 |
|----------|-----|----------|------|
| overlay2(容器层) | 850 | 45 ms | CoW 开销,不推荐 |
| volume(本地 ext4) | 1200 | 25 ms | 推荐 |
| bind mount(本地 xfs) | 1180 | 26 ms | 推荐 |
| NFS volume | 320 | 180 ms | 网络开销,不推荐 |

---

## 9. 与其他方案的关系

### 9.1 overlay2 vs Btrfs / ZFS subvolume

| 维度 | overlay2 | Btrfs subvolume | ZFS |
|------|----------|-----------------|-----|
| 文件系统 | 任意(ext4/xfs) | Btrfs | ZFS |
| CoW | 文件级 | 文件级 | 块级 |
| 快照 | 无 | 有(瞬时) | 有(瞬时) |
| 压缩 | 无 | 有 | 有 |
| 生态 | Docker 默认 | Docker 支持 | Docker 支持 |
| 适用 | 通用 | 高级用户 | 高级用户 |

### 9.2 overlay2 vs VFS / fuse-overlayfs

| 驱动 | 用途 |
|------|------|
| overlay2 | Linux 原生(默认) |
| fuse-overlayfs | rootless(用户态) |
| vfs | 兜底(无 CoW,慢) |

### 9.3 Docker 镜像与 OCI artifact

OCI 镜像规范基于 Docker 镜像规范,manifest + config + layers。UnionFS 是 Docker 实现细节,OCI 规范不规定存储方式。

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| OverlayFS 三层是什么? | lowerdir(只读镜像层)、upperdir(可读写容器层)、workdir(overlay 内部用)。 |
| 写时复制(CoW)是什么? | 写文件时先把下层文件复制到上层,再修改,下层不变。 |
| 删除镜像文件为什么镜像不变小? | 在 upper 创建 whiteout,隐藏下层文件,但下层文件还在,镜像不变小。 |
| overlay2 比 aufs 好在哪? | 内核原生(无需补丁)、性能更好、层数支持多、维护活跃。 |
| 容器内修改文件后,镜像层变了吗? | 没变,改的是 upper(容器层);镜像层只读。 |
| 为什么数据库不用 overlay2? | CoW 影响 fsync 性能,大文件修改触发整文件复制;用 volume 直挂本地 ext4/xfs。 |
| 镜像层数过多有什么影响? | 启动慢(每层一次 mount)、查找慢(深链)、inode 浪费。 |
| 多个镜像共享底层靠什么? | overlay2 的 lowerdir 可被多个容器引用,磁盘只存一份。 |
| whiteout 是什么? | upper 中的字符设备(0/0),overlay 看到它就隐藏 lower 的对应文件。 |
| opaque 目录是什么? | upper 中的目录带 xattr "trusted.overlay.opaque=y",overlay 完全忽略 lower 的同名目录。 |

---

## 11. 综合面试题

### 题 1(原理)
**问**:解释 `docker run` 时 overlay mount 的过程。

**答题要点**:
- 检查镜像层,找到所有 lowerdir
- 创建容器 upperdir 与 workdir
- 调用 mount -t overlay,组合 lowerdir + upperdir + workdir → merged
- pivot_root 到 merged
- 容器进程在 merged 视图内运行

### 题 2(实战)
**问**:`RUN rm -rf /var/lib/apt/lists/*` 为什么不影响镜像大小?

**答题要点**:
- rm 在 upper 创建 whiteout
- lower(镜像层)的文件还在
- 镜像总大小 = lower + upper(whiteout 几乎不占空间)
- 修复:在同一 RUN 内 rm

### 题 3(故障)
**问**:容器内数据库 IO 慢,如何排查是否 overlay 问题?

**答题要点**:
- 看 /proc/mounts,数据目录是否在 overlay
- 用 volume 直挂本地 ext4/xfs
- 测试:`fio` 在 overlay vs volume 上对比
- overlay2 的 CoW 影响 fsync
- 修复:数据目录用 volume

### 题 4(深度)
**问**:解释 overlay2 的链式 lower 与旧 overlay 的合并 lower 区别。

**答题要点**:
- 旧 overlay:lower 是一个合并目录(预先合并所有层)
  - 优点:查找快(O(1))
  - 缺点:层数多时合并开销大
- overlay2:lower 是多个目录(链式查找)
  - 优点:无需合并,启动快
  - 缺点:查找 O(n),层数多时慢
- Docker 默认 overlay2

### 题 5(优化)
**问**:镜像从 1.2 GB 减到 50 MB,有哪些手段?

**答题要点**:
- 多阶段构建(builder + runtime)
- 基础镜像用 alpine / distroless / scratch
- RUN 合并(减少层数)
- 同层删除(rm 与 install 同一 RUN)
- 用 .dockerignore 排除大文件
- 二进制静态链接 + upx 压缩
- 不带源码、不带编译器、不带 shell

### 题 6(工业)
**问**:大厂如何优化镜像存储?

**答题要点**:
- 基础镜像统一(共享底层)
- P2P 分发(Dragonfly / Kraken)
- 按需加载(Nydus / Stargz)
- 镜像层去重
- 压缩(zstd)
- 监控:镜像存储占用、inode 使用率

### 题 7(架构)
**问**:overlay2 为什么不能跨主机?

**答题要点**:
- overlay2 是本地文件系统挂载
- lowerdir 必须在本地磁盘
- 跨主机需:NFS / 分布式存储 / CSI
- 或:每台机器本地存镜像(分布式镜像分发)
- K8s 不直接跨主机共享 rootfs

### 题 8(故障)
**问**:`docker run` 报 `overlayfs: maximum overlay stacking depth exceeded`,如何处理?

**答题要点**:
- 镜像层数超过 128(overlay2 限制)
- 修复:多阶段构建,减少层数
- 或:docker build --squash(实验性)
- 或:重新组织 Dockerfile,合并 RUN

### 题 9(性能)
**问**:overlay2 的性能开销主要在哪?

**答题要点**:
- 创建容器:mount overlay(几毫秒)
- 文件查找:链式 lower(深链慢)
- 第一次写:CoW(大文件慢)
- metadata 操作:lookup 有开销
- 整体 < 10% 性能损失

### 题 10(综合)
**问**:从内核视角解释 Docker 镜像分层的实现。

**答题要点**:
- 镜像 = 多层只读 rootfs
- 每层是 `/var/lib/docker/overlay2/<hash>/diff/`
- overlay2 把多层叠加成统一视图(merged)
- 容器启动:加 upperdir(可读写) + workdir → mount overlay
- 写时复制:写 upper,lower 不变
- whiteout:删除 lower 文件,upper 创建字符设备
- 共享底层:多镜像引用相同 lower,磁盘只存一份
- 性能开销 < 10%

---

## 12. 故障复盘

### 案例 1:镜像层数过多导致启动慢

**现象**:某镜像 80 层,容器启动 8 秒,影响弹性扩容。

**根因**:
- 历史 Dockerfile,每条命令一层
- overlay2 链式查找,80 层深链

**修复**:
```dockerfile
# 合并 RUN
RUN apt-get update \
    && apt-get install -y curl git vim \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir /app
```

**效果**:层数 80 → 15,启动 8s → 2s。

### 案例 2:overlay2 跑 MySQL 性能差

**现象**:某团队 MySQL 容器化后,TPS 下降 30%。

**根因**:
- 数据目录在 overlay2
- CoW 影响 fsync,写放大

**修复**:
```bash
docker run -v /data/mysql:/var/lib/mysql mysql:8
# 用 volume 直挂本地 ext4
```

**效果**:TPS 恢复。

**防范**:
- 数据库不用 overlay2
- 监控:容器内 fio 测试基线

### 案例 3:容器内删除文件不释放空间

**现象**:某容器内日志占 50 GB,rm 后 `df` 显示没变。

**根因**:
- 日志在容器层(upper)
- rm 后文件被进程持有(handle 未释放)
- 容器层空间未释放

**修复**:
```bash
# 重启持有文件的进程
docker exec web kill -HUP <pid>

# 或重启容器
docker restart web
```

**防范**:
- 日志写 stdout/stderr,不写文件
- 或日志写 volume,定期轮转

### 案例 4:inode 耗尽

**现象**:节点 `df -h` 显示磁盘 30%,但容器起不来,报 `No space left on device`。

**根因**:
- 镜像层数多,小文件多
- inode 耗尽(虽然 block 还有)

**修复**:
```bash
df -i /var/lib/docker
# 看 inode 使用率

# 清理悬空镜像
docker image prune -a
```

**防范**:
- 监控 inode 使用率
- 节点定期清理
- 镜像层数控制

### 案例 5:overlay mount 失败

**现象**:容器启动报 `overlayfs: failed to resolve ...`。

**根因**:
- 镜像层文件损坏(磁盘坏块)
- 或被外部删除(误删 /var/lib/docker)

**修复**:
```bash
# 删除损坏镜像
docker image rm <image>

# 重新拉取
docker pull <image>
```

**防范**:
- 磁盘健康监控
- 不手动改 /var/lib/docker
- 定期备份

---

## 13. 参考与延伸

### 官方文档

- OverlayFS — https://docs.docker.com/storage/storagedriver/overlayfs-driver/
- kernel docs — https://www.kernel.org/doc/html/latest/filesystems/overlayfs.html
- Docker storage drivers — https://docs.docker.com/storage/storagedriver/

### 内核源码

- `fs/overlayfs/super.c`
- `fs/overlayfs/copy_up.c`
- `fs/overlayfs/inode.c`

### 工具

- dive — 镜像分层分析
- docker-slim — 镜像自动瘦身
- Nydus — 按需加载镜像
- Stargz — 按需加载镜像

### 大厂实践

- 阿里 Dragonfly + Nydus
- 字节镜像层去重
- Google Stargz(eStargz)

### 相关模块

- [08-底层原理-namespaces](./08-底层原理-namespaces.md) — 隔离机制
- [09-底层原理-cgroups](./09-底层原理-cgroups.md) — 资源限制
- [11-OCI规范与运行时](./11-OCI规范与运行时.md) — 镜像规范
- [03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md) — Dockerfile 优化
- [06-数据存储与卷](./06-数据存储与卷.md) — volume vs overlay
- [23-工业实战-镜像分发与CDN](./23-工业实战-镜像分发与CDN.md) — 大规模镜像分发

---

> **下一章**:[11-OCI规范与运行时](./11-OCI规范与运行时.md)

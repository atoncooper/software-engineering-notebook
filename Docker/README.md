# Docker 学习笔记

> 容器化技术的基础原理、工程实践与生产最佳实践笔记。
>
> 目标：从 **使用** → **原理** → **底层实现** → **生产运维** → **工业实战** 五层递进，构建可追溯、可复用的 Docker 知识体系。
>
> 工业视角贯穿全程：大厂生产案例、性能基准数据、故障复盘、容量与成本规划、供应链安全实践。

---

## 一、模块定位

本模块不是「命令速查表」，而是一套面向 **工程实践 + 底层原理 + 面试纵深 + 生产运维 + 工业实战** 五位一体的 Docker 学习笔记。

每篇笔记遵循统一结构：

1. **问题定义与边界**：解决什么、不解决什么
2. **直觉解释**：先讲直觉，再上命令
3. **核心概念与架构**：C/S 架构、命名空间、cgroups、UnionFS
4. **操作流程与命令**：CLI 用法 + 参数详解
5. **底层原理**：namespace / cgroup / overlayfs 源码级剖析
6. **代码与配置示例**：Dockerfile、compose.yaml、生产级模板
7. **常见陷阱与调优**：镜像膨胀、层数过多、权限、安全
8. **工业案例与基准数据**：大厂生产实践、P95 延迟、镜像大小、启动耗时、密度上限
9. **与其他方案的关系**：vs Podman、vs 虚拟机、vs Kubernetes
10. **面试速答**：高频问题的一句话答案
11. **综合面试题**：由浅入深，含答题要点
12. **故障复盘**：真实生产事故案例、根因、修复、防范
13. **参考与延伸**：官方文档、源码、跨文件链接

---

## 二、目录结构规划

```
Docker/
├── README.md                         # 本文件
│
├── 01-基础与核心概念.md               # 容器 vs 虚拟机、架构、术语
├── 02-安装与CLI基础.md                # daemon、CLI、镜像拉取运行
├── 03-镜像原理与Dockerfile.md         # 分层、UnionFS、构建优化
├── 04-容器运行与生命周期.md            # 启停、资源限制、命名空间
├── 05-容器网络.md                     # bridge/host/overlay、DNS、服务发现
├── 06-数据存储与卷.md                  # volume/bind mount/tmpfs
├── 07-Docker-Compose.md               # 多容器编排、服务依赖
│
├── 08-底层原理-namespaces.md           # PID/NET/MNT/UTS/IPC/USER/CGROUP
├── 09-底层原理-cgroups.md              # v1/v2、CPU/内存/IO 限制
├── 10-底层原理-UnionFS.md              # OverlayFS、分层存储、写时复制
├── 11-OCI规范与运行时.md               # runc、containerd、OCI spec
│
├── 12-安全与隔离.md                    # seccomp/AppArmor/SELinux、镜像签名
├── 13-镜像仓库与分发.md                # Hub、Harbor、镜像分发、GC
├── 14-监控与日志.md                    # stats/logs、Prometheus、ELK
│
├── 15-Dockerfile实战模板.md            # 各语言生产级 Dockerfile
├── 16-Docker与CI-CD.md                 # 流水线、多阶段构建、缓存
├── 17-生产最佳实践.md                  # 镜像瘦身、安全加固、故障排查
│
├── 18-Docker-Swarm入门.md              # 原生编排、服务、栈
├── 19-容器生态对比.md                  # Podman/containerd/CRI-O/K8s
├── 20-常见问题与陷阱.md                # 调试、性能、安全 FAQ
│
├── 21-工业实战-大厂镜像构建流水线.md    # 阿里/字节/Netflix 构建系统
├── 22-工业实战-大规模集群密度优化.md    # 单机容器数、资源碎片、调度
├── 23-工业实战-镜像分发与CDN.md         # 多地域、P2P、Dragonfly、Kraken
├── 24-工业实战-供应链安全.md            # SBOM、签名、Cosign、准入控制
├── 25-工业实战-故障复盘集.md            # 生产事故根因、修复、防范
└── 26-工业实战-成本与容量规划.md        # 资源利用率、FinOps、弹性
```

### 状态图例

- ✅ 已建：笔记已完成
- ⏳ 待建：目录已占位，内容规划中

---

## 三、章节索引

### 第一部分：基础与使用

#### 01-基础与核心概念 ⏳

容器技术演进、Docker 是什么、C/S 架构、核心术语（镜像 / 容器 / 仓库 / 引擎 / 运行时）。

#### 02-安装与 CLI 基础 ⏳

Docker Engine 安装（Linux / macOS / Windows）、daemon 配置、CLI 常用命令、镜像拉取与容器运行初体验。

#### 03-镜像原理与 Dockerfile ⏳

镜像分层结构、UnionFS、Dockerfile 指令全集、多阶段构建、构建缓存、镜像瘦身。

#### 04-容器运行与生命周期 ⏳

`docker run` 全参数、容器状态机、资源限制（`--cpus` / `--memory`）、重启策略、健康检查。

#### 05-容器网络 ⏳

四种网络模型（bridge / host / none / container）、自定义 bridge、端口映射、DNS、overlay 跨主机通信、macvlan。

#### 06-数据存储与卷 ⏳

三种存储方式（volume / bind mount / tmpfs）的适用场景、数据持久化、备份与迁移、性能考量。

#### 07-Docker Compose ⏳

多容器编排、`compose.yaml` 语法、服务依赖、网络与卷声明、开发环境与生产环境差异。

---

### 第二部分：底层原理

#### 08-底层原理 - namespaces ⏳

Linux namespaces 全解：PID / NET / MNT / UTS / IPC / USER / CGROUP，`unshare` / `nsenter` 实操，源码级剖析。

#### 09-底层原理 - cgroups ⏳

cgroups v1 vs v2、子系统（cpu / memory / blkio / pids）、`docker run` 资源限制的底层映射、OOM Killer。

#### 10-底层原理 - UnionFS ⏳

OverlayFS 工作原理、lowerdir / upperdir / workdir、写时复制（CoW）、镜像分层与挂载、`overlay2` 存储驱动。

#### 11-OCI 规范与运行时 ⏳

OCI Image Spec / Runtime Spec、`runc`、`containerd`、`CRI`、Docker Engine 与 containerd 的关系、 shim 架构。

---

### 第三部分：安全、运维与生态

#### 12-安全与隔离 ⏳

容器安全全景：namespaces 隔离边界、seccomp profile、AppArmor / SELinux、capability 裁剪、镜像签名（Notary / Cosign）、漏洞扫描（Trivy）。

#### 13-镜像仓库与分发 ⏳

Docker Hub、私有仓库（registry / Harbor）、镜像分发优化、GC、镜像签名、供应链安全。

#### 14-监控与日志 ⏳

`docker stats` / `docker logs`、Prometheus + cAdvisor、Grafana 看板、ELK / Loki 日志聚合、容器 PID 1 与信号处理。

---

### 第四部分：实战与生产

#### 15-Dockerfile 实战模板 ⏳

各语言生产级模板：Go（多阶段 + scratch）、Java（分层 JAR）、Python（venv + 非 root）、Node.js（pnpm + 构建缓存）、Rust（musl 静态链接）。

#### 16-Docker 与 CI/CD ⏳

GitHub Actions / GitLab CI 中的镜像构建、构建缓存策略（BuildKit / buildx）、镜像多架构（multi-arch）、镜像推送与签名。

#### 17-生产最佳实践 ⏳

镜像瘦身十二式、非 root 用户、只读根文件系统、健康检查、优雅停止、日志驱动选择、故障排查（`docker system df` / `docker events`）。

---

### 第五部分：编排与生态

#### 18-Docker Swarm 入门 ⏳

Swarm 模式、service / task / node 模型、`docker stack deploy`、rolling update、与 K8s 的取舍。

#### 19-容器生态对比 ⏳

Docker vs Podman（守护进程 vs daemonless、root vs rootless）、containerd / CRI-O 在 K8s 中的角色、Firecracker / gVisor / Kata Containers（沙箱运行时）。

#### 20-常见问题与陷阱 ⏳

调试技巧（`docker exec` / `docker inspect` / nsenter）、性能问题、磁盘占用、网络不通、权限问题、Windows / macOS 的 Hyper-V / VirtioFS 陷阱。

---

### 第六部分：工业实战

> 本部分汇总大厂生产实践、性能基准、故障复盘与成本规划，是面试加分项与生产参考的双保险。

#### 21-工业实战 - 大厂镜像构建流水线 ⏳

- **阿里云 ACR + 云效**：多架构构建、镜像加速、P2P 分发
- **字节跳动 ByteCycle**：万级微服务构建缓存、增量构建
- **Netflix Spinnaker + Titus**：镜像构建与发布一体化
- **Google Borg + Blaze**：远程构建、分层缓存共享
- **GitHub Actions + buildx**：开源项目可复用模板
- 基准数据：万行代码项目构建耗时从 8min → 90s 的优化路径

#### 22-工业实战 - 大规模集群密度优化 ⏳

- 单机容器密度：从 50 → 200 → 500 的演进（cgroup v2、内核调优、sidecar 拆分）
- 资源碎片问题：CPU / 内存绑定策略、Kubelet 的 `--cpu-manager-policy=static`
- 启动风暴：上千容器同时拉起时的镜像拉取优化、镜像预热、`docker run` 限速
- 阿里 Sigma、字节 Kamino、腾讯 STKE 的密度实践对比
- 基准数据：单节点 Pod 密度极限、内核参数（`/proc/sys/fs/inotify/max_user_instances`）

#### 23-工业实战 - 镜像分发与 CDN ⏳

- **Dragonfly**（阿里）：P2P 镜像分发，万节点并发拉取带宽降 80%
- **Kraken**（Uber）：去中心化镜像分发，与 Dragonfly 对比
- **腾讯 TCR 加速**：跨地域镜像同步、按需加载（Lazy Pull）
- **Stargz / eStargz / Nydus**：按需加载镜像，启动时间从 30s → 3s
- 私有仓库 GC 实战：Harbor 在线 / 离线 GC、空间回收
- 基准数据：1GB 镜像在 1000 节点并发拉取的耗时与带宽对比

#### 24-工业实战 - 供应链安全 ⏡

- **SBOM 生成**：`syft` / `trivy` 生成物料清单，对接 SCA
- **镜像签名**：Cosign + Sigstore + Rekor 透明日志、Notary v2
- **准入控制**：K8s Admission Controller 拦截未签名 / 含严重漏洞镜像
- **运行时检测**：Falco 实时检测异常 syscall、Tracee eBPF 检测
- **DevSecOps 流水线**：CI 中嵌入 Trivy / Grype / Snyk / Anchore
- 案例：SolarWinds 与 Log4Shell 后,业界对镜像供应链的整改
- 基准数据：完整签名 + SBOM 流水线对 CI 耗时的影响（+15%）

#### 25-工业实战 - 故障复盘集 ⏳

> 真实生产事故的根因分析、修复过程、防范措施,是面试中"踩过什么坑"的最佳素材。

| 案例 | 现象 | 根因 | 修复 | 防范 |
|------|------|------|------|------|
| 容器 OOM 导致节点雪崩 | 单 Pod 内存泄漏,触发 OOM Killer 连锁 | cgroup v1 memory.kmem 未限制 | 升级 cgroup v2 + memory limit | 压测 + OOM 告警 |
| 镜像层数过多导致拉取超时 | 50+ 层镜像拉取 > 5min | 历史 Dockerfile 累积 RUN | 多阶段构建 + squashing | Dockerfile lint |
| PID 1 不处理 SIGTERM | 滚动更新耗时 10min | Java JVM 未注册信号处理器 | tini / dumb-init 作 PID 1 | 容器优雅停止审计 |
| overlayfs 磁盘占满 | 节点磁盘 100%,Pod 无法调度 | 死容器与镜像未清理 | `docker system prune` 定时任务 | 存储监控 + GC |
| inotify 句柄耗尽 | 新 Pod 启动失败 | 单节点 500+ 容器文件监听 | 调高 `fs.inotify.max_user_instances` | 内核参数基线 |
| 跨主机网络不通 | overlay 网络 MTU 不匹配 | 物理网络 MTU 1400,overlay 默认 1450 | 自定义 MTU | 网络基线文档 |

#### 26-工业实战 - 成本与容量规划 ⏳

- **资源利用率**：从均值 15% → 40% 的混部实践（在线 + 离线）
- **FinOps**：镜像存储成本、构建机成本、带宽成本核算
- **弹性**：HPA / VPA / Cluster Autoscaler 与镜像预拉取的配合
- **预留与超卖**：CPU 超卖比 3:1、内存超卖比 1.5:1 的取舍
- **冷启动优化**：Serverless 容器（AWS Fargate / 阿里 ECI）的镜像优化
- 基准数据：千节点集群年度成本拆解（计算 / 存储 / 网络 / 镜像仓库）

---

## 四、知识地图

```
                ┌─────────────────────────────────────┐
                │            基础概念层               │
                │  容器 vs VM / 架构 / 术语 / 安装    │
                └─────────────────┬───────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        ▼                         ▼                         ▼
┌─────────────────┐     ┌───────────────────┐     ┌───────────────────┐
│  使用与编排     │     │   底层原理         │     │   安全与运维      │
│  镜像 / 容器    │     │   namespaces       │     │   隔离 / 签名     │
│  网络 / 存储    │     │   cgroups          │     │   监控 / 日志     │
│  Compose        │     │   UnionFS / OCI    │     │   仓库 / 分发     │
└────────┬────────┘     └─────────┬─────────┘     └────────┬──────────┘
         │                        │                        │
         └────────────────────────┼────────────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │           生产与生态                │
                │  Dockerfile 模板 / CI-CD / 最佳实践 │
                │  Swarm / Podman / containerd / K8s  │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │           工业实战                  │
                │  大厂流水线 / 集群密度 / 分发 /     │
                │  供应链安全 / 故障复盘 / 成本规划   │
                └─────────────────────────────────────┘
```

---

## 五、推荐学习路径

- **应用开发者**：01 → 02 → 03 → 04 → 07 → 15 → 16
- **后端 / 运维工程师**：01 → 03 → 05 → 06 → 07 → 14 → 17 → 13 → 22 → 25
- **底层 / 平台工程师**：01 → 04 → 08 → 09 → 10 → 11 → 19 → 22 → 23
- **安全工程师**：01 → 04 → 08 → 12 → 13 → 17 → 24 → 25
- **SRE / 稳定性工程师**：01 → 04 → 14 → 17 → 22 → 25 → 26
- **面试准备**：01 → 03 → 04 → 05 → 08 → 09 → 10 → 11 → 17 → 19 → 21 → 25

---

## 六、写作约定

- **操作系统**：以 **Linux（Ubuntu / Debian）** 为主，macOS / Windows 差异单独标注
- **版本基线**：Docker Engine 24.x + BuildKit + containerd 1.7
- **命令示例**：可复制运行，标注输出与副作用
- **图示**：优先 ASCII 图说明架构；复杂图示标注来源
- **原理剖析**：关键命令对应到内核特性（namespace / cgroup / overlayfs），不省略中间步骤
- **代码**：Dockerfile / compose.yaml 给出生产级模板与注释
- **跨文件链接**：相关概念使用相对路径链接，便于跳转
- **优先级**：正确性 > 完整性 > 速度

---

## 七、参考资源

### 官方文档

- Docker Docs — https://docs.docker.com/
- OCI Specifications — https://opencontainers.org/
- containerd — https://containerd.io/
- runc — https://github.com/opencontainers/runc

### 内核与底层

- Linux man-pages：`namespaces(7)` / `cgroups(7)` / `overlayfs(8)`
- Kernel docs — https://www.kernel.org/doc/html/latest/
- LWN.net 容器相关文章

### 书籍

- 《Docker Deep Dive》—— Nigel Poulton
- 《Container Security》—— Liz Rice
- 《Cloud Native Patterns》—— Cornelia Davis
- 《Linux 内核设计艺术》—— 内核视角理解 namespace / cgroup
- 《SRE：Google 运维解密》—— 容器化在生产环境中的稳定性实践
- 《Cloud Native DevOps with Kubernetes》—— 工业级流水线实战

### 论文与演讲

- *Borg, Omega, and Kubernetes* — Acar et al., 2016（容器编排演进）
- *Improve Container Security with gVisor* — Google KCCM
- *Large-scale cluster management at Google with Borg* — Verma et al., 2015
- *Aliyun Dragonfly: P2P Image Distribution* — Alibaba Tech Blog
- *Kraken: Uber’s Open Source Peer-to-Peer Docker Registry* — Uber Engineering
- *Stargz / eStargz: Lazy Pulling Container Images* — Nydus Surge Talk
- Liz Rice — *Building a container from scratch in Go*（GOTO 会议）

### 大厂工程博客

- Alibaba Cloud Native — https://www.alibabacloud.com/zh/blog
- ByteDance Tech Blog — 字节跳动技术团队
- Netflix TechBlog — Titus 容器平台
- Uber Engineering — Kraken / Marmaray
- Google Cloud Blog — Borg / GKE / gVisor
- AWS Open Source — Firecracker / Bottlerocket

### 相关模块

- [分布式系统](../分布式系统/) — 容器在分布式系统中的部署、调度
- [infra开发](../infra开发/) — 容器与网关、服务网格、可观测性的关系
- [并行计算](../并行计算/) — GPU 容器、CUDA in Docker
- [云计算安全](../云计算安全/) — 容器安全、镜像供应链

---

## 八、TODO / 路线图

- [x] 目录占位、README 落地
- [ ] 01-基础与核心概念
- [ ] 02-安装与 CLI 基础
- [ ] 03-镜像原理与 Dockerfile
- [ ] 04-容器运行与生命周期
- [ ] 05-容器网络
- [ ] 06-数据存储与卷
- [ ] 07-Docker Compose
- [ ] 08-namespaces 底层原理
- [ ] 09-cgroups 底层原理
- [ ] 10-UnionFS 底层原理
- [ ] 11-OCI 规范与运行时
- [ ] 12-安全与隔离
- [ ] 13-镜像仓库与分发
- [ ] 14-监控与日志
- [ ] 15-Dockerfile 实战模板
- [ ] 16-Docker 与 CI/CD
- [ ] 17-生产最佳实践
- [ ] 18-Docker Swarm 入门
- [ ] 19-容器生态对比
- [ ] 20-常见问题与陷阱
- [ ] 21-工业实战-大厂镜像构建流水线
- [ ] 22-工业实战-大规模集群密度优化
- [ ] 23-工业实战-镜像分发与CDN
- [ ] 24-工业实战-供应链安全
- [ ] 25-工业实战-故障复盘集
- [ ] 26-工业实战-成本与容量规划
- [ ] 跨模块知识链接（K8s / 服务网格 / 可观测性）

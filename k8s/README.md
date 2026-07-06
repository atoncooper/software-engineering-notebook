# Kubernetes 学习笔记

> 容器编排的事实标准,从 Google Borg 十年沉淀到云原生时代的基础设施。
>
> 目标:从 **使用** → **架构** → **底层原理** → **生产运维** → **工业实战** 五层递进,构建可追溯、可复用的 K8s 知识体系。
>
> 工业视角贯穿全程:大厂生产案例、性能基准数据、故障复盘、容量与成本规划、多集群治理、Serverless 与服务网格实践。

---

## 一、模块定位

本模块不是「命令速查表」,而是一套面向 **工程实践 + 底层原理 + 面试纵深 + 生产运维 + 工业实战** 五位一体的 Kubernetes 学习笔记。

每篇笔记遵循统一结构(13 节):

1. **问题定义与边界**:解决什么、不解决什么
2. **直觉解释**:先讲直觉,再上 YAML
3. **核心概念与架构**:Control Plane / Node / Pod / Service 全景
4. **操作流程与命令**:kubectl 用法 + 参数详解
5. **底层原理**:API Server / etcd / kubelet / kube-proxy / CRI / CNI / CSI 源码级剖析
6. **代码与配置示例**:YAML 清单、Helm Chart、Operator CRD
7. **常见陷阱与调优**:资源限制、调度、网络、存储陷阱
8. **工业案例与基准数据**:大厂生产实践、单集群规模、调度延迟、密度上限
9. **与其他方案的关系**:vs Docker Swarm / Nomad / Mesos / Borg
10. **面试速答**:高频问题的一句话答案
11. **综合面试题**:由浅入深,含答题要点
12. **故障复盘**:真实生产事故案例、根因、修复、防范
13. **参考与延伸**:官方文档、源码、跨文件链接

---

## 二、目录结构规划

```
k8s/
├── README.md                         # 本文件
│
├── 01-基础与核心概念.md               # K8s 是什么、Borg 血统、声明式 API、控制循环
├── 02-架构与组件.md                   # Control Plane / Node / etcd / apiserver / kubelet / kube-proxy
├── 03-安装与部署.md                   # kubeadm / kind / k3s / 云厂商托管 / 二进制
├── 04-Pod与工作负载.md                # Pod / Deployment / ReplicaSet / StatefulSet / DaemonSet / Job
│
├── 05-Service与网络.md                # Service / Endpoint / Ingress / DNS / kube-proxy
├── 06-存储.md                         # PV / PVC / StorageClass / CSI / Volume 类型
├── 07-ConfigMap与Secret.md            # 配置注入、加密、热更新、外部密钥
│
├── 08-调度器.md                       # kube-scheduler / 过滤打分 / 亲和性 / 污点容忍 / 优先级
├── 09-控制器模式.md                   # Reconcile Loop / Informer / List-Watch / Finalizer
├── 10-HPA-VPA-CA.md                   # 水平/垂直/集群自动伸缩、资源管理
├── 11-滚动更新与发布策略.md            # RollingUpdate / BlueGreen / Canary / Argo Rollouts
│
├── 12-APIServer与etcd.md              # REST / Watch / RBAC / admission / etcd 一致性
├── 13-kubelet与Pod生命周期.md          # CRI / syncLoop / PLEG / 探针 / 优雅终止
├── 14-kube-proxy与服务转发.md          # iptables / IPVS / eBPF / Conntrack
├── 15-CNI与网络模型.md                 # Bridge / Calico / Cilium / Flannel / 跨节点通信
├── 16-CSI与存储编排.md                 # Attach / Mount / Provision / Snapshot
│
├── 17-RBAC与认证授权.md                # ServiceAccount / ClusterRole / Token / OIDC
├── 18-NetworkPolicy与流量管控.md       # Namespace 隔离 / Pod 隔离 / 默认策略
├── 19-Pod安全.md                       # PSA / seccomp / AppArmor / gVisor / Kata
├── 20-策略与治理.md                    # OPA Gatekeeper / Kyverno / 准入控制 / 合规审计
│
├── 21-监控与指标.md                    # Prometheus / metrics-server / Grafana / 黄金信号
├── 22-日志与追踪.md                    # Loki / ELK / Fluent Bit / Jaeger / OpenTelemetry
├── 23-故障排查与诊断.md                # kubectl debug / crictl / 常见故障树
├── 24-集群运维.md                      # 升级 / 备份 / 灾难恢复 / 多集群管理
│
├── 25-大厂K8s平台.md                   # 阿里 ACK / 字节 TCE / 腾讯 TKE / GKE / EKS
├── 26-大规模集群优化.md                # 单集群 1万节点 / etcd 调优 / 调度器扩展
├── 27-CRD与Operator生态.md             # Operator 模式 / Helm / Kustomize / Argo CD
├── 28-服务网格与Serverless.md          # Istio / Linkerd / Knative / OpenFaaS
├── 29-故障复盘集.md                    # 生产事故根因、修复、防范
└── 30-成本与容量规划.md                # FinOps / 资源利用率 / 混部 / 弹性
```

### 状态图例

- ✅ 已建:笔记已完成
- ⏳ 待建:目录已占位,内容规划中

---

## 三、章节索引

### 第一部分:基础与使用

#### 01-基础与核心概念 ⏳

K8s 是什么、从 Google Borg 演进、声明式 vs 命令式 API、Reconcile 控制循环、云原生基金会 CNCF 生态。

#### 02-架构与组件 ⏳

Control Plane (apiserver / scheduler / controller-manager / etcd) + Worker Node (kubelet / kube-proxy / container runtime) 双层架构,组件间通信路径。

#### 03-安装与部署 ⏳

五种部署形态:本地 (kind / minikube / k3d)、自建 (kubeadm / 二进制)、托管 (EKS / GKE / AKS / ACK)、边缘 (k3s / MicroK8s)、Serverless (Fargate / Cloud Run)。

#### 04-Pod 与工作负载 ⏳

Pod 是最小调度单元、5 种工作负载 (Deployment / StatefulSet / DaemonSet / Job / CronJob) 的取舍、Init Container、Sidecar 模式。

---

### 第二部分:网络、存储、配置

#### 05-Service 与网络 ⏳

ClusterIP / NodePort / LoadBalancer / ExternalName、Endpoint / EndpointSlice、Ingress、CoreDNS、Service Mesh 入口。

#### 06-存储 ⏳

PV / PVC / StorageClass 三层抽象、Volume 类型 (emptyDir / hostPath / configMap / secret / persistentVolumeClaim)、动态供给、StatefulSet 与存储。

#### 07-ConfigMap 与 Secret ⏳

配置注入 (env / volume / subPath)、Secret 类型 (Opaque / dockerconfigjson / tls / service-account-token)、外部密钥 (External Secrets / Vault / CSI Secret Store)、热更新机制。

---

### 第三部分:调度与编排

#### 08-调度器 ⏳

kube-scheduler 调度流水线 (Filter → Score → Bind)、节点亲和性 / Pod 亲和性 / 污点容忍、优先级与抢占、调度器扩展 (Scheduler Framework)。

#### 09-控制器模式 ⏳

Reconcile Loop 是 K8s 的灵魂、Informer / List-Watch 机制、Finalizer 优雅删除、Owner Reference 与级联垃圾回收、自定义控制器开发。

#### 10-HPA-VPA-CA ⏳

水平 Pod 自动伸缩 (HPA)、垂直 Pod 自动伸缩 (VPA)、集群自动伸缩 (Cluster Autoscaler)、KEDA 事件驱动伸缩、伸缩策略与冷却。

#### 11-滚动更新与发布策略 ⏳

RollingUpdate / Recreate、蓝绿部署、金丝雀发布、A/B 测试、Argo Rollouts / Flagger 高级发布、流量切分。

---

### 第四部分:底层原理

#### 12-APIServer 与 etcd ⏳

REST API 设计、Watch 机制 (etcd watch → apiserver watch)、Admission Controller 链、etcd 一致性 (Raft)、性能调优。

#### 13-kubelet 与 Pod 生命周期 ⏳

CRI 接口、syncLoop 主循环、PLEG (Pod Lifecycle Event Generator)、探针 (liveness / readiness / startup)、优雅终止、Pod 退出码。

#### 14-kube-proxy 与服务转发 ⏳

iptables 模式 (KUBE-SVC / KUBE-SEP 链)、IPVS 模式 (负载均衡算法)、eBPF 模式 (Cilium)、Conntrack 连接跟踪、会话保持。

#### 15-CNI 与网络模型 ⏳

CNI 插件接口、Bridge / Calico / Cilium / Flannel / Weave Net、网络模型 (Overlay / BGP / eBPF)、跨节点通信、NetworkPolicy 实现。

#### 16-CSI 与存储编排 ⏳

CSI 三阶段 (Provision / Attach / Mount)、Snapshot、Volume Expansion、Inline Volume、主流 CSI (EBS / EFS / CSI-HostPath / Longhorn / Rook-Ceph)。

---

### 第五部分:安全与治理

#### 17-RBAC 与认证授权 ⏳

认证 (X.509 / Token / OIDC / Webhook)、授权 (RBAC / ABAC / Node)、准入控制 (Mutating / Validating)、ServiceAccount、IRSA / WI。

#### 18-NetworkPolicy 与流量管控 ⏳

Namespace 默认策略、Pod 入站出站隔离、CNI 实现差异、零信任网络、Service Mesh 流量策略。

#### 19-Pod 安全 ⏳

Pod Security Admission (PSA) 三级 (privileged / baseline / restricted)、PodSecurityPolicy 弃用迁移、seccomp / AppArmor / SELinux、gVisor / Kata Containers / Firecracker 沙箱运行时。

#### 20-策略与治理 ⏳

OPA Gatekeeper / Kyverno 策略引擎、准入 Webhook、合规审计 (CIS Benchmark)、多租户隔离 (Namespace / vCluster / Karmada)、镜像供应链 (Cosign / SBOM)。

---

### 第六部分:运维与可观测性

#### 21-监控与指标 ⏳

Prometheus + Grafana 监控栈、metrics-server、kube-state-metrics、cAdvisor、黄金信号 (延迟 / 流量 / 错误 / 饱和度)、SLI / SLO。

#### 22-日志与追踪 ⏳

Loki / ELK / Fluent Bit 日志聚合、Jaeger / OpenTelemetry 分布式追踪、结构化日志、日志保留与采样、成本控制。

#### 23-故障排查与诊断 ⏳

kubectl debug / ephemeral container、crictl / ctr、节点诊断 (SOS)、常见故障树 (Pod Pending / CrashLoopBackOff / ImagePullBackOff)、tcpdump / strace / bcc 工具。

#### 24-集群运维 ⏳

版本升级 (kubeadm / 托管 / 蓝绿)、etcd 备份恢复 (velero)、灾难恢复、多集群管理 (Cluster API / Karmada / KubeFed)、配置管理。

---

### 第七部分:工业实战

#### 25-大厂 K8s 平台 ⏳

- **阿里 ACK / ASK**:万节点集群、混部、Serverless 容器
- **字节 TCE / ByteKube**:内部平台、火山引擎容器服务
- **腾讯 TKE / EKS**:超大规模、原生节点与超级节点
- **Google GKE**:Borg 嫡传、Autopilot、GKE On-Prem
- **AWS EKS / Fargate**:托管控制面、IAM 集成、Spot 实例
- **Azure AKS**:Windows 节点、Azure AD 集成

#### 26-大规模集群优化 ⏳

- 单集群从 1000 → 5000 → 10000 节点的演进
- etcd 调优 (defrag / compaction / quota / mlock)
- API Server 限流 (APF API Priority and Fairness)、Watch 缓存
- 调度器扩展 (Scheduler Framework、Volcano 批调度)
- Kubelet 优化 (PLEG、CPU Manager、Topology Manager)
- 基准数据:5K 节点的 Pod 创建延迟、Watch 延迟

#### 27-CRD 与 Operator 生态 ⏳

- Operator 模式 (custom controller + CRD)
- Operator SDK / Kubebuilder 开发框架
- Helm 包管理、Kustomize 覆盖
- GitOps (Argo CD / Flux CD)
- 主流 Operator (Prometheus Operator / Strimzi Kafka / Cert-Manager / Argo Workflows)

#### 28-服务网格与 Serverless ⏳

- **Istio**:数据面 (Envoy) + 控制面 (Istiod)、流量管理、安全、可观测性
- **Linkerd**:轻量级、Rust 数据面
- **Knative**:Serverless 框架、自动缩容到 0
- **OpenFaaS / OpenWhisk**:函数计算
- **Argo Workflows / Tekton**:CI/CD 流水线

#### 29-故障复盘集 ⏳

> 真实生产事故的根因分析、修复过程、防范措施。

| 案例 | 现象 | 根因 | 修复 | 防范 |
|------|------|------|------|------|
| etcd 雪崩导致全集群不可用 | apiserver 超时、所有写操作失败 | etcd 磁盘满、quorum 丢失 | 紧急扩盘 + 备份恢复 | 磁盘水位告警 + 定期 velero 备份 |
| CoreDNS 解析延迟飙升 | 服务间歇性 5xx | Pod 数 5K+ 后 DNS QPS 过载 | CoreDNS 横向扩容 + NodeLocalDNS | DNS 监控 + 缓存层 |
| 调度器卡住、Pod 持续 Pending | 新 Pod 无法调度 | scheduler cache 失效、扩展器超时 | 重启调度器 + 升级 | 调度器 HA + 健康检查 |
| 大规模滚动更新雪崩 | 滚动更新导致服务不可用 | 就绪探针配置过严 + 优雅停止未处理 | 调整探针 + preStop hook | 金丝雀发布 + 流量观测 |
| PLEG 卡死、节点 NotReady | 节点 Pod 全部 Evict | 容器运行时卡死、PLEG 超时 | 重启 kubelet + containerd | PLEG 监控 + 节点自愈 |
| NetworkPolicy 误配全集群断网 | Pod 间无法通信 | 默认 deny 策略覆盖过广 | 紧急回滚 + 分批应用 | 策略 staging + 干运行 |

#### 30-成本与容量规划 ⏳

- **资源利用率**:从均值 20% → 60% 的混部路径 (在线 + 离线)
- **FinOps**:节点成本、存储成本、网络成本、License 成本核算
- **弹性**:HPA + Cluster Autoscaler + Spot 实例的组合策略
- **预留与超卖**:CPU 超卖比 3:1、内存超卖比 1.5:1 的取舍
- **Serverless 容器**:按秒计费 vs 按节点计费的临界点
- **多集群成本分摊**:租户级成本报表、Showback / Chargeback

---

## 四、知识地图

```
                ┌─────────────────────────────────────┐
                │            基础概念层               │
                │  K8s 是什么 / 架构 / 术语 / 安装    │
                └─────────────────┬───────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        ▼                         ▼                         ▼
┌─────────────────┐     ┌───────────────────┐     ┌───────────────────┐
│  工作负载       │     │   底层原理         │     │   网络与存储      │
│  Pod / Deploy   │     │   APIServer/etcd   │     │   Service / CNI   │
│  Service / 卷   │     │   kubelet / proxy  │     │   CSI / ConfigMap │
│  ConfigMap      │     │   CRI / 调度器     │     │   Ingress / DNS   │
└────────┬────────┘     └─────────┬─────────┘     └────────┬──────────┘
         │                        │                        │
         └────────────────────────┼────────────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │         调度与编排                  │
                │  调度器 / 控制器模式 / HPA / 发布   │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │         安全与治理                  │
                │  RBAC / NetworkPolicy / PSA / OPA   │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │         运维与可观测性              │
                │  监控 / 日志 / 追踪 / 排障 / 升级   │
                └─────────────────┬───────────────────┘
                                  ▼
                ┌─────────────────────────────────────┐
                │           工业实战                  │
                │  大厂平台 / 大规模优化 / Operator / │
                │  服务网格 / 故障复盘 / 成本规划     │
                └─────────────────────────────────────┘
```

---

## 五、推荐学习路径

- **应用开发者**:01 → 02 → 04 → 05 → 06 → 07 → 11 → 27
- **后端 / SRE 工程师**:01 → 02 → 04 → 05 → 08 → 10 → 21 → 22 → 23 → 29
- **平台 / 底层工程师**:01 → 02 → 12 → 13 → 14 → 15 → 16 → 26
- **安全工程师**:01 → 02 → 17 → 18 → 19 → 20 → 29
- **架构师 / 技术负责人**:01 → 02 → 04 → 08 → 09 → 12 → 25 → 26 → 28 → 30
- **面试准备**:01 → 02 → 04 → 05 → 06 → 08 → 09 → 12 → 13 → 14 → 17 → 19 → 21 → 25 → 29

---

## 六、写作约定

- **操作系统**:以 **Linux(Ubuntu / Debian)** 为主,macOS / Windows 差异单独标注
- **版本基线**:Kubernetes 1.30 (2024 LTS) + containerd 1.7 + CoreDNS 1.11 + Cilium 1.15
- **命令示例**:可复制运行,标注输出与副作用
- **图示**:优先 ASCII 图说明架构;复杂图示标注来源
- **原理剖析**:关键操作对应到组件源码(apiserver / kubelet / kube-proxy),不省略中间步骤
- **代码**:YAML 清单 / Helm / Operator Go 代码给生产级模板与注释
- **跨文件链接**:相关概念使用相对路径链接,便于跳转
- **优先级**:正确性 > 完整性 > 速度

---

## 七、参考资源

### 官方文档

- Kubernetes Docs — https://kubernetes.io/docs/
- Kubernetes API Reference — https://kubernetes.io/docs/reference/generated/kubernetes-api/
- CNCF Landscape — https://landscape.cncf.io/
- Kubernetes SIGs — https://github.com/kubernetes/community

### 内核与底层

- etcd Docs — https://etcd.io/docs/
- CRI Spec — https://github.com/kubernetes/cri-api
- CNI Spec — https://github.com/containernetworking/cni/blob/master/SPEC.md
- CSI Spec — https://github.com/container-storage-interface/spec
- OCI Spec — https://opencontainers.org/

### 书籍

- 《Kubernetes Up & Running》—— Brendan Burns 等
- 《Kubernetes in Action》—— Marko Lukša
- 《Programming Kubernetes》—— Michael Hausenblas / Stefan Schimanski
- 《Kubernetes Patterns》—— Bilgin Ibryam / Roland Huss
- 《Cloud Native DevOps with Kubernetes》—— John Arundel
- 《SRE:Google 运维解密》—— 容器编排在生产环境中的稳定性实践
- 《Production Kubernetes》—— Josh Rosso 等

### 论文与演讲

- *Borg, Omega, and Kubernetes* — Acar et al., 2016(容器编排演进)
- *Large-scale cluster management at Google with Borg* — Verma et al., 2015
- *Kubernetes at Scale: Lessons Learned* — KubeCon EU 2018
- *The State of Kubernetes at Twitter* — KubeCon NA 2019
- *Scaling Kubernetes to 7,500 Nodes at Alibaba* — KubeCon China 2019
- *Tuning Kubernetes for Performance and Scale* — KubeCon EU 2020
- *The Kubernetes Scheduler* — SIG-Scheduling deep dive

### 大厂工程博客

- Alibaba Cloud Native — https://www.alibabacloud.com/zh/blog
- ByteDance Tech Blog — 字节跳动技术团队
- Tencent Cloud TKE — 腾讯云容器服务
- Google Cloud Blog — Borg / GKE / Autopilot
- AWS Containers Blog — EKS / Fargate / Bottlerocket
- Microsoft Azure Blog — AKS / Arc
- Netflix TechBlog — Titus 容器平台
- Uber Engineering — 大规模 Pod 调度

### 相关模块

- [Docker](../Docker/) — 容器基础,K8s 的运行时载体
- [分布式系统](../分布式系统/) — 一致性、共识、调度理论
- [云计算安全](../云计算安全/) — 容器安全、镜像供应链、合规
- [infra开发](../infra开发/) — 网关、服务网格、可观测性
- [并行计算](../并行计算/) — GPU 调度、Volcano 批调度

---

## 八、TODO / 路线图

- [x] 目录占位、README 落地
- [ ] 01-基础与核心概念
- [ ] 02-架构与组件
- [ ] 03-安装与部署
- [ ] 04-Pod 与工作负载
- [ ] 05-Service 与网络
- [ ] 06-存储
- [ ] 07-ConfigMap 与 Secret
- [ ] 08-调度器
- [ ] 09-控制器模式
- [ ] 10-HPA-VPA-CA
- [ ] 11-滚动更新与发布策略
- [ ] 12-APIServer 与 etcd
- [ ] 13-kubelet 与 Pod 生命周期
- [ ] 14-kube-proxy 与服务转发
- [ ] 15-CNI 与网络模型
- [ ] 16-CSI 与存储编排
- [ ] 17-RBAC 与认证授权
- [ ] 18-NetworkPolicy 与流量管控
- [ ] 19-Pod 安全
- [ ] 20-策略与治理
- [ ] 21-监控与指标
- [ ] 22-日志与追踪
- [ ] 23-故障排查与诊断
- [ ] 24-集群运维
- [ ] 25-大厂 K8s 平台
- [ ] 26-大规模集群优化
- [ ] 27-CRD 与 Operator 生态
- [ ] 28-服务网格与 Serverless
- [ ] 29-故障复盘集
- [ ] 30-成本与容量规划
- [ ] 跨模块知识链接(服务网格 / 可观测性 / 多集群)

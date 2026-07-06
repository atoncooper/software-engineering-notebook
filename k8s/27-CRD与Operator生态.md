# 27. CRD 与 Operator 生态

> 关键词:CRD、Custom Controller、Operator Pattern、Operator SDK、Kubebuilder、Controller Runtime、Helm、Kustomize、Argo CD、Flux CD、GitOps、Prometheus Operator、Cert-Manager、Strimzi、Argo Workflows、Crossplane、AWS ACK

> 版本基线:Kubernetes 1.30 + Operator SDK 1.34 + Kubebuilder 4.0 + Controller Runtime 0.18 + Helm 3.14 + Kustomize 5.4 + Argo CD 2.11 + Flux 2.2

------

## 27.1 问题定义与边界

### 27.1.1 本章解决什么

K8s 内置资源(Pod/Deployment/Service/Ingress/Job/CronJob)覆盖了 **无状态应用** 与 **通用工作负载**,但企业落地时还有大量 **领域专用工作负载** 无法用原生资源表达:

- 一个 Redis Cluster 需要:6 节点拓扑、主从复制、槽位分配、故障转移、备份策略、监控接入 —— Deployment + StatefulSet 表达不了。
- 一个 Kafka 集群需要:Broker 编号、ZK/KRaft 共识、Topic 创建、Partition Rebalance、ISR 管理 —— 远超 K8s 原生能力。
- 一张 TLS 证书需要:CSR 生成、ACME 协议、Let's Encrypt 交互、证书轮换、Secret 注入 —— K8s 没有 Certificate 资源。
- 一套 Prometheus 监控需要:配置文件生成、Rule 管理、Thanos Sidecar 注入、多副本一致性 —— ConfigMap 装不下。

如果全靠人工 kubectl apply + runbook,运维成本随服务数量线性增长。**核心问题**:

> 如何把 **领域运维知识(Domain Knowledge)** 编码成 K8s 原生资源,让 K8s 控制面自动驱动应用生命周期,而不是让人去驱动?

同时衍生问题:

- 如何 **打包分发** 一套多资源 K8s 应用(Chart/Overlay)?
- 如何 **持续同步** Git 仓库到集群(GitOps)?
- 如何 **复用** 已有的 Operator 生态而不是重复造轮子?

### 27.1.2 不解决什么

- 不讲 K8s 控制器底层 List-Watch / Informer / WorkQueue 细节(见 [09-控制器模式](./09-控制器模式.md))
- 不讲准入 Webhook 实现细节(见 [20-策略与治理](./20-策略与治理.md))
- 不讲 Service Mesh(见 [28-服务网格与Serverless](./28-服务网格与Serverless.md))
- 不讲多集群联邦治理(见 [24-集群运维](./24-集群运维.md))
- 不讲大规模集群下 Operator 的性能调优(见 [26-大规模集群优化](./26-大规模集群优化.md))

### 27.1.3 问题边界

```
+----------------------------------------------------------+
|  用户视角:我只想声明 "给我一个 3 副本 Redis Cluster"      |
+----------------------------------------------------------+
                          ↓
+----------------------------------------------------------+
|  K8s 视角:没有 RedisCluster 这个资源类型                  |
|  内置资源只能描述容器编排,不能描述应用领域语义            |
+----------------------------------------------------------+
                          ↓
+----------------------------------------------------------+
|  解决:CRD 定义新资源类型 + Operator 实现控制逻辑          |
|  RedisCluster CR → Operator 看到后 → 创建 StatefulSet/    |
|  ConfigMap/Service/PDB → 持续 Reconcile 到期望状态        |
+----------------------------------------------------------+
```

------

## 27.2 直觉解释

### 27.2.1 Operator = 人类运维专家的代码化

把 Operator 想象成 **一个 7x24 小时不下班的 SRE**:

| 人类 SRE | Operator |
|----------|----------|
| 看 dashboard | Watch CR 状态 |
| 发现故障 | Reconcile 触发 |
| 翻 runbook | 内置领域知识 |
| 执行命令 | 调 K8s API |
| 升级版本 | 滚动更新 Pod |
| 备份恢复 | 定期 snapshot |
| 扩容缩容 | 修改 replicas |
| 半夜被叫醒 | 永远在线 |

人类 SRE 的价值不在敲命令,而在 **知道什么时候敲什么命令**。Operator 把"知道"这部分固化成代码。

### 27.2.2 CRD = 自定义资源类型

```
K8s 内置资源:
  Pod / Deployment / Service / Ingress / ConfigMap / Secret
  StatefulSet / DaemonSet / Job / CronJob
  PV / PVC / StorageClass
  ...共约 50+ 种

CRD(CustomResourceDefinition):
  用户自定义的新资源类型
  和 Pod/Deployment 一样有 spec/status
  一样能 kubectl get / describe / apply
  一样有 validation / default / webhook
  
示例:
  apiVersion: redis.operator.io/v1
  kind: RedisCluster
  metadata:
    name: my-cache
  spec:
    replicas: 6
    image: redis:7.2
    persistence: enabled
```

### 27.2.3 Helm = K8s 包管理器

类比 Linux 的 apt/yum:

| apt | Helm |
|-----|------|
| .deb 包 | Chart(tgz) |
| apt install | helm install |
| apt repo | helm repo add |
| /var/lib/dpkg | Release(秘密存储在集群) |
| apt upgrade | helm upgrade |
| apt remove | helm uninstall |
| dpkg -l | helm list |

Helm 解决"一次写好,多次部署"问题:模板化 YAML + values.yaml 覆盖 + 仓库分发。

### 27.2.4 Kustomize = YAML 覆盖工具

```
Helm: 模板 + 变量 → 渲染
Kustomize: 已有 YAML + 补丁 → 合并

优势:
  - 不破坏原 YAML(无模板语法)
  - 多环境 overlay 叠加
  - 原生 kubectl apply -k 支持
  - 无需 Helm 运行时
```

### 27.2.5 GitOps = Git as Source of Truth

```
传统 CI/CD:
  CI pipeline → kubectl apply(推送模型)
  问题:谁在什么时间改了什么?集群实际状态是什么?

GitOps:
  Git 仓库 → 期望状态
  集群 → 实际状态
  Operator(Argo CD/Flux) → 持续 Reconcile(拉取模型)
  
  1. 开发改 Git
  2. Argo CD 检测到 diff
  3. 自动 sync(或人工审批)
  4. 集群状态收敛到 Git
  
核心:Git commit = 集群期望状态的唯一真相来源
```

### 27.2.6 整体生态地图

```
+-------------------+   +-------------------+   +-------------------+
|    应用打包层     |   |    应用部署层     |   |    应用治理层     |
|                   |   |                   |   |                   |
|  Helm Chart       |   |  Argo CD          |   |  Prometheus Op    |
|  Kustomize        |→  |  Flux CD          |←  |  Cert-Manager     |
|  Carvel ytt       |   |  Jenkins X        |   |  Strimzi Kafka    |
|  OCI Artifact     |   |  Argo Rollouts    |   |  Crossplane       |
+-------------------+   +-------------------+   +-------------------+
         ↓                       ↑                       ↓
+----------------------------------------------------------+
|              K8s API Server(声明式 API)                 |
+----------------------------------------------------------+
         ↓
+----------------------------------------------------------+
|  CRD + Operator(领域控制器,扩展 K8s 能力)              |
|  - 自定义资源类型(CRD)                                  |
|  - 自定义控制器(Operator)                               |
|  - Webhook(校验/修改)                                  |
+----------------------------------------------------------+
```

------

## 27.3 核心概念与架构

### 27.3.1 CRD 全景

```
CRD(CustomResourceDefinition):
  - 声明新资源类型的 schema
  - 注册到 apiextension-apiserver
  - 自动生成 RESTful API: /apis/<group>/<version>/namespaces/<ns>/<resource>

CR(Custom Resource):
  - CRD 的实例
  - 和 Pod/Deployment 一样存在 etcd
  - kubectl get <resource> 直接查看

Controller:
  - 持续 Watch CR 变化
  - 执行 Reconcile 逻辑
  - 把 status 写回 CR

Webhook:
  - MutatingWebhook:修改 CR(默认值、注入字段)
  - ValidatingWebhook:校验 CR(防止非法配置)
  - ConversionWebhook:CRD 多版本转换(v1alpha1 ↔ v1beta1 ↔ v1)
```

CRD YAML 关键字段:

```yaml
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: redisclusters.redis.operator.io
spec:
  group: redis.operator.io               # API Group
  names:
    kind: RedisCluster                    # PascalCase
    listKind: RedisClusterList
    singular: rediscluster
    plural: redisclusters
    shortNames: [rc, redis]               # kubectl get rc
    categories: [database, cache]         # kubectl get database
  scope: Namespaced                       # Namespaced | Cluster
  versions:
  - name: v1
    served: true                          # 是否提供 API
    storage: true                         # 是否存到 etcd(只能一个)
    subresources:
      status: {}                          # /status 子资源
      scale:                              # /scale 子资源
        specReplicasPath: .spec.replicas
        statusReplicasPath: .status.readyReplicas
    additionalPrinterColumns:             # kubectl get 显示列
    - name: Replicas
      type: integer
      jsonPath: .spec.replicas
    - name: Age
      type: date
      jsonPath: .metadata.creationTimestamp
    - name: Phase
      type: string
      jsonPath: .status.phase
    schema:
      openAPIV3Schema:
        type: object
        properties:
          spec:
            type: object
            required: [replicas, image]
            properties:
              replicas:
                type: integer
                minimum: 3
                maximum: 100
              image:
                type: string
                pattern: '^[a-z0-9./:-]+$'
              persistence:
                type: object
                properties:
                  enabled:
                    type: boolean
                  size:
                    type: string
          status:
            type: object
            properties:
              phase:
                type: string
                enum: [Pending, Running, Failed]
              readyReplicas:
                type: integer
              nodes:
                type: array
                items:
                  type: object
                  properties:
                    name: {type: string}
                    role: {type: string}
                    ip: {type: string}
```

### 27.3.2 Operator 架构

```
+----------------------------------------------------------+
|                     Operator Pod                          |
|                                                          |
|  +-------------------+   +-------------------+            |
|  |   Manager         |   |   Webhook Server  |            |
|  |   (controller-    |   |   (:9443)         |            |
|  |    runtime)       |   |                   |            |
|  |                   |   |   /mutate         |            |
|  |  +-------------+  |   |   /validate       |            |
|  |  |  Cache      |  |   |   /convert        |            |
|  |  | (Informer)  |  |   +-------------------+            |
|  |  +-------------+  |                                   |
|  |         |         |                                   |
|  |  +-------------+  |                                   |
|  |  |  Client     |  |                                   |
|  |  | (k8s API)   |  |                                   |
|  |  +-------------+  |                                   |
|  |         |         |                                   |
|  |  +-------------+  |                                   |
|  |  |  Reconciler |  |                                   |
|  |  +-------------+  |                                   |
|  +-------------------+                                   |
+----------------------------------------------------------+
         |                          |
         ↓                          ↓
+-----------------+        +-----------------+
|   K8s API       |        |  CR + 子资源    |
|   Server        |        |  (StatefulSet,  |
|                 |        |   Service,      |
+-----------------+        |   ConfigMap)    |
                           +-----------------+
```

### 27.3.3 Controller Runtime 组件

```
Manager:
  - 启动并管理 Controller、Webhook
  - 共享 Cache、Client、Scheme
  - 提供 Metrics/Healthz/Readyz 端点

Cache:
  - 内部维护 Informer
  - List-Watch 资源到本地内存
  - 减少 API Server 压力
  - 标签选择器过滤

Client:
  - 读:走 Cache(默认)
  - 写:走 API Server
  - 提供 Get/List/Create/Update/Patch/Delete

Reconcile:
  - 输入:Request{NamespacedName}
  - 输出:Result{RequeueAfter}, error
  - 幂等:同一输入应产生同一结果
  - 不要依赖事件顺序

Watch:
  - 监听资源变化
  - Owner-Based:子资源变化触发父资源 Reconcile
  - Field-Based:字段变化触发

EventFilter:
  - 过滤事件,减少 Reconcile 次数
  - 例如:只关心 spec 变化,忽略 status

Finalizer:
  - 资源删除前钩子
  - 防止"孤儿资源"(子资源没删干净)
  - 必须幂等
```

### 27.3.4 Helm 核心概念

```
Chart:
  - 一个 K8s 应用包
  - 目录结构:
    mychart/
    ├── Chart.yaml          # 元信息
    ├── values.yaml         # 默认值
    ├── values.schema.json  # 值校验 schema
    ├── charts/             # 依赖 chart
    ├── templates/          # YAML 模板
    │   ├── deployment.yaml
    │   ├── service.yaml
    │   ├── _helpers.tpl
    │   └── NOTES.txt
    ├── crds/               # CRD 定义
    ├── templates/
    └── README.md

Release:
  - Chart 的一次安装实例
  - 同一 chart 可多次安装(release name 区分)
  - Release 状态存储在 Secret(k8s 1.30 默认)

Repository:
  - Chart 仓库(HTTP 服务器 + index.yaml)
  - 或 OCI Registry(helm push/pull)

Library Chart:
  - 共享模板(不产生 release)
  - 被 chart 依赖复用
```

### 27.3.5 Kustomize 核心概念

```
Base:
  - 共享 YAML 资源
  - kustomization.yaml 引用

Overlay:
  - 环境定制(dev/staging/prod)
  - 继承 base + 应用 patch

Patch 策略:
  - Strategic Merge(默认):K8s 原生合并
  - JSON Patch(RFC 6902):精准路径修改
  - Strategic Merge with directives:replace/delete/add

Components:
  - 可选功能模块(比 overlay 更轻)
  - 多个 component 组合

关键指令:
  - resources: 引用文件
  - patches: 应用补丁
  - namePrefix/nameSuffix: 改名
  - namespace: 修改命名空间
  - commonLabels/commonAnnotations: 注入标签
  - images: 替换镜像
  - replicas: 修改副本数
  - configMapGenerator/secretGenerator: 生成配置
```

### 27.3.6 Argo CD 架构

```
+----------------------------------------------------------+
|                    Argo CD 架构                           |
|                                                          |
|  +-------------------+   +-------------------+            |
|  |  API Server       |   |  Application      |            |
|  |  (:443)           |   |  Controller       |            |
|  |                   |   |                   |            |
|  |  - gRPC/REST      |   |  - Watch App      |            |
|  |  - Auth/RBAC      |   |  - Diff Git↔Cluster|           |
|  |  - Webhook        |   |  - Sync           |            |
|  +-------------------+   +-------------------+            |
|                                                          |
|  +-------------------+   +-------------------+            |
|  |  Repo Server      |   |  Redis            |            |
|  |                   |   |                   |            |
|  |  - git clone      |   |  - 缓存           |            |
|  |  - helm template  |   |  - session        |            |
|  |  - kustomize build|   |                   |            |
|  +-------------------+   +-------------------+            |
|                                                          |
|  +-------------------+                                   |
|  |  ApplicationSet   |                                   |
|  |  Controller       |                                   |
|  |                   |                                   |
|  |  - 多集群自动生成 |                                   |
|  |  - Git Generator  |                                   |
|  |  - Cluster Gen    |                                   |
|  +-------------------+                                   |
+----------------------------------------------------------+
```

核心 CRD:

- **Application**:单个部署单元,Git 仓库 + 目标集群 + 同步策略
- **AppProject**:命名空间隔离、RBAC、仓库白名单
- **ApplicationSet**:多集群/多应用自动生成

### 27.3.7 Flux CD 架构

```
+----------------------------------------------------------+
|                     Flux CD 架构                          |
|                                                          |
|  +-------------------+   +-------------------+            |
|  |  Source Controller|   |  Kustomize        |            |
|  |                   |   |  Controller       |            |
|  |  - GitRepository  |   |                   |            |
|  |  - OCIRepository  |   |  - Kustomization  |            |
|  |  - HelmRepository |   |    (apply -k)     |            |
|  |  - Bucket         |   |                   |            |
|  +-------------------+   +-------------------+            |
|                                                          |
|  +-------------------+   +-------------------+            |
|  |  Helm Controller  |   |  Notification     |            |
|  |                   |   |  Controller       |            |
|  |  - HelmRelease    |   |                   |            |
|  |    (helm install) |   |  - Alert          |            |
|  |                   |   |  - Provider       |            |
|  |                   |   |  - Receiver       |            |
|  +-------------------+   +-------------------+            |
|                                                          |
|  +-------------------+   +-------------------+            |
|  |  Image Reflector  |   |  Image Automation |            |
|  |  Controller       |   |  Controller       |            |
|  |                   |   |                   |            |
|  |  - ImageRepository|   |  - ImagePolicy    |            |
|  |  - ImagePolicy    |   |  - ImageUpdate    |            |
|  |                   |   |    Automation     |            |
|  +-------------------+   +-------------------+            |
+----------------------------------------------------------+
```

Flux 模块化设计:每个 Controller 独立部署,职责单一。

### 27.3.8 Operator 成熟度等级

Operator Capability Levels(由 Operator Hub 定义):

```
Level 1: Basic Install
  - 创建资源
  - 配置应用
  - 等价于 Helm install

Level 2: Seamless Upgrades
  - 滚动升级
  - 版本回滚
  - 数据库 schema migration

Level 3: Full Lifecycle
  - 备份恢复
  - 故障转移
  - 扩容缩容
  - TLS 证书管理

Level 4: Deep Insights
  - 应用级指标
  - 告警规则
  - 日志集成
  - 分布式追踪

Level 5: Auto Pilot
  - 自动调优
  - 自愈(无人工介入)
  - 弹性伸缩
  - 异常检测
```

主流 Operator 成熟度:

| Operator | Level | 说明 |
|----------|-------|------|
| Prometheus Operator | 5 | 自动调优、自愈 |
| Cert-Manager | 4 | 全自动证书生命周期 |
| Strimzi Kafka | 5 | 自动 Rebalance、自愈 |
| Argo CD Operator | 3 | 全生命周期 |
| PostgreSQL Operator(CrunchyData) | 5 | 自动备份、Point-in-Time 恢复 |

------

## 27.4 操作流程与命令

### 27.4.1 CRD 操作流程

```bash
# 1. 创建 CRD
kubectl apply -f rediscluster-crd.yaml

# 2. 查看 CRD
kubectl get crd
kubectl get crd redisclusters.redis.operator.io
kubectl describe crd redisclusters.redis.operator.io

# 3. 创建 CR 实例
kubectl apply -f my-redis.yaml
# apiVersion: redis.operator.io/v1
# kind: RedisCluster
# metadata:
#   name: my-cache
# spec:
#   replicas: 6
#   image: redis:7.2

# 4. 查看 CR
kubectl get redisclusters
kubectl get rc my-cache -o yaml
kubectl get rc -o custom-columns=NAME:.metadata.name,REPLICAS:.spec.replicas,PHASE:.status.phase

# 5. 更新 CR
kubectl patch rediscluster my-cache --type=merge -p '{"spec":{"replicas":8}}'
kubectl scale rediscluster my-cache --replicas=8

# 6. 删除 CR
kubectl delete rediscluster my-cache

# 7. 删除 CRD(会删所有 CR)
kubectl delete crd redisclusters.redis.operator.io
```

### 27.4.2 Kubebuilder 开发流程

```bash
# 1. 初始化项目
kubebuilder init \
  --domain operator.io \
  --repo github.com/example/redis-operator \
  --license apache2

# 2. 创建 API(CRD + Controller)
kubebuilder create api \
  --group redis \
  --version v1 \
  --kind RedisCluster \
  --resource --controller

# 3. 创建 Webhook(可选)
kubebuilder create webhook \
  --group redis \
  --version v1 \
  --kind RedisCluster \
  --defaulting \
  --validation \
  --conversion

# 4. 编辑 API 定义
# api/v1/rediscluster_types.go
# - 定义 Spec 结构体 + 字段
# - 定义 Status 结构体 + 字段
# - 加 kubebuilder 标注

# 5. 编辑 Controller
# internal/controller/rediscluster_controller.go
# - 实现 Reconcile 方法
# - 加 Watch(子资源)
# - 加 Finalizer

# 6. 生成 CRD / RBAC / Deepcopy
make manifests
make generate

# 7. 本地测试
make install              # 安装 CRD 到集群
make run                  # 本地运行 controller

# 8. 集成测试(envtest)
make test

# 9. 构建镜像
make docker-build IMG=redis-operator:v0.1.0

# 10. 推送镜像
make docker-push IMG=redis-operator:v0.1.0

# 11. 部署到集群
make deploy IMG=redis-operator:v0.1.0

# 12. 发布 bundle(可选)
make bundle IMG=redis-operator:v0.1.0
make catalog-build
```

### 27.4.3 Operator SDK 开发流程

```bash
# Operator SDK 支持 3 种类型:
# - Go(原生 K8s,等同 Kubebuilder)
# - Helm(模板化 Operator)
# - Ansible(Playbook Operator)

# 1. Go 类型(基本等同 Kubebuilder)
operator-sdk init --domain=operator.io --repo=github.com/example/redis-operator
operator-sdk create api --group=redis --version=v1 --kind=RedisCluster --resource --controller

# 2. Helm 类型(把 Helm Chart 包成 Operator)
operator-sdk init --plugins=helm --domain=operator.io --group=redis --version=v1 --kind=RedisCluster
operator-sdk create api --plugins=helm --group=redis --version=v1 --kind=RedisCluster --helm-chart=redis

# 3. Ansible 类型
operator-sdk init --plugins=ansible --domain=operator.io
operator-sdk create api --group=redis --version=v1 --kind=RedisCluster --plugins=ansible --generate-role

# 4. 打包 OLM(Operator Lifecycle Manager)
operator-sdk bundle validate ./bundle
operator-sdk catalog validate ./catalog
```

### 27.4.4 Helm 完整生命周期

```bash
# 1. 添加 chart 仓库
helm repo add bitnami https://charts.bitnami.com/bitnami
helm repo update
helm search repo redis
helm search hub redis       # 在 Artifact Hub 搜索

# 2. 拉取 chart
helm pull bitnami/redis --untar
helm pull bitnami/redis --version 18.0.0

# 3. 创建自定义 chart
helm create mychart
# 生成:
# mychart/
# ├── Chart.yaml
# ├── values.yaml
# ├── charts/
# ├── templates/
# │   ├── deployment.yaml
# │   ├── service.yaml
# │   ├── ingress.yaml
# │   ├── hpa.yaml
# │   ├── serviceaccount.yaml
# │   ├── _helpers.tpl
# │   ├── NOTES.txt
# │   └── tests/
# └── .helmignore

# 4. 模板渲染(本地预览)
helm template my-release bitnami/redis -f values.yaml
helm lint mychart                              # 检查语法
helm install my-release bitnami/redis --dry-run --debug   # 模拟安装

# 5. 安装 release
helm install my-release bitnami/redis \
  --namespace cache \
  --create-namespace \
  --set replica.count=3 \
  --set persistence.enabled=true \
  --set persistence.size=10Gi \
  --version 18.0.0 \
  --wait --timeout 5m \
  --atomic                   # 失败自动回滚

# 6. 查看 release
helm list -A
helm list -n cache
helm status my-release -n cache
helm get all my-release -n cache        # 所有信息
helm get values my-release -n cache     # 当前值
helm get manifest my-release -n cache   # 渲染后的 YAML
helm get notes my-release -n cache      # NOTES.txt
helm history my-release -n cache        # 历史版本

# 7. 升级 release
helm upgrade my-release bitnami/redis \
  --namespace cache \
  --set replica.count=5 \
  --reuse-values                # 保留之前未指定的值

# 8. 回滚 release
helm rollback my-release 1 -n cache    # 回滚到 revision 1
helm history my-release -n cache

# 9. 卸载 release
helm uninstall my-release -n cache
helm uninstall my-release -n cache --keep-history   # 保留历史

# 10. OCI Registry(新方式)
helm registry login registry.example.com
helm push mychart-0.1.0.tgz oci://registry.example.com/charts
helm pull oci://registry.example.com/charts/mychart --version 0.1.0
helm install my-release oci://registry.example.com/charts/mychart --version 0.1.0
```

### 27.4.5 Kustomize 操作流程

```bash
# 1. 构建(渲染)
kustomize build overlays/prod
kustomize build overlays/prod | kubectl apply -f -

# 2. 原生 kubectl
kubectl apply -k overlays/prod
kubectl diff -k overlays/prod          # diff
kubectl delete -k overlays/prod

# 3. 查看渲染结果
kustomize cfg cat overlays/prod
kustomize cfg tree overlays/prod

# 4. 验证
kustomize build overlays/prod | kubectl apply --dry-run=client -f -

# 5. 编辑 kustomization.yaml
# overlays/prod/kustomization.yaml
```

### 27.4.6 Argo CD 操作流程

```bash
# 1. 安装
kubectl create namespace argocd
kubectl apply -n argocd -f https://raw.githubusercontent.com/argoproj/argo-cd/stable/manifests/install.yaml

# 2. 获取密码
kubectl -n argocd get secret argocd-initial-admin-secret \
  -o jsonpath="{.data.password}" | base64 -d

# 3. CLI
argocd login argocd.example.com --username admin --password <password>
argocd account update-password

# 4. 创建 application
argocd app create my-app \
  --repo https://github.com/example/myapp.git \
  --path manifests \
  --dest-server https://kubernetes.default.svc \
  --dest-namespace production

# 5. 同步
argocd app sync my-app
argocd app sync my-app --prune          # 删除多余资源
argocd app sync my-app --dry-run        # 预览
argocd app diff my-app                  # diff Git vs Cluster

# 6. 历史
argocd app history my-app
argocd app rollback my-app <revision>

# 7. 管理
argocd app list
argocd app get my-app
argocd app delete my-app

# 8. 多集群
argocd cluster add prod-context
argocd cluster list

# 9. 项目
argocd proj create my-project
argocd proj add-source my-project https://github.com/example/myapp.git
argocd proj allow-cluster-resource my-project apps Deployment

# 10. ApplicationSet(多集群自动生成)
argocd appset create appset.yaml
```

### 27.4.7 Flux CD 操作流程

```bash
# 1. 安装
flux install --version=v2.2.0

# 2. 接入 Git
flux bootstrap github \
  --owner=my-org \
  --repository=my-fleet \
  --branch=main \
  --path=clusters/prod

# 3. 创建 Source
flux create source git my-app \
  --url=https://github.com/example/myapp \
  --branch=main \
  --interval=1m

# 4. 创建 Kustomization
flux create kustomization my-app \
  --source=my-app \
  --path="./manifests" \
  --prune=true \
  --interval=5m \
  --target-namespace=production

# 5. 创建 HelmRelease
flux create source helm bitnami \
  --url=https://charts.bitnami.com/bitnami

flux create helmrelease redis \
  --source=HelmRepository/bitnami \
  --chart=redis \
  --chart-version=18.0.0 \
  --target-namespace=cache

# 6. 查看
flux get sources all
flux get kustomizations
flux get helmreleases
flux logs --all-namespaces

# 7. 手动 reconcile
flux reconcile source git my-app
flux reconcile kustomization my-app --with-source
flux reconcile helmrelease redis

# 8. 暂停/恢复
flux suspend kustomization my-app
flux resume kustomization my-app

# 9. 镜像自动更新
flux create image repository my-app \
  --image=my-registry/my-app \
  --interval=1m

flux create image policy my-app \
  --image-repository=my-app \
  --select-numeric=asc \
  --filter-regex='^v[0-9]+\.[0-9]+\.[0-9]+$'

flux create image update my-app \
  --git-repo-ref=my-app \
  --git-repo-path="./manifests" \
  --regulatory=true
```

------

## 27.5 底层原理

### 27.5.1 CRD 与 API 路径

```
CRD 注册流程:
  1. kubectl apply -f crd.yaml
  2. apiextension-apiserver 收到 CRD 创建请求
  3. 校验 OpenAPI v3 schema
  4. 写入 etcd
  5. CRD Controller 检测到新 CRD
  6. 在 API Server 注册新 RESTful 路径:
     /apis/<group>/<version>/namespaces/<ns>/<resource>
  7. etcd 存储后端自动创建(每个 CRD 独立 prefix)

访问路径示例:
  /apis/redis.operator.io/v1/namespaces/default/redisclusters
  /apis/redis.operator.io/v1/namespaces/default/redisclusters/my-cache
  /apis/redis.operator.io/v1/namespaces/default/redisclusters/my-cache/status
  /apis/redis.operator.io/v1/namespaces/default/redisclusters/my-cache/scale

CRD Conversion Webhook:
  - 多版本共存(v1alpha1, v1beta1, v1)
  - 不同版本共享存储(只读 storage: true 的版本)
  - 读写时自动转换
  - 转换逻辑在 webhook 中实现
```

### 27.5.2 OpenAPI v3 Schema 细节

```yaml
schema:
  openAPIV3Schema:
    type: object
    required: [spec]
    properties:
      spec:
        type: object
        required: [replicas, image]
        properties:
          replicas:
            type: integer
            minimum: 3
            maximum: 100
            default: 6                # 默认值(kubectl apply 缺省时填)
          image:
            type: string
            # x-kubernetes-* 注解扩展
            x-kubernetes-preserve-unknown-fields: false   # 是否允许未知字段
          resources:
            type: object
            x-kubernetes-preserve-unknown-fields: true    # 透传 ResourceRequirements
          config:
            type: object
            x-kubernetes-embedded-resource: true          # 嵌套资源(有 apiVersion/kind)
            x-kubernetes-preserve-unknown-fields: true
          affinity:
            type: object
            x-kubernetes-list-type: map                   # list 类型(set/map/atomic)
            x-kubernetes-list-map-keys: [topologyKey]
          ports:
            type: array
            items:
              type: object
              required: [name, port]
              properties:
                name: {type: string}
                port: {type: integer}
                protocol:
                  type: string
                  enum: [TCP, UDP, SCTP]
            x-kubernetes-list-type: map
            x-kubernetes-list-map-keys: [name]
          nodeSelector:
            type: object
            additionalProperties:
              type: string
      status:
        type: object
        properties:
          phase:
            type: string
            enum: [Pending, Running, Failed]
          conditions:
            type: array
            items:
              type: object
              required: [type, status]
              properties:
                type: {type: string}
                status:
                  type: string
                  enum: ["True", "False", Unknown]
                reason: {type: string}
                message: {type: string}
                lastTransitionTime:
                  type: string
                  format: date-time
            x-kubernetes-list-type: map
            x-kubernetes-list-map-keys: [type]
```

关键 x-kubernetes-* 注解:

- `x-kubernetes-preserve-unknown-fields: true` - 保留未知字段(默认 false 严格校验)
- `x-kubernetes-embedded-resource: true` - 嵌套完整资源(自动校验 apiVersion/kind)
- `x-kubernetes-list-type: set/map/atomic` - list 合并策略
- `x-kubernetes-list-map-keys: [key]` - map 类型 list 的键
- `x-kubernetes-map-type: atomic/granular` - map 合并策略
- `x-kubernetes-validations: [...]` - CEL 表达式校验(K8s 1.25+)

### 27.5.3 Controller Runtime 内部

```
Manager 初始化流程:
  1. ctrl.NewManager(config, ctrl.Options{...})
     - 创建 Scheme(注册所有 GVK)
     - 创建 Cache(InformerFactory)
     - 创建 Client(读写分离)
     - 创建 Webhook Server

  2. ctrl.NewControllerManagedBy(mgr)
     .For(&RedisCluster{})
     .Owns(&appsv1.StatefulSet{})
     .WithEventFilter(predicate...)
     .Complete(r)

  3. mgr.Start(ctx)
     - 启动 Cache(List-Watch)
     - 启动所有 Controller
     - 启动 Webhook Server

Reconcile 调用链:
  API Server → Watch Event → Informer → EventHandler → WorkQueue → Reconcile

EventHandler:
  - OnAdd:    入队 {namespace, name}
  - OnUpdate: 入队 {namespace, name}
  - OnDelete: 入队 {namespace, name}(触发 Finalizer 检查)

WorkQueue:
  - 限速队列(默认 10 qps,指数退避)
  - 去重(同一 key 多次入队只处理一次)
  - 延迟入队(RequeueAfter)

Reconcile 实现要点:
  func (r *RedisClusterReconciler) Reconcile(ctx, req) (ctrl.Result, error) {
      // 1. 获取 CR
      redis, err := r.Get(ctx, req.NamespacedName)
      if IsNotFound(err) { return OK }  // 已删除,忽略
      
      // 2. 检查 Finalizer
      if !Contains(redis.Finalizers, "redis.operator.io/finalizer") {
          redis.Finalizers = Append(redis.Finalizers, "...")
          r.Update(ctx, redis)
          return Requeue
      }
      
      // 3. 检查 DeletionTimestamp
      if !redis.DeletionTimestamp.IsZero() {
          // 执行清理(删外部资源、备份等)
          r.cleanup(ctx, redis)
          // 移除 Finalizer
          redis.Finalizers = Remove(redis.Finalizers, "...")
          r.Update(ctx, redis)
          return OK
      }
      
      // 4. 主逻辑
      r.reconcileStatefulSet(ctx, redis)
      r.reconcileService(ctx, redis)
      r.reconcileConfigMap(ctx, redis)
      
      // 5. 更新 status
      redis.Status.Phase = "Running"
      redis.Status.ReadyReplicas = ...
      r.Status().Update(ctx, redis)
      
      // 6. 重新入队(可选,周期性 Reconcile)
      return RequeueAfter(30s)
  }

Cache 与 Client 区别:
  - Cache:本地内存,只读,来自 Informer
  - Client:写走 API Server,读走 Cache(默认)
  - Client 写后 Cache 可能不一致(短暂延迟)
  - Status().Update() 走 API Server

OwnerReferences 与 Owns():
  - 子资源(StatefulSet)设 OwnerReferences 指向 CR(RedisCluster)
  - CR 删除时,子资源被 Garbage Collector 自动清理
  - Owns() 让 Controller Watch 子资源,子资源变化触发 CR Reconcile
```

### 27.5.4 Helm Template Engine

```
Helm 用 Go template + Sprig 函数库:

{{- /* 注释 */ -}}
{{ include "redis.fullname" . }}          # 引用 helper
{{ .Values.replica.count }}                # 取值
{{ .Release.Name }}                        # release 名
{{ .Release.Namespace }}                   # namespace
{{ .Chart.Version }}                       # chart 版本
{{ .Files.Get "config.txt" }}              # 文件内容
{{ .Capabilities.KubeVersion.Version }}    # K8s 版本

控制结构:
{{- if .Values.persistence.enabled }}
  persistence:
    enabled: true
{{- else }}
  persistence:
    enabled: false
{{- end }}

{{- range .Values.replica.ports }}
- name: {{ .name }}
  port: {{ .port }}
{{- end }}

函数:
{{ .Values.image | default "redis:7.0" }}  # 默认值
{{ .Values.password | b64enc }}            # base64 编码
{{ include "redis.labels" . | nindent 4 }} # 缩进
{{ now | date "2006-01-02" }}              # 当前日期
{{ randAlphaNum 16 }}                       # 随机字符串

Pipeline:
{{ .Values.port | toString | quote }}
等价于: quote (toString .Values.port)

Release 状态存储(K8s 1.30):
  - Secret(默认,加密):
    secretName: sh.helm.release.v1.my-release.v1
    type: helm.sh/release.v2
    data:
      release: <base64 压缩后的 release 数据>
  - ConfigMap(老版本):
    type: helm.sh/release.v1

3-way merge patch:
  - helm upgrade 时,合并三方:
    1. 旧 chart 渲染结果(从 release 历史读)
    2. 新 chart 渲染结果
    3. 集群当前实际状态
  - 比传统 kubectl apply 更智能:
    apply 只看 2 和 3,upgrade 看 1, 2, 3
  - 例:用户手动 kubectl scale deployment=5
    helm upgrade 时,如果新 values 改了 replicas=3,
    3-way merge 会更新到 3(用户手动改被覆盖)
    但如果 values 没改 replicas,helm 会保留用户的 5(因为 1 和 2 一致)
```

### 27.5.5 Kustomize Patch 策略

```
1. Strategic Merge(默认):
   patches:
   - path: patch.yaml
   
   patch.yaml:
   apiVersion: apps/v1
   kind: Deployment
   metadata:
     name: my-app
   spec:
     replicas: 5
     template:
       spec:
         containers:
         - name: app
           image: my-app:v2    # 替换
         # 其他容器保留

   特点:
   - 列表合并按 name 字段匹配(对 K8s 原生资源)
   - 不能匹配的列表整体替换
   - $patch: replace / $patch: delete 指令

2. JSON Patch(RFC 6902):
   patches:
   - path: patch.json
     target:
       kind: Deployment
       name: my-app
   
   patch.json:
   [
     {"op": "replace", "path": "/spec/replicas", "value": 5},
     {"op": "add", "path": "/spec/template/spec/containers/0/env/-",
      "value": {"name": "NEW_ENV", "value": "true"}},
     {"op": "remove", "path": "/spec/template/spec/containers/1"},
     {"op": "move", "from": "/spec/x", "path": "/spec/y"},
     {"op": "copy", "from": "/spec/a", "path": "/spec/b"},
     {"op": "test", "path": "/spec/replicas", "value": 3}
   ]

3. Strategic Merge with directives:
   patches:
   - path: patch.yaml
   
   patch.yaml:
   apiVersion: apps/v1
   kind: Deployment
   metadata:
     name: my-app
   spec:
     template:
       spec:
         containers:
         - name: app
           $patch: delete       # 删除该容器
         - name: sidecar
           $patch: replace      # 替换整个容器
           image: sidecar:v2

4. Inline Patch:
   patches:
   - target:
       kind: Deployment
       name: my-app
     patch: |-
       - op: replace
         path: /spec/replicas
         value: 5
```

### 27.5.6 Argo CD Sync 引擎

```
Sync 流程:
  1. Git Repository 变化(或手动 sync)
  2. Application Controller 检测到 diff
  3. Repo Server 渲染清单:
     - git clone
     - 检测是 Helm / Kustomize / Plain YAML
     - helm template / kustomize build / 直接读
     - 返回渲染后的 YAML 列表
  4. Application Controller 三方 diff:
     - Git 渲染结果(Desired)
     - 集群 Live 状态(Live)
     - 上次同步状态(Synced)
  5. 计算需要 apply / prune / skip 的资源
  6. 按 Sync Wave 排序
  7. Apply 到目标集群
  8. 等待 Health Check 通过
  9. 更新 Application status

Sync Wave:
  - 资源按 wave 排序应用
  - wave 越小越早执行(可为负)
  - 同 wave 内按 kind 排序(Namespace → CRD → ClusterRole → ...)
  - 注解:
    argocd.argoproj.io/sync-wave: "-5"

Sync Hook:
  - PreSync: 同步前执行(如 db migration)
  - Sync: 同步主体
  - PostSync: 同步后执行(如通知)
  - SyncFail: 同步失败时执行
  - 注解:
    argocd.argoproj.io/hook: PreSync
    argocd.argoproj.io/hook-delete-policy: HookSucceeded

Resource Hook:
  - Skip: 不应用该资源
  - Prune: 删除该资源
  - 注解:
    argocd.argoproj.io/sync-options: SkipDryRunOnMissingResource=true

Health Check:
  - 内置:Deployment / StatefulSet / DaemonSet / Job / Ingress 等
  - 自定义(lua 脚本):
    argocd.argoproj.io/health.lua: |
      hs = {}
      if obj.status ~= nil then
        if obj.status.phase == "Running" then
          hs.status = "Healthy"
        else
          hs.status = "Progressing"
        end
      end
      return hs

Diffing:
  - 三方 diff:Desired vs Live vs Last Synced
  - ignoreDifferences:忽略指定字段
  - ignoreAggregatedRoles:忽略 ClusterRole 聚合差异
  - resource.customizations.ignoreDifferences: CRD 自定义忽略
```

### 27.5.7 Flux CD 依赖编排

```
Flux Kustomization 依赖:
  apiVersion: kustomize.toolkit.fluxcd.io/v1
  kind: Kustomization
  metadata:
    name: my-app
  spec:
    dependsOn:
    - name: infra-redis        # 必须先 Ready
    - name: infra-cert-manager
    sourceRef:
      kind: GitRepository
      name: my-app
    path: ./manifests
    prune: true
    wait: true                 # 等待健康检查
    healthChecks:
    - apiVersion: apps/v1
      kind: Deployment
      name: my-app
      namespace: production
    
依赖图:
  infra-cert-manager → my-app
  infra-redis → my-app
  
Flux 控制器会按拓扑顺序 Reconcile,前一个 Ready 才执行下一个。

如果循环依赖,Flux 拒绝应用并报错。
```

------

## 27.6 代码与配置示例

### 27.6.1 完整 Kubebuilder Operator 项目结构

```
redis-operator/
├── PROJECT                              # Kubebuilder 项目元信息
├── Makefile                             # 构建/测试/部署命令
├── go.mod
├── go.sum
├── Dockerfile
├── README.md
├── LICENSE
├── .golangci.yml
│
├── api/
│   └── v1/
│       ├── rediscluster_types.go        # CRD 字段定义
│       ├── rediscluster_webhook.go      # Webhook 实现
│       ├── groupversion_info.go         # GVK 元信息
│       ├── zz_generated.deepcopy.go     # 自动生成
│       └── zz_generated.defaults.go     # 自动生成
│
├── internal/
│   ├── controller/
│   │   ├── rediscluster_controller.go   # 主控制器
│   │   ├── statefulset_controller.go    # 子资源 Reconcile
│   │   ├── service_controller.go
│   │   └── suite_test.go                # envtest 集成测试
│   └── webhook/
│       └── v1/
│           └── rediscluster_webhook.go
│
├── config/
│   ├── default/                         # 默认配置(Kustomize base)
│   ├── manager/                         # Operator Deployment
│   ├── manifests/                       # CRD / RBAC
│   ├── prometheus/                      # ServiceMonitor
│   ├── rbac/                            # ClusterRole
│   ├── scorecard/                       # Operator SDK scorecard
│   └── webhook/                         # Webhook Service/Config
│
├── test/
│   ├── e2e/                             # E2E 测试
│   └── fuzz/                            # Fuzz 测试
│
└── bundle/                              # OLM bundle(发布用)
    ├── manifests/
    ├── metadata/
    └── tests/
```

### 27.6.2 CRD 类型定义(Go)

`api/v1/rediscluster_types.go`:

```go
package v1

import (
    appsv1 "k8s.io/api/apps/v1"
    corev1 "k8s.io/api/core/v1"
    metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// +kubebuilder:object:root=true
// +kubebuilder:subresource:status
// +kubebuilder:subresource:scale:specpath=.spec.replicas,statuspath=.status.readyReplicas
// +kubebuilder:resource:shortName=rc;redis,categories=database;cache
// +kubebuilder:printcolumn:name="Replicas",type=integer,JSONPath=`.spec.replicas`
// +kubebuilder:printcolumn:name="Ready",type=integer,JSONPath=`.status.readyReplicas`
// +kubebuilder:printcolumn:name="Phase",type=string,JSONPath=`.status.phase`
// +kubebuilder:printcolumn:name="Age",type=date,JSONPath=`.metadata.creationTimestamp`
// +kubebuilder:storageversion
// +kubebuilder:validation:Optional
type RedisCluster struct {
    metav1.TypeMeta   `json:",inline"`
    metav1.ObjectMeta `json:"metadata,omitempty"`

    Spec   RedisClusterSpec   `json:"spec,omitempty"`
    Status RedisClusterStatus `json:"status,omitempty"`
}

type RedisClusterSpec struct {
    // +kubebuilder:validation:Minimum=3
    // +kubebuilder:validation:Maximum=100
    // +kubebuilder:default=6
    Replicas int32 `json:"replicas"`

    // +kubebuilder:validation:Pattern=`^[a-z0-9./:-]+$`
    Image string `json:"image"`

    // +kubebuilder:optional
    // +kubebuilder:default="6379"
    Port int32 `json:"port,omitempty"`

    // +kubebuilder:optional
    Persistence *PersistenceSpec `json:"persistence,omitempty"`

    // +kubebuilder:optional
    Resources *corev1.ResourceRequirements `json:"resources,omitempty"`

    // +kubebuilder:optional
    // +kubebuilder:default="requirepass $(REDIS_PASSWORD)"
    Command []string `json:"command,omitempty"`

    // +kubebuilder:optional
    Affinity *corev1.Affinity `json:"affinity,omitempty"`

    // +kubebuilder:optional
    NodeSelector map[string]string `json:"nodeSelector,omitempty"`

    // +kubebuilder:optional
    Tolerations []corev1.Toleration `json:"tolerations,omitempty"`

    // +kubebuilder:optional
    // +kubebuilder:default=IfNotPresent
    ImagePullPolicy corev1.PullPolicy `json:"imagePullPolicy,omitempty"`

    // +kubebuilder:optional
    Backup *BackupSpec `json:"backup,omitempty"`

    // +kubebuilder:optional
    Monitoring *MonitoringSpec `json:"monitoring,omitempty"`

    // +kubebuilder:optional
    // +kubebuilder:default={enabled: true}
    Auth *AuthSpec `json:"auth,omitempty"`
}

type PersistenceSpec struct {
    // +kubebuilder:default=true
    Enabled bool `json:"enabled"`

    // +kubebuilder:default="10Gi"
    Size string `json:"size"`

    // +kubebuilder:default="standard"
    StorageClass string `json:"storageClass,omitempty"`

    // +kubebuilder:default="fast-rbd"
    AccessMode corev1.PersistentVolumeAccessMode `json:"accessMode,omitempty"`
}

type BackupSpec struct {
    Enabled bool `json:"enabled"`

    // +kubebuilder:validation:Enum=hourly;daily;weekly
    Schedule string `json:"schedule"`

    // +kubebuilder:default=7
    RetentionDays int `json:"retentionDays,omitempty"`

    Destination string `json:"destination"`  // s3://bucket/path
}

type MonitoringSpec struct {
    Enabled bool `json:"enabled"`

    // +kubebuilder:default=true
    Prometheus bool `json:"prometheus,omitempty"`
}

type AuthSpec struct {
    Enabled bool `json:"enabled"`

    // SecretKeySelector for password
    PasswordSecretRef *corev1.SecretKeySelector `json:"passwordSecretRef,omitempty"`
}

type RedisClusterStatus struct {
    // +kubebuilder:validation:Enum=Pending;Running;Failed;Scaling
    Phase string `json:"phase,omitempty"`

    ReadyReplicas int32 `json:"readyReplicas,omitempty"`

    Nodes []RedisNode `json:"nodes,omitempty"`

    Conditions []metav1.Condition `json:"conditions,omitempty"`

    // ObservedGeneration:Controller 处理到的 generation
    ObservedGeneration int64 `json:"observedGeneration,omitempty"`

    // LastBackupTime
    LastBackupTime *metav1.Time `json:"lastBackupTime,omitempty"`
}

type RedisNode struct {
    Name string `json:"name"`
    Role string `json:"role"`  // master / replica
    IP   string `json:"ip"`
    // +kubebuilder:optional
    Slot string `json:"slot,omitempty"`
}

// +kubebuilder:object:root=true
type RedisClusterList struct {
    metav1.TypeMeta `json:",inline"`
    metav1.ListMeta `json:"metadata,omitempty"`
    Items           []RedisCluster `json:"items"`
}

func init() {
    SchemeBuilder.Register(&RedisCluster{}, &RedisClusterList{})
}
```

### 27.6.3 Controller 实现(Go)

`internal/controller/rediscluster_controller.go`:

```go
package controller

import (
    "context"
    "fmt"
    "time"

    appsv1 "k8s.io/api/apps/v1"
    corev1 "k8s.io/api/core/v1"
    "k8s.io/apimachinery/pkg/api/equality"
    apierrors "k8s.io/apimachinery/pkg/api/errors"
    "k8s.io/apimachinery/pkg/api/meta"
    metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
    "k8s.io/apimachinery/pkg/runtime"
    "k8s.io/apimachinery/pkg/types"
    ctrl "sigs.k8s.io/controller-runtime"
    "sigs.k8s.io/controller-runtime/pkg/client"
    "sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
    "sigs.k8s.io/controller-runtime/pkg/log"

    redisv1 "github.com/example/redis-operator/api/v1"
)

const (
    finalizerName = "redis.operator.io/finalizer"

    ConditionReady          = "Ready"
    ConditionStatefulSetReady = "StatefulSetReady"
    ConditionServiceReady     = "ServiceReady"
)

type RedisClusterReconciler struct {
    client.Client
    Scheme *runtime.Scheme
}

// +kubebuilder:rbac:groups=redis.operator.io,resources=redisclusters,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=redis.operator.io,resources=redisclusters/status,verbs=get;update;patch
// +kubebuilder:rbac:groups=redis.operator.io,resources=redisclusters/finalizers,verbs=update
// +kubebuilder:rbac:groups=apps,resources=statefulsets,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups="",resources=services;configmaps;secrets,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups="",resources=pods;nodes,verbs=get;list;watch
func (r *RedisClusterReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
    logger := log.FromContext(ctx).WithValues("rediscluster", req.NamespacedName)

    // 1. 获取 CR
    redis := &redisv1.RedisCluster{}
    if err := r.Get(ctx, req.NamespacedName, redis); err != nil {
        if apierrors.IsNotFound(err) {
            return ctrl.Result{}, nil
        }
        return ctrl.Result{}, err
    }

    // 2. 处理删除(Finalizer)
    if !redis.DeletionTimestamp.IsZero() {
        return r.reconcileDelete(ctx, redis)
    }

    // 3. 确保 Finalizer 存在
    if !controllerutil.ContainsFinalizer(redis, finalizerName) {
        controllerutil.AddFinalizer(redis, finalizerName)
        if err := r.Update(ctx, redis); err != nil {
            return ctrl.Result{}, err
        }
        return ctrl.Result{Requeue: true}, nil
    }

    // 4. 主 Reconcile 逻辑
    if err := r.reconcileStatefulSet(ctx, redis); err != nil {
        r.setCondition(ctx, redis, ConditionStatefulSetReady, metav1.ConditionFalse, "ReconcileError", err.Error())
        return ctrl.Result{RequeueAfter: 30 * time.Second}, err
    }

    if err := r.reconcileService(ctx, redis); err != nil {
        r.setCondition(ctx, redis, ConditionServiceReady, metav1.ConditionFalse, "ReconcileError", err.Error())
        return ctrl.Result{RequeueAfter: 30 * time.Second}, err
    }

    if err := r.reconcileConfigMap(ctx, redis); err != nil {
        return ctrl.Result{RequeueAfter: 30 * time.Second}, err
    }

    // 5. 计算 status
    readyReplicas, err := r.getReadyReplicas(ctx, redis)
    if err != nil {
        return ctrl.Result{}, err
    }

    phase := redisv1.PhaseRunning
    if readyReplicas < redis.Spec.Replicas {
        phase = redisv1.PhaseScaling
    }

    newStatus := redisv1.RedisClusterStatus{
        Phase:             phase,
        ReadyReplicas:     readyReplicas,
        ObservedGeneration: redis.Generation,
        Conditions:        redis.Status.Conditions,
    }

    if !equality.Semantic.DeepEqual(redis.Status, newStatus) {
        redis.Status = newStatus
        if err := r.Status().Update(ctx, redis); err != nil {
            return ctrl.Result{}, err
        }
    }

    if readyReplicas == redis.Spec.Replicas {
        r.setCondition(ctx, redis, ConditionReady, metav1.ConditionTrue, "AllReplicasReady", "All replicas are ready")
    }

    logger.Info("Reconciled", "phase", phase, "ready", readyReplicas, "desired", redis.Spec.Replicas)

    // 6. 周期性 Reconcile(检查备份、健康)
    return ctrl.Result{RequeueAfter: 5 * time.Minute}, nil
}

func (r *RedisClusterReconciler) reconcileStatefulSet(ctx context.Context, redis *redisv1.RedisCluster) error {
    sts := &appsv1.StatefulSet{}
    err := r.Get(ctx, types.NamespacedName{Name: redis.Name, Namespace: redis.Namespace}, sts)
    if apierrors.IsNotFound(err) {
        sts = r.buildStatefulSet(redis)
        if err := controllerutil.SetControllerReference(redis, sts, r.Scheme); err != nil {
            return err
        }
        return r.Create(ctx, sts)
    }
    if err != nil {
        return err
    }

    // Diff 检查,只在 spec 变化时更新
    desired := r.buildStatefulSet(redis)
    if !equality.Semantic.DeepEqual(sts.Spec.Replicas, &redis.Spec.Replicas) ||
       !equality.Semantic.DeepEqual(sts.Spec.Template.Spec.Containers[0].Image, redis.Spec.Image) {
        sts.Spec.Replicas = &redis.Spec.Replicas
        sts.Spec.Template.Spec.Containers[0].Image = redis.Spec.Image
        return r.Update(ctx, sts)
    }

    return nil
}

func (r *RedisClusterReconciler) buildStatefulSet(redis *redisv1.RedisCluster) *appsv1.StatefulSet {
    sts := &appsv1.StatefulSet{
        ObjectMeta: metav1.ObjectMeta{
            Name:      redis.Name,
            Namespace: redis.Namespace,
            Labels:    r.labelsForRedis(redis),
        },
        Spec: appsv1.StatefulSetSpec{
            ServiceName: redis.Name + "-headless",
            Replicas:    &redis.Spec.Replicas,
            Selector: &metav1.LabelSelector{
                MatchLabels: r.labelsForRedis(redis),
            },
            Template: corev1.PodTemplateSpec{
                ObjectMeta: metav1.ObjectMeta{
                    Labels: r.labelsForRedis(redis),
                },
                Spec: corev1.PodSpec{
                    Containers: []corev1.Container{{
                        Name:            "redis",
                        Image:           redis.Spec.Image,
                        ImagePullPolicy: redis.Spec.ImagePullPolicy,
                        Ports: []corev1.ContainerPort{{
                            ContainerPort: redis.Spec.Port,
                            Name:          "redis",
                        }},
                        Command: redis.Spec.Command,
                        Env: []corev1.EnvVar{{
                            Name: "REDIS_PASSWORD",
                            ValueFrom: &corev1.EnvVarSource{
                                SecretKeyRef: redis.Spec.Auth.PasswordSecretRef,
                            },
                        }},
                        Resources: *redis.Spec.Resources,
                        LivenessProbe: &corev1.Probe{
                            ProbeHandler: corev1.ProbeHandler{
                                Exec: &corev1.ExecAction{
                                    Command: []string{"redis-cli", "ping"},
                                },
                            },
                            InitialDelaySeconds: 30,
                            PeriodSeconds:       10,
                        },
                        VolumeMounts: []corev1.VolumeMount{{
                            Name:      "data",
                            MountPath: "/data",
                        }},
                    }},
                    Affinity:    redis.Spec.Affinity,
                    NodeSelector: redis.Spec.NodeSelector,
                    Tolerations: redis.Spec.Tolerations,
                },
            },
        },
    }

    if redis.Spec.Persistence != nil && redis.Spec.Persistence.Enabled {
        sts.Spec.VolumeClaimTemplates = []corev1.PersistentVolumeClaim{{
            ObjectMeta: metav1.ObjectMeta{Name: "data"},
            Spec: corev1.PersistentVolumeClaimSpec{
                AccessModes: []corev1.PersistentVolumeAccessMode{redis.Spec.Persistence.AccessMode},
                Resources: corev1.VolumeResourceRequirements{
                    Requests: corev1.ResourceList{
                        corev1.ResourceStorage: corev1.MustParse(redis.Spec.Persistence.Size),
                    },
                },
                StorageClassName: &redis.Spec.Persistence.StorageClass,
            },
        }}
    }

    return sts
}

func (r *RedisClusterReconciler) reconcileDelete(ctx context.Context, redis *redisv1.RedisCluster) (ctrl.Result, error) {
    logger := log.FromContext(ctx)

    if controllerutil.ContainsFinalizer(redis, finalizerName) {
        // 执行清理:删除外部资源、备份等
        if err := r.cleanupExternalResources(ctx, redis); err != nil {
            return ctrl.Result{}, err
        }

        // 移除 Finalizer
        controllerutil.RemoveFinalizer(redis, finalizerName)
        if err := r.Update(ctx, redis); err != nil {
            return ctrl.Result{}, err
        }
        logger.Info("RedisCluster deleted, finalizer removed")
    }

    return ctrl.Result{}, nil
}

func (r *RedisClusterReconciler) cleanupExternalResources(ctx context.Context, redis *redisv1.RedisCluster) error {
    // 1. 删除外部备份(如 S3 prefix)
    // 2. 注销监控
    // 3. 通知下游服务
    return nil
}

func (r *RedisClusterReconciler) setCondition(ctx context.Context, redis *redisv1.RedisCluster, condType string, status metav1.ConditionStatus, reason, message string) {
    meta.SetStatusCondition(&redis.Status.Conditions, metav1.Condition{
        Type:               condType,
        Status:             status,
        Reason:             reason,
        Message:            message,
        ObservedGeneration: redis.Generation,
    })
    _ = r.Status().Update(ctx, redis)
}

func (r *RedisClusterReconciler) labelsForRedis(redis *redisv1.RedisCluster) map[string]string {
    return map[string]string{
        "app.kubernetes.io/name":     "redis",
        "app.kubernetes.io/instance": redis.Name,
        "app.kubernetes.io/managed-by": "redis-operator",
    }
}

// SetupWithManager 注册 Watch
func (r *RedisClusterReconciler) SetupWithManager(mgr ctrl.Manager) error {
    return ctrl.NewControllerManagedBy(mgr).
        For(&redisv1.RedisCluster{}).
        Owns(&appsv1.StatefulSet{}).           // Watch 子资源 StatefulSet
        Owns(&corev1.Service{}).                // Watch 子资源 Service
        Owns(&corev1.ConfigMap{}).              // Watch 子资源 ConfigMap
        WithEventFilter(predicate.ResourceVersionChanged{}).
        Complete(r)
}
```

### 27.6.4 Webhook 实现

`api/v1/rediscluster_webhook.go`:

```go
package v1

import (
    "fmt"
    "reflect"

    "k8s.io/apimachinery/pkg/runtime"
    "k8s.io/apimachinery/pkg/util/validation/field"
    ctrl "sigs.k8s.io/controller-runtime"
    "sigs.k8s.io/controller-runtime/pkg/webhook"
    "sigs.k8s.io/controller-runtime/pkg/webhook/admission"
)

func (r *RedisCluster) SetupWebhookWithManager(mgr ctrl.Manager) error {
    return ctrl.NewWebhookManagedBy(mgr).
        For(r).
        Complete()
}

// +kubebuilder:webhook:path=/mutate-redis-operator-io-v1-rediscluster,mutating=true,failurePolicy=fail,sideEffects=None,groups=redis.operator.io,resources=redisclusters,verbs=create;update,versions=v1,name=mrediscluster.kb.io,admissionReviewVersions=v1

var _ webhook.Defaulter = &RedisCluster{}

// Default implements webhook.Defaulter
func (r *RedisCluster) Default() {
    if r.Spec.Port == 0 {
        r.Spec.Port = 6379
    }
    if r.Spec.ImagePullPolicy == "" {
        r.Spec.ImagePullPolicy = "IfNotPresent"
    }
    if r.Spec.Auth == nil {
        r.Spec.Auth = &AuthSpec{Enabled: true}
    }
}

// +kubebuilder:webhook:path=/validate-redis-operator-io-v1-rediscluster,mutating=false,failurePolicy=fail,sideEffects=None,groups=redis.operator.io,resources=redisclusters,verbs=create;update;delete,versions=v1,name=vrediscluster.kb.io,admissionReviewVersions=v1

var _ webhook.Validator = &RedisCluster{}

// ValidateCreate implements webhook.Validator
func (r *RedisCluster) ValidateCreate() (admission.Warnings, error) {
    return nil, r.validateRedisCluster()
}

// ValidateUpdate implements webhook.Validator
func (r *RedisCluster) ValidateUpdate(old runtime.Object) (admission.Warnings, error) {
    oldRedis, ok := old.(*RedisCluster)
    if !ok {
        return nil, fmt.Errorf("expected *RedisCluster, got %T", old)
    }

    // 不可变字段检查
    if old.Spec.Persistence != nil && old.Spec.Persistence.Enabled &&
       r.Spec.Persistence != nil && !r.Spec.Persistence.Enabled {
        return nil, field.Invalid(field.NewPath("spec.persistence.enabled"),
            r.Spec.Persistence.Enabled, "persistence cannot be disabled once enabled")
    }

    if old.Spec.Persistence != nil && r.Spec.Persistence != nil &&
       old.Spec.Persistence.Size != r.Spec.Persistence.Size {
        // 只能扩容,不能缩容
        return nil, field.Invalid(field.NewPath("spec.persistence.size"),
            r.Spec.Persistence.Size, "persistence size cannot be shrunk")
    }

    return nil, r.validateRedisCluster()
}

// ValidateDelete implements webhook.Validator
func (r *RedisCluster) ValidateDelete() (admission.Warnings, error) {
    return nil, nil
}

func (r *RedisCluster) validateRedisCluster() error {
    var allErrs field.ErrorList

    // 副本数必须为偶数(主从配对)
    if r.Spec.Replicas%2 != 0 {
        allErrs = append(allErrs, field.Invalid(
            field.NewPath("spec.replicas"),
            r.Spec.Replicas,
            "replicas must be even (master-replica pairs)",
        ))
    }

    // 副本数最少 3(1 主 2 从)
    if r.Spec.Replicas < 3 {
        allErrs = append(allErrs, field.Invalid(
            field.NewPath("spec.replicas"),
            r.Spec.Replicas,
            "minimum replicas is 3",
        ))
    }

    // 镜像必填
    if r.Spec.Image == "" {
        allErrs = append(allErrs, field.Required(
            field.NewPath("spec.image"),
            "image is required",
        ))
    }

    // 备份目标必填(如果开启备份)
    if r.Spec.Backup != nil && r.Spec.Backup.Enabled && r.Spec.Backup.Destination == "" {
        allErrs = append(allErrs, field.Required(
            field.NewPath("spec.backup.destination"),
            "backup destination is required when backup enabled",
        ))
    }

    if len(allErrs) == 0 {
        return nil
    }
    return allErrs.ToAggregate()
}
```

### 27.6.5 Helm Chart 完整示例

`mychart/Chart.yaml`:

```yaml
apiVersion: v2
name: myapp
description: A Helm chart for my application
type: application
version: 0.3.0           # Chart 版本
appVersion: "1.16.0"     # 应用版本
keywords:
  - web
  - api
maintainers:
  - name: team-platform
    email: platform@example.com
dependencies:
  - name: redis
    version: 18.x.x
    repository: https://charts.bitnami.com/bitnami
    condition: redis.enabled
icon: https://example.com/icon.png
```

`mychart/values.yaml`:

```yaml
replicaCount: 3

image:
  repository: my-app
  tag: ""
  pullPolicy: IfNotPresent
  pullSecrets: []

service:
  type: ClusterIP
  port: 80
  targetPort: 8080

ingress:
  enabled: false
  className: nginx
  annotations: {}
  hosts:
    - host: myapp.example.com
      paths:
        - path: /
          pathType: Prefix
  tls: []

resources:
  requests:
    cpu: 100m
    memory: 128Mi
  limits:
    cpu: 500m
    memory: 512Mi

autoscaling:
  enabled: false
  minReplicas: 3
  maxReplicas: 10
  targetCPUUtilizationPercentage: 70

nodeSelector: {}
tolerations: []
affinity: {}

redis:
  enabled: false     # 依赖子 chart
  auth:
    enabled: true

# values.schema.json 校验
```

`mychart/templates/deployment.yaml`:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: {{ include "myapp.fullname" . }}
  labels:
    {{- include "myapp.labels" . | nindent 4 }}
spec:
  {{- if not .Values.autoscaling.enabled }}
  replicas: {{ .Values.replicaCount }}
  {{- end }}
  selector:
    matchLabels:
      {{- include "myapp.selectorLabels" . | nindent 6 }}
  template:
    metadata:
      {{- with .Values.podAnnotations }}
      annotations:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      labels:
        {{- include "myapp.selectorLabels" . | nindent 8 }}
    spec:
      {{- with .Values.image.pullSecrets }}
      imagePullSecrets:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      serviceAccountName: {{ include "myapp.serviceAccountName" . }}
      containers:
        - name: {{ .Chart.Name }}
          image: "{{ .Values.image.repository }}:{{ .Values.image.tag | default .Chart.AppVersion }}"
          imagePullPolicy: {{ .Values.image.pullPolicy }}
          ports:
            - name: http
              containerPort: {{ .Values.service.targetPort }}
              protocol: TCP
          {{- if .Values.probes.enabled }}
          livenessProbe:
            httpGet:
              path: /healthz
              port: http
            initialDelaySeconds: 30
            periodSeconds: 10
          readinessProbe:
            httpGet:
              path: /ready
              port: http
            initialDelaySeconds: 5
            periodSeconds: 5
          {{- end }}
          resources:
            {{- toYaml .Values.resources | nindent 12 }}
          env:
            - name: APP_ENV
              value: {{ .Values.env.name | quote }}
            {{- range $key, $val := .Values.env.extra }}
            - name: {{ $key }}
              value: {{ $val | quote }}
            {{- end }}
      {{- with .Values.nodeSelector }}
      nodeSelector:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      {{- with .Values.affinity }}
      affinity:
        {{- toYaml . | nindent 8 }}
      {{- end }}
```

`mychart/templates/_helpers.tpl`:

```yaml
{{- define "myapp.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{- define "myapp.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{- define "myapp.labels" -}}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{ include "myapp.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{- define "myapp.selectorLabels" -}}
app.kubernetes.io/name: {{ include "myapp.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}
```

### 27.6.6 Kustomize 完整示例

```
myapp/
├── base/
│   ├── kustomization.yaml
│   ├── deployment.yaml
│   ├── service.yaml
│   └── configmap.yaml
├── overlays/
│   ├── dev/
│   │   ├── kustomization.yaml
│   │   ├── replicas-patch.yaml
│   │   └── image-patch.yaml
│   ├── staging/
│   │   ├── kustomization.yaml
│   │   └── staging-patch.yaml
│   └── prod/
│       ├── kustomization.yaml
│       ├── replicas-patch.yaml
│       ├── resources-patch.yaml
│       └── ingress-patch.yaml
└── components/
    ├── monitoring/
    │   ├── kustomization.yaml
    │   ├── service-monitor.yaml
    │   └── dashboard.yaml
    └── network-policy/
        ├── kustomization.yaml
        └── network-policy.yaml
```

`base/kustomization.yaml`:

```yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization

resources:
- deployment.yaml
- service.yaml
- configmap.yaml

commonLabels:
  app.kubernetes.io/name: myapp
  app.kubernetes.io/managed-by: kustomize

images:
- name: myapp
  newTag: "1.0.0"

configMapGenerator:
- name: app-config
  literals:
  - LOG_LEVEL=info
  - FEATURE_FLAGS=true

generatorOptions:
  disableNameSuffixHash: true
```

`overlays/prod/kustomization.yaml`:

```yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization

namespace: production
namePrefix: prod-

resources:
- ../../base

components:
- ../../components/monitoring
- ../../components/network-policy

patches:
- path: replicas-patch.yaml
- path: resources-patch.yaml
- target:
    kind: Deployment
    name: myapp
  patch: |-
    - op: add
      path: /spec/template/spec/containers/0/env/-
      value:
        name: ENV
        value: production

images:
- name: myapp
  newName: registry.example.com/myapp
  newTag: "1.2.3"

replicas:
- name: myapp
  count: 10

commonAnnotations:
  deployed-by: argocd
  ticket: PROD-1234

configMapGenerator:
- name: app-config
  behavior: merge
  literals:
  - LOG_LEVEL=warn
  - DATABASE_POOL_SIZE=50
```

`overlays/prod/replicas-patch.yaml`:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: myapp
spec:
  replicas: 10
```

### 27.6.7 Argo CD Application 完整示例

```yaml
apiVersion: argoproj.io/v1alpha1
kind: Application
metadata:
  name: myapp-prod
  namespace: argocd
  finalizers:
  - resources-finalizer.argocd.argoproj.io  # 删除 app 时清理资源
spec:
  project: production                        # 关联 AppProject

  source:
    repoURL: https://github.com/example/myapp-manifests.git
    targetRevision: main
    path: overlays/prod
    
    # 多源(Multi-source)
    # - ref: values           # 引用其他源
    #   repoURL: https://github.com/example/values.git
    #   targetRevision: main
    #   path: prod
    
    # Helm 选项
    # plugin:
    #   env:
    #   - name: HELM_VALUES
    #     value: |
    #       image:
    #         tag: 1.2.3
    
  destination:
    server: https://kubernetes.default.svc   # 当前集群
    # server: https://1.2.3.4                # 外部集群
    namespace: production

  syncPolicy:
    automated:                               # 自动同步
      prune: true                            # 删除多余资源
      selfHeal: true                         # 自动修复手动改动
      allowEmpty: false                      # 防止误删所有资源
    
    syncOptions:
    - CreateNamespace=true
    - PrunePropagationPolicy=foreground
    - PruneLast=true
    - SkipDryRunOnMissingResource=true
    - ApplyOutOfSyncOnly=true
    - ServerSideApply=true
    
    retry:
      limit: 5
      backoff:
        duration: 5s
        factor: 2
        maxDuration: 3m

  revisionHistoryLimit: 10                   # 保留 10 次历史
  
  ignoreDifferences:
  - group: apps
    kind: Deployment
    jsonPointers:
    - /spec/replicas                         # 忽略 HPA 调整的副本数
  - group: ""
    kind: Service
    jsonPointers:
    - /spec/clusterIP                        # 忽略 Service ClusterIP

  info:
  - name: team
    value: platform
  - name: slack
    value: "#prod-alerts"
```

ApplicationSet(多集群自动生成):

```yaml
apiVersion: argoproj.io/v1alpha1
kind: ApplicationSet
metadata:
  name: myapp-multi-cluster
  namespace: argocd
spec:
  generators:
  - git:
      repoURL: https://github.com/example/myapp-manifests.git
      revision: main
      directories:
      - path: overlays/*           # 每个目录生成一个 app
  - clusters:
      selector:
        matchLabels:
          env: production           # 只选 prod 集群
  template:
    metadata:
      name: '{{path.basename}}-myapp'
    spec:
      project: production
      source:
        repoURL: https://github.com/example/myapp-manifests.git
        targetRevision: main
        path: '{{path}}'
      destination:
        server: '{{server}}'
        namespace: myapp
      syncPolicy:
        automated:
          prune: true
          selfHeal: true
```

### 27.6.8 Flux CD HelmRelease 完整示例

```yaml
apiVersion: source.toolkit.fluxcd.io/v1
kind: HelmRepository
metadata:
  name: bitnami
  namespace: flux-system
spec:
  url: https://charts.bitnami.com/bitnami
  interval: 5m
  type: oci                     # oci | default
---
apiVersion: helm.toolkit.fluxcd.io/v2beta1
kind: HelmRelease
metadata:
  name: redis
  namespace: cache
spec:
  interval: 5m
  chart:
    spec:
      chart: redis
      version: "18.x.x"
      sourceRef:
        kind: HelmRepository
        name: bitnami
        namespace: flux-system
  values:
    architecture: replication
    auth:
      enabled: true
      existingSecret: redis-auth
    master:
      persistence:
        enabled: true
        size: 10Gi
        storageClass: standard
      resources:
        requests:
          cpu: 100m
          memory: 256Mi
    replica:
      replicaCount: 3
      persistence:
        enabled: true
        size: 10Gi
    metrics:
      enabled: true
      serviceMonitor:
        enabled: true
  dependsOn:
  - name: cert-manager          # 依赖 cert-manager 已 Ready
  - name: storage-provisioner
  install:
    remediation:
      retries: 3
    crds: CreateReplace
  upgrade:
    remediation:
      retries: 3
      strategy: rollback
    crds: CreateReplace
  rollback:
    timeout: 5m
    cleanupOnFail: true
  test:
    enable: true
  driftDetection:
    mode: enabled
    ignore:
    - paths: ["/spec/replicas"]    # 忽略 HPA 改动
```

Flux Kustomization 示例:

```yaml
apiVersion: source.toolkit.fluxcd.io/v1
kind: GitRepository
metadata:
  name: myapp
  namespace: flux-system
spec:
  url: https://github.com/example/myapp
  ref:
    branch: main
  interval: 1m
  secretRef:
    name: github-deploy-key      # 私有仓库
---
apiVersion: kustomize.toolkit.fluxcd.io/v1
kind: Kustomization
metadata:
  name: myapp
  namespace: flux-system
spec:
  interval: 5m
  path: ./manifests
  prune: true
  wait: true
  targetNamespace: production
  sourceRef:
    kind: GitRepository
    name: myapp
  dependsOn:
  - name: redis
  healthChecks:
  - apiVersion: apps/v1
    kind: Deployment
    name: myapp
    namespace: production
  - apiVersion: v1
    kind: Service
    name: myapp
    namespace: production
  postBuild:
    substitute:
      cluster_name: prod-cluster-1
      region: us-east-1
    substituteFrom:
    - kind: ConfigMap
      name: cluster-vars
    - kind: Secret
      name: cluster-secrets
```

------

## 27.7 常见陷阱与调优

### 27.7.1 CRD 陷阱

**陷阱 1:CRD schema 不严**

```
错误:openAPIV3Schema 留空,所有字段都接受
后果:用户写错字段名,Operator 拿到 nil 触发 panic

修复:
  - 必填字段用 required
  - 枚举用 enum
  - 数值范围用 minimum/maximum
  - 字符串格式用 pattern
  - x-kubernetes-validations 用 CEL 表达式做复杂校验
```

**陷阱 2:status 子资源未启用**

```
错误:CRD 没声明 status 子资源
后果:Controller 调 Update() 时会覆盖 spec(因为读改写 race)

修复:
  versions:
  - name: v1
    subresources:
      status: {}        # 启用 /status 子资源
    schema: {...}

  然后代码里调 r.Status().Update(ctx, redis)
```

**陷阱 3:CRD 多版本未做 conversion**

```
错误:v1alpha1 → v1beta1 → v1 升级,旧 CR 数据读不出
后果:升级后 Operator 启动 panic

修复:
  - storage: true 只能标记一个版本
  - 其他版本用 conversion webhook 转换
  - v1 storage, v1alpha1/v1beta1 served 但转换
```

**陷阱 4:additionalPrinterColumns 没设**

```
错误:CRD 未设 printer columns
后果:kubectl get rediscluster 只显示 NAME, AGE
     运维看不到状态

修复:
  additionalPrinterColumns:
  - name: Replicas
    type: integer
    jsonPath: .spec.replicas
  - name: Ready
    type: integer
    jsonPath: .status.readyReplicas
  - name: Phase
    type: string
    jsonPath: .status.phase
```

### 27.7.2 Operator Reconcile 陷阱

**陷阱 1:Reconcile 非幂等**

```
错误:Reconcile 依赖外部状态(时间、随机数)
      每次执行结果不同
后果:无限 Reconcile,API Server 压力暴增

修复:
  - Reconcile 必须幂等
  - 随机数用 deterministic 算法(基于 name)
  - 时间用 CR 的 creationTimestamp
  - 状态判断基于实际集群状态,不依赖本地变量
```

**陷阱 2:无限 Reconcile 循环**

```
错误:每次 Reconcile 都 Update Status,即使没变化
后果:Status 更新触发 Watch,触发新 Reconcile,无限循环

修复:
  if !equality.Semantic.DeepEqual(oldStatus, newStatus) {
      redis.Status = newStatus
      r.Status().Update(ctx, redis)
  }
```

**陷阱 3:Finalizer 未移除导致 CR 卡死**

```
错误:Finalizer 逻辑里 return err,导致 Finalizer 永远不移除
后果:kubectl delete rediscluster 永远 hang

修复:
  func reconcileDelete():
      // 即使 cleanup 失败,也要继续
      _ = r.cleanupExternalResources()
      controllerutil.RemoveFinalizer(redis, finalizerName)
      return r.Update(ctx, redis)  // 移除 Finalizer
```

**陷阱 4:Watch 子资源未设 OwnerReferences**

```
错误:Controller 创建 StatefulSet 时没 SetControllerReference
后果:CR 删除后 StatefulSet 残留(孤儿资源)

修复:
  if err := controllerutil.SetControllerReference(redis, sts, r.Scheme); err != nil {
      return err
  }
  // 同时 .Owns(&appsv1.StatefulSet{}) 让控制器 Watch
```

**陷阱 5:Cache 不一致导致竞态**

```
错误:Create 后立刻 Get(走 Cache)
后果:Cache 还没同步,Get 返回 NotFound,触发重复 Create

修复:
  - Create 后用 CreateOrUpdate 模式:
    err := r.Get(...)
    if IsNotFound(err) {
        r.Create(...)
    } else {
        // Update logic
    }
  - 或用 r.Client.Get(走 API Server 而非 Cache)
  - 或 server-side apply:r.Patch(ctx, obj, client.Apply, ...)
```

### 27.7.3 Helm 陷阱

**陷阱 1:Helm release 卡死(pending-upgrade)**

```
错误:helm upgrade 中断(网络、OOM)
后果:release 状态卡在 pending-upgrade,无法再操作

修复:
  helm rollback my-release <last-good-revision>
  
  或强制解除:
  kubectl get secret -l owner=helm,name=my-release | \
    awk '{print $1}' | xargs kubectl delete secret
  
  Helm 3.14+:
  helm history my-release
  helm rollback my-release <revision>
```

**陷阱 2:Helm hook 顺序错误**

```
错误:CRD 安装 hook 用 post-install
后果:CR 在 CRD 之前创建,失败

修复:
  annotations:
    "helm.sh/hook": pre-install,pre-upgrade
    "helm.sh/hook-weight": "-5"      # 越小越早
    "helm.sh/hook-delete-policy": before-hook-creation,hook-succeeded
```

**陷阱 3:values 文件优先级混淆**

```
Helm values 优先级(高 → 低):
  1. --set / --set-string
  2. --values (-f) 后面文件
  3. --values 前面文件
  4. values.yaml
  
陷阱:
  helm install -f dev.yaml -f prod.yaml --set replicas=5
  实际:prod.yaml 覆盖 dev.yaml,--set 覆盖所有
  
注意:--set 会做类型推断,字符串数字可能出错
  --set-string 强制字符串
```

**陷阱 4:large chart 渲染慢**

```
错误:100+ 模板的 chart,helm template 几秒
后果:Argo CD/Flux 同步慢

优化:
  - 拆 chart:大 chart 拆成多个小 chart + 依赖
  - 减少 range 循环嵌套
  - 用 .Files.Get 替代 inline 大文本
  - 用 library chart 复用模板
```

### 27.7.4 Kustomize 陷阱

**陷阱 1:strategic merge 对 CRD 不生效**

```
错误:用 strategic merge patch CRD 资源
后果:整个列表被替换,不是合并

原因:strategic merge 依赖 K8s 内置 patch strategy,CRD 没有

修复:
  - CRD 资源用 JSON Patch(op: add/replace/remove)
  - 或用 server-side apply
```

**陷阱 2:namePrefix 破坏资源引用**

```
错误:namePrefix: prod-
      Service 引用 myapp,变成 prod-myapp 引用 myapp(不匹配)
后果:Service 找不到 Deployment

修复:
  - K8s 原生资源(Deployment/Service)Kustomize 自动改引用
  - 自定义 CRD 引用需手动 nameReference 配置:
    nameReference:
    - kind: Deployment
      fieldSpecs:
      - path: spec/scaleTargetRef/name
        kind: HorizontalPodAutoscaler
```

**陷阱 3:configMapGenerator 改名导致滚动更新失效**

```
错误:configMapGenerator 默认加 hash 后缀
      每次配置改,ConfigMap 名字变
      Deployment 不引用 hash,Pod 不重启
后果:配置更新不生效

修复:
  generatorOptions:
    disableNameSuffixHash: true   # 关闭 hash
  或:在 Deployment 里通过 name 引用 hash 版本
```

### 27.7.5 Argo CD 陷阱

**陷阱 1:SelfHeal 与手动改动冲突**

```
错误:运维 kubectl scale deployment=5(临时扩容)
     Argo CD selfHeal 立刻改回 3
后果:运维操作被覆盖

修复:
  - 临时:暂停 app(argocd app stop / suspend)
  - 持久:ignoreDifferences 忽略 replicas
  - 治本:HPA 接管 replicas,Argo CD 不再管
```

**陷阱 2:Sync 失败但 Status 显示 Healthy**

```
错误:Sync Hook 失败但 Deployment Ready
后果:Argo CD 显示 Healthy 但实际未同步

修复:
  - syncOptions: FailOnSharedResource=true
  - 用 PostSync Hook 验证
  - 配合 Health Check 严格判断
```

**陷阱 3:Prune 误删资源**

```
错误:Git 删了 PVC,Argo CD prune 删 PVC
后果:数据丢失

修复:
  - syncOptions: PrunePropagationPolicy=foreground
  - 关键资源加 annotation:
    argocd.argoproj.io/sync-options: Prune=false
  - 或:OrphanedResourceWarnLevel: warn
```

### 27.7.6 Flux CD 陷阱

**陷阱 1:依赖循环**

```
错误:A dependsOn B,B dependsOn A
后果:Flux 拒绝应用,所有 app 卡住

修复:
  - 拆解依赖图
  - 用 sourceRef 代替 dependsOn
  - 共同依赖提升到上层 Kustomization
```

**陷阱 2:HelmRelease values 改了不生效**

```
错误:values 改了但 helm upgrade 没触发
后果:配置陈旧

修复:
  - 检查 Source 是否同步:flux reconcile source helm bitnami
  - 检查 HelmRelease:flux reconcile helmrelease redis
  - 看 flux logs
  - 检查 values 是否被其他 Kustomization 覆盖
```

------

## 27.8 工业案例与基准数据

### 27.8.1 阿里 ACK Operator Hub

阿里云 ACK(阿里云容器服务 Kubernetes)运营自己的 Operator Hub,提供 60+ 认证 Operator:

```
典型 Operator:
  - 数据库:RDS Operator、PolarDB Operator、MongoDB Operator
  - 消息:RocketMQ Operator、Kafka Operator(自研分支)
  - 缓存:Tair Operator(增强版 Redis)
  - AI:PAI Operator(训练任务)
  - 网络:ALB Ingress Controller、ASM Operator

规模数据(2024 年披露):
  - 60+ 认证 Operator
  - 部署到 10 万+ ACK 集群
  - 单 Operator 日均 reconcile 1 亿次
  - Operator 升级影响 5 万+ 集群(灰度发布)
  - 平均 Operator 镜像 < 100MB,启动 < 5s
  
工程经验:
  1. Operator 必须支持优雅升级(双版本兼容)
  2. CRD conversion webhook 是核心
  3. Operator 自身 metrics 必须接入 SLS
  4. 多集群 Operator 用 OCM(Open Cluster Model)管理
```

### 27.8.2 字节内部 Operator 平台

字节跳动内部 K8s 平台(代号 TCE / ByteK8s)统一管理 1000+ Operator:

```
架构:
  Operator Registry → Operator Lifecycle Manager → 多集群分发
  
特点:
  - 自研 Operator Framework(兼容 OLM)
  - 内部 Operator 仓库 + 公网 Operator Hub 同步
  - 多集群 Operator 版本管理
  - Operator 健康度评分(基于 reconcile 失败率、资源占用)

规模数据:
  - 1000+ Operator 覆盖 5000+ 微服务
  - 单集群平均 50+ Operator 同时运行
  - Operator 控制面 CPU 总占用 < 5% 节点资源
  - reconcile p99 延迟 < 2s
  - 每天处理 10 亿次 CR 变更

典型场景:
  1. 抖音推荐:训推一体 Operator(PyTorch + vLLM)
  2. 飞书协同:Office Operator(文档转换、协作)
  3. 电商大促:扩缩容 Operator(基于流量预测)
  4. 直播转码:GPU Operator(自动 NVIDIA Driver 安装)
```

### 27.8.3 Netflix Spinnaker + Argo CD

Netflix 流媒体平台从 Spinnaker 迁移到 Spinnaker + Argo CD 混合架构:

```
原架构:
  Spinnaker Pipeline → 直接调 K8s API
  问题:配置散落,审计困难,回滚慢

新架构:
  Spinnaker(编排) → 改 Git 仓库 → Argo CD(同步) → K8s
  
收益:
  - 部署频率:从每天 200 次提升到 2000 次
  - 部署延迟:从 15 分钟降到 3 分钟
  - 回滚时间:从 10 分钟降到 1 分钟
  - 审计能力:Git log 即完整审计

规模:
  - Argo CD 管理 200+ 集群
  - 5000+ Application
  - 每天 50000+ 同步操作
  - Git 仓库 1TB+(含历史)
```

### 27.8.4 Google Config Connector

Google Cloud 提供的 Config Connector 是把 GCP 资源暴露成 K8s CRD 的 Operator:

```
示例:
  apiVersion: compute.cnrm.cloud.google.com/v1beta1
  kind: ComputeInstance
  metadata:
    name: my-vm
  spec:
    machineType: n1-standard-4
    zone: us-central1-a
    image: debian-cloud/debian-11
    bootDisk:
      sourceRef:
        kind: ComputeDisk
        name: my-disk

特点:
  - 100+ GCP 资源类型覆盖
  - 跨云资源用 K8s API 统一管理
  - 与 Terraform 互补(Terraform 是命令式,Config Connector 是声明式)
  - 全局资源(Cloud SQL、Pub/Sub、BigQuery)也能用 CRD

规模:
  - 部署到 5 万+ GKE 集群
  - 单集群管理 1 万+ GCP 资源
  - reconcile 延迟 < 30s
```

### 27.8.5 AWS Controllers for Kubernetes(ACK)

AWS 开源的 K8s Operator 集合,把 AWS 服务暴露为 CRD:

```
ACK 项目结构:
  aws-controllers-k8s/
  ├── services/
  │   ├── s3/           # S3 Operator
  │   ├── dynamodb/     # DynamoDB Operator
  │   ├── rds/          # RDS Operator
  │   ├── sqs/          # SQS Operator
  │   ├── sns/          # SNS Operator
  │   ├── lambda/       # Lambda Operator
  │   ├── ecr/          # ECR Operator
  │   └── ...           # 50+ 服务

示例:
  apiVersion: s3.services.k8s.aws/v1alpha1
  kind: Bucket
  metadata:
    name: my-bucket
  spec:
    name: my-bucket-2024
  
  Operator 调 S3 API 创建 bucket,状态写回 status

规模:
  - 50+ AWS 服务支持
  - 单 Operator 镜像 80MB
  - reconcile p99 < 5s
  - 部署到 EKS 2 万+ 集群
```

### 27.8.6 Prometheus Operator 规模

Prometheus Operator 是 CNCF 最成功的 Operator 之一:

```
功能:
  - 一键部署 Prometheus + Alertmanager + Grafana
  - ServiceMonitor CRD 自动发现监控目标
  - PrometheusRule CRD 管理告警规则
  - 多副本 Prometheus 高可用
  - Thanos 集成长期存储

规模数据(2024 年):
  - 部署到 10 万+ 集群(CNCF 调查)
  - GitHub Star 8800+
  - 单 Operator 管理 100+ Prometheus 实例
  - 单 Prometheus 抓 100 万 series
  - 集群规模:5000 节点 + 5 万 Pod

性能数据:
  - Operator 内存占用:200MB(管理 100 实例)
  - Operator CPU 占用:0.5 core
  - ServiceMonitor 变更到生效:< 30s
  - 配置生成延迟:< 5s
```

### 27.8.7 Cert-Manager 规模

Cert-Manager 是 K8s 生态证书管理事实标准:

```
功能:
  - Let's Encrypt 自动签发
  - 私有 CA(Vault、CFSSL)集成
  - 证书自动轮换(到期前 30 天)
  - 多种签发器(SelfSigned/CA/ACME/Vault)
  - 通配符证书

规模数据(2024 年 Jetstack 调查):
  - 部署到 50 万+ 集群
  - 每天签发 100 万+ 证书
  - 单集群管理 1 万+ 证书
  - 签发延迟:Let's Encrypt 5-10s,SelfSigned < 1s
  - 内存占用:200MB(1 万证书)
  - 证书轮换成功率 99.9%+

主流用户:
  - Cloudflare、DigitalOcean、GitLab、Shopify
  - 阿里云、腾讯云、华为云均用作证书服务底座
```

### 27.8.8 Strimzi Kafka Operator

Strimzi 是 Kafka on K8s 的事实标准:

```
功能:
  - Kafka Cluster 部署(ZK / KRaft 模式)
  - Topic / User CRD 管理
  - MirrorMaker2 跨集群复制
  - Kafka Connect / Bridge
  - 滚动升级(优雅重启 Broker)
  - 自动 Rebalance Partition

规模数据:
  - GitHub Star 4700+
  - 部署到 1 万+ 集群
  - 单集群管理 100+ Kafka Cluster
  - 单 Kafka Cluster 100+ Broker
  - Topic 数量 10 万+
  - 升级时间:100 Broker 滚动重启 2 小时

工程亮点:
  - 优雅重启:逐 Broker,等待 ISR 同步
  - 配置变更:滚动生效,不重启 Broker
  - 故障转移:Broker 挂自动检测,Partition 重新分配
```

### 27.8.9 Argo Workflows

Argo Workflows 是 K8s 上的工作流引擎:

```
功能:
  - DAG / Steps 工作流
  - 容器化任务
  - 工件传递(Artifact)
  - 定时 CronWorkflow
  - CI/CD Pipeline

规模数据:
  - GitHub Star 14500+
  - 部署到 2 万+ 集群
  - 单工作流 1000+ 步骤
  - 并行任务 10000+
  - 工作流时长:分钟级到周级

典型场景:
  - 机器学习训练 pipeline(Kubeflow 底层)
  - 数据 ETL(爱奇艺、Netflix)
  - 视频转码(字节、Netflix)
  - 金融风控批量计算
```

### 27.8.10 Crossplane

Crossplane 把 K8s 扩展成多云控制平面:

```
理念:
  任何云资源都是 K8s CRD
  K8s API 即多云 API

架构:
  Crossplane Provider(AWS/Azure/GCP/Alibaba)
  → ProviderConfig(凭证)
  → Managed Resource(S3Bucket, RDSInstance, ...)
  → Composition(组合多个资源成新类型)
  → XR(Crossplane Resource,用户视角)

规模:
  - GitHub Star 8800+
  - CNCF Incubating 项目
  - 100+ 云资源类型覆盖
  - 部署到 1 万+ 集群

vs Terraform:
  - Terraform:命令式 apply,无 Reconcile
  - Crossplane:声明式 + Reconcile,持续同步
  - 适合 GitOps
```

------

## 27.9 与其他方案的关系

### 27.9.1 Operator vs Controller vs Webhook

```
Controller:
  - 通用术语,任何实现 Reconcile Loop 的程序
  - K8s 内置:Deployment Controller、Node Controller 等
  - 自定义 Controller:不一定要 CRD,可以 watch 内置资源

Operator:
  - Controller 的子集
  - 必须有 CRD(自定义资源)
  - 必须封装领域知识(运维 SRE 经验)
  - 例:Redis Operator、Prometheus Operator

Webhook:
  - 准入控制,不改 Reconcile 逻辑
  - 在资源写入 etcd 前拦截
  - MutatingWebhook:修改对象
  - ValidatingWebhook:校验对象
  - ConversionWebhook:CRD 版本转换

关系:
  Operator = CRD + Controller + (可选)Webhook
  Webhook 是 Operator 的可选组件,用于校验和默认值
```

### 27.9.2 Helm vs Kustomize vs Carvel ytt

| 维度 | Helm | Kustomize | Carvel ytt |
|------|------|-----------|------------|
| 模型 | 模板 + 变量 | 覆盖 + 补丁 | 编程式 overlay |
| 语法 | Go template + Sprig | YAML + Patch | Python-like 脚本 |
| 学习曲线 | 中(学模板语法) | 低(纯 YAML) | 高(学脚本) |
| 灵活度 | 高 | 中 | 极高 |
| 运行时 | 需要 Helm CLI | kubectl 内置 | 需 ytt CLI |
| 仓库分发 | Chart 仓库/OCI | 无(纯文件) | 无 |
| 适合场景 | 应用打包分发 | 多环境 overlay | 复杂逻辑 |
| Release 管理 | 有(release history) | 无 | 无 |
| 生态 | 最大(Chart Hub) | 大(原生集成) | 中 |

实践建议:

- 单应用多环境:Kustomize
- 应用打包分发(给客户):Helm
- 复杂逻辑分支:ytt
- 混合:Helm chart + Kustomize overlay(customize Helm 渲染结果)

### 27.9.3 Argo CD vs Flux CD vs Jenkins X

| 维度 | Argo CD | Flux CD | Jenkins X |
|------|---------|---------|-----------|
| 架构 | 单体(多组件) | 微服务(多 controller) | 全栈 CI/CD |
| UI | 丰富(可视化) | 简单(命令行为主) | 中等 |
| 多集群 | ApplicationSet | 原生支持 | 支持 |
| Helm | 内置渲染 | 内置 HelmRelease | 内置 |
| Kustomize | 内置 | 内置 | 内置 |
| 镜像更新 | Image Updater(独立) | Image Automation(原生) | 内置 |
| 通知 | 通知 controller | 通知 controller | 内置 |
| RBAC | 强(项目级) | 中 | 强 |
| 同步策略 | 自动/手动 | 自动/手动 | 自动 |
| Hook | PreSync/Sync/PostSync | 无(用 dependsOn) | 内置 |
| 适合场景 | 企业级、需 UI | GitOps 原教旨 | 全流程 |
| CNCF 状态 | Graduated | Graduated | Archived(已存档) |

实践建议:

- 重 UI、企业级:Argo CD
- 偏好命令行、轻量:Flux CD
- 全流程 CI/CD:Jenkins X(已存档,建议 Argo Workflows + Argo CD)

### 27.9.4 Operator SDK vs Kubebuilder vs Operator Framework

```
Kubebuilder:
  - K8s SIG 项目
  - 只支持 Go
  - 框架:controller-runtime + controller-gen
  - 项目脚手架
  - 不打包分发

Operator SDK:
  - Red Hat 主导
  - 支持 Go / Helm / Ansible
  - 基于 Kubebuilder(Go 模式)
  - 增加:OLM bundle、scorecard、helm/ansible 模式
  - 完整发布流程

Operator Framework:
  - 包含:
    * Operator SDK(开发)
    * Operator Lifecycle Manager(OLM,运行)
    * OperatorHub.io(分发)
  - 端到端生态

关系:
  Kubebuilder ⊂ Operator SDK ⊂ Operator Framework
  
  Kubebuilder 是 Operator SDK Go 模式的底层
  Operator Framework 是 Operator SDK + OLM + Hub
```

### 27.9.5 GitOps vs 传统 CI/CD

| 维度 | 传统 CI/CD | GitOps |
|------|-----------|--------|
| 推/拉 | 推(kubectl apply) | 拉(Controller sync) |
| 状态源 | CI 工具变量 | Git commit |
| 集群凭证 | CI 持有 | 集群内 Controller 持有 |
| 审计 | CI 日志 | Git log |
| 回滚 | 重新跑 pipeline | Git revert |
| 多集群 | 每集群一套 pipeline | 一套 Controller 管多集群 |
| 漂移检测 | 无 | 持续 diff |
| 自愈 | 无 | 自动 sync |

GitOps 4 原则(Olimpiadas):

1. **声明式**:系统状态用声明式描述
2. **版本化**:状态存 Git,完整历史
3. **自动拉取**:集群主动拉取,不是推送
4. **持续协调**:Controller 持续 diff + sync

------

## 27.10 面试速答

**Q1:CRD 和 CR 的区别?**
A:CRD 是资源类型定义(类似 class),CR 是资源实例(类似 object)。CRD 注册到 K8s 后,API Server 自动暴露 RESTful 接口,用户可以 kubectl apply 创建 CR。

**Q2:Operator 和 Controller 的区别?**
A:Operator 是 Controller 的子集。任何实现 Reconcile Loop 的程序都是 Controller;Operator 必须有 CRD + 领域知识,封装运维经验。所有 Operator 都是 Controller,反之不成立。

**Q3:为什么 Reconcile 必须幂等?**
A:Reconcile 会被各种事件触发(Watch、定时、手动),同一输入可能触发多次。如果不幂等,会导致状态发散、资源泄漏、无限循环。幂等性靠:基于实际状态判断、不依赖事件顺序、不依赖外部状态。

**Q4:Finalizer 的作用?**
A:防止 CR 删除时子资源或外部资源残留。删除 CR 时,K8s 不会立即删除,而是加 DeletionTimestamp,等 Controller 执行清理逻辑后移除 Finalizer,CR 才真正被删除。

**Q5:Helm 和 Kustomize 的本质区别?**
A:Helm 是"模板 + 变量"渲染,需要 Chart 仓库分发;Kustomize 是"已有 YAML + 补丁"合并,无运行时依赖。Helm 适合打包分发,Kustomize 适合多环境 overlay。

**Q6:Helm 的 3-way merge 是什么?**
A:upgrade 时合并三方:旧 chart 渲染、新 chart 渲染、集群实际状态。让 helm 知道用户手动改了什么,智能决定是否覆盖。kubectl apply 只有 2-way(当前 vs 集群)。

**Q7:Argo CD 的 Sync Wave 是什么?**
A:资源按 wave 排序应用,wave 越小越早执行。常用于 CRD 必须先于 CR、Namespace 先于 Pod。注解 `argocd.argoproj.io/sync-wave: "-5"`。

**Q8:GitOps 的核心原则?**
A:4 原则:声明式、版本化(Git)、自动拉取(集群主动)、持续协调。Git 是唯一真相源,集群状态自动收敛到 Git。

**Q9:Operator 的 5 个成熟度等级?**
A:L1 基础安装、L2 无缝升级、L3 全生命周期(备份/恢复/扩缩)、L4 深度洞察(指标/告警)、L5 自动驾驶(自愈/调优)。

**Q10:Controller Runtime 中 Cache 和 Client 的区别?**
A:Cache 是本地 Informer 缓存,只读,来自 List-Watch。Client 写走 API Server,读默认走 Cache。读写分离降低 API Server 压力。Status 子资源必须用 Status().Update()。

**Q11:CRD 多版本如何共存?**
A:只有一个 storage: true 版本(实际存到 etcd 的格式),其他版本 served 但通过 conversion webhook 转换。读写时自动调用 webhook 转换格式。

**Q12:Kustomize 的 strategic merge 对 CRD 资源为什么不生效?**
A:strategic merge 依赖 K8s 内置 patch strategy(由 controller-gen 生成),CRD 没有这个信息。CRD 资源应用 JSON Patch 或 server-side apply。

**Q13:Argo CD 和 Flux CD 怎么选?**
A:重 UI 和企业级治理选 Argo CD,偏好命令行和轻量选 Flux CD。两者都是 CNCF 毕业项目,功能相当。Argo CD 有 ApplicationSet 多集群,Flux 有原生多集群。

**Q14:Operator 升级时如何不破坏现有 CR?**
A:1) CRD 向后兼容(只加字段不删);2) 用 conversion webhook 处理版本转换;3) Operator 双版本兼容(新 Operator 能读旧 CR);4) 灰度升级(先升级非生产);5) 备份 CR 数据。

**Q15:Cert-Manager 的核心 CRD 有哪些?**
A:Certificate(证书申请)、Issuer/ClusterIssuer(签发器)、CertificateRequest(CSR 流程)、ACMEChallenge(Let's Encrypt 验证)。Certificate 描述期望证书,Issuer 描述如何签发。

------

## 27.11 综合面试题

### 题 1(初级):CRD 与 Operator 基础

**问题**:请描述 CRD + Operator 的工作原理,并用一个具体场景(如 Redis Cluster)说明 Operator 如何工作。

**答题要点**:

1. CRD 定义新资源类型 RedisCluster,注册到 API Server
2. 用户 kubectl apply 创建 RedisCluster CR
3. Operator(自定义 Controller)Watch 到 CR 变化
4. Reconcile Loop 读取 CR spec,创建 StatefulSet/Service/ConfigMap
5. 持续 Watch 子资源,确保状态收敛
6. 用户改 replicas,Operator 滚动扩容
7. CR 删除,Finalizer 触发清理流程

### 题 2(中级):Operator 设计陷阱

**问题**:你写了一个 Operator,生产环境发现:CR 删除后卡死、Status 字段不更新、Pod 反复重建。请分析可能原因。

**答题要点**:

**CR 删除卡死**:
- Finalizer 逻辑里 return err,永远不移除
- 修复:即使 cleanup 失败也要移除 Finalizer

**Status 不更新**:
- 没启用 status 子资源,r.Status().Update() 失败
- 或 Status 写法和 spec Update 混淆
- 或 equality.DeepEqual 判断错误(总是相等)

**Pod 反复重建**:
- Reconcile 每次都 Update StatefulSet(即使没变化)
- 或 Liveness Probe 配置过严
- 或镜像 tag 用 latest,每次 pull 不同版本

### 题 3(中级):Helm vs Kustomize 选型

**问题**:公司有 50 个微服务,每个有 dev/staging/prod 三个环境。用 Helm 还是 Kustomize?

**答题要点**:

- 50 个微服务 + 3 环境 = 150 个部署单元
- 推荐混合方案:
  - 每个微服务一个 Helm Chart(打包标准化)
  - 每个环境一个 Kustomize overlay(定制化)
  - Argo CD 渲染 Helm + Kustomize 二次定制
- 理由:
  - Chart 适合复用(给其他团队、客户)
  - Kustomize 适合环境差异(不改 chart)
  - 避免 values.yaml 膨胀(几十环境 values 难维护)

### 题 4(高级):多集群 GitOps 设计

**问题**:设计一个支持 1000 个集群的 GitOps 系统,要求:1) 应用统一管理;2) 集群差异化配置;3) 故障隔离;4) 审计合规。

**答题要点**:

**架构**:
- 中心 Git 仓库组织:
  ```
  fleet/
  ├── clusters/
  │   ├── cluster-001/
  │   │   ├── apps/          # 该集群的 app
  │   │   └── config/        # 集群定制
  │   ├── cluster-002/
  │   └── ...
  ├── apps/                  # 全局 app 模板
  └── policies/              # 全局策略
  ```
- Argo CD ApplicationSet 自动生成(基于 git generator)
- 每集群一个 Argo CD 实例(故障隔离)
- 中心 Argo CD of Argo CD(App-of-Apps 模式)

**差异化**:
- Kustomize base + cluster overlay
- ConfigMap 存集群元数据(region、env、tier)
- substitute(Flux)/tpl(Argo CD)动态替换

**故障隔离**:
- 每集群独立 Argo CD,故障不影响其他
- 中心只做配置下发,不做实际部署
- 集群断网时本地 Argo CD 持续 Reconcile(最后一次 Git 状态)

**审计合规**:
- Git commit = 完整审计
- Argo CD 操作日志集中收集
- 关键操作(生产 sync)需 PR Review
- 定期 dry-run diff 报告

### 题 5(高级):Operator 升级方案

**问题**:你维护的 Operator v1 已在生产运行 1 年,有 5000 个 CR。现在要升级到 v2,改了 spec 字段(旧字段 `replicas` 改成 `scaling.replicas`)。如何不中断服务地升级?

**答题要点**:

**步骤**:
1. **CRD 保留 v1 和 v2**:`served: true`,v1 `storage: true`(暂)
2. **实现 conversion webhook**:v1 ↔ v2 字段映射
3. **Operator 同时支持 v1 和 v2**:Reconcile 内部用 v2 逻辑,webhook 转换
4. **灰度升级**:
   - 先升级非生产集群
   - 观察 1 周,确认无异常
   - 灰度生产(10% → 50% → 100%)
5. **迁移 CR**:
   - 工具批量 `kubectl get rc.v1 -o yaml | convert | kubectl apply -f -`
   - 或等用户自然更新
6. **下线 v1**:全部迁移后,改 v1 `served: false`,下个版本删除

**回滚方案**:
- 保留 v1 Operator 镜像
- CRD conversion webhook 双向兼容
- 紧急回滚:重部署旧 Operator + 改 CRD storage

### 题 6(高级):GitOps 漂移检测与自愈

**问题**:GitOps 环境中,运维 kubectl edit 直接改了 Deployment 的资源限制。Argo CD 应该如何处理?设计漂移检测策略。

**答题要点**:

**漂移类型**:
1. **有意漂移**:HPA 调整 replicas、kubelet 改 status
2. **无意漂移**:运维手动 kubectl edit、应急修改
3. **恶意漂移**:未授权修改

**Argo CD 策略**:
- **ignoreDifferences**:忽略有意漂移(如 replicas、status、clusterIP)
- **selfHeal: true**:自动 sync 修正无意漂移
- **网络钩子**:检测到漂移 → 告警 → PR Review 决定是否回滚

**漂移检测实现**:
```yaml
syncPolicy:
  automated:
    selfHeal: true              # 自动修正
    prune: true
ignoreDifferences:
- group: apps
  kind: Deployment
  jsonPointers:
  - /spec/replicas             # HPA 管理,忽略
  - /metadata/annotations/last-applied-configuration
- group: ""
  kind: Service
  jsonPointers:
  - /spec/clusterIP
```

**告警**:
- Argo CD 通知 → Slack
- 关键资源(生产 DB)漂移 → 立即告警
- 频繁漂移 → 审查运维流程

------

## 27.12 故障复盘

### 案例 1:Operator 升级把 CRD 改坏

**故障时间**:2024-03-15

**故障现象**:
- Operator v2 升级后,5000 个 RedisCluster CR 全部 spec 字段丢失
- Operator 反复 panic,API Server 拒绝创建新 CR
- 影响范围:200+ 业务集群,数万服务

**根因**:
- v2 CRD 删除了 `spec.command` 字段(认为没人用)
- v1 CR 的 spec.command 有值
- CRD 升级后,API Server 拒绝带未知字段的 CR
- Operator 读 CR 时,Decoding 失败 panic

**修复**:
1. 紧急回滚 CRD 到 v1
2. v2 CRD 重新加上 `spec.command` 字段(标记 deprecated)
3. conversion webhook 处理字段迁移
4. 重新发布 v2.1

**经验**:
- CRD 字段只加不删(向后兼容)
- 删字段流程:先标记 deprecated → 等待 N 版本 → conversion webhook 兜底
- CRD 升级必须做兼容性测试(用真实 CR 数据)

### 案例 2:Helm 释放无法回滚

**故障时间**:2024-06-22

**故障现象**:
- helm upgrade 后 release 卡在 pending-upgrade
- helm rollback 失败:Error: another operation is in progress
- 业务服务 30 分钟无法部署

**根因**:
- helm upgrade 时网络中断,Release 状态没写回
- release Secret 锁定在 pending-upgrade
- helm 3.13 之前没有自动恢复机制

**修复**:
1. 找到 release Secret:
   `kubectl get secret -l owner=helm,name=my-release`
2. 看 release 状态:
   `kubectl get secret sh.helm.release.v1.my-release.v5 -o yaml | grep status`
3. 手动改 Secret 状态(deployed)或删除该 Secret
4. helm rollback 到 last-good revision

**经验**:
- helm 3.14+ 加了 release 锁自动恢复(--atomic)
- 关键 release 用 `--atomic --timeout`,失败自动回滚
- 监控 helm release 状态,pending 状态告警

### 案例 3:Argo CD 自动同步误删 PVC

**故障时间**:2024-09-08

**故障现象**:
- 开发在 Git 删了测试环境的 PVC
- Argo CD selfHeal + prune 立刻删 PVC
- 测试环境数据库数据全部丢失
- 影响测试团队 1 天工作

**根因**:
- prune: true 删除 Git 中不存在的资源
- PVC 没加保护注解
- 测试环境复用生产配置(selfHeal: true)

**修复**:
1. 从备份恢复 PVC 数据(每天全量备份)
2. 给所有 PVC 加注解:
   `argocd.argoproj.io/sync-options: Prune=false`
3. 全局策略:
   ```yaml
   syncOptions:
   - PrunePropagationPolicy=foreground
   - ApplyOutOfSyncOnly=true
   ```
4. Argo CD 配置 OrphanedResourceWarnLevel: warn
5. 测试环境 selfHeal 改为 false

**经验**:
- PVC、PV、Secret 等有状态资源默认不 prune
- 测试环境配置不能直接复用生产(selfHeal 不同)
- Git 操作敏感资源(删 PVC)需 PR Review

### 案例 4:CRD 丢失数据(etcd quota)

**故障时间**:2024-11-30

**故障现象**:
- 一个租户的 1000 个 MySQL CR 突然全部读不出
- kubectl get mysql 返回空
- API Server 日志:etcdserver: mvcc: database space exceeded
- 集群其他资源正常

**根因**:
- 该租户 Operator 频繁更新 CR status(每秒 10 次)
- 每次更新产生 etcd 历史版本
- etcd quota 默认 2GB,1 周内填满
- etcd 满后,所有写操作拒绝
- CRD 数据(包括最新版本)被压缩

**修复**:
1. 紧急扩容 etcd quota(--quota-backend-bytes=8GB)
2. 触发 etcd 压缩:
   ```
   etcdctl compact $(etcdctl endpoint status -w json | jq -r '.[0].Status.header.revision')
   etcdctl defrag
   ```
3. 修复 Operator:
   - Status 只在变化时更新(deep equal 检查)
   - 周期性 Reconcile 间隔从 1s 调到 30s
4. 集群加 etcd 空间监控告警(>70% 告警)

**经验**:
- Operator 不能频繁写 Status(deep equal 必做)
- etcd quota 监控是基础设施
- CRD 大对象(>1MB)应该用单独的 etcd 或 Object Storage

### 案例 5:Flux 依赖循环导致全集群卡死

**故障时间**:2025-01-12

**故障现象**:
- 一个集群 Flux 所有 Kustomization 状态 Stopped
- 错误:dependency cycle detected
- 集群无法部署任何应用 4 小时

**根因**:
- 团队 A 加了 `myapp dependsOn infra-redis`
- 团队 B 同时加了 `infra-redis dependsOn myapp`(误以为 myapp 是基础设施)
- Flux 检测到循环,拒绝所有 Reconcile

**修复**:
1. 临时:暂停其中一个 Kustomization
2. 修正 dependsOn 关系(去掉错误依赖)
3. 灰度恢复其他 Kustomization

**经验**:
- dependsOn 必须有清晰层级(基础设施 → 中间件 → 应用)
- 多团队协作应有依赖图 review
- Flux 应该有循环检测告警(早发现)
- CI 阶段做依赖图 lint

------

## 27.13 参考与延伸

### 27.13.1 官方文档

- [Kubernetes Custom Resources](https://kubernetes.io/docs/concepts/extend-kubernetes/api-extension/custom-resources/)
- [CRD 定义](https://kubernetes.io/docs/tasks/extend-kubernetes/custom-resources/custom-resource-definitions/)
- [Operator Pattern](https://kubernetes.io/docs/concepts/extend-kubernetes/operator/)
- [Helm 文档](https://helm.sh/docs/)
- [Kustomize 文档](https://kustomize.io/)
- [Argo CD 文档](https://argo-cd.readthedocs.io/)
- [Flux CD 文档](https://fluxcd.io/)
- [Controller Runtime](https://pkg.go.dev/sigs.k8s.io/controller-runtime)
- [Kubebuilder Book](https://book.kubebuilder.io/)
- [Operator SDK](https://sdk.operatorframework.io/)

### 27.13.2 主流 Operator

- [Prometheus Operator](https://prometheus-operator.dev/)
- [Cert-Manager](https://cert-manager.io/)
- [Strimzi Kafka Operator](https://strimzi.io/)
- [Argo Workflows](https://argoproj.github.io/argo-workflows/)
- [Crossplane](https://crossplane.io/)
- [AWS ACK](https://aws-controllers-k8s.github.io/community/)
- [Google Config Connector](https://cloud.google.com/config-connector/docs/overview)
- [CrunchyData PostgreSQL Operator](https://github.com/CrunchyData/postgres-operator)
- [Kubeflow Operators](https://github.com/kubeflow)
- [OpenShift OLM](https://olm.operatorframework.io/)

### 27.13.3 GitOps 工具

- [Argo CD](https://github.com/argoproj/argo-cd)
- [Argo CD ApplicationSet](https://argocd-applicationset.readthedocs.io/)
- [Argo Rollouts](https://argoproj.github.io/rollouts/)
- [Flux CD](https://github.com/fluxcd/flux2)
- [Flux Notifications](https://fluxcd.io/flux/components/notification/)
- [Jenkins X](https://jenkins-x.io/)(已存档)
- [Rancher Fleet](https://fleet.rancher.io/)
- [Argo CD Image Updater](https://argocd-image-updater.readthedocs.io/)

### 27.13.4 源码导航

- `kubernetes/apiextensions-apiserver/` - CRD API Server
- `kubernetes/pkg/controller/` - 内置 Controller
- `sigs.k8s.io/controller-runtime` - Operator 框架
- `sigs.k8s.io/controller-tools` - CRD / RBAC 生成器
- `sigs.k8s.io/kubebuilder/` - 项目脚手架
- `helm/helm/` - Helm 主仓库
- `kubernetes-sigs/kustomize/` - Kustomize 源码
- `argoproj/argo-cd/` - Argo CD 源码
- `fluxcd/flux2/` - Flux CD 源码

### 27.13.5 工业实践

- [Alibaba ACK Operator Hub](https://www.alibabacloud.com/help/en/ack/ack-managed-ack/)
- [ByteDance TCE Platform](https://www.bytedance.com/zh/lp/tce)
- [Netflix Spinnaker + Argo CD](https://netflixtechblog.com/)
- [Google Config Connector](https://cloud.google.com/config-connector/)
- [AWS ACK](https://github.com/aws-controllers-k8s)
- [Shopify Helm + Argo CD](https://shopify.engineering/)
- [GitLab GitOps](https://docs.gitlab.com/ee/user/clusters/gitops.html)

### 27.13.6 推荐阅读

- [Kubernetes Operators Best Practices](https://github.com/operator-framework/community/blob/master/best-practices.md)
- [Operator Capability Levels](https://operatorhub.io/getting-started)
- [GitOps Principles](https://opengitops.dev/)
- [The Kubebuilder Book](https://book.kubebuilder.io/)
- [Helm Best Practices](https://helm.sh/docs/chart_best_practices/)
- [Argo CD Best Practices](https://argo-cd.readthedocs.io/en/stable/user-guide/best_practices/)
- [Flux CD Best Practices](https://fluxcd.io/flux/guides/)

### 27.13.7 相关章节

- [09-控制器模式.md](./09-控制器模式.md) - Reconcile Loop / Informer / Finalizer 底层
- [12-APIServer与etcd.md](./12-APIServer与etcd.md) - API Server 准入 / etcd 存储
- [17-RBAC与认证授权.md](./17-RBAC与认证授权.md) - Operator 权限
- [20-策略与治理.md](./20-策略与治理.md) - 准入 Webhook / OPA / Kyverno
- [24-集群运维.md](./24-集群运维.md) - 多集群 Operator 管理
- [26-大规模集群优化.md](./26-大规模集群优化.md) - Operator 性能调优
- [28-服务网格与Serverless.md](./28-服务网格与Serverless.md) - Istio Operator / Knative Operator
- [29-故障复盘集.md](./29-故障复盘集.md) - Operator 故障案例
- [30-成本与容量规划.md](./30-成本与容量规划.md) - Operator 资源规划

### 27.13.8 进阶主题

- **Operator Hub**:CNCF 运营的 Operator 市场
- **OLM(Operator Lifecycle Manager)**:Operator 的"应用商店"
- **Carvel(ytt/kapp/imgpkg)**:VMware 开源的 K8s 工具链
- **Terraform Operator**:把 Terraform 暴露成 CRD
- **KRO(Resource Orchestrator)**:AWS 主导的多资源编排
- **KubeVela**:阿里主导的应用交付平台(OAM 模型)
- **Knative Operator**:Serverless 工作负载 Operator
- **KEDA Operator**:事件驱动自动伸缩


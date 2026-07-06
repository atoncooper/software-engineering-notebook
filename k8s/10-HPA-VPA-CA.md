# 10 - HPA / VPA / CA 自动伸缩

> 流量来了扩容,流量走了缩容,节点不够加机器,这是 K8s 弹性的核心。本章讲透 HPA(水平)/ VPA(垂直)/ CA(集群)三种伸缩,以及生产级伸缩策略与陷阱。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- HPA / VPA / CA 各自伸缩什么?何时选哪个?
- HPA v2 的指标体系(CPU / Mem / 自定义)怎么用?
- VPA 为什么生产很少用?有什么风险?
- Cluster Autoscaler 怎么工作?为什么扩节点慢?
- 大规模集群怎么优化伸缩延迟?

### 1.2 不解决什么

- 不讲 KEDA(事件驱动伸缩,见第 28 章)
- 不讲自定义指标部署(Prometheus Adapter,见第 21 章)
- 不讲调度器内部(见第 08 章)

---

## 2. 直觉解释

### 2.1 "公司人力管理"类比

- **HPA**:加人 / 裁人(改副本数)
  - 业务忙 → 多招几个员工
  - 业务闲 → 让部分员工下班
- **VPA**:调薪(改资源 requests/limits)
  - 员工活多 → 加薪水(给更多 CPU/内存)
  - 员工闲 → 降薪水
- **CA**:租办公室(改节点数)
  - 工位不够 → 租新办公室
  - 工位空了 → 退租

### 2.2 三者关系

```
   ┌─────────────────────────────────────────┐
   │ HPA:Pod 数量伸缩                         │
   │  Pod × 3 ←→ Pod × 10                     │
   │  触发:CPU / 内存 / 自定义指标            │
   │  延迟:秒级(创建 Pod)                   │
   └─────────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────┐
   │ VPA:Pod 资源伸缩                         │
   │  cpu 500m ←→ cpu 2000m                   │
   │  触发:历史用量推荐                       │
   │  延迟:分钟级(需重启 Pod)              │
   └─────────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────┐
   │ CA:Node 数量伸缩                         │
   │  Node × 10 ←→ Node × 100                 │
   │  触发:Pod 调度失败                       │
   │  延迟:分钟级(启新机器)                │
   └─────────────────────────────────────────┘
```

---

## 3. 核心概念

### 3.1 HPA(HorizontalPodAutoscaler)

#### 3.1.1 v2 版本(1.23+ 稳定)

```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: web-app
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: web-app
  minReplicas: 3
  maxReplicas: 50
  metrics:
  - type: Resource
    resource:
      name: cpu
      target:
        type: Utilization           # 利用率(requests 的百分比)
        averageUtilization: 70      # 目标 70%
  - type: Resource
    resource:
      name: memory
      target:
        type: Utilization
        averageUtilization: 80
  - type: Pods
    pods:
      metric:
        name: http_requests_per_second
      target:
        type: AverageValue
        averageValue: "1000"        # 每 Pod 1000 QPS
  - type: External
    external:
      metric:
        name: queue_length
        selector:
          matchLabels:
            queue: rabbitmq
      target:
        type: AverageValue
        averageValue: "30"          # 每 Pod 处理 30 条消息
  behavior:
    scaleUp:
      stabilizationWindowSeconds: 0  # 立即扩
      policies:
      - type: Percent
        value: 100                   # 每次最多翻倍
        periodSeconds: 60
      - type: Pods
        value: 4
        periodSeconds: 60
      selectPolicy: Max
    scaleDown:
      stabilizationWindowSeconds: 300  # 5min 内不缩
      policies:
      - type: Percent
        value: 10                     # 每次最多缩 10%
        periodSeconds: 60
```

#### 3.1.2 指标类型

| 类型 | 来源 | 例子 |
|------|------|------|
| **Resource** | metrics-server | CPU / Memory |
| **Pods** | custom.metrics.k8s.io | QPS / 延迟 / 连接数 |
| **External** | external.metrics.k8s.io | 队列长度 / Redis 长度 |
| **ContainerResource** | metrics-server | 容器级 CPU(多容器 Pod) |

#### 3.1.3 计算公式

```
   desiredReplicas = ceil(currentReplicas × (currentMetric / targetMetric))
   
   例子:
   当前 3 副本,CPU 平均 90%,目标 70%
   desired = ceil(3 × 90/70) = ceil(3.86) = 4
```

#### 3.1.4 行为参数

- **stabilizationWindowSeconds**:稳定窗口,避免抖动
  - 扩容:0-60s(快速响应)
  - 缩容:300s+(避免抖动)
- **policies**:扩缩速率限制
  - Percent:百分比(扩 100% = 翻倍)
  - Pods:绝对数(扩 4 个)
- **selectPolicy**:多策略选哪个
  - Max:取最大(更激进)
  - Min:取最小(更保守)
  - Disabled:禁用

### 3.2 VPA(VerticalPodAutoscaler)

#### 3.2.1 三种模式

```yaml
apiVersion: autoscaling.k8s.io/v1
kind: VerticalPodAutoscaler
metadata:
  name: app-vpa
spec:
  targetRef:
    apiVersion: "apps/v1"
    kind: Deployment
    name: my-app
  updatePolicy:
    updateMode: "Auto"           # Off / Initial / Auto
  resourcePolicy:
    containerPolicies:
    - containerName: '*'
      minAllowed:
        cpu: 100m
        memory: 128Mi
      maxAllowed:
        cpu: 4000m
        memory: 8Gi
      controlledResources: ["cpu", "memory"]
```

| 模式 | 行为 |
|------|------|
| **Off** | 仅推荐,不修改 |
| **Initial** | 仅创建时按推荐设置 |
| **Auto** | 运行中也调整(需重启 Pod) |

#### 3.2.2 工作流程

```
   1. VPA Recommender 监控 Pod 资源用量
   2. 计算推荐值(基于历史 P50/P95/P99)
   3. 写入 VPA Status.Recommendation
   4. (Auto 模式)VPA Updater 驱逐 Pod
   5. Pod 重新创建,使用推荐值
```

#### 3.2.3 VPA 风险

- **需要重启 Pod**:K8s 不能在线改 requests,必须重建
- **与 HPA 冲突**:同时改 CPU/内存,HPA 算不准
- **不可预测**:推荐值可能突然跳大,影响调度
- **生产慎用**:多数大厂用 HPA + 资源规划,而非 VPA

### 3.3 CA(Cluster Autoscaler)

#### 3.3.1 工作原理

```
   1. CA Watch Pod 状态
   2. 发现 Pending Pod(调度失败)
   3. 计算需要什么 Node(根据 Pod requests)
   4. 调用云厂商 API 创建 Node
   5. Node 启动,kubelet 注册
   6. scheduler 重新调度 Pending Pod
   7. (缩容)Node 利用率 < 50% → 驱逐 Pod → 删 Node
```

#### 3.3.2 Node Group(节点组)

```yaml
# 阿里云 ACK 节点池
apiVersion: v1
kind: ConfigMap
metadata:
  name: cluster-autoscaler-status
data:
  nodeGroups: |
    - name: online-pool
      minSize: 3
      maxSize: 50
      instanceType: ecs.g6.xlarge
    - name: offline-pool
      minSize: 0
      maxSize: 100
      instanceType: ecs.g6.large
```

#### 3.3.3 扩容延迟分解

```
   T=0     Pod Pending
   T=10s   CA 检测到 Pending(默认 10s 轮询)
   T=30s   CA 调云 API 创建 Node
   T=2min  Node 启动完成(云厂商差异)
   T=2.5min kubelet 注册,Node Ready
   T=3min  scheduler 调度 Pod
   T=3.5min Pod Running(已缓存镜像)
   T=5min  Pod Ready(应用启动)
```

**总延迟**:3-5min(已缓存镜像),未缓存镜像 +2-5min。

#### 3.3.4 缩容策略

```yaml
scaleDown:
  enabled: true
  delayAfterAdd: 10m            # 扩容后 10min 不缩
  delayAfterDelete: 0s
  delayAfterFailure: 3m
  stabilizationWindowSeconds: 300
  utilizationThreshold: 0.5     # 利用率 < 50% 才考虑缩
```

**注意事项**:
- 缩容时驱逐 Pod(遵循 PDB)
- 优先删低优 Pod 多的 Node
- 避免频繁扩缩(flapping)

### 3.4 KEDA(事件驱动伸缩)

```yaml
apiVersion: keda.sh/v1alpha1
kind: ScaledObject
metadata:
  name: kafka-consumer
spec:
  scaleTargetRef:
    name: kafka-consumer
  minReplicaCount: 0            # 可缩到 0
  maxReplicaCount: 100
  pollingInterval: 30
  cooldownPeriod: 300
  triggers:
  - type: kafka
    metadata:
      bootstrapServers: kafka:9092
      consumerGroup: my-group
      topic: my-topic
      lagThreshold: "1000"      # 单分区 lag > 1000 触发扩
```

**优势**:
- 缩到 0(HPA 不行)
- 事件驱动(Kafka / RabbitMQ / Redis / Prometheus / Cron)
- 与 HPA 共存(KEDA 内部用 HPA)

---

## 4. 操作流程与命令

### 4.1 安装 metrics-server

```bash
kubectl apply -f https://github.com/kubernetes-sigs/metrics-server/releases/latest/download/components.yaml

# 验证
kubectl top nodes
kubectl top pods
```

### 4.2 部署 Prometheus Adapter(自定义指标)

```bash
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm install prometheus-adapter prometheus-community/prometheus-adapter \
  -n monitoring \
  --set prometheus.url=http://prometheus.monitoring.svc:9090
```

```yaml
# 自定义指标规则
apiVersion: v1
kind: ConfigMap
metadata:
  name: prometheus-adapter-rules
data:
  http_requests_per_second: |
    - seriesQuery: 'http_requests_total{namespace!="",pod!=""}'
      resources:
        overrides:
          namespace: {resource: "namespace"}
          pod: {resource: "pod"}
      name:
        matches: "^(.*)_total"
        as: "${1}_per_second"
      metricsQuery: 'sum(rate(<<.Series>>{<<.LabelMatchers>>}[2m])) by (<<.GroupBy>>)'
```

### 4.3 部署 Cluster Autoscaler

```bash
# 阿里云 ACK
helm repo add ack https://aliyun-containers.oss-cn-hangzhou.aliyuncs.com/charts
helm install cluster-autoscaler ack/cluster-autoscaler \
  -n kube-system \
  --set clusterName=my-cluster \
  --set region=cn-hangzhou \
  --set nodeSelector."kubernetes\.io/role"=master \
  --set tolerations[0].key=node-role.kubernetes.io/master \
  --set tolerations[0].effect=NoSchedule
```

### 4.4 HPA 排查

```bash
# 看 HPA 状态
kubectl get hpa
kubectl describe hpa web-app

# 看 HPA 计算的指标
kubectl describe hpa web-app | grep -A 10 Metrics

# 看 metrics-server 数据
kubectl top pods -l app=web-app
kubectl top deployment web-app

# 看自定义指标
kubectl get --raw="/apis/custom.metrics.k8s.io/v1beta1/namespaces/production/pods/*/http_requests_per_second"

# 看 CA 状态
kubectl logs -n kube-system cluster-autoscaler-xxx | grep -E "scaleUp|scaleDown"
kubectl get cm cluster-autoscaler-status -n kube-system -o yaml
```

---

## 5. 底层原理

### 5.1 HPA 控制循环

```
   ┌─────────────────────────────────────────┐
   │ HPA Controller (kube-controller-mgr)    │
   │                                          │
   │ 每 15s:                                  │
   │  1. 读 HPA 对象                          │
   │  2. 查 metrics-server / adapter          │
   │  3. 计算目标副本数                       │
   │  4. 应用 behavior 策略                   │
   │  5. 更新 Deployment.spec.replicas        │
   └─────────────────────────────────────────┘
```

### 5.2 指标聚合管道

```
   Pod cAdvisor (kubelet)
       │
       │ /metrics/cadvisor
       ▼
   metrics-server
       │
       │ /apis/metrics.k8s.io/v1beta1
       ▼
   HPA Controller
       │
       │ 计算 desiredReplicas
       ▼
   Deployment Controller
       │
       │ 创建/删除 Pod
       ▼
   实际 Pod 数变化
```

### 5.3 CA 调度模拟

```
   1. CA 发现 Pending Pod
   2. 模拟调度:假设加 Node-A,这些 Pod 能调度吗?
   3. 评估所有 Node Group:
      - online-pool(规格 4C8G):能调 8 Pod/Node
      - offline-pool(规格 2C4G):能调 4 Pod/Node
   4. 选最优 Node Group(成本/性能)
   5. 调云 API 创建 Node
   6. 等 Node Ready(最多 15min,超时失败)
   7. Pod 自动调度
```

### 5.4 缩容决策

```
   1. CA 周期(默认 10s)扫描
   2. 找利用率低的 Node(< 50%)
   3. 模拟:把该 Node 上所有 Pod 重新调度到其他 Node,能成功吗?
   4. 能 → 标记该 Node 为待删除
   5. 驱逐 Pod(遵循 PDB + grace period)
   6. 等 Pod 重新调度
   7. 调云 API 删 Node
```

---

## 6. 配置示例

### 6.1 生产级 HPA(CPU + QPS + 队列)

```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: web-app
  namespace: production
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: web-app
  minReplicas: 6
  maxReplicas: 100
  metrics:
  - type: Resource
    resource:
      name: cpu
      target:
        type: Utilization
        averageUtilization: 60
  - type: Pods
    pods:
      metric:
        name: http_requests_per_second
      target:
        type: AverageValue
        averageValue: "500"
  - type: External
    external:
      metric:
        name: rabbitmq_queue_length
        selector:
          matchLabels:
            queue: web-tasks
      target:
        type: AverageValue
        averageValue: "20"
  behavior:
    scaleUp:
      stabilizationWindowSeconds: 30
      policies:
      - type: Percent
        value: 100
        periodSeconds: 60
      - type: Pods
        value: 10
        periodSeconds: 60
      selectPolicy: Max
    scaleDown:
      stabilizationWindowSeconds: 600
      policies:
      - type: Percent
        value: 10
        periodSeconds: 60
      selectPolicy: Min
```

### 6.2 KEDA(Kafka 消费者)

```yaml
apiVersion: keda.sh/v1alpha1
kind: ScaledObject
metadata:
  name: kafka-consumer
  namespace: production
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: kafka-consumer
  minReplicaCount: 3
  maxReplicaCount: 100
  pollingInterval: 30
  cooldownPeriod: 300
  triggers:
  - type: kafka
    metadata:
      bootstrapServers: kafka.production:9092
      consumerGroup: web-consumer
      topic: web-events
      lagThreshold: "1000"
      offsetResetPolicy: latest
      partitionLimitation: "0,1,2,3"
```

### 6.3 CA 节点池配置(阿里云)

```yaml
# 节点池
apiVersion: v1
kind: ConfigMap
metadata:
  name: cluster-autoscaler-priority
  namespace: kube-system
data:
  priority: |
    online-pool: 50      # 优先扩
    offline-pool: 10     # 备选
    gpu-pool: 100        # GPU 最高优
```

---

## 7. 常见陷阱与调优 ⚠️

### 7.1 HPA 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **没设 requests** | HPA 不工作 | CPU% 基于 requests | 必设 requests |
| **扩容抖动** | 频繁扩缩 | stabilization 短 | 缩容窗口 5min+ |
| **指标延迟** | 扩容滞后 | metrics-server 60s | 用 Pod level 指标 |
| **maxReplicas 不够** | 仍超载 | 容量评估错 | 加大 + 监控 |
| **minReplicas 太低** | 流量突增响应慢 | 冷启动 | minReplicas 留余量 |

### 7.2 VPA 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Pod 重启频繁** | 业务中断 | Auto 模式 | 用 Initial 或 Off |
| **与 HPA 冲突** | HPA 算错 | 同时改资源 | 不要同用 CPU |
| **推荐值跳变** | 资源不稳 | 历史数据少 | 设 minAllowed/maxAllowed |

### 7.3 CA 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **扩容慢** | Pod Pending 几分钟 | Node 启动慢 | 预留节点 / 提前扩 |
| **缩容雪崩** | 节点频繁扩缩 | utilizationThreshold 错 | 调高 + stabilization |
| **节点池选错** | 大 Pod 调度不上 | 选了小规格池 | 按 Pod 大小选池 |
| **云配额满** | 创建 Node 失败 | 配额不足 | 提前申请配额 |

### 7.4 通用陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **PDB 与 CA 冲突** | 缩容卡住 | PDB 阻止驱逐 | PDB minAvailable 适度 |
| **冷启动慢** | 新 Pod 接流量慢 | 应用启动慢 | 预热 + startup probe |
| **指标不可用** | HPA 不工作 | metrics-server 异常 | 监控 + 告警 |

---

## 8. 工业案例与基准数据

### 8.1 阿里:双 11 HPA + CA 实战

**场景**:双 11 期间,Web 应用流量翻 10 倍。

**架构**:
- HPA:CPU 60% 触发,扩容到 1000 副本
- CA:节点池 0-500,自动扩
- KEDA:Kafka 队列驱动(离线消费)

**数据**:
- 流量峰值:1M QPS
- HPA 扩容:3min(从 50 → 1000)
- CA 扩容:5min(从 50 → 200 节点)
- 资源利用率:60-80%(白天)/ 30%(夜间)

**经验**:
1. **minReplicas 留余量**:低峰也保持 50 副本,避免冷启动
2. **节点池预热**:提前 30min 扩容,准备突发
3. **指标用 QPS 而非 CPU**:CPU 滞后,QPS 实时
4. **PDB 严格**:minAvailable=50%,避免缩容雪崩

### 8.2 字节:VPA 在线调优实践

**场景**:某业务 Pod 资源配置不合理,部分浪费,部分 OOM。

**方案**:
- VPA Off 模式(仅推荐)
- 定期 review 推荐值
- 人工调整 requests/limits

**经验**:
- VPA Auto 模式风险大(重启 + 不可预测)
- Off 模式作为推荐工具很有效
- 资源调优后,集群密度提升 30%

### 8.3 Netflix:KEDA 事件驱动

**场景**:视频转码,突发任务。

**方案**:
- KEDA + RabbitMQ 触发
- minReplicaCount=0(无任务时不耗资源)
- maxReplicaCount=500

**数据**:
- 节省成本 60%(无任务时 0 副本)
- 任务响应延迟 < 1min
- 资源利用率提升

### 8.4 各方案延迟基线

| 伸缩方式 | 触发延迟 | 执行延迟 | 总延迟 |
|---------|---------|---------|--------|
| HPA(已缓存镜像) | 15s | 30s-2min | 1-3min |
| HPA(未缓存镜像) | 15s | 5-10min | 5-10min |
| VPA(重启) | 1min | 1-2min | 2-3min |
| CA(已有节点池) | 10s | 1-3min | 1.5-3.5min |
| CA(无节点池) | 10s | 3-5min | 3.5-5.5min |
| KEDA(缩 0 → 1) | 30s | 1-3min | 1.5-3.5min |

---

## 9. 与其他方案的关系

### 9.1 HPA vs VPA

| 维度 | HPA | VPA |
|------|-----|-----|
| 伸缩对象 | Pod 数量 | Pod 资源 |
| 触发 | 实时指标 | 历史推荐 |
| 重启 | 不需要 | 需要(Auto 模式) |
| 生产成熟 | 高 | 中 |
| 与 HPA 共用 | - | 冲突(资源维度) |

### 9.2 K8s CA vs AWS Auto Scaling Group

| 维度 | K8s CA | AWS ASG |
|------|--------|---------|
| 触发 | Pod Pending | CloudWatch 告警 |
| 决策 | 模拟调度 | 阈值 |
| 与 K8s 集成 | 原生 | 需 ALB / EKS 集成 |
| 灵活性 | Pod 级 | EC2 级 |

### 9.3 HPA vs KEDA

| 维度 | HPA | KEDA |
|------|-----|------|
| 缩到 0 | 不行(至少 1) | 行 |
| 触发源 | metrics-server | 任意事件 |
| 实现 | 原生 | 内部用 HPA |
| 复杂度 | 低 | 中 |

---

## 10. 面试速答 ⭐

| 问题 | 一句话答案 |
|------|----------|
| HPA v2 指标类型? | Resource(CPU/Mem) / Pods(自定义) / External(外部) |
| HPA 计算公式? | desiredReplicas = currentReplicas × (currentMetric / targetMetric) |
| HPA 不能缩到 0 吗? | 不能,minReplicas 最少 1;用 KEDA 可缩 0 |
| VPA 为什么生产少用? | 需重启 Pod,与 HPA 冲突,推荐值不可预测 |
| CA 怎么触发扩容? | Pod Pending(调度失败) |
| CA 扩容延迟? | 3-5min(云厂商创建 Node + kubelet 注册) |
| HPA + VPA 共用? | 不要同用 CPU,可共用内存(但建议避免) |
| KEDA 优势? | 缩到 0 + 事件驱动(Kafka / Redis / Cron) |
| stabilizationWindowSeconds? | 稳定窗口,避免扩缩抖动 |
| metrics-server 作用? | 提供 CPU/内存指标给 HPA |

---

## 11. 综合面试题

### 11.1 基础题

**Q1**: HPA / VPA / CA 区别,何时用哪个?

**答题要点**:
- HPA:Pod 数量伸缩,适合流量波动业务(Web / API)
- VPA:Pod 资源伸缩,适合资源配错(应用历史推荐)
- CA:Node 数量伸缩,适合节点不够(整体容量)
- 三者可组合:HPA 扩 Pod → Pod Pending → CA 扩 Node

**Q2**: HPA 怎么计算目标副本数?

**答题要点**:
- 公式:desiredReplicas = ceil(currentReplicas × currentMetric / targetMetric)
- 例子:3 副本,CPU 90%,目标 70% → desired = ceil(3 × 90/70) = 4
- 多指标:取最大值(保守)
- behavior 限制扩缩速率

### 11.2 进阶题

**Q3**: 一个 HPA 不工作,怎么排查?

**答题要点**:
1. `kubectl describe hpa` 看 Conditions
   - ScalingActive=False:指标问题
   - AbleToScale=False:Deployment 问题
2. 看 metrics:`kubectl top pods -l app=xxx`
   - 没数据:metrics-server 异常
3. 检查 requests:HPA CPU% 基于 requests
   - 没设 requests → HPA 算不出 %
4. 看 events
5. 看 HPA 控制器日志

**Q4**: CA 扩容慢,怎么优化?

**答题要点**:
1. **预留节点**:节点池 minSize > 0,常备几个
2. **预缓存镜像**:节点启动时拉关键镜像
3. **节点池预热**:低峰提前扩,准备突发
4. **多节点池**:不同规格,按 Pod 大小选
5. **优先级**:关键业务高优 PriorityClass,CA 优先扩
6. **云厂商优化**:用 Spot Instance + 自定义镜像(预装 kubelet)
7. **Karpenter**(AWS):比 CA 快,直接选最优实例

### 11.3 高级题

**Q5**: 设计一个支持突发流量的弹性方案,要求 5min 内扩容 1000 副本。

**答题要点**:
- **HPA 配置**:
  - maxReplicas: 1000+
  - 扩容策略:Percent 100(翻倍)+ Pods 100
  - stabilization:30s(快速响应)
- **指标选择**:
  - 主:QPS(实时)
  - 辅:CPU(资源)
  - 队列长度(Kafka lag)
- **节点池**:
  - minSize: 50(常备)
  - maxSize: 500
  - 多规格(分散风险)
- **镜像优化**:
  - 预缓存到所有节点
  - 镜像 P2P 分发
- **应用优化**:
  - 启动 < 30s
  - 预热(连接池 / 缓存)
  - startup probe
- **监控**:
  - HPA 扩容延迟
  - Pod 启动延迟
  - 节点扩容延迟
- **预案**:
  - 流量峰值提前 30min 手动扩
  - 监控告警,异常立即介入

**Q6**: VPA 的风险?生产怎么用?

**答题要点**:
- 风险:
  1. 需重启 Pod(K8s 不能在线改 requests)
  2. 与 HPA 同用 CPU 会冲突
  3. 推荐值跳变,影响调度
  4. 资源突然变大,可能调度不上
- 生产用法:
  1. 用 Off 模式(仅推荐,不修改)
  2. 定期 review 推荐值
  3. 人工调整 requests/limits
  4. 配合资源监控,逐步优化
- 替代方案:
  - HPA + 资源规划(根据业务量预估)
  - VPA Initial(仅创建时)

### 11.4 设计题

**Q7**: 设计一个 K8s 集群的弹性伸缩方案,要求成本最优 + 性能保证。

**答题要点**:
- **多层伸缩**:
  - L1:HPA(Pod 数量,秒级)
  - L2:CA(Node 数量,分钟级)
  - L3:KEDA(事件驱动,缩到 0)
- **节点池策略**:
  - 在线池:On-demand 实例,minSize=10,maxSize=200
  - 离线池:Spot 实例,minSize=0,maxSize=500
  - GPU 池:On-demand,高优
- **混部**:
  - 在线 + 离线同节点
  - 在线 Guaranteed,离线 BestEffort
  - 离线可被驱逐
- **指标**:
  - 业务:QPS / 延迟 / 错误率
  - 资源:CPU / 内存 / GPU
  - 队列:Kafka lag / RabbitMQ length
- **成本优化**:
  - Spot 实例(70% 成本节省)
  - KEDA 缩到 0(无任务 0 副本)
  - 节点池分规格(避免大材小用)
  - VPA Off 推荐(资源优化)
- **性能保证**:
  - minReplicas 留余量
  - 节点池预热
  - 关键业务高优
  - 监控告警 + 应急预案

---

## 12. 故障复盘

### 12.1 案例 1:HPA 抖动导致业务不稳

**业务影响**:2023 年某公司,HPA 每分钟扩缩一次,Pod 频繁创建删除,业务 P99 延迟波动大。

**根因**:
- scaleDown.stabilizationWindowSeconds=60(太短)
- scaleUp 立即响应
- 指标小幅波动就触发

**修复过程**:
1. scaleDown.stabilizationWindowSeconds=600(10min)
2. scaleUp.stabilizationWindowSeconds=30
3. 指标平滑(用 rate 2min 而非 1min)

**防范**:
- HPA 配置严格 review
- 监控扩缩频率,异常告警
- 测试环境验证

### 12.2 案例 2:CA 扩容失败导致业务降级

**业务影响**:2022 年某公司,流量峰值,Pod Pending,CA 创建节点失败(云配额满),业务降级 30 分钟。

**根因**:
- 没提前申请云配额
- 监控告警不及时

**修复过程**:
1. 紧急:申请配额(云厂商客服)
2. 等配额生效(15min)
3. CA 扩容,业务恢复

**防范**:
- 提前评估容量,申请配额
- 监控云配额使用率
- 配额接近上限时告警
- 多云备份(配额满了切其他云)

### 12.3 案例 3:VPA Auto 模式导致 Pod 重启风暴

**业务影响**:2024 年某公司,VPA Auto 模式,所有 Pod 都在重启,业务中断。

**根因**:
- VPA 推荐值频繁变化
- Updater 频繁驱逐 Pod
- Pod 还没稳定又被驱逐

**修复过程**:
1. 紧急:停 VPA
2. 改 Off 模式(仅推荐)
3. 人工调整资源

**防范**:
- 不用 VPA Auto 模式
- 用 Off 模式做推荐
- 监控 Pod 重启频率

---

## 13. 参考与延伸

### 13.1 官方文档

- [Horizontal Pod Autoscaling](https://kubernetes.io/docs/tasks/run-application/horizontal-pod-autoscale/)
- [HPA v2 API](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.30/#horizontalpodautoscaler-v2-autoscaling)
- [Vertical Pod Autoscaler](https://github.com/kubernetes/autoscaler/tree/master/vertical-pod-autoscaler)
- [Cluster Autoscaler](https://github.com/kubernetes/autoscaler/tree/master/cluster-autoscaler)
- [KEDA](https://keda.sh/)

### 13.2 工具与项目

- **metrics-server**:资源指标
- **Prometheus Adapter**:自定义指标
- **KEDA**:事件驱动伸缩
- **Karpenter**(AWS):比 CA 更快的节点伸缩
- **Krane**(Shopify):HPA 智能推荐

### 13.3 跨文件链接

- 上一章: [09 - 控制器模式](./09-控制器模式.md)
- 下一章: [11 - 滚动更新与发布策略](./11-滚动更新与发布策略.md)
- 详见: [08 - 调度器](./08-调度器.md) / [21 - 监控与告警](./21-监控与告警.md) / [28 - Operator 与 CRD](./28-Operator与CRD.md)
- 参考平行模块: [分布式系统/负载均衡](../分布式系统/README.md)

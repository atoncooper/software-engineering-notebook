# 04 - Pod 与工作负载

> Pod 是 K8s 的最小调度单元,工作负载(Workload)是 Pod 的"模板 + 副本管理"。本章从 Pod 内核到 5 种 Workload(Deployment / StatefulSet / DaemonSet / Job / CronJob)的全场景覆盖,含生产模板与工业案例。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 为什么 K8s **不直接调度容器**,而要套一层 Pod?
- Pod 的**生命周期、探针、终止、QoS** 怎么工作?
- 5 种 Workload 各自的**适用场景**与**陷阱**?
- 生产环境如何选对 Workload?如何写好 YAML?

### 1.2 不解决什么

- 不讲网络(见第 5 章)
- 不讲存储(见第 6 章)
- 不讲调度细节(见第 8 章)
- 不讲 Operator(见第 28 章)

---

## 2. 直觉解释

### 2.1 "工位 vs 团队"类比

- **容器**:员工,一个独立单元
- **Pod**:工位,一个工位上可以坐多个员工,共享桌面(网络/卷)
- **Workload**:团队组织方式
  - **Deployment**:常规团队,招几个人随时换人
  - **StatefulSet**:有编号的团队(1号、2号),编号稳定,适合主从
  - **DaemonSet**:每个工区都要有一个(每个 Node 一个)
  - **Job**:临时项目组,做完解散
  - **CronJob**:定期项目组,每周一开会

### 2.2 为什么是 Pod 不是容器

```
┌─────────────────────────────────────┐
│              Pod (10.244.0.5)         │
│  ┌────────────┐  ┌──────────────┐  │
│  │ Container A│  │ Container B  │  │
│  │ main app   │  │ sidecar      │  │
│  │ :8080      │  │ log forward  │  │
│  └────────────┘  └──────────────┘  │
│  共享: 网络 (pause 容器) / Volume  │
└─────────────────────────────────────┘
```

- 容器 A 与 B 通过 `localhost` 互通
- 共享 Volume(如 emptyDir / ConfigMap)
- 同生共死(大部分场景)
- **设计模式**:Sidecar / Adapter / Ambassador / Init Container

### 2.3 Pod 的本质

Pod = pause 容器(持有网络命名空间)+ 业务容器(共享 pause 的 netns)

```
   ┌──── Pod ────────────────────────────┐
   │  ┌──────────┐  ┌──────┐  ┌──────┐  │
   │  │  pause   │  │ app  │  │ side │  │
   │  │ (net/IPC)│  │      │  │      │  │
   │  └──────────┘  └──────┘  └──────┘  │
   │       ▲                              │
   │       └── 共享 network / IPC ns     │
   │  cgroup / mount ns 各自独立          │
   └─────────────────────────────────────┘
```

---

## 3. 核心概念:Pod 详解

### 3.1 Pod 的生命周期

#### 3.1.1 Phase(5 种)

| Phase | 含义 | 触发条件 |
|-------|------|---------|
| Pending | 已创建,未运行 | 调度中 / 拉镜像 / 挂卷 |
| Running | 已运行 | 所有容器已创建,至少一个 Running |
| Succeeded | 成功终止 | 所有容器 exit 0(仅 Job) |
| Failed | 失败终止 | 任一容器 exit != 0 |
| Unknown | 状态未知 | apiserver 与 kubelet 失联 |

#### 3.1.2 Container State(3 种)

- **Waiting**:等待启动(拉镜像、下 Secret)
- **Running**:运行中
- **Terminated**:已终止(含 exit code / signal / reason)

#### 3.1.3 Pod Status Conditions

```yaml
status:
  conditions:
  - type: PodScheduled      # 是否已调度
    status: "True"
  - type: Initialized        # init 容器完成
    status: "True"
  - type: ContainersReady    # 所有容器 ready
    status: "True"
  - type: Ready              # Pod ready(含探针)
    status: "True"
```

#### 3.1.4 Pod 生命周期事件时序

```
   T=0    Pod 创建,写入 etcd
          │
   T=0.1  ▼
          scheduler 调度,写 nodeName
          │
   T=0.2  ▼
          kubelet Watch 到 Pod,开始 syncPod
          │
   T=0.5  ▼
          CRI 拉镜像(若未缓存)
          │
   T=2s   ▼
          创建 pause 容器,持有网络命名空间
          │
   T=2.1  ▼
          CNI 配置 Pod 网络(分配 IP、路由)
          │
   T=2.2  ▼
          挂载 Volume(CSI)
          │
   T=2.3  ▼
          依次启动 init 容器(串行)
          │
   T=3s   ▼
          启动主容器 + sidecar(并行)
          │
   T=3.5  ▼
          startup probe 开始
          │
   T=10s  ▼
          startup 通过 → liveness / readiness 开始
          │
   T=11s  ▼
          readiness 通过 → Pod Ready,加入 Service Endpoints
          │
   ...    ▼
          运行期:liveness 失败重启,readiness 失败摘流
          │
   T=N    ▼
          收到 SIGTERM(kubectl delete)
          │
   T=N    ▼
          preStop hook 执行
          │
   T=N+30s ▼
          SIGTERM 给主进程
          │
   T=N+30s ▼
          等待 grace period(默认 30s)
          │
   T=N+60s ▼
          SIGKILL(若仍存活)
          │
   T=N+60s ▼
          Pod 删除,资源回收
```

### 3.2 探针(Probe)详解

#### 3.2.1 三种探针

| 探针 | 作用 | 失败后果 |
|------|------|---------|
| **startup** | 慢启动应用就绪检测 | 失败:重启容器;在 startup 通过前,liveness/readiness 不工作 |
| **liveness** | 健康检查(死锁 / 内存泄漏) | 失败:重启容器 |
| **readiness** | 流量就绪检查(依赖未就绪 / 过载) | 失败:从 Service Endpoints 摘除,**不重启** |

#### 3.2.2 三种检测方式

| 方式 | 实现 | 适用 |
|------|------|------|
| **httpGet** | HTTP GET,2xx/3xx 为成功 | Web 应用 |
| **tcpSocket** | TCP 连接成功 | 数据库 / 缓存 |
| **exec** | 执行命令,exit 0 为成功 | 复杂检测脚本 |

#### 3.2.3 探针配置示例

```yaml
livenessProbe:
  httpGet:
    path: /healthz
    port: 8080
    httpHeaders:
    - name: X-Custom
      value: probe
  initialDelaySeconds: 30      # 启动后 30s 开始
  periodSeconds: 10            # 每 10s 一次
  timeoutSeconds: 5            # 超时 5s
  successThreshold: 1          # 成功 1 次判定 healthy
  failureThreshold: 3          # 连续失败 3 次判定 unhealthy
```

#### 3.2.4 探针陷阱

| 陷阱 | 现象 | 解决 |
|------|------|------|
| **liveness 与 readiness 用同一接口** | 应用过载被重启 | liveness 检内部(死锁),readiness 检依赖 |
| **initialDelay 太短** | 启动慢的应用被误杀 | 用 startup probe 代替 |
| **failureThreshold 太小** | 网络抖动触发重启 | 设为 5-10 |
| **timeoutSeconds 太短** | 接口慢误判 | 设为 5-10s |
| **exec 探针太重** | 探针占 CPU | 改 httpGet |

### 3.3 QoS(Quality of Service)

K8s 根据 requests / limits 把 Pod 分 3 个 QoS 等级:

| QoS | 条件 | 用途 |
|-----|------|------|
| **Guaranteed** | requests = limits(CPU/Mem 都设) | 最高优先级,最后驱逐 |
| **Burstable** | 部分设 requests/limits | 中等优先级 |
| **BestEffort** | 都不设 | 最低优先级,最先驱逐 |

```yaml
# Guaranteed
resources:
  requests:
    cpu: 500m
    memory: 512Mi
  limits:
    cpu: 500m
    memory: 512Mi

# Burstable
resources:
  requests:
    cpu: 100m
  limits:
    cpu: 500m

# BestEffort
# 不设 resources
```

**驱逐顺序**:BestEffort → Burstable(超 request 多的)→ Guaranteed(超 request 多的)

### 3.4 restartPolicy

| 值 | 含义 | 适用 |
|----|------|------|
| Always | 容器退出后总重启(默认) | Deployment / StatefulSet / DaemonSet |
| OnFailure | 非 0 退出才重启 | Job |
| Never | 不重启 | Job(手动 / 一次性) |

### 3.5 terminationGracePeriodSeconds

```yaml
spec:
  terminationGracePeriodSeconds: 60   # 默认 30s
  containers:
  - name: app
    lifecycle:
      preStop:
        exec:
          command: ["/bin/sh", "-c", "sleep 15 && nginx -s quit"]
```

**优雅终止流程**:
1. preStop hook 执行(15s sleep + nginx 优雅关)
2. SIGTERM 给主进程
3. 等待 terminationGracePeriodSeconds(60s)
4. 超时发 SIGKILL

---

## 4. 五种工作负载详解

### 4.1 Deployment

**定位**:无状态应用,水平扩展,滚动更新。

#### 4.1.1 完整生产模板

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: web-app
  namespace: production
  labels:
    app: web-app
    tier: frontend
spec:
  replicas: 6
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 1                # 滚动时多出 1 个
      maxUnavailable: 0          # 滚动时不允许少(零停机)
  revisionHistoryLimit: 10       # 保留 10 个旧 ReplicaSet
  progressDeadlineSeconds: 600   # 600s 未完成视为失败
  selector:
    matchLabels:
      app: web-app
  template:
    metadata:
      labels:
        app: web-app
        tier: frontend
    spec:
      affinity:
        podAntiAffinity:
          preferredDuringSchedulingIgnoredDuringExecution:
          - weight: 100
            podAffinityTerm:
              labelSelector:
                matchLabels:
                  app: web-app
              topologyKey: kubernetes.io/hostname
      containers:
      - name: web
        image: registry.example.com/web-app:v1.5.0
        imagePullPolicy: IfNotPresent
        ports:
        - name: http
          containerPort: 8080
        env:
        - name: ENV
          value: production
        - name: DB_HOST
          valueFrom:
            secretKeyRef:
              name: db-secret
              key: host
        resources:
          requests:
            cpu: 500m
            memory: 512Mi
          limits:
            cpu: 1000m
            memory: 1Gi
        startupProbe:
          httpGet:
            path: /startup
            port: http
          failureThreshold: 30
          periodSeconds: 10
        livenessProbe:
          httpGet:
            path: /healthz
            port: http
          periodSeconds: 10
          timeoutSeconds: 5
          failureThreshold: 3
        readinessProbe:
          httpGet:
            path: /ready
            port: http
          periodSeconds: 5
          failureThreshold: 3
        lifecycle:
          preStop:
            exec:
              command: ["/bin/sh", "-c", "sleep 15 && nginx -s quit"]
        securityContext:
          allowPrivilegeEscalation: false
          readOnlyRootFilesystem: true
          runAsNonRoot: true
          runAsUser: 1000
          capabilities:
            drop: [ALL]
      terminationGracePeriodSeconds: 60
      imagePullSecrets:
      - name: registry-credentials
```

#### 4.1.2 滚动更新机制

```
   旧版本 v1: [P1] [P2] [P3] [P4] [P5] [P6]
                            │
   T=0    kubectl set image deployment/web-app web=web-app:v1.6.0
                            │
   T=1s   ▼
          Deployment Controller 创建新 ReplicaSet(v1.6.0),副本数 0
                            │
   T=2s   ▼
          新 RS 扩到 1(maxSurge=1 + maxUnavailable=0)
          [P7 v1.6.0]
                            │
   T=5s   ▼
          P7 Ready → 旧 RS 缩到 5
          [P1 v1] [P2 v1] [P3 v1] [P4 v1] [P5 v1] [P7 v1.6]
                            │
   T=10s  ▼
          新 RS 扩到 2,旧 RS 缩到 4
          ... 重复 ...
                            │
   T=60s  ▼
          最终:[P7-P12 全是 v1.6.0]
```

#### 4.1.3 回滚

```bash
# 查看历史版本
kubectl rollout history deployment/web-app

# 查看具体版本
kubectl rollout history deployment/web-app --revision=2

# 回滚到上一版本
kubectl rollout undo deployment/web-app

# 回滚到指定版本
kubectl rollout undo deployment/web-app --to-revision=2

# 暂停滚动(可用于分批)
kubectl rollout pause deployment/web-app

# 恢复滚动
kubectl rollout resume deployment/web-app

# 查看状态
kubectl rollout status deployment/web-app
```

### 4.2 StatefulSet

**定位**:有状态应用,稳定网络标识 + 稳定存储 + 有序部署。

#### 4.2.1 适用场景

- 数据库(MySQL / PostgreSQL 主从)
- 消息队列(Kafka / RabbitMQ)
- 分布式存储(Elasticsearch / etcd / Consul)
- 任何需要"稳定身份"的应用

#### 4.2.2 完整生产模板

```yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: mysql
  namespace: production
spec:
  serviceName: mysql          # 必须配 Headless Service
  replicas: 3
  selector:
    matchLabels:
      app: mysql
  template:
    metadata:
      labels:
        app: mysql
    spec:
      affinity:
        podAntiAffinity:
          requiredDuringSchedulingIgnoredDuringExecution:
          - labelSelector:
              matchLabels:
                app: mysql
            topologyKey: kubernetes.io/hostname
      containers:
      - name: mysql
        image: mysql:8.0
        ports:
        - name: mysql
          containerPort: 3306
        env:
        - name: MYSQL_ROOT_PASSWORD
          valueFrom:
            secretKeyRef:
              name: mysql-secret
              key: root-password
        - name: POD_NAME
          valueFrom:
            fieldRef:
              fieldPath: metadata.name
        - name: POD_ORDINAL
          valueFrom:
            fieldRef:
              fieldPath: metadata.annotations['podOrdinal']
        - name: NODE_ID
          value: "$(POD_ORDINAL)"
        volumeMounts:
        - name: data
          mountPath: /var/lib/mysql
        - name: config
          mountPath: /etc/mysql/conf.d
        livenessProbe:
          exec:
            command: ["mysqladmin", "ping", "-h", "localhost"]
          initialDelaySeconds: 30
          periodSeconds: 10
        readinessProbe:
          exec:
            command: ["mysql", "-h", "127.0.0.1", "-e", "SELECT 1"]
          initialDelaySeconds: 5
          periodSeconds: 2
  volumeClaimTemplates:        # 每个 Pod 独立 PVC
  - metadata:
      name: data
    spec:
      accessModes: [ "ReadWriteOnce" ]
      storageClassName: ssd
      resources:
        requests:
          storage: 100Gi
  - metadata:
      name: config
    spec:
      accessModes: [ "ReadWriteOnce" ]
      storageClassName: ssd
      resources:
        requests:
          storage: 1Gi
---
apiVersion: v1
kind: Service
metadata:
  name: mysql
  namespace: production
spec:
  clusterIP: None              # Headless
  selector:
    app: mysql
  ports:
  - port: 3306
    name: mysql
```

#### 4.2.3 关键特性

1. **稳定网络标识**:Pod 名 `mysql-0 / mysql-1 / mysql-2`,DNS `mysql-0.mysql.production.svc.cluster.local`
2. **稳定存储**:每个 Pod 独立 PVC(`data-mysql-0` / `data-mysql-1`),Pod 重新调度仍挂同 PVC
3. **有序部署**:0 → 1 → 2(串行),前一个 Ready 才创建下一个
4. **有序删除**:2 → 1 → 0(逆序)
5. **滚动更新**:逆序更新,2 → 1 → 0,可配 `partition` 实现灰度

#### 4.2.4 partition 灰度

```yaml
spec:
  updateStrategy:
    type: RollingUpdate
    rollingUpdate:
      partition: 2    # 只有 ordinal >= 2 的 Pod 更新
```

### 4.3 DaemonSet

**定位**:每个 Node 一个 Pod,适合节点级守护进程。

#### 4.3.1 适用场景

- 日志采集(Fluent Bit / Filebeat)
- 监控代理(Node Exporter / Prometheus Agent)
- 网络插件(Calico / Cilium)
- 存储插件(CSI Node)
- 入口代理(Nginx Ingress Controller)

#### 4.3.2 完整生产模板

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: node-exporter
  namespace: monitoring
  labels:
    app: node-exporter
spec:
  selector:
    matchLabels:
      app: node-exporter
  updateStrategy:
    type: RollingUpdate
    rollingUpdate:
      maxUnavailable: 10%      # 单批最多 10% 节点同时更新
  template:
    metadata:
      labels:
        app: node-exporter
    spec:
      hostNetwork: true         # 用主机网络,性能优
      hostPID: true
      nodeSelector:
        kubernetes.io/os: linux
      tolerations:
      - key: node-role.kubernetes.io/control-plane
        operator: Exists
        effect: NoSchedule
      containers:
      - name: node-exporter
        image: prom/node-exporter:v1.7.0
        args:
        - --path.procfs=/host/proc
        - --path.sysfs=/host/sys
        - --path.rootfs=/host/root
        - --collector.filesystem.mount-points-exclude=^/(dev|proc|sys|var/lib/docker/.+)($|/)
        ports:
        - name: metrics
          containerPort: 9100
          hostPort: 9100
        resources:
          requests:
            cpu: 50m
            memory: 64Mi
          limits:
            cpu: 200m
            memory: 128Mi
        volumeMounts:
        - name: proc
          mountPath: /host/proc
          readOnly: true
        - name: sys
          mountPath: /host/sys
          readOnly: true
        - name: root
          mountPath: /host/root
          readOnly: true
      volumes:
      - name: proc
        hostPath:
          path: /proc
      - name: sys
        hostPath:
          path: /sys
      - name: root
        hostPath:
          path: /
```

### 4.4 Job

**定位**:一次性任务,成功完成即退出。

#### 4.4.1 完整模板

```yaml
apiVersion: batch/v1
kind: Job
metadata:
  name: data-migration
  namespace: production
spec:
  completions: 5              # 总共成功 5 个 Pod
  parallelism: 2              # 并发 2 个
  backoffLimit: 3             # 失败重试 3 次
  activeDeadlineSeconds: 3600 # 1h 超时
  ttlSecondsAfterFinished: 86400  # 完成后保留 1 天
  template:
    spec:
      restartPolicy: OnFailure
      containers:
      - name: migration
        image: migration-tool:v1.2.0
        command: ["/migrate.sh"]
        env:
        - name: BATCH_ID
          value: "2026-07-05"
        resources:
          requests:
            cpu: 1000m
            memory: 2Gi
          limits:
            cpu: 2000m
            memory: 4Gi
```

#### 4.4.2 模式

| 模式 | completions | parallelism | 适用 |
|------|------------|------------|------|
| 单任务 | 1 | 1 | 一次性迁移 |
| 并发任务 | N | M | 批量处理 |
| 工作队列 | 1 | N | Redis 队列消费(N 个 Pod 共同消费) |

### 4.5 CronJob

**定位**:定时任务,cron 表达式触发。

```yaml
apiVersion: batch/v1
kind: CronJob
metadata:
  name: backup-database
  namespace: production
spec:
  schedule: "0 2 * * *"                   # 每天凌晨 2 点
  timeZone: Asia/Shanghai
  startingDeadlineSeconds: 200            # 错过 200s 不执行
  concurrencyPolicy: Forbid               # 不允许并发
  successfulJobsHistoryLimit: 3
  failedJobsHistoryLimit: 1
  jobTemplate:
    spec:
      backoffLimit: 1
      activeDeadlineSeconds: 3600
      template:
        spec:
          restartPolicy: OnFailure
          containers:
          - name: backup
            image: backup-tool:v1.0
            command: ["/backup.sh"]
```

**concurrencyPolicy**:
- Allow(默认):允许并发
- Forbid:禁止并发(上次没跑完,跳过这次)
- Replace:杀掉上次,跑这次

---

## 5. 底层原理

### 5.1 Deployment Controller 与 ReplicaSet 的协作

```
   ┌─────────────────────────────────────────┐
   │ Deployment Controller                   │
   │  Watch Deployment 变化                  │
   │  ↓                                       │
   │  计算 desired ReplicaSet 状态           │
   │  ↓                                       │
   │  创建/更新 ReplicaSet(通过 apiserver) │
   └─────────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────┐
   │ ReplicaSet Controller                   │
   │  Watch ReplicaSet 变化                  │
   │  ↓                                       │
   │  计算当前 Pod 数 vs desired             │
   │  ↓                                       │
   │  创建/删除 Pod(通过 apiserver)         │
   └─────────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────┐
   │ kube-scheduler                          │
   │  Watch Pod (nodeName="")                │
   │  ↓                                       │
   │  Filter + Score + Bind                  │
   └─────────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────┐
   │ kubelet (目标 Node)                     │
   │  Watch Pod (nodeName=本机)              │
   │  ↓                                       │
   │  syncPod: CRI / CNI / CSI               │
   └─────────────────────────────────────────┘
```

### 5.2 StatefulSet 的有序性实现

```
   1. StatefulSet Controller Watch 到新 StatefulSet
   2. 创建 Pod mysql-0
   3. 等 Pod mysql-0 status.ready=True(通过 apiserver Watch)
   4. 创建 Pod mysql-1
   5. 等 Pod mysql-1 Ready
   6. 创建 Pod mysql-2
   ...
```

**关键**:
- Pod 名带 ordinal(0、1、2...)
- 每个 Pod 有独立 PVC(由 volumeClaimTemplates 创建)
- PVC 名:`<pvc-template-name>-<pod-name>`,如 `data-mysql-0`

### 5.3 Pod 启动顺序(syncPod 内部)

```
1. 计算 Pod 是否需要 sync(状态变化)
2. 创建 Pod sandbox(pause 容器,持有 netns)
3. 配置网络(CNI)
4. 挂载 Volume(CSI)
5. 串行启动 init 容器(每个完成才下一个)
6. 并行启动业务容器
7. 启动探针检测
8. 上报 Pod status
```

### 5.4 Pod 优雅终止的完整流程

```
   T=0    apiserver 收到 DELETE,Pod metadata.deletionTimestamp 标记
          │
   T=0    ▼
          kubelet Watch 到 deletionTimestamp
          │
   T=0    ▼
          触发 killPod:
          1. 执行 preStop hook(同步等待,最多 terminationGracePeriodSeconds)
          2. 发 SIGTERM 给主进程
          3. 等待 grace period(默认 30s)
          4. 超时发 SIGKILL
          │
   T=0    ▼
          Endpoint Controller Watch 到 Pod terminating
          从 EndpointSlice 移除该 Pod
          │
   T=0.5  ▼
          kube-proxy Watch EndpointSlice
          更新 iptables/IPVS 规则,新流量不再来
          │
   T=1s   ▼
          已有连接的处理:
          - 应用层应主动关闭(SIGTERM 后 nginx -s quit)
          - 长连接等待 conntrack 超时(默认 5min)
          │
   T=30s  ▼
          Pod 真正删除,kubelet 清理容器
```

---

## 6. 配置示例:Pod 设计模式

### 6.1 Sidecar 模式

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: web-with-logging
spec:
  containers:
  - name: nginx
    image: nginx
    volumeMounts:
    - name: logs
      mountPath: /var/log/nginx
  - name: log-forwarder
    image: fluent-bit
    volumeMounts:
    - name: logs
      mountPath: /var/log/nginx
      readOnly: true
  volumes:
  - name: logs
    emptyDir: {}
```

### 6.2 Init Container 模式

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: app-with-init
spec:
  initContainers:
  - name: wait-for-db
    image: busybox
    command: ['sh', '-c', 'until nc -z db 3306; do sleep 2; done']
  - name: init-config
    image: busybox
    command: ['sh', '-c', 'echo "config" > /config/app.conf']
    volumeMounts:
    - name: config
      mountPath: /config
  containers:
  - name: app
    image: my-app
    volumeMounts:
    - name: config
      mountPath: /etc/app
  volumes:
  - name: config
    emptyDir: {}
```

### 6.3 Ambassador 模式(代理外部服务)

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: app-with-proxy
spec:
  containers:
  - name: app
    image: my-app
    env:
    - name: REDIS_URL
      value: "localhost:6379"   # 通过 sidecar 代理
  - name: redis-proxy
    image: envoy
    # Envoy 代理到真实 Redis 集群
```

### 6.4 Adapter 模式(标准化输出)

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: app-with-adapter
spec:
  containers:
  - name: legacy-app
    image: legacy-app            # 输出非标准日志格式
  - name: log-adapter
    image: logfmt-adapter        # 转换为标准 JSON 日志
```

---

## 7. 常见陷阱与调优 ⚠️

### 7.1 Pod 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Pod 卡 Pending** | 调度失败 | 资源不足 / 污点 / 亲和性 | describe pod 看 Events |
| **Pod 卡 ContainerCreating** | 创建中 | 拉镜像慢 / 挂卷失败 / CNI 错 | describe pod + 看 kubelet 日志 |
| **Pod CrashLoopBackOff** | 反复重启 | 应用启动失败 | 看容器日志 + liveness 配置 |
| **Pod ImagePullBackOff** | 拉镜像失败 | 镜像不存在 / 鉴权失败 | 检查 imagePullSecrets |
| **Pod 卡 Terminating** | 删不掉 | finalizer / kubelet 死锁 | 检查 finalizer,必要时重启 kubelet |
| **OOPS OOMKilled** | 容器被杀 | limits.memory 太低 | 调高或排查内存泄漏 |

### 7.2 Deployment 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **滚动更新有损** | 部分请求 5xx | maxUnavailable > 0 或 readiness 错 | maxUnavailable=0,严格 readiness |
| **滚动卡住** | 长时间不完成 | 新 Pod 不 Ready | 检查 readiness / 资源 / 镜像 |
| **revisionHistoryLimit 太大** | etcd 压力 | 保留太多旧 RS | 设为 5-10 |
| **回滚丢配置** | 回滚后 ConfigMap 未回退 | ConfigMap 不在 revision 里 | 配置变更单独走 GitOps |

### 7.3 StatefulSet 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Pod 卡 Pending** | 0 创建后 1 不创建 | PVC pending / 资源不足 | 检查 PVC / 节点资源 |
| **滚动更新慢** | 串行更新,耗时长 | 默认策略 OrderedReady | 改 parallelism > 1(慎用) |
| **数据不一致** | 主从不同步 | 应用未正确处理主从切换 | 应用层处理 + partition 灰度 |
| **PVC 残留** | StatefulSet 删了 PVC 还在 | 默认不删 PVC | 手动清理或配置 retentionPolicy |

### 7.4 DaemonSet 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **更新炸全集群** | 一次性更新所有 Node | maxUnavailable=100% | 设 maxUnavailable=10% |
| **nodeSelector 太严** | 部分 Node 没有 | 节点标签变化 | 定期巡检节点标签 |

### 7.5 Job 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Job 永不完成** | 持续运行 | 主进程不退出 | 设 activeDeadlineSeconds |
| **失败重试太多** | 浪费资源 | backoffLimit 太大 | 设 1-3 |
| **Job 残留** | 大量历史 Job | ttlSecondsAfterFinished 未设 | 设 86400(1 天) |

---

## 8. 工业案例与基准数据

### 8.1 阿里巴巴:超大规模 Pod 调度

**场景**:双 11 期间,Pod 数从 100k 扩到 300k。

**经验**:
1. **Pod 启动加速**:
   - 镜像 P2P 分发(ImageFS),5000 节点并发拉镜像 < 5min
   - 镜像预拉(节点启动时拉关键镜像)
   - Pod 启动模板优化(去掉不必要的 init container)
2. **QoS 分级**:
   - Guaranteed:核心业务(支付、订单)
   - Burstable:一般业务
   - BestEffort:离线计算
3. **混部**:
   - 在线 + 离线混部,白天在线 / 夜间离线
   - 离线 Pod 优先级低,BestEffort,资源紧张时驱逐
4. **资源利用率**:
   - 自建:平均 CPU 20-30%
   - 混部后:平均 CPU 50-60%
   - 双 11 峰值:80%+

### 8.2 字节:StatefulSet 大规模 Kafka

**场景**:Kafka 集群 100+ broker,StatefulSet 管理。

**经验**:
1. **partition 灰度**:
   - `partition: 99`,只更新 ordinal >= 99(只更新 1 个)
   - 验证 1 小时后再 partition: 98,逐个推进
2. **PodDisruptionBudget**:
   - `minAvailable: 95%`,防止 evict 过多
3. **优雅终止**:
   - preStop:通知 Kafka controller 该 broker 下线
   - grace period: 300s(等数据迁移)
4. **跨机房**:
   - StatefulSet 跨 AZ 调度(nodeAffinity)
   - 每个 broker 副本在不同 AZ

### 8.3 Google:Pod 启动优化

**GKE Autopilot 数据**:
- Pod 启动 P50:2.5s
- Pod 启动 P99:8s
- 镜像缓存命中率:95%

**优化点**:
1. 镜像自动预拉(基于历史数据预测)
2. Containerd 优化(snapshotter = stargz,延迟加载)
3. CNI 优化(Cilium eBPF,无 iptables)
4. 调度器优化(scheduling framework + cache)

### 8.4 Netflix:DaemonSet 监控代理

**场景**:每 Node 一个 Atlas 代理,采集指标。

**经验**:
1. **资源限制严格**:CPU 100m / Mem 256Mi,避免影响业务
2. **hostNetwork: true**:性能优先
3. **更新分批**:maxUnavailable=5%,跨 AZ 滚动
4. **配置热更新**:ConfigMap + signal SIGHUP,不重启 Pod

---

## 9. 与其他方案的关系

### 9.1 K8s Workload vs Docker Compose

| 维度 | K8s Workload | Docker Compose |
|------|-------------|----------------|
| 多机 | 是 | 否(Swarm 才行) |
| 副本管理 | ReplicaSet / StatefulSet | replicas |
| 滚动更新 | Deployment 原生 | 手动 |
| 状态管理 | StatefulSet 稳定标识 | 弱 |
| 网络抽象 | Service + DNS | links |
| 适合 | 生产集群 | 单机开发 |

### 9.2 StatefulSet vs Operator

| 维度 | StatefulSet | Operator |
|------|------------|---------|
| 复杂度 | 中 | 高(写代码) |
| 适合 | 简单主从 | 复杂分布式(集群管理) |
| 自动化 | 部署 / 扩缩 | 备份 / 恢复 / 升级 / 故障切换 |
| 例子 | MySQL 主从 | TiDB Operator / Kafka Operator |

**经验**:简单场景用 StatefulSet,复杂运维用 Operator(Operator 内部通常用 StatefulSet 管理 Pod)。

---

## 10. 面试速答 ⭐

| 问题 | 一句话答案 |
|------|----------|
| 为什么 K8s 用 Pod 而不是容器? | Pod 提供共享网络/卷的容器组,支持 sidecar 模式,且 pause 容器持有 netns 简化生命周期 |
| Deployment 与 ReplicaSet 关系? | Deployment 创建/管理 ReplicaSet,ReplicaSet 管理 Pod,Deployment 通过 RS 实现滚动 |
| StatefulSet 与 Deployment 区别? | StatefulSet 有稳定网络标识 + 独立 PVC + 有序部署,适合有状态应用 |
| Pod 的 QoS 三级? | Guaranteed(requests=limits) > Burstable(部分设) > BestEffort(不设) |
| liveness vs readiness? | liveness 失败重启容器,readiness 失败摘流量不重启 |
| 滚动更新如何零停机? | maxSurge=1 + maxUnavailable=0 + readiness probe + preStop + grace period |
| Pod 卡 ContainerCreating 怎么排查? | describe pod 看 Events,常见:镜像拉不下 / 挂卷失败 / CNI 错 |
| StatefulSet 滚动怎么灰度? | updateStrategy.partition=N,只更新 ordinal >= N 的 Pod |
| Pod 终止流程? | preStop hook → SIGTERM → 等 grace period(默认 30s) → SIGKILL |
| Job 与 CronJob 区别? | Job 一次性,CronJob 定时触发创建 Job |

---

## 11. 综合面试题

### 11.1 基础题

**Q1**: 描述 Pod 的生命周期和 5 个 Phase。

**答题要点**:
- Pending:已创建,未运行(调度中 / 拉镜像 / 挂卷)
- Running:容器已创建,至少一个运行
- Succeeded:所有容器 exit 0(仅 Job)
- Failed:任一容器 exit != 0
- Unknown:kubelet 失联

**Q2**: 解释三种探针的区别和适用场景。

**答题要点**:
- startup:慢启动应用就绪,在通过前 liveness/readiness 不工作
- liveness:健康检查,失败重启容器(死锁 / 内存泄漏)
- readiness:流量就绪,失败摘流量不重启(依赖未就绪 / 过载)
- 三种探针可分别用 httpGet / tcpSocket / exec

### 11.2 进阶题

**Q3**: 写一个零停机滚动的 Deployment,关键配置是什么?

**答题要点**:
- strategy.rollingUpdate.maxSurge=1, maxUnavailable=0
- readiness probe 严格(确保新 Pod 真就绪才进流量)
- preStop hook(等连接排空)
- terminationGracePeriodSeconds 足够长(60s+)
- 健康检查接口与业务接口分离
- 资源 requests/limits 合理(避免调度失败)

**Q4**: StatefulSet 怎么实现稳定网络标识?

**答题要点**:
- Pod 名:`<statefulset-name>-<ordinal>`,如 mysql-0
- Headless Service(clusterIP=None)为每个 Pod 创建 A 记录
- DNS:`<pod-name>.<service-name>.<namespace>.svc.cluster.local`
- Pod 重新调度,名字不变,DNS 不变
- 配合 PVC(`<pvc-template>-<pod-name>`),存储也稳定

**Q5**: 一个 Deployment 滚动更新卡住了,怎么排查?

**答题要点**:
1. `kubectl rollout status deployment/xxx` 看进度
2. `kubectl get rs` 看新旧 RS 副本数
3. `kubectl describe deployment xxx` 看 Events
4. 检查新 Pod 是否 Ready:
   - `kubectl get pods` 看新 Pod 状态
   - 不 Ready:describe pod 看 Events(镜像 / 资源 / 探针)
5. 检查 progressDeadlineSeconds(默认 600s,超时视为失败)
6. 检查 PodDisruptionBudget 是否阻止
7. 解决后 `kubectl rollout resume`

### 11.3 高级题

**Q6**: 设计一个 K8s 上的 MySQL 主从集群。

**答题要点**:
- StatefulSet + Headless Service
- 3 副本:mysql-0(主) / mysql-1 / mysql-2(从)
- 每个 Pod 独立 PVC(volumeClaimTemplates)
- Pod 间反亲和(跨 Node / AZ)
- 通过 POD_ORDINAL 判断角色:
  - ordinal=0:主,read-write
  - ordinal>0:从,read-only,从主同步
- 初始化脚本(init container):
  - ordinal=0:初始化为主
  - ordinal>0:从主同步数据,配置为从
- failover:
  - 主挂 → StatefulSet 重建 mysql-0
  - 应用通过 Service 读写,Service 区分读写
  - 或用 Operator(如 Oracle MySQL Operator)自动 failover
- 备份:CronJob 定期 mysqldump 到 S3

**Q7**: Pod 频繁 OOMKilled,怎么排查?

**答题要点**:
1. `kubectl describe pod` 看 Last State.OOMKilled
2. 看 limits.memory 是否合理(应用真实需求)
3. 监控应用内存:
   - Prometheus 进程内存(RSS)
   - heap dump(Java jmap / Go pprof)
4. 区分:
   - limits 太低:调高
   - 内存泄漏:应用层修复
   - 突发流量:加 HPA + 调高 limits
5. 临时缓解:
   - 加 swap(不推荐,K8s 默认禁用)
   - 加 memory request(优先级高)
   - 重启策略 Always(自动恢复)

### 11.4 设计题

**Q8**: 设计一个批处理系统,处理 1M 张图片的缩略图生成。

**答题要点**:
- 架构:
  ```
  Redis 队列 → Job Worker (Job with parallelism) → S3 输出
  ```
- Job 配置:
  - completions: 1(工作队列模式)
  - parallelism: 100(并发 100)
  - backoffLimit: 3
  - activeDeadlineSeconds: 7200(2h 超时)
  - restartPolicy: OnFailure
- Worker 逻辑:
  - while True:从 Redis pop 任务,处理,无任务退出
  - 退出后 Job 完成
- 扩缩:
  - KEDA 基于 Redis 队列长度自动扩缩 Job 副本
- 监控:
  - 处理速率 / 队列长度 / 失败率
- 优化:
  - 镜像预拉
  - spot 实例(成本低)
  - 优雅终止(处理完当前任务再退出)

---

## 12. 故障复盘

### 12.1 案例 1:滚动更新导致 5xx 错误率上升

**业务影响**:2023 年某公司滚动更新 web 应用,5xx 错误率从 0.01% 升到 5%,持续 2 分钟。

**根因**:
- maxUnavailable=1(默认 25%),导致滚动时少一个 Pod
- preStop 未配,Pod 收到 SIGTERM 立即退出
- 长连接(WebSocket)被打到已终止的 Pod

**修复过程**:
1. maxUnavailable=0(零停机)
2. preStop: sleep 15 + nginx -s quit
3. terminationGracePeriodSeconds=60
4. readiness probe 严格(/ready 接口检查依赖)

**防范**:
- 滚动配置严格 review
- 灰度发布(canary)先放 5% 流量
- 监控 5xx 错误率,异常立即 rollback

### 12.2 案例 2:StatefulSet PVC 残留导致数据错乱

**业务影响**:2022 年某公司删除 StatefulSet(MySQL),PVC 残留,重新创建 StatefulSet 后 mysql-0 挂载旧 PVC,数据错乱。

**根因**:
- StatefulSet 默认不删 PVC(数据安全考虑)
- 重新创建后,PVC 名匹配,挂到 mysql-0
- 但 mysql-0 之前是主,现在是新集群,数据不一致

**修复过程**:
1. 紧急:停所有写,数据修复
2. 删除残留 PVC
3. 重新初始化

**防范**:
- 删 StatefulSet 前先确认是否保留数据
- 用 `persistentVolumeClaimRetentionPolicy`(1.27+):
  ```yaml
  persistentVolumeClaimRetentionPolicy:
    whenDeleted: Delete
    whenScaled: Retain
  ```
- 生产环境用 Operator(MySQL Operator),自动管理 PVC

### 12.3 案例 3:CronJob 漏执行导致日报缺失

**业务影响**:2024 年某公司,CronJob 每天凌晨生成日报,某天因节点资源紧张,Job 调度失败,日报缺失,业务方未及时收到。

**根因**:
- startingDeadlineSeconds 未设,Job 错过窗口不报错
- 失败的 Job 未告警
- concurrencyPolicy: Forbid,上次没跑完这次跳过

**修复过程**:
1. 紧急:手动触发 Job
2. 加告警:CronJob 失败 / 错过 → 立即告警
3. 配置 startingDeadlineSeconds: 200
4. 加监控:CronJob 上次成功时间 > 25h → 告警

**防范**:
- 关键 CronJob 配告警
- 重要业务用工作流引擎(Argo Workflows / Airflow),而非裸 CronJob
- 失败重试 + 通知(Slack / 钉钉)

---

## 13. 参考与延伸

### 13.1 官方文档

- [Pods](https://kubernetes.io/docs/concepts/workloads/pods/)
- [Pod Lifecycle](https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/)
- [Deployments](https://kubernetes.io/docs/concepts/workloads/controllers/deployment/)
- [StatefulSets](https://kubernetes.io/docs/concepts/workloads/controllers/statefulset/)
- [DaemonSets](https://kubernetes.io/docs/concepts/workloads/controllers/daemonset/)
- [Jobs](https://kubernetes.io/docs/concepts/workloads/controllers/job/)
- [CronJobs](https://kubernetes.io/docs/concepts/workloads/controllers/cron-jobs/)
- [Configure Liveness, Readiness and Startup Probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/)

### 13.2 设计模式参考

- [Container Design Patterns](https://www.redhat.com/en/blog/container-design-patterns) —— 阿里 cloud design patterns
- 《Kubernetes Patterns》—— Bilgin Ibryam & Roland Huß
- [Google SRE Workbook - Kubernetes Workloads](https://sre.google/workbook/kubernetes/)

### 13.3 跨文件链接

- 上一章: [03 - 安装与部署](./03-安装与部署.md)
- 下一章: [05 - Service 与网络](./05-Service与网络.md)
- 详见: [08 - 调度器](./08-调度器.md) / [09 - 控制器模式](./09-控制器模式.md) / [13 - kubelet 与 Pod 生命周期](./13-kubelet与Pod生命周期.md)
- 参考平行模块: [Docker/01 - 基础与核心概念](../Docker/01-基础与核心概念.md)

# 16. CSI 与存储编排

> 关键词：CSI、PV、PVC、StorageClass、Attach、Mount、Provision、Snapshot、Expand、Clone

------

## 16.1 问题定义

K8s 把存储从 Pod 解耦：

- **Pod 临时数据**：容器重启丢失
- **持久化需求**：数据库、消息队列、文件存储
- **多种后端**：块存储、文件存储、对象存储
- **多种厂商**：AWS EBS、Azure Disk、Ceph、NFS、Longhorn...

如何让 **任何存储厂商** 都能 **以统一方式** 接入 K8s？

**核心问题**：

> CSI（Container Storage Interface）如何标准化 K8s 与存储系统的对接,让 Pod 拿到持久化卷?

------

## 16.2 直觉解释

把 CSI 想象成 **快递柜与快递公司的标准接口**：

| 快递柜 | CSI |
|--------|-----|
| 用户扫码取件 | Pod 挂载 PVC |
| 不同快递公司都能投递 | 不同存储厂商都能接入 |
| 柜子有大小规格 | PV 有容量限制 |
| 临时缺货自动补货 | StorageClass 动态供给 |
| 暂存包裹给他人 | Volume Snapshot |
| 复制包裹给另一柜 | Volume Clone |
| 升级更大柜子 | Volume Expand |

关键点：CSI 把存储操作拆成 **三阶段（Provision / Attach / Mount）**,各阶段独立解耦。

------

## 16.3 核心概念

### 16.3.1 CSI 在架构中的位置

```
                ┌──────────────────────────┐
                │       APIServer          │
                │  (PV/PVC/StorageClass)   │
                └────────────┬─────────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
       ┌──────────┐  ┌──────────┐  ┌──────────┐
       │External  │  │External  │  │External  │
       │Provisioner│ │Attacher  │  │Resizer   │
       │(Sidecar) │  │(Sidecar) │  │(Sidecar) │
       └────┬─────┘  └────┬─────┘  └────┬─────┘
            │              │              │
            ▼              ▼              ▼
       ┌─────────────────────────────────────┐
       │    CSI Driver (gRPC)                │
       │  Identity / Controller / Node       │
       └─────────────────────────────────────┘
            │              │
            ▼              ▼
       ┌──────────┐  ┌──────────┐
       │ 存储后端  │  │ 节点     │
       │ (EBS/Ceph)│  │ (kubelet)│
       └──────────┘  └──────────┘
```

### 16.3.2 PV / PVC / StorageClass 三层模型

```
StorageClass: 存储类别（"模板"）
  - provisioner: ebs.csi.aws.com
  - parameters: type=gp3, fsType=ext4
  - volumeBindingMode: Immediate / WaitForFirstConsumer
  - reclaimPolicy: Delete / Retain
  - allowVolumeExpansion: true

PVC: 用户对存储的"申请"
  - resources.requests.storage: 100Gi
  - storageClassName: fast-ssd
  - accessModes: [ReadWriteOnce]

PV: 实际的"物理卷"
  - capacity: 100Gi
  - accessModes: [ReadWriteOnce]
  - csi.driver: ebs.csi.aws.com
  - csi.volumeHandle: vol-xxx
  - claimRef: 指向 PVC
```

### 16.3.3 CSI 三大组件

```
1. Identity Service（身份服务）
   - GetPluginInfo: 返回驱动名、版本
   - GetPluginCapabilities: 声明支持的能力
   - Probe: 健康检查

2. Controller Service（控制器服务，集群级）
   - CreateVolume: 创建卷（→ Provision）
   - DeleteVolume: 删除卷
   - ControllerPublishVolume: 卷挂载到节点（→ Attach）
   - ControllerUnpublishVolume: 卷从节点卸载
   - CreateSnapshot: 创建快照
   - ControllerExpandVolume: 在线扩容

3. Node Service（节点级，每节点一个 DaemonSet）
   - NodeStageVolume: 块设备格式化 + 挂载到 staging 路径
   - NodePublishVolume: 从 staging 路径 bind mount 到 Pod 路径
   - NodeUnpublishVolume: 解除 Pod 路径
   - NodeUnstageVolume: 解除 staging
   - NodeGetCapabilities: 声明节点能力
```

### 16.3.4 三阶段存储操作

```
阶段 1: Provision（供给）
  - 由 External Provisioner + Controller.CreateVolume 完成
  - 在存储后端创建实际卷（如 AWS EBS 卷）
  - 创建 PV 对象
  
阶段 2: Attach（挂接到节点）
  - 由 External Attacher + Controller.ControllerPublishVolume 完成
  - 把卷 attach 到 Pod 所在节点的设备（如 /dev/xvdf）
  - 仅块存储需要，文件存储（NFS）跳过

阶段 3: Mount（挂载到 Pod）
  - 由 kubelet + Node.NodeStageVolume + Node.NodePublishVolume 完成
  - 格式化块设备（如 ext4/xfs）
  - mount 到 /var/lib/kubelet/pods/xxx/volumes/...
  - bind mount 到容器内路径

两步 mount（Stage + Publish）的原因：
  - 同一卷被同节点多 Pod 使用时，Stage 只执行一次
  - Publish 可多次执行（每 Pod 一次）
  - 减少 I/O 与状态管理复杂度
```

### 16.3.5 AccessModes（访问模式）

| 模式 | 缩写 | 说明 | 典型后端 |
|------|------|------|---------|
| ReadWriteOnce | RWO | 单节点读写 | EBS、Ceph RBD |
| ReadOnlyMany | ROX | 多节点只读 | NFS、CephFS |
| ReadWriteMany | RWX | 多节点读写 | NFS、CephFS、FSx |
| ReadWriteOncePod | RWOP | 单 Pod 读写（K8s 1.22+） | EBS、Ceph RBD |

**关键陷阱**：AccessModes 是 PV 的"声明能力"，**不强制后端支持**。比如把 EBS（仅 RWO）声明为 RWX，K8s 不会拒绝,但实际多节点同时挂载会损坏数据。

### 16.3.6 Reclaim Policy（回收策略）

```
Retain:    PVC 删除后 PV 保留,数据仍存在,可手动回收
Delete:    PVC 删除后 PV 与底层卷一起删除
Recycle:   (已废弃) 清空数据后保留
```

### 16.3.7 Volume Binding Mode

```
Immediate:              PVC 创建时立即 Provision + Bind
WaitForFirstConsumer:   等到 Pod 调度后才 Provision（推荐）
  - 避免跨可用区挂载失败
  - 节点亲和性匹配后创建
  - 适合拓扑敏感存储（如 EBS）
```

### 16.3.8 Volume Snapshot / Clone / Expand

```
Snapshot:  从 PV 创建快照,可恢复出新 PV
  - VolumeSnapshotClass
  - VolumeSnapshot
  - VolumeSnapshotContent
  
Clone:     从现有 PVC 克隆出新 PVC
  - dataSource: kind=PVC, name=src-pvc
  - 同 StorageClass,同后端
  
Expand:    在线/离线扩容 PVC
  - PVC.spec.resources.requests.storage 改大
  - StorageClass.allowVolumeExpansion: true
  - 文件系统在线扩容（ext4/xfs）
  - 块设备扩容可能需要 Pod 重启
```

------

## 16.4 操作流程

### 16.4.1 动态供给完整时序

```
T0: 用户创建 PVC（storageClassName=fast-ssd, size=100Gi）
T1: External Provisioner Watch 到新 PVC
T2: Provisioner 读取 StorageClass 配置
T3: Provisioner 调用 CSI Controller.CreateVolume
T4: CSI Driver 调用后端 API 创建卷（如 EBS CreateVolume）
T5: 后端返回卷 ID（vol-xxx）
T6: Provisioner 创建 PV 对象（volumeHandle=vol-xxx）
T7: K8s 自动绑定 PVC 与 PV
T8: 用户创建 Pod（使用该 PVC）
T9: Scheduler 调度 Pod 到 NodeA
T10: kubelet 收到 Pod,开始 syncPod
T11: kubelet 的 VolumeManager 调用 CSI Node.NodeStageVolume
    - attach 设备到节点（如果是 CSI Attach）
    - 格式化（ext4/xfs）
    - mount 到 staging 路径
T12: kubelet 调用 CSI Node.NodePublishVolume
    - bind mount 到 Pod 路径
T13: 容器启动,看到挂载点
```

### 16.4.2 Pod 删除时存储清理

```
T0: kubectl delete pod
T1: kubelet 收到 deletionTimestamp
T2: 容器优雅终止
T3: kubelet 调用 CSI Node.NodeUnpublishVolume
    - umount Pod 路径
T4: kubelet 调用 CSI Node.NodeUnstageVolume
    - umount staging 路径
T5: 若该节点无其他 Pod 用此卷:
    - External Attacher 调用 Controller.ControllerUnpublishVolume
    - 设备从节点 detach
T6: PV 与底层卷保留（按 reclaimPolicy）
```

### 16.4.3 PVC 删除时存储清理

```
T0: kubectl delete pvc
T1: PVC 设置 deletionTimestamp
T2: PV 按 reclaimPolicy 处理:
    - Retain: PV 保留,状态 Released
    - Delete: External Provisioner 调用 CSI Controller.DeleteVolume
T3: Delete 模式:
    - 后端删除卷（EBS DeleteVolume）
    - PV 对象删除
T4: Retain 模式:
    - PV 保留,claimRef 清空
    - 可手动恢复或删除
```

------

## 16.5 底层原理

### 16.5.1 CSI gRPC 接口

```protobuf
service Identity {
  rpc GetPluginInfo(GetPluginInfoRequest) returns (GetPluginInfoResponse);
  rpc GetPluginCapabilities(GetPluginCapabilitiesRequest) returns (GetPluginCapabilitiesResponse);
  rpc Probe(ProbeRequest) returns (ProbeResponse);
}

service Controller {
  rpc CreateVolume(CreateVolumeRequest) returns (CreateVolumeResponse);
  rpc DeleteVolume(DeleteVolumeRequest) returns (DeleteVolumeResponse);
  rpc ControllerPublishVolume(...) returns (...);
  rpc ControllerUnpublishVolume(...) returns (...);
  rpc ValidateVolumeCapabilities(...) returns (...);
  rpc ListVolumes(...) returns (...);
  rpc GetCapacity(...) returns (...);
  rpc CreateSnapshot(...) returns (...);
  rpc DeleteSnapshot(...) returns (...);
  rpc ListSnapshots(...) returns (...);
  rpc ControllerExpandVolume(...) returns (...);
}

service Node {
  rpc NodeStageVolume(...) returns (...);
  rpc NodeUnstageVolume(...) returns (...);
  rpc NodePublishVolume(...) returns (...);
  rpc NodeUnpublishVolume(...) returns (...);
  rpc NodeGetVolumeStats(...) returns (...);
  rpc NodeExpandVolume(...) returns (...);
  rpc NodeGetCapabilities(...) returns (...);
}
```

### 16.5.2 External Sidecar 工作原理

K8s 把 CSI 控制逻辑拆成多个 sidecar 容器:

| Sidecar | 职责 |
|---------|------|
| external-provisioner | Watch PVC,调 CreateVolume/DeleteVolume |
| external-attacher | Watch VolumeAttachment,调 ControllerPublish/Unpublish |
| external-resizer | Watch PVC 容量变化,调 ControllerExpandVolume |
| external-snapshotter | Watch VolumeSnapshot,调 CreateSnapshot/DeleteSnapshot |
| node-driver-registrar | 注册 CSI Driver 到 kubelet |
| liveness-probe | 健康检查 |
| external-health-monitorer | 卷健康监控 |

**好处**:CSI 驱动只实现 gRPC 接口,与 K8s 版本解耦。

### 16.5.3 VolumeAttachment 对象

```yaml
apiVersion: storage.k8s.io/v1
kind: VolumeAttachment
metadata:
  name: csi-xxx
spec:
  attacher: ebs.csi.aws.com
  source:
    persistentVolumeName: pvc-xxx
  nodeName: node-a    # 挂到哪个节点
status:
  attached: true
  attachmentMetadata:
    devicePath: /dev/xvdf  # 节点上的设备路径
```

**作用**:声明"PV 应该挂到哪个节点",由 External Attacher Watch 后调 CSI。

### 16.5.4 kubelet VolumeManager

```go
// kubelet 内部 VolumeManager(简化)
type VolumeManager struct {
  desiredStateOfWorld: 期望状态(Pod 引用的卷)
  actualStateOfWorld:  实际状态(已挂载的卷)
  reconciler:          协调器
}

func (m *VolumeManager) Run() {
  for {
    desired := m.desiredStateOfWorld.GetVolumes()
    actual := m.actualStateOfWorld.GetVolumes()
    
    // 期望有但实际没有 → 挂载
    for v := range desired - actual {
      m.mountVolume(v)
      // 1. 调 CSI NodeStageVolume
      // 2. 调 CSI NodePublishVolume
    }
    
    // 实际有但期望没有 → 卸载
    for v := range actual - desired {
      m.unmountVolume(v)
      // 1. 调 CSI NodeUnpublishVolume
      // 2. 调 CSI NodeUnstageVolume
    }
  }
}
```

### 16.5.5 Stage/Publish 双层挂载

```
设备 /dev/xvdf
  ↓ format
  ↓ mount
/var/lib/kubelet/pods/<pod-uid>/volumes/kubernetes.io~csi/<vol-id>/mount  (staging 路径)
  ↓ bind mount
/var/lib/kubelet/pods/<pod-uid>/volumes/kubernetes.io~csi/<vol-id>/mount  (Pod 路径)
  ↓ bind mount (rshared)
容器内 /data

为什么两步:
  1. Stage 一次:格式化、挂载,后续同节点多 Pod 共用
  2. Publish 多次:每 Pod bind mount,隔离
  3. 减少 I/O,避免重复格式化
```

### 16.5.6 RWX 实现原理

```
RWO (ReadWriteOnce):
  - 块存储(EBS/Ceph RBD)
  - 单节点 attach,文件系统单点挂载
  - 多节点同时挂载会损坏文件系统

RWX (ReadWriteMany):
  - 文件存储(NFS/CephFS)
  - 多节点同时挂载,文件系统支持并发
  - 通过 NFS/CephFS 协议保证一致性

伪 RWX 风险:
  - 把 RWO 声明为 RWX,K8s 允许
  - 多节点同时 attach,数据损坏
  - 文件系统不一致,需 fsck
```

### 16.5.7 在线扩容机制

```
K8s 1.11+ 在线扩容:
  1. PVC.spec.resources.requests.storage 改大
  2. external-resizer Watch 到变化
  3. 调 CSI Controller.ControllerExpandVolume
     - 后端扩容(EBS ModifyVolume)
     - 块设备容量更新
  4. 调 CSI Node.NodeExpandVolume
     - 文件系统扩容(resize2fs/xfs_growfs)
     - 无需重启 Pod

注意:
  - ext4/xfs 支持在线扩容
  - 某些存储需要 Pod 重启才能识别新容量
  - 缩容不支持
```

### 16.5.8 Snapshot 恢复流程

```
1. 创建 VolumeSnapshotClass
2. 创建 VolumeSnapshot:
   apiVersion: snapshot.storage.k8s.io/v1
   kind: VolumeSnapshot
   spec:
     source:
       persistentVolumeClaimName: src-pvc
     volumeSnapshotClassName: csi-snapclass
3. CSI Controller.CreateSnapshot 在后端创建快照
4. 创建 VolumeSnapshotContent 对象
5. 恢复时创建 PVC:
   spec:
     dataSource:
       kind: VolumeSnapshot
       name: my-snap
       apiGroup: snapshot.storage.k8s.io
6. CSI Controller.CreateVolume 从快照创建新卷
```

------

## 16.6 配置示例

### 16.6.1 StorageClass 定义

```yaml
# AWS EBS gp3
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: ebs-gp3
provisioner: ebs.csi.aws.com
parameters:
  type: gp3
  iops: "3000"
  throughput: "125"
  fsType: ext4
reclaimPolicy: Delete
volumeBindingMode: WaitForFirstConsumer
allowVolumeExpansion: true
mountOptions:
  - noatime
  - nodiratime
---
# Ceph RBD
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: ceph-rbd
provisioner: rbd.csi.ceph.com
parameters:
  clusterID: xxx
  pool: rbd-pool
  imageFormat: "2"
  imageFeatures: layering
  csi.storage.k8s.io/provisioner-secret-name: ceph-secret
  csi.storage.k8s.io/provisioner-secret-namespace: ceph
  csi.storage.k8s.io/node-stage-secret-name: ceph-secret
  csi.storage.k8s.io/node-stage-secret-namespace: ceph
reclaimPolicy: Delete
volumeBindingMode: WaitForFirstConsumer
allowVolumeExpansion: true
---
# NFS（静态供给）
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: nfs
provisioner: nfs.csi.k8s.io
parameters:
  server: nfs-server.example.com
  share: /exported/path
reclaimPolicy: Retain
volumeBindingMode: Immediate
```

### 16.6.2 PVC 与 Pod

```yaml
# PVC
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: data-pvc
spec:
  accessModes: [ReadWriteOnce]
  storageClassName: ebs-gp3
  resources:
    requests:
      storage: 100Gi
---
# Pod
apiVersion: v1
kind: Pod
metadata:
  name: db
spec:
  containers:
  - name: postgres
    image: postgres:15
    volumeMounts:
    - name: data
      mountPath: /var/lib/postgresql/data
    - name: config
      mountPath: /etc/postgresql/postgresql.conf
      subPath: postgresql.conf   # 单文件挂载
  volumes:
  - name: data
    persistentVolumeClaim:
      claimName: data-pvc
  - name: config
    configMap:
      name: pg-config
```

### 16.6.3 Volume Snapshot

```yaml
# VolumeSnapshotClass
apiVersion: snapshot.storage.k8s.io/v1
kind: VolumeSnapshotClass
metadata:
  name: ebs-snapclass
driver: ebs.csi.aws.com
deletionPolicy: Delete
parameters:
  encrypted: "true"
---
# VolumeSnapshot
apiVersion: snapshot.storage.k8s.io/v1
kind: VolumeSnapshot
metadata:
  name: db-snap-20260705
spec:
  volumeSnapshotClassName: ebs-snapclass
  source:
    persistentVolumeClaimName: data-pvc
---
# 从快照恢复
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: restored-pvc
spec:
  accessModes: [ReadWriteOnce]
  storageClassName: ebs-gp3
  resources:
    requests:
      storage: 100Gi
  dataSource:
    kind: VolumeSnapshot
    name: db-snap-20260705
    apiGroup: snapshot.storage.k8s.io
```

### 16.6.4 Volume Clone

```yaml
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: cloned-pvc
spec:
  accessModes: [ReadWriteOnce]
  storageClassName: ebs-gp3
  resources:
    requests:
      storage: 100Gi
  dataSource:
    kind: PersistentVolumeClaim
    name: data-pvc   # 从现有 PVC 克隆
```

### 16.6.5 Volume Expand

```yaml
# 在线扩容（修改 PVC）
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: data-pvc
spec:
  resources:
    requests:
      storage: 200Gi   # 从 100Gi 扩到 200Gi
# 查看扩容状态:
# kubectl get pvc data-pvc
# STATUS: Capacity=200Gi（已扩容）
```

### 16.6.6 VolumeMetrics 与监控

```yaml
# Pod 内查看卷使用情况
# kubelet 提供 NodeGetVolumeStats 接口
# 监控数据来源:
#   /var/lib/kubelet/pods/<uid>/volumes/.../volumemetrics
# 
# Prometheus 抓取 kubelet metrics:
# kubelet_volume_stats_capacity_bytes
# kubelet_volume_stats_available_bytes
# kubelet_volume_stats_used_bytes
# kubelet_volume_stats_inodes
# kubelet_volume_stats_inodes_free
```

### 16.6.7 CSI Driver 部署示例（简化）

```yaml
# AWS EBS CSI Driver（Helm 安装后大致结构）
# 1. Controller Deployment（集群级,1 副本或主从）
apiVersion: apps/v1
kind: Deployment
metadata:
  name: ebs-csi-controller
  namespace: kube-system
spec:
  replicas: 2
  template:
    spec:
      containers:
      - name: ebs-plugin
        image: amazon/aws-ebs-csi-driver:v1.30.0
        args: [controller]
      - name: external-provisioner
        image: registry.k8s.io/sig-storage/csi-provisioner:v4.0.0
      - name: external-attacher
        image: registry.k8s.io/sig-storage/csi-attacher:v4.4.0
      - name: external-resizer
        image: registry.k8s.io/sig-storage/csi-resizer:v1.9.0
      - name: external-snapshotter
        image: registry.k8s.io/sig-storage/csi-snapshotter:v7.0.0
      - name: liveness-probe
        image: registry.k8s.io/sig-storage/livenessprobe:v2.12.0
---
# 2. Node DaemonSet（每节点一个）
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: ebs-csi-node
  namespace: kube-system
spec:
  template:
    spec:
      containers:
      - name: ebs-plugin
        image: amazon/aws-ebs-csi-driver:v1.30.0
        args: [node]
        securityContext:
          privileged: true
        volumeMounts:
        - name: kubelet-dir
          mountPath: /var/lib/kubelet
          mountPropagation: Bidirectional
        - name: plugin-dir
          mountPath: /csi
        - name: device-dir
          mountPath: /dev
      - name: node-driver-registrar
        image: registry.k8s.io/sig-storage/csi-node-driver-registrar:v2.9.0
      volumes:
      - name: kubelet-dir
        hostPath: {path: /var/lib/kubelet}
      - name: plugin-dir
        hostPath: {path: /var/lib/kubelet/plugins/ebs.csi.aws.com}
      - name: device-dir
        hostPath: {path: /dev}
```

### 16.6.8 hostPath / emptyDir / local 对比

```yaml
# 1. emptyDir: Pod 临时空间,Pod 删除即消失
volumes:
- name: cache
  emptyDir:
    sizeLimit: 1Gi
    medium: Memory   # tmpfs(内存)

# 2. hostPath: 直接挂载节点路径(危险)
volumes:
- name: docker-sock
  hostPath:
    path: /var/run/docker.sock
    type: Socket

# 3. local: 节点本地持久化卷
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: local-storage
provisioner: kubernetes.io/no-provisioner
volumeBindingMode: WaitForFirstConsumer
---
apiVersion: v1
kind: PersistentVolume
metadata:
  name: local-pv
spec:
  capacity:
    storage: 100Gi
  accessModes: [ReadWriteOnce]
  persistentVolumeReclaimPolicy: Retain
  storageClassName: local-storage
  local:
    path: /mnt/disks/ssd1
  nodeAffinity:
    required:
      nodeSelectorTerms:
      - matchExpressions:
        - key: kubernetes.io/hostname
          operator: In
          values: [node-a]
```

------

## 16.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | 把 RWO 声明为 RWX | 多节点写损坏数据 | 严格按后端能力声明 |
| 2 | reclaimPolicy=Delete 误删数据 | 数据丢失 | 重要数据用 Retain |
| 3 | Immediate binding 跨 AZ | Pod 调度失败 | WaitForFirstConsumer |
| 4 | StorageClass 未允许扩容 | 无法在线扩容 | allowVolumeExpansion: true |
| 5 | 文件系统不在线扩容 | Pod 重启后才识别 | 用 ext4/xfs |
| 6 | Snapshot 用错 StorageClass | 恢复失败 | 源与目标 StorageClass 一致 |
| 7 | PVC 卡 Pending | Pod 无法启动 | 检查 Provisioner 与 StorageClass |
| 8 | 节点故障 PV 无法 detach | Pod 卡 Terminating | 强制删除 VolumeAttachment |
| 9 | hostPath 滥用 | 节点污染、安全风险 | 用 PVC/local |
| 10 | emptyDir.medium=Memory 滥用 | 内存爆掉 | 限制 sizeLimit |
| 11 | CSI Driver 版本不匹配 | API 调用失败 | 看 CSI 兼容矩阵 |
| 12 | mountPropagation 未设 | 容器看不到挂载 | 用 Bidirectional |
| 13 | 跨可用区 EBS 挂载 | Pod 调度失败 | WaitForFirstConsumer + 拓扑约束 |
| 14 | subPath 误用 | 文件被覆盖 | subPath 用相对路径 |
| 15 | 静态 PV 与 PVC 容量不匹配 | Bind 失败 | PV ≥ PVC |
| 16 | local PV 节点故障数据丢失 | 数据丢失 | 多副本或备份 |
| 17 | 多 Pod 共用 RWO PVC | 第二 Pod 调度失败 | 用 RWX 或单 Pod |

------

## 16.8 工业案例

### 16.8.1 阿里 ACK:云盘 CSI 优化

**场景**:大规模 K8s 上 EBS(阿里云盘)性能瓶颈。

**优化项**:
1. **多队列并发**:CSI Controller 多副本,CreateVolume 并发
2. **卷预创建**:预创建卷池,减少冷启动延迟
3. **Attach 加速**:并行 attach 多卷
4. **快照增量**:基于快照链的增量备份,降低存储成本
5. **卷监控**:CloudMonitor 集成,IOPS/吞吐可视化

**结果**:卷创建从 30s 降到 5s,Pod 启动加速 60%。

### 16.8.2 字节跳动:Ceph RBD 大规模实践

**场景**:自建 Ceph 集群支持 10000+ Pod,单集群 PB 级存储。

**架构**:
- Ceph RBD for block(数据库)
- CephFS for shared(日志/配置)
- RGW for object(冷数据)

**优化**:
1. **RBD 镜像分层**:基础镜像只读层,提升创建速度
2. **Ceph Cache Tiering**:热数据 SSD 缓存
3. **CRUSH Map 调优**:跨机架副本分布
4. **CSI Driver 优化**:批量 CreateVolume,减少 PG 压力
5. **快照 + 增量备份**:RBD diff 备份到对象存储

**结果**:卷 IOPS 单卷 5000+,集群总吞吐 50GB/s。

### 16.8.3 Google GKE:PD CSI Driver

**GKE Persistent Disk CSI**:
- 默认 StorageClass:standard-rwo / premium-rwo
- Regional PD:多区域副本,可用区故障不丢数据
- PD Snapshot:增量快照,秒级恢复
- PD Capacity Auto-scaling:基于使用率自动扩容

**特色**:
- Filestore CSI:NFS 高性能共享存储
- GCS FUSE CSI:对象存储直接挂载到 Pod

### 16.8.4 AWS EKS:EBS CSI 迁移

**背景**:K8s 1.23+ 弃用 in-tree EBS,迁移到 CSI。

**迁移坑**:
1. **既有 PV 升级**:需要 CSI Migration 特性门控
2. **IAM 权限**:EBS CSI Driver 需独立 IAM Role
3. **卷 ID 格式变化**:aws://az/vol-id → vol-id
4. **快照兼容**:旧快照需转换格式

**结果**:迁移后支持 gp3、快照、克隆等新特性。

### 16.8.5 Netflix:Longhorn 自建分布式存储

**场景**:边缘节点无云存储,需自建持久化。

**方案**:Longhorn(开源,K8s 原生)
- 节点本地磁盘 → 虚拟卷
- 多副本(默认 3 副本)
- 增量快照
- 跨节点备份到 S3

**结果**:成本降低 80%(对比云 EBS),可靠性达 99.99%。

------

## 16.9 与其他方案关系

### 16.9.1 CSI vs in-tree Volume

| 维度 | in-tree | CSI |
|------|---------|-----|
| 集成方式 | K8s 内核代码 | 外部 gRPC |
| 升级 | 需升级 K8s | 独立升级 |
| 厂商接入 | 修改 K8s 源码 | 实现 CSI 接口 |
| K8s 版本耦合 | 强 | 弱 |
| 维护 | K8s 社区 | 各厂商 |
| 趋势 | 弃用 | 唯一推荐 |

K8s 1.27+ 已弃用多数 in-tree 驱动,迁移到 CSI。

### 16.9.2 CSI vs Docker Volume

| 维度 | Docker Volume | CSI |
|------|---------------|-----|
| 部署 | 单机 | 集群 |
| 接口 | Docker API | gRPC + K8s API |
| 生命周期 | 容器 | PVC |
| 多 Pod 共享 | 困难 | RWX 支持 |
| 生态 | Docker | K8s/Mesos |

### 16.9.3 CSI 与 SAN/NAS

```
传统 SAN/NAS:
  - 厂商专用 API
  - 管理员手动配置
  - 与编排系统脱节

CSI:
  - 标准接口
  - 自动动态供给
  - 与 K8s 深度集成
  - 但底层仍是 SAN/NAS

关系:CSI 是 SAN/NAS 与 K8s 之间的桥梁。
```

### 16.9.4 与 S3/对象存储

```
对象存储(S3/OSS)与 CSI:
  1. CSI 直接挂载(如 S3 FUSE CSI)
     - 通过 s3fs/goofys 把 S3 挂载为文件系统
     - 性能差,适合只读场景
  2. SDK 访问(推荐)
     - Pod 内应用用 S3 SDK
     - 不挂载文件系统
     - 性能最优
  3. Mountpoint for S3(新)
     - AWS 出品,性能优于 s3fs
     - 适合 AI/ML 训练数据集
```

### 16.9.5 与 Operator 状态存储

```
Operator(如 EtcdOperator/PostgresOperator):
  - 通常用 PVC 持久化状态
  - 部分用本地文件 + 备份
  
vs K8s 原生 API:
  - Operator 应用层管理存储
  - CSI 提供 IaaS 层抽象
  - 互补关系
```

------

## 16.10 面试速答

**Q1: CSI 三大组件?**

Identity Service(身份)、Controller Service(集群级控制)、Node Service(节点级操作)。对应 gRPC 三个 service。

**Q2: PV / PVC / StorageClass 关系?**

StorageClass 是模板(定义 provisioner 与参数),PVC 是用户申请,PV 是实际卷。PVC 通过 StorageClass 动态生成 PV,或绑定到静态 PV。

**Q3: 三阶段存储操作?**

Provision(供给,创建卷)、Attach(挂接到节点,块设备 attach)、Mount(挂载到 Pod,格式化 + mount)。

**Q4: 为什么 Stage + Publish 两步?**

Stage 在节点级一次性格式化与挂载,Publish 在 Pod 级 bind mount。同节点多 Pod 共用一卷时,Stage 只执行一次,避免重复 I/O。

**Q5: RWO / RWX 区别?**

RWO 单节点读写(块存储),RWX 多节点读写(文件存储)。把 RWO 声明为 RWX 不会报错,但多节点同时挂载会损坏数据。

**Q6: WaitForFirstConsumer 作用?**

等到 Pod 调度后才创建卷,确保卷与 Pod 同可用区。避免跨 AZ 挂载失败。

**Q7: CSI External Sidecar 有哪些?**

external-provisioner、external-attacher、external-resizer、external-snapshotter、node-driver-registrar、liveness-probe。

**Q8: PVC 卡 Pending 怎么排查?**

1. kubectl describe pvc 看 Events
2. 检查 StorageClass 是否存在
3. 检查 CSI Controller pod 是否 Running
4. 检查 CSI Controller 日志
5. 检查后端 API 是否可达、配额是否充足

**Q9: Volume Snapshot 工作原理?**

VolumeSnapshot 触发 CSI Controller.CreateSnapshot,在后端创建快照并返回快照 ID。恢复时 PVC.dataSource 指向 Snapshot,CSI 从快照创建新卷。

**Q10: 在线扩容原理?**

PVC 改大容量 → external-resizer Watch 到 → 调 CSI Controller.ControllerExpandVolume(后端扩容) → 调 CSI Node.NodeExpandVolume(文件系统扩容,resize2fs/xfs_growfs)。ext4/xfs 支持在线扩容,无需重启 Pod。

------

## 16.11 综合面试题

### 题 1:设计一个支持数据库的 K8s 存储方案

```
需求:PostgreSQL 单实例,500GB 数据,高 IOPS,低延迟,定期备份

设计:
1. StorageClass:
   - AWS EBS gp3(3000 IOPS,125 MiB/s)
   - 或本地 NVMe(local PV)+ 异步备份
   
2. PVC:
   - accessModes: ReadWriteOnce
   - storageClassName: ebs-gp3
   - resources: 500Gi
   - volumeBindingMode: WaitForFirstConsumer
   
3. Pod:
   - 资源:8C 32G
   - nodeSelector:节点亲和到 c5.4xlarge
   - volumeMounts:/var/lib/postgresql/data
   - 挂载选项:noatime,nodiratime
   - securityContext:fsGroup=999(postgres)
   
4. 备份:
   - VolumeSnapshot 定时备份(每天 1 次)
   - 增量快照保留 7 天
   - Wal-g 流式备份到 S3
   
5. 监控:
   - kubelet_volume_stats_*(容量)
   - node_volume_metrics_*(IOPS)
   - PostgreSQL exporter(查询性能)
   
6. 高可用:
   - 主备:Patroni + 同步复制
   - 备库:跨 AZ,独立 EBS
   - 故障切换:1-3 分钟
```

### 题 2:Pod 卡在 ContainerCreating,卷挂载失败,怎么排查?

```
1. kubectl describe pod → 看 Events
   - 常见错误:FailedMount,Unable to attach or mount
   
2. 检查 PVC 状态:
   kubectl get pvc
   - 是否 Bound?
   - 卡 Pending?→ 检查 StorageClass/Provisioner
   
3. 检查 VolumeAttachment:
   kubectl get volumeattachment
   - 是否 attached?
   - attacher 是否报错?
   
4. 检查 CSI Driver pod:
   kubectl get pod -n kube-system | grep csi
   kubectl logs <csi-controller-pod>
   kubectl logs <csi-node-pod>
   
5. 节点级检查:
   ssh node
   lsblk  # 设备是否 attach?
   mount | grep <vol>  # 是否已挂载?
   ls /var/lib/kubelet/pods/<uid>/volumes/
   
6. 跨 AZ 问题:
   - Pod 在 AZ-a,卷在 AZ-b → 无法 attach
   - 用 WaitForFirstConsumer
   
7. 强制清理(最后手段):
   kubectl delete volumeattachment <name> --force
   kubectl delete pod <name> --force
```

### 题 3:解释 CSI Driver 的部署架构

```
1. Controller 组件(Deployment):
   - 1-2 副本(主从,Leader 选举)
   - Sidecar 容器:
     * csi-provisioner:Watch PVC,调 CreateVolume
     * csi-attacher:Watch VolumeAttachment,调 ControllerPublish
     * csi-resizer:Watch PVC 容量,调 ExpandVolume
     * csi-snapshotter:Watch VolumeSnapshot,调 CreateSnapshot
     * liveness-probe:健康检查
   - 主容器:CSI Driver(实现 gRPC)
   
2. Node 组件(DaemonSet):
   - 每节点一个
   - Sidecar:
     * node-driver-registrar:向 kubelet 注册
     * liveness-probe
   - 主容器:CSI Driver(node 模式)
   - 必须特权模式
   - 挂载 /var/lib/kubelet(双向传播)
   - 挂载 /dev(块设备访问)
   
3. RBAC:
   - ServiceAccount + ClusterRole
   - 允许操作 PV/PVC/VolumeAttachment/VolumeSnapshot
   
4. 配置:
   - 通过 ConfigMap 传递后端配置
   - 通过 Secret 传递凭证
```

### 题 4:PV / PVC 绑定规则?

```
绑定条件:
1. accessModes 兼容(PV 必须满足 PVC 的所有 mode)
2. storageClassName 一致(或都为空)
3. capacity ≥ PVC 请求
4. PV 未被其他 PVC 绑定
5. nodeAffinity 匹配(若有)

绑定流程:
1. PVC 创建
2. Controller 查找匹配 PV
3. 找到 → 绑定(claimRef)
4. 找不到 + 有 StorageClass → 动态供给
5. 找不到 + 无 StorageClass → 卡 Pending

注意:
- 绑定是单向的,PVC 删除后 PV(若 Retain)可被新 PVC 绑定
- 但需手动清除 claimRef
- volumeBindingMode=WaitForFirstConsumer 时延后绑定
```

### 题 5:解释 CSI Snapshot 与 Velero 备份的区别

```
CSI Snapshot:
  - 存储后端级快照(EBS Snapshot/Ceph Snap)
  - 速度快(秒级)
  - 与存储后端耦合
  - 仅备份卷数据,不含 K8s 资源
  - 跨集群恢复需转换

Velero 备份:
  - 应用级备份
  - 含 K8s 资源(YAML)+ 卷数据
  - 卷数据来源:
    * CSI Snapshot(推荐)
    * restic(file-level,慢但通用)
    * EBS Snapshot 等
  - 跨集群迁移友好
  - 增量备份支持

互补关系:
  - CSI Snapshot:快速恢复点
  - Velero:整体应用迁移
  - 生产常组合使用
```

### 题 6:RWX 的实现原理与陷阱

```
RWX 实现:
1. 文件存储(NFS/CephFS/FSx):
   - 多节点同时挂载
   - 文件系统级并发控制
   - 真正的多写
   
2. 块存储伪装(不安全):
   - 把 EBS 声明为 RWX,K8s 允许
   - 多节点 attach,文件系统损坏
   - 部分驱动会拒绝(如 EBS CSI 1.20+)

陷阱:
1. 应用层并发:
   - 即使 RWX,多个 Pod 同时写同一文件仍需应用层锁
   - 数据库不能直接 RWX
   
2. 性能:
   - NFS 性能差,延迟高
   - CephFS 优于 NFS
   - 高 IOPS 场景用块存储 + 单 Pod
   
3. 缓存一致性:
   - 多节点文件缓存不一致
   - 需 cache:false 或 O_DIRECT

最佳实践:
- 数据库:RWO + 单 Pod
- 共享配置:RWX + ConfigMap(更好)
- 共享数据(读多写少):RWX
- 共享日志:RWX + 文件名隔离
```

------

## 16.12 故障复盘

### 案例 1:跨 AZ 卷挂载失败

**故障时间**:2023-08-10

**故障现象**:
- Pod 调度到 AZ-b,但 PVC 卡 Pending
- 错误:VolumeAvailabilityZoneMismatch

**根因**:
- StorageClass volumeBindingMode=Immediate
- 卷在 AZ-a 创建
- Pod 调度到 AZ-b,无法 attach

**修复**:
1. StorageClass 改为 WaitForFirstConsumer
2. 删除旧 PVC,重新创建

**经验**:跨可用区存储必须用 WaitForFirstConsumer。

### 案例 2:reclaimPolicy=Delete 误删生产数据

**故障时间**:2024-01-15

**故障现象**:
- 开发误删 PVC,生产数据丢失
- EBS 卷被自动删除

**根因**:
- 默认 StorageClass reclaimPolicy=Delete
- 没有 backup
- 误操作

**修复**:
1. 紧急从 EBS 快照恢复(幸好有定时快照)
2. 改 reclaimPolicy=Retain
3. 加强权限管控

**经验**:
- 生产 StorageClass 用 Retain
- 关键数据定期 VolumeSnapshot
- PVC 删除加确认机制(OPA/Kyverno)

### 案例 3:EBS gp2 IOPS 不足

**故障时间**:2024-03-22

**故障现象**:
- PostgreSQL 慢查询暴增
- IOPS 使用率 100%

**根因**:
- gp2 100GB 仅 300 IOPS(3 IOPS/GB)
- 数据库需要 3000+ IOPS

**修复**:
1. 升级 gp3,显式指定 3000 IOPS
2. 在线扩容,无需停机

**经验**:gp2 IOPS 随容量增长,数据库应选 gp3 + 显式 IOPS。

### 案例 4:Ceph RBD 单 PG 阻塞

**故障时间**:2024-05-08

**故障现象**:
- 集群 5000+ RBD image
- 部分 Pod 创建慢(30s+)
- Ceph monitor 压力大

**根因**:
- 单 pool PG 数固定
- image metadata 集中
- CSI CreateVolume 串行

**修复**:
1. 拆分多个 pool(按业务隔离)
2. 调大 PG 数
3. CSI Controller 多副本并发

**经验**:大规模 Ceph 必须做好 pool 规划。

### 案例 5:NFS 挂载导致 Pod 启动失败

**故障时间**:2024-07-19

**故障现象**:
- 部分 Pod 启动失败
- 错误:mount.nfs: Connection timed out

**根因**:
- NFS 服务器网络分区
- kubelet mount 超时 5 分钟
- Pod 卡 ContainerCreating

**修复**:
1. 修复 NFS 服务器
2. 配置 mountOptions:soft,timeo=30,retrans=2
3. 关键应用改用块存储

**经验**:NFS 不适合核心应用,做共享配置用 ConfigMap。

------

## 16.13 参考与延伸

### 官方文档
- [CSI](https://kubernetes.io/docs/concepts/storage/volumes/#csi)
- [Persistent Volumes](https://kubernetes.io/docs/concepts/storage/persistent-volumes/)
- [Storage Classes](https://kubernetes.io/docs/concepts/storage/storage-classes/)
- [Volume Snapshots](https://kubernetes.io/docs/concepts/storage/volume-snapshots/)
- [CSI Drivers](https://kubernetes-csi.github.io/docs/drivers.html)

### KEP
- [KEP-596: CSI Volume Health](https://github.com/kubernetes/enhancements/tree/master/keps/sig-storage/596-csi-volume-health)
- [KEP-2317: Volume Capacity Tracking](https://github.com/kubernetes/enhancements/tree/master/keps/sig-storage/2317-volume-capacity-tracking)
- [KEP-3141: CSI Migration](https://github.com/kubernetes/enhancements/tree/master/keps/sig-storage/1490-csi-migration)

### CSI Driver 项目
- [AWS EBS CSI](https://github.com/kubernetes-sigs/aws-ebs-csi-driver)
- [Azure Disk CSI](https://github.com/kubernetes-sigs/azuredisk-csi-driver)
- [GCE PD CSI](https://github.com/kubernetes-sigs/gcp-compute-persistent-disk-csi-driver)
- [Ceph CSI](https://github.com/ceph/ceph-csi)
- [Longhorn](https://longhorn.io/)
- [OpenEBS](https://openebs.io/)
- [Rook](https://rook.io/)
- [NFS CSI](https://github.com/kubernetes-csi/csi-driver-nfs)

### 源码导航
- `kubernetes/pkg/volume/csi/` - kubelet CSI 调用
- `kubernetes/pkg/controller/volume/` - 卷控制器
- `vendor/github.com/container-storage-interface/spec/` - CSI 规范

### 相关章节
- [06-存储.md](./06-存储.md) - 存储基础概念
- [13-kubelet与Pod生命周期.md](./13-kubelet与Pod生命周期.md) - kubelet 调 CSI
- [15-CNI与网络模型.md](./15-CNI与网络模型.md) - 节点组件协同
- [09-控制器模式.md](./09-控制器模式.md) - Reconcile 模式同源

### 推荐阅读
- [CSI Spec](https://github.com/container-storage-interface/spec/blob/master/csi.proto)
- [Kubernetes Storage Deep Dive](https://www.tkng.io/storage/)
- [Ceph CSI Best Practices](https://github.com/ceph/ceph-csi/blob/master/docs/ceph-csi-best-practices.md)
- [Longhorn Architecture](https://longhorn.io/docs/1.5.0/concepts/)

### 工具
- `kubectl get pv/pvc/storageclass`
- `kubectl get volumeattachment`
- `kubectl get volumesnapshot`
- `kubectl describe pvc`
- `crictl exec` - 容器内查看挂载
- `lsblk` - 查看块设备
- `mount` - 查看挂载点

### 进阶主题
- **Volume Populator**:从镜像/数据源预填充卷
- **ReadOnlyVolumeRoot**:Pod 内卷根目录只读
- **Cross-Cluster Volume**:Submariner + CSI
- **Storage Objects in Use Protection**:卷被使用时禁止删除
- **Recursive Read-Only Mounts**(RRO):K8s 1.30+,挂载递归只读
- **CSI ephemeral volume**:短暂生命周期卷
- **Generic Ephemeral Volume**:CSI 临时卷

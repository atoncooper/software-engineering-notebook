# 23. 镜像分发 CDN - 大规模镜像分发的工业方案

> 章节定位: 工业实战篇 · 第三章
> 前置章节: [22-集群密度优化](./22-集群密度优化.md)
> 后续章节: [24-供应链安全](./24-供应链安全.md)

---

## 23.1 思维导图

```
                镜像分发挑战
                     │
        ┌────────────┼────────────┐
        │            │            │
     规模问题       技术方案       工业实践
        │            │            │
   ┌────┴────┐  ┌────┴────┐  ┌────┴────┐
   │         │  │         │  │         │
 1000 节点  P2P 分发   Dragonfly
 同时拉取   延迟加载   Kraken
 带宽瓶颈   镜像瘦身   Nydus
 registry  CDN        Stargz
   过载     预热       阿里 ACR
        │            │            │
        └────────────┼────────────┘
                     │
                     ▼
              分发方案选型
                     │
   ┌─────────────────┼─────────────────┐
   │                 │                 │
 小规模            中规模            大规模
 < 100 节点        100-1000          1000+
   │                 │                 │
 直连 registry    P2P(Dragonfly)    P2P + 延迟加载
 + 镜像加速        + 预热             + CDN + 多源
```

**分发性能对比**(1GB 镜像,1000 节点):

| 方案 | 时长 | registry 负载 | 适用 |
|------|------|--------------|------|
| 直连 registry | 60min | 100%(过载) | < 100 节点 |
| 镜像加速器 | 30min | 50% | < 500 节点 |
| P2P(Dragonfly) | 5min | 5% | 1000+ 节点 |
| P2P + 延迟加载 | 30s 启动 | 1% | 万级节点 |

---

## 23.2 章节简介

大规模容器集群的镜像分发是核心难题: 1000 节点同时拉 1GB 镜像,registry 带宽瞬间打满,部署 60min 才完成。

本章系统讲解工业级镜像分发方案:
1. **P2P 分发**: Dragonfly / Kraken,peer 间共享
2. **延迟加载**: Nydus / Stargz,按需拉取
3. **镜像加速**: registry mirror / 多源
4. **预热机制**: 提前推送到节点
5. **CDN 分发**: 跨地域分发

每节包含工业实测数据,涵盖阿里/字节/Netflix 等大厂实践。

**本章工业焦点**:
- 阿里 Dragonfly: 1GB 镜像 1000 节点 5min 完成
- 字节 Nydus: 启动时间 30s(原 5min)
- Netflix Bullseye: 镜像预热到所有节点
- 腾讯 TCR: 跨地域复制 + P2P

---

## 23.3 核心概念

### 23.3.1 分发挑战

```
场景: 1000 节点同时拉 1GB 镜像

直连 registry:
┌──────────────┐
│   Registry   │  ← 10Gbps 出口带宽
└──────┬───────┘
       │
   1000 节点同时拉
       │
       ▼
带宽打满: 10Gbps / 1000 = 10Mbps/节点
拉取时长: 1GB / 10Mbps = 800s = 13min
registry 过载: CPU 100%, 内存满
实际: 60min+(拥塞重传)
```

**4 大挑战**:

1. **带宽瓶颈**: registry 出口带宽有限
2. **并发限制**: registry 最大连接数
3. **跨地域延迟**: 远距离拉取慢
4. **网络抖动**: 重传加剧拥塞

### 23.3.2 P2P 分发原理

```
传统 C/S(直连):
┌──────┐
│Reg   │
└──┬───┘
   │
   ▼
┌──┴──┐
│所有节点│  ← 每个节点都从 registry 拉
└─────┘

P2P 分发:
┌──────┐
│Reg   │  ← Seed
└──┬───┘
   │
   ▼
┌──┴──┐
│Peer1│  ← 拉 100% 后做种子
└──┬──┘
   │
   ├──────► Peer2(从 Peer1 拉)
   │
   ├──────► Peer3(从 Peer1 拉)
   │
   ▼
┌──────┐
│Peer4 │  ← 同时从 Peer1, Peer2, Peer3 拉(多源)
└──────┘

理论加速: N 节点 → N 倍带宽
```

**关键设计**:

1. **分片**: 镜像分成小块(4MB),便于多源拉取
2. **调度**: 中心调度器分配 peer
3. **去重**: layer 级别去重(同 layer 不重复拉)
4. **加密**: P2P 传输加密(防篡改)

### 23.3.3 延迟加载原理

**传统方式**:
```
docker pull nginx
  → 拉取所有 layer(1GB)
  → 解压所有 layer
  → 启动容器
时长: 5min(下载) + 1min(解压) = 6min
```

**延迟加载(Stargz/Nydus)**:
```
docker run nginx
  → 仅拉取启动必需的元数据(几 MB)
  → 启动容器
  → 运行时按需拉取文件(首次访问)
时长: 5s(启动) + 后台渐进拉取
```

**对比**:
```
传统:  0s ──────── 6min(完全启动)
延迟:  0s ── 5s(启动) ──── 后台渐进
```

**适用**:
- 启动快(Pod 扩容)
- 按需加载(大镜像小启动)
- 节省带宽(只拉用到的)

### 23.3.4 镜像预热

**原理**: 提前把镜像推到所有节点,部署时直接用本地。

```bash
# DaemonSet 定时拉取关键镜像
apiVersion: apps/v1
kind: DaemonSet
spec:
  template:
    spec:
      containers:
      - name: preloader
        image: alpine
        command:
        - sh
        - -c
        - |
          while true; do
            # 拉取最新镜像
            crictl pull myapp:latest
            sleep 1h
          done
```

**适用**:
- 关键业务镜像
- 部署速度要求高
- 节点数固定

---

## 23.4 底层原理

### 23.4.1 Dragonfly 架构

```
┌─────────────────────────────────────────┐
│           Dragonfly Cluster             │
│                                         │
│  ┌─────────────┐  ┌─────────────┐      │
│  │   Scheduler  │  │   Manager   │      │
│  │  (调度器)    │  │  (管理)     │      │
│  └──────┬──────┘  └─────────────┘      │
│         │                               │
│  ┌──────┴──────────────────────────┐   │
│  │                                 │   │
│  ▼                                 ▼   │
│ ┌─────────┐  ┌─────────┐  ┌─────────┐│
│ │ Dfget   │  │ Dfget   │  │ Dfget   ││
│ │ Peer1   │  │ Peer2   │  │ Peer3   ││
│ │ (节点)  │  │ (节点)  │  │ (节点)  ││
│ └────┬────┘  └────┬────┘  └────┬────┘│
│      │            │            │      │
│      └────────────┼────────────┘      │
│                   │                    │
│                   ▼                    │
│              ┌─────────┐               │
│              │ Seed    │               │
│              │ (源)    │               │
│              └─────────┘               │
└─────────────────────────────────────────┘
```

**组件**:
- **Scheduler**: 调度 peer,分配分片
- **Manager**: 集群管理,监控
- **Dfget**: 节点代理,替代 docker pull
- **Seed**: 镜像源(registry)

**工作流程**:

```
1. 节点发起拉取
   docker pull nginx → dfget 拦截

2. dfget 请求 Scheduler
   "我要拉 nginx,有哪些 peer?"

3. Scheduler 返回 peer 列表
   "从 Peer2 拉分片 1-10,从 Peer3 拉分片 11-20"

4. dfget 并行从多个 peer 拉
   分片 1-10 ← Peer2
   分片 11-20 ← Peer3
   分片 21-30 ← Seed(原 registry)

5. 拉取完成,本节点也成 peer
   供其他节点拉取
```

### 23.4.2 Stargz 延迟加载

**stargz 格式**:
```
传统 tar.gz:
┌──────────────────┐
│ file1(10MB)      │
│ file2(20MB)      │
│ ...              │
│ fileN(5MB)       │
│ footer           │
└──────────────────┘
拉取: 必须全部下载

stargz(可 seek):
┌──────────────────┐
│ file1 chunk1     │  ← 可单独拉取
│ file1 chunk2     │
│ file2 chunk1     │
│ ...              │
│ index(元数据)    │  ← 先拉这个
└──────────────────┘
拉取: 先拉 index,运行时按需拉 chunk
```

**工作流程**:
```
1. docker run nginx
2. stargz 拉取 index(几 KB)
3. 容器启动
4. 应用读 /usr/share/nginx/html/index.html
5. stargz 拦截读取
6. 仅拉取该文件对应 chunk(几 KB)
7. 应用继续运行
8. 后台渐进拉取其他 chunk
```

### 23.4.3 Nydus 架构

**Nydus**(阿里开源):
```
┌─────────────────────────────┐
│ Container                   │
│  ├─ 文件读取                │
│  └─ /var/lib/containerd/... │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│ Nydus Daemon                │
│  ├─ FUSE 挂载               │
│  ├─ 本地缓存                │
│  └─ 按需拉取                │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│ Nydus Image Format          │
│  ├─ Bootstrap(元数据)      │
│  ├─ Blob(数据块)           │
│  └─ 索引                    │
└─────────────────────────────┘
```

**特点**:
- FUSE 实现,用户态
- 按需拉取(类似 Stargz)
- 镜像格式优化(更小)
- 兼容 OCI 标准

### 23.4.4 镜像 layer 去重

```
镜像 A:
  layer1: ubuntu:22.04(50MB)  ← 共享
  layer2: jre-17(150MB)        ← 共享
  layer3: app-a(5MB)           ← 独有

镜像 B:
  layer1: ubuntu:22.04(50MB)  ← 共享(已存在)
  layer2: jre-17(150MB)        ← 共享(已存在)
  layer3: app-b(8MB)           ← 独有

节点已有镜像 A,拉取镜像 B:
  仅拉 layer3(8MB)
  layer1/layer2 复用
```

**收益**:
- 节省带宽 90%+
- 加速部署
- 节省存储

---

## 23.5 代码实现

### 23.5.1 Dragonfly 部署

```yaml
# dragonfly.yaml
apiVersion: v1
kind: Namespace
metadata:
  name: dragonfly-system
---
apiVersion: helm.cattle.io/v1
kind: HelmChart
metadata:
  name: dragonfly
  namespace: dragonfly-system
spec:
  chart: dragonfly
  repo: https://dragonflyoss.github.io/helm-charts
  targetNamespace: dragonfly-system
  valuesContent: |-
    scheduler:
      replicas: 3
      resources:
        limits:
          cpu: 2
          memory: 4Gi
    manager:
      replicas: 3
      resources:
        limits:
          cpu: 1
          memory: 2Gi
    seedClient:
      replicas: 3
      resources:
        limits:
          cpu: 2
          memory: 4Gi
    dfdaemon:
      config:
        proxy:
          registryMirror:
            dynamic: true
            url: https://index.docker.io
          proxies:
            - regx: registry-1.docker.io
```

**节点配置**(用 dfdaemon 替代 docker pull):

```json
// /etc/docker/daemon.json
{
  "registry-mirrors": ["http://127.0.0.1:65001"]
}
```

dfdaemon 监听 127.0.0.1:65001,拦截 docker pull,自动走 P2P。

### 23.5.2 Nydus 部署

**1. 转换镜像为 Nydus 格式**:

```bash
# 安装 nydusify
wget https://github.com/dragonflyoss/nydus/releases/download/v2.2.4/nydus-static-v2.2.4-linux-amd64.tgz
tar xzf nydus-static-*.tgz
cp nydus-static/* /usr/local/bin/

# 转换
nydusify convert \
  --source docker://nginx:1.25 \
  --target docker://registry/nginx-nydus:1.25
```

**2. K8s 节点配置 Nydus snapshotter**:

```toml
# /etc/containerd/config.toml
[plugins."io.containerd.grpc.v1.cri".containerd]
  snapshotter = "nydus"

  [plugins."io.containerd.grpc.v1.cri".containerd.snapshotters]
    [plugins."io.containerd.grpc.v1.cri".containerd.snapshotters.nydus]
      root = "/var/lib/containerd/io.containerd.snapshotter.v1.nydus"
```

**3. 使用 Nydus 镜像**:

```yaml
spec:
  containers:
  - name: app
    image: registry/nginx-nydus:1.25
    # 自动延迟加载
```

### 23.5.3 Stargz 部署

**1. 转换镜像为 stargz**:

```bash
# 安装 stargz-snapshotter
git clone https://github.com/containerd/stargz-snapshotter.git
cd stargz-snapshotter
make install

# 转换
ctr-remote image optimize \
  --stargz \
  docker.io/library/nginx:1.25 \
  registry/nginx-stargz:1.25
```

**2. 节点配置 stargz**:

```toml
# /etc/containerd/config.toml
[plugins."io.containerd.grpc.v1.cri".containerd]
  snapshotter = "stargz"

  [plugins."io.containerd.grpc.v1.cri".containerd.snapshotters.stargz]
    root = "/var/lib/containerd-stargz-grpc"
```

### 23.5.4 镜像预热

```yaml
# image-preloader.yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: image-preloader
  namespace: kube-system
spec:
  selector:
    matchLabels:
      app: image-preloader
  template:
    metadata:
      labels:
        app: image-preloader
    spec:
      tolerations:
      - operator: Exists   # 所有节点
      containers:
      - name: preloader
        image: alpine:3.18
        command:
        - sh
        - -c
        - |
          while true; do
            # 拉取关键镜像
            for image in \
              nginx:1.25 \
              redis:7-alpine \
              myapp:v1.2.3; do
              crictl pull $image
            done
            sleep 1h
          done
        resources:
          requests:
            cpu: 100m
            memory: 128Mi
          limits:
            cpu: 500m
            memory: 512Mi
        volumeMounts:
        - name: containerd-sock
          mountPath: /run/containerd/containerd.sock
      volumes:
      - name: containerd-sock
        hostPath:
          path: /run/containerd/containerd.sock
```

### 23.5.5 多 registry 镜像配置

```yaml
# K8s Pod 用多源镜像
spec:
  containers:
  - name: app
    # 主 registry
    image: harbor-1.myorg.com/myapp:v1
    # 备份(在 Pod 中配置)
    # 通过 imagePullSecrets 配置多个 registry
  imagePullSecrets:
  - name: harbor-1-secret
  - name: harbor-2-secret
```

```bash
# 节点配置多 registry mirror
# /etc/containerd/config.toml
[plugins."io.containerd.grpc.v1.cri".registry.mirrors."harbor.myorg.com"]
  endpoint = [
    "https://harbor-1.myorg.com",
    "https://harbor-2.myorg.com",
    "https://harbor-3.myorg.com"
  ]
```

---

## 23.6 配置示例

### 23.6.1 Dragonfly 调优

```yaml
# dragonfly-config.yaml
scheduler:
  config:
    # 调度策略
    algorithm: "ml"  # 机器学习调度
    # peer 选择
    peer-selection:
      strategy: "best-peer"  # 最优 peer
      filter:
        - "same-subnet"  # 优先同子网
    # 分片大小
    piece-size: 4194304  # 4MB
    # 并发数
    concurrent:
      download: 6   # 同时 6 个分片
      upload: 4     # 同时上传 4 个

dfdaemon:
  config:
    # 缓存
    cache:
      type: "disk"
      path: "/var/cache/dragonfly"
      size: "100GB"
    # 限速
    rate-limit:
      download: "1Gbps"
      upload: "500Mbps"
```

### 23.6.2 镜像仓库同步

```yaml
# Harbor 复制策略
apiVersion: v1
kind: ConfigMap
metadata:
  name: harbor-replication
data:
  replication.yaml: |
    - name: cross-region-replication
      enabled: true
      trigger:
        type: scheduled
        cron: "0 */6 * * *"  # 每 6 小时
      filters:
        - repository: "myapp/*"
      destinations:
        - registry: harbor-us.myorg.com
          namespace: myapp
        - registry: harbor-eu.myorg.com
          namespace: myapp
        - registry: harbor-cn.myorg.com
          namespace: myapp
```

### 23.6.3 CDN 配置

```
                   用户请求
                       │
                       ▼
              ┌────────────────┐
              │  CDN(全球)    │
              │  - Cloudflare  │
              │  - 阿里 CDN    │
              │  - 腾讯 CDN    │
              └────────┬───────┘
                       │
                       │ 缓存未命中
                       ▼
              ┌────────────────┐
              │  源站 Harbor   │
              └────────────────┘
```

```nginx
# Nginx 反向代理 + 缓存
proxy_cache_path /var/cache/nginx levels=1:2 keys_zone=registry_cache:100m max_size=100g;

server {
    listen 443 ssl;
    server_name registry.myorg.com;

    location /v2/ {
        proxy_pass http://harbor-backend;
        proxy_cache registry_cache;
        proxy_cache_valid 200 24h;
        proxy_cache_valid 404 1m;
        proxy_cache_key $request_uri;
    }
}
```

---

## 23.7 工业案例

### 23.7.1 案例 1: 阿里 Dragonfly

**规模**:
- 日均分发: 1000万+ 镜像
- 峰值带宽: 10Tbps
- 节点数: 10万+
- P2P 比例: 95%+

**架构**:
```
                开发者
                  │
                  ▼
           ┌─────────────┐
           │  ACR(仓库) │
           └──────┬──────┘
                  │
                  ▼
           ┌─────────────┐
           │ Dragonfly   │
           │ Scheduler   │
           └──────┬──────┘
                  │
        ┌─────────┼─────────┐
        │         │         │
        ▼         ▼         ▼
     Peer1     Peer2     Peer3
        │         │         │
        └─────────┼─────────┘
                  │
                  ▼
              10万节点
```

**关键设计**:

1. **ML 调度**: 机器学习选最优 peer
2. **同子网优先**: 减少跨子网流量
3. **多源拉取**: 同时从 6 个 peer 拉
4. **层级缓存**: 节点 / 机架 / 集群

**性能**:
- 1GB 镜像 1000 节点: 5min(直连 60min)
- registry 带宽节省: 95%
- 部署速度提升: 12x

### 23.7.2 案例 2: 字节 Nydus

**场景**: 镜像大(2GB),启动慢,扩容延迟。

**优化**:

```
原方案:
  docker pull 2GB → 5min
  启动 → 30s
  总: 5.5min

Nydus:
  拉取 bootstrap → 5s
  启动 → 30s
  后台渐进拉取
  总: 35s
```

**收益**:
- 启动时间: 5.5min → 35s(10x)
- 带宽节省: 70%(只拉用到的)
- 用户体验: 显著改善

### 23.7.3 案例 3: Netflix Bullseye

**场景**: Netflix 跨 AZ 部署,镜像分发慢。

**方案**: Bullseye 预热系统。

```
1. CI 构建镜像
2. Bullseye 接收通知
3. 预热到所有节点
   - 多 AZ 并行
   - P2P 分发
4. 部署时本地已有
   - 秒级启动
```

**收益**:
- 部署时间: 30min → 2min
- AZ 间流量减少 80%
- 部署可靠性 99.9%+

### 23.7.4 案例 4: 腾讯 TCR

**腾讯云容器镜像服务**:
- 跨地域复制: 3 地区实时同步
- P2P 分发: 集成 Dragonfly
- 安全扫描: 内置 Trivy
- 镜像签名: Cosign

**性能**:
- 1GB 镜像 1000 节点: 4min
- 跨地域同步延迟: < 30s
- 可用性: 99.95%

### 23.7.5 性能基准

**分发性能对比**(1GB 镜像,1000 节点):

| 方案 | 时长 | registry 带宽 | 节点带宽 |
|------|------|--------------|---------|
| 直连 | 60min | 1Gbps(满) | 1Mbps |
| 镜像加速 | 30min | 500Mbps | 2Mbps |
| Dragonfly | 5min | 50Mbps | 1Gbps(P2P) |
| Nydus | 30s 启动 | 10Mbps | 按需 |
| Dragonfly + Nydus | 10s 启动 | 5Mbps | 按需 |

**启动时间对比**(2GB Java 镜像):

| 方案 | 启动时间 | 用户感知 |
|------|---------|---------|
| 直连 | 6min | 慢 |
| P2P | 1min | 可接受 |
| Nydus | 35s | 快 |
| Nydus + P2P | 15s | 极快 |

### 23.7.6 大厂分发方案对比

| 厂商 | 方案 | 规模 | 关键技术 |
|------|------|------|---------|
| 阿里 | Dragonfly | 10万节点 | P2P + ML 调度 |
| 字节 | Nydus | 自研 | 延迟加载 + FUSE |
| Netflix | Bullseye | 全球 | 预热 + P2P |
| 腾讯 | TCR | 多地区 | 复制 + P2P |
| Google | Bazel + 远程缓存 | 内部 | 增量 + 缓存 |
| AWS | ECR + S3 | 全球 | CDN + 加速 |

---

## 23.8 故障复盘

### 23.8.1 故障 1: registry 单点故障

**背景**: 2024-03,某公司 registry 宕机,所有部署失败。

**根因**:
- 单 registry,无备份
- 高峰期负载过高
- 数据库锁死

**修复**:
1. 紧急重启 registry
2. 部署多 registry + LB
3. 关键镜像多源
4. 接入 P2P(Dragonfly)

**预防**:
- registry 高可用(P0)
- 多区域备份
- P2P 降级(registry 故障也能拉)
- 定期 DR 演练

### 23.8.2 故障 2: P2P 调度器瓶颈

**背景**: 2024-04,某公司 Dragonfly 调度器 CPU 100%,分发变慢。

**根因**:
- 单 Scheduler 实例
- 5000 节点同时拉
- 调度器成为瓶颈

**修复**:
1. Scheduler 横向扩容(3 → 5)
2. 分片调度(不同镜像走不同 Scheduler)
3. 客户端缓存 peer 列表(减少调度请求)

**预防**:
- Scheduler 高可用
- 监控调度延迟
- 压测验证极限

### 23.8.3 故障 3: Nydus 兼容性问题

**背景**: 2024-05,某公司用 Nydus 后,部分应用启动失败。

**根因**:
- 应用启动时读取大量文件
- Nydus 按需拉取,延迟高
- 启动超时

**修复**:
1. 配置 Nydus 预读
2. 关键文件标记 prefetch
3. 兼容性问题应用回退普通镜像

**预防**:
- 测试 Nydus 兼容性
- 配置合理预读
- 监控启动延迟

### 23.8.4 故障 4: 跨地域分发慢

**背景**: 2024-06,某公司跨国部署,镜像拉取慢。

**根因**:
- 单地域 registry
- 跨国带宽低
- 延迟高

**修复**:
1. 多地域 registry 复制
2. CDN 加速
3. 跨地域 P2P

**预防**:
- 关键镜像多地域
- 跨地域延迟监控
- CDN + P2P 组合

### 23.8.5 故障 5: 镜像层损坏

**背景**: 2024-07,某公司部署后应用异常,排查发现镜像层损坏。

**根因**:
- 网络抖动导致下载不完整
- registry 存储 layer 损坏
- 无完整性校验

**修复**:
1. 清理损坏 layer
2. 重新拉取
3. 启用 digest 校验

**预防**:
- 启用 digest 校验(P0)
- 镜像签名(Cosign)
- 定期校验 registry 数据

---

## 23.9 最佳实践

### 23.9.1 分发方案选型

```
节点数 < 100:
└─ 直连 registry + 镜像加速器

节点数 100-1000:
└─ P2P(Dragonfly)

节点数 1000+:
└─ P2P + 延迟加载(Nydus)

跨地域:
└─ 多地域 registry + CDN + P2P
```

### 23.9.2 镜像仓库高可用

```
主 Harbor ──┐
            ├─ LB(健康检查)
备 Harbor ──┘
            │
            ├─ 主备同步(实时)
            │
            ▼
        多 AZ 部署
```

**关键**:
- 多副本(>= 3)
- 跨 AZ
- 实时同步
- 自动故障转移
- 定期备份

### 23.9.3 镜像优化

**减小镜像体积**:
1. 多阶段构建(参考 [15 章](./15-Dockerfile生产模板.md))
2. distroless/alpine
3. 共享 base image
4. layer 复用

**减小分发量**:
1. layer 复用(共享 base)
2. 增量更新(只发变更)
3. 镜像 diff(只发差异)

### 23.9.4 预热策略

```
关键业务:
├─ 部署前 30min 预热到所有节点
├─ DaemonSet 定时拉取最新版
└─ P2P 加速

普通业务:
├─ 部署时按需拉
├─ P2P 分发
└─ 延迟加载
```

### 23.9.5 监控告警

**关键指标**:
- registry CPU/内存/带宽
- P2P 比例(目标 90%+)
- 拉取时长(目标 < 1min)
- 拉取失败率(目标 < 0.1%)
- 跨地域同步延迟

**告警**:
- registry 带宽 > 80% → 告警
- P2P 比例 < 80% → 告警
- 拉取失败率 > 1% → 告警

---

## 23.10 常见陷阱

### 23.10.1 陷阱 1: 镜像加速器缓存过期

**问题**: 镜像加速器缓存旧镜像,新版本不生效。

**解决**:
- 缓存时间合理(24h)
- 重要镜像 push 后主动刷新
- 用 digest 而非 tag

### 23.10.2 陷阱 2: P2P 首次冷启动慢

**问题**: 首次部署,无 peer,等同于直连。

**解决**:
- 预热(提前拉)
- Seed peer 配置足够
- 监控首次部署

### 23.10.3 陷阱 3: Nydus 启动后卡顿

**问题**: 应用首次访问文件,触发拉取,卡顿。

**解决**:
- prefetch 关键文件
- 应用预热
- 监控文件读取延迟

### 23.10.4 陷阱 4: 跨地域同步延迟

**问题**: 跨地域 registry 同步延迟,部署到旧版本。

**解决**:
- 同步完成才部署
- 用 digest 锁定版本
- 监控同步状态

### 23.10.5 陷阱 5: registry 配额不足

**问题**: registry 存储配满,无法 push。

**解决**:
- 监控存储使用
- 定期清理旧镜像
- 配置 GC

### 23.10.6 陷阱 6: P2P peer 离线

**问题**: peer 拉取过程中离线,分片失败。

**解决**:
- 多源拉取(同时 6 个 peer)
- 失败重试
- Scheduler 监控 peer 健康度

---

## 23.11 面试题

### Q1: 1000 节点同时拉镜像,registry 撑不住怎么办?

**答**:
3 个方案:
1. **P2P 分发**(Dragonfly): peer 间共享,registry 负载降 95%
2. **延迟加载**(Nydus): 仅拉启动必需,启动 30s
3. **预热**: 提前推到节点

**组合**: P2P + 延迟加载 + 预热 = 10s 启动 1000 节点。

### Q2: Dragonfly 工作原理?

**答**:
1. 节点拉镜像,dfdaemon 拦截
2. 请求 Scheduler 获取 peer 列表
3. 多源并行拉分片(4MB/片)
4. 拉取完成后本节点成 peer
5. registry 仅作 seed

**收益**: 1000 节点 1GB 镜像 5min(直连 60min)。

### Q3: Nydus 延迟加载原理?

**答**:
- 镜像转 Nydus 格式(元数据 + 数据块)
- 启动时仅拉 bootstrap(几 KB)
- 运行时按需拉取文件 chunk
- 后台渐进拉取

**收益**: 2GB 镜像启动 35s(原 5.5min)。

### Q4: 镜像层去重怎么实现?

**答**:
- layer 用 content-addressable(内容 hash 作 digest)
- 同 digest 的 layer 跨镜像共享
- 节点已有该 layer 不重复拉
- registry 也只存一份

**收益**: 节点存储/带宽节省 90%+。

### Q5: 镜像仓库怎么高可用?

**答**:
1. 多副本(>= 3)
2. 跨 AZ 部署
3. LB 健康检查
4. 实时主备同步
5. 定期备份
6. 多地域复制(关键业务)
7. P2P 降级(registry 故障也能拉)

### Q6: 怎么加速跨地域镜像分发?

**答**:
1. 多地域 registry(实时复制)
2. CDN 加速(全球缓存)
3. 跨地域 P2P
4. 增量同步(只发变更)
5. 预热策略

### Q7: 镜像预热有什么坑?

**答**:
- 占用节点存储(需清理旧版本)
- 带宽消耗(非高峰期执行)
- 版本管理(避免预热错版本)
- 监控(预热成功率)

### Q8: Stargz 和 Nydus 区别?

**答**:
| 维度 | Stargz | Nydus |
|------|--------|-------|
| 来源 | Google | 阿里 |
| 实现 |容器运行时| FUSE |
| 格式 | stargz | 自定义 |
| 兼容 | OCI | OCI |
| 性能 | 类似 | 略优 |

### Q9: 怎么验证镜像完整性?

**答**:
1. **digest 校验**: 拉取后比对 sha256
2. **Cosign 签名**: 镜像签名验证
3. **SBOM**: 软件物料清单
4. **准入控制**: Kyverno 强制验证

### Q10: 大规模镜像分发成本怎么控制?

**答**:
1. **P2P**: 减少 registry 出口带宽
2. **layer 复用**: 减少传输量
3. **镜像瘦身**: 1GB → 50MB
4. **延迟加载**: 按需拉取
5. **跨地域**: 就近拉取
6. **预热**: 非高峰期执行

---

## 23.12 总结

### 23.12.1 核心要点

1. **大规模分发挑战**: 带宽/并发/延迟/抖动
2. **P2P 是核心**: Dragonfly 降 95% registry 负载
3. **延迟加载加速启动**: Nydus 30s 启动
4. **预热保部署**: 关键镜像提前推
5. **多源增加可靠**: 多 registry + CDN
6. **监控是保障**: 防止过载与故障

### 23.12.2 工业定位

- **阿里 Dragonfly**: P2P + ML 调度,95%+ P2P 比例
- **字节 Nydus**: 延迟加载,启动 10x 加速
- **Netflix Bullseye**: 预热 + P2P,30min → 2min
- **腾讯 TCR**: 多地域 + P2P,99.95% 可用

### 23.12.3 选型决策

```
节点数:
├─ < 100 → 直连 + 镜像加速器
├─ 100-1000 → P2P(Dragonfly)
└─ 1000+ → P2P + 延迟加载(Nydus)

跨地域:
└─ 多 registry + CDN + P2P

关键业务:
└─ 预热 + P2P + 多源
```

### 23.12.4 与其他章节联系

- **[13-镜像仓库与分发](./13-镜像仓库与分发.md)**: 仓库基础
- **[15-Dockerfile生产模板](./15-Dockerfile生产模板.md)**: 镜像瘦身
- **[17-生产实践](./17-生产实践.md)**: 大规模部署
- **[22-集群密度优化](./22-集群密度优化.md)**: 节点资源优化

---

## 23.13 参考资料

### P2P 分发
- [Dragonfly](https://d7y.io/)
- [Kraken](https://github.com/uber/kraken)
- [Dragonfly Documentation](https://dragonflyoss.github.io/dragonflydocs/)

### 延迟加载
- [Nydus](https://nydus.dev/)
- [Stargz](https://github.com/containerd/stargz-snapshotter)
- [Nydus Documentation](https://github.com/dragonflyoss/nydus)

### 镜像仓库
- [Harbor](https://goharbor.io/)
- [阿里 ACR](https://www.aliyun.com/product/acr)
- [腾讯 TCR](https://cloud.tencent.com/product/tcr)
- [AWS ECR](https://aws.amazon.com/ecr/)
- [Google Artifact Registry](https://cloud.google.com/artifact-registry)

### 工业实践
- [阿里 Dragonfly 实践](https://developer.aliyun.com/article/782018)
- [字节 Nydus 实践](https://bytedance.feishu.cn/docs/)
- [Netflix Bullseye](https://netflixtechblog.com/)
- [Uber Kraken](https://www.uber.com/blog/kraken-open-source-peer-to-peer-docker-registry/)

### 标准与规范
- [OCI Distribution Spec](https://github.com/opencontainers/distribution-spec)
- [OCI Image Spec](https://github.com/opencontainers/image-spec)

---

> 下一章: [24-供应链安全](./24-供应链安全.md) - 容器供应链安全深度实践

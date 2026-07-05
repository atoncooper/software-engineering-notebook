# 18. Docker Swarm

> 章节定位: 编排生态篇 · 第一章
> 前置章节: [17-生产实践](./17-生产实践.md)
> 后续章节: [19-生态对比](./19-生态对比.md)

---

## 18.1 思维导图

```
                Docker Swarm
                     │
        ┌────────────┼────────────┐
        │            │            │
     架构         核心概念       操作
        │            │            │
   ┌────┴────┐  ┌────┴────┐  ┌────┴────┐
   │         │  │         │  │         │
 Manager   Service   部署服务
 Worker    Task     滚动更新
 Node      Stack    扩缩容
 Raft      Network  回滚
        │            │            │
        └────────────┼────────────┘
                     │
                     ▼
              适用场景决策
                     │
   ┌─────────────────┼─────────────────┐
   │                 │                 │
 小团队/简单场景   边缘计算         K8s 过重
 单集群 < 50 节点   IoT 设备         学习成本高
```

**Swarm vs K8s 一句话定位**:

| 维度 | Swarm | K8s |
|------|-------|-----|
| 复杂度 | 低 | 高 |
| 功能 | 基础 | 丰富 |
| 学习曲线 | 1 天 | 1 月+ |
| 生态 | 小 | 巨大 |
| 适用 | 小规模/简单 | 大规模/复杂 |

---

## 18.2 章节简介

Docker Swarm 是 Docker 原生容器编排工具,2016 年随 Docker 1.12 发布。它内置于 Docker Engine,无需额外安装,用 `docker` 命令即可管理集群。

虽然 K8s 已成为编排事实标准,但 Swarm 在以下场景仍有价值:
- **小团队/简单业务**: 不需要 K8s 复杂性
- **边缘计算**: 资源受限,Swarm 轻量
- **快速 PoC**: 几分钟搭建集群
- **Docker Desktop**: 本地开发多容器编排

本章系统讲解 Swarm 架构、核心概念、操作实践,并与 K8s 对比,帮助选型决策。

**本章工业焦点**:
- Docker 公司 Swarm 战略失误回顾
- Swarm 在边缘计算/IoT 的新生命
- 阿里 / 字节为何不用 Swarm
- Swarm 模式下 docker-compose 的演化

---

## 18.3 核心概念

### 18.3.1 Swarm 架构

```
        ┌──────────────────────────────────────────┐
        │           Swarm Cluster                  │
        │                                          │
        │   ┌──────────────┐  ┌──────────────┐    │
        │   │  Manager 1   │  │  Manager 2   │    │
        │   │  (Leader)    │◄►│  (Follower)  │    │
        │   │   Raft       │  │   Raft       │    │
        │   └──────┬───────┘  └──────┬───────┘    │
        │          │                 │            │
        │          └────────┬────────┘            │
        │                   │                     │
        │            ┌──────┴──────┐              │
        │            │  Manager 3  │              │
        │            │  (Follower) │              │
        │            └──────┬──────┘              │
        │                   │                     │
        │   ┌───────────────┼───────────────┐    │
        │   │               │               │    │
        │   ▼               ▼               ▼    │
        │ Worker 1      Worker 2       Worker 3  │
        │ (容器运行)   (容器运行)    (容器运行)  │
        └──────────────────────────────────────────┘

Manager 数: 奇数(3/5/7),Raft 一致性
Worker 数: 任意,运行容器
Manager 也可运行容器(但生产建议分离)
```

### 18.3.2 核心概念

**1. Node(节点)**:
- **Manager**: 管理集群状态,调度任务,Raft 一致性
- **Worker**: 接收 Manager 调度,运行容器

**2. Service(服务)**:
- 集群中部署的应用抽象
- 指定镜像、副本数、网络、端口等
- 类似 K8s 的 Deployment

**3. Task(任务)**:
- Service 的单个容器实例
- 不可变(调度后即固定到某节点)
- 类似 K8s 的 Pod

**4. Stack**:
- 多 Service 的组合(类似 docker-compose)
- 用 docker-compose.yml 部署

**5. Raft 共识**:
- Manager 间数据一致性
- 多数派写入(2/3, 3/5)
- Leader 选举自动故障转移

### 18.3.3 Service 模式

```bash
# 1. Replicated(副本模式,默认)
docker service create --replicas 3 --name web nginx
# 3 个副本分布在集群

# 2. Global(全局模式)
docker service create --mode global --name agent monitoring-agent
# 每个节点一个副本
```

**对比**:

| 模式 | 副本数 | 调度 | 适用 |
|------|-------|------|------|
| Replicated | 任意 | 自动分布 | 业务服务 |
| Global | = 节点数 | 每节点一个 | 日志采集/监控 |

### 18.3.4 调度约束

```bash
# 节点标签
docker node update --label-add env=prod node-1
docker node update --label-add env=stg node-2

# 服务约束
docker service create \
  --name web \
  --constraint node.labels.env==prod \
  --replicas 3 \
  nginx

# 资源约束
docker service create \
  --name db \
  --reserve-cpu 2 \
  --reserve-memory 2g \
  --limit-cpu 4 \
  --limit-memory 4g \
  postgres
```

### 18.3.5 网络模型

**1. ingress(入口网络)**:
- 路由网格(Routing Mesh)
- 任意节点访问服务端口,自动路由到容器
- 类似 K8s NodePort + 负载均衡

**2. overlay(覆盖网络)**:
- 跨节点容器通信
- VXLAN 隧道
- 加密可选

**3. bridge/host**:
- 单节点网络
- 较少用于 Swarm

```bash
# 创建 overlay 网络
docker network create -d overlay --attachable my-net

# Service 用 overlay
docker service create --network my-net --name app my-image
```

---

## 18.4 底层原理

### 18.4.1 Raft 共识

```
Manager 1(Leader)        Manager 2(Follower)    Manager 3(Follower)
       │                        │                       │
       │  1. Client 写入         │                       │
       ▼                        │                       │
   ┌───────┐                    │                       │
   │ 提议  │───2. 复制────────►│                       │
   └───────┘                    │                       │
       │                        │                       │
       │                   3. 确认                    3. 确认
       │◄───────────────────────┴───────────────────────┘
       │
       │ 4. 多数派确认,提交
       ▼
   ┌───────┐
   │ 提交  │───5. 通知 Followers
   └───────┘
       │
       │ 6. 返回客户端成功
       ▼
```

**关键参数**:
- 选举超时: 1500ms
- 心跳间隔: 500ms
- 多数派: ceil(N/2) + 1

**故障场景**:
- Leader 故障: Follower 选举新 Leader(< 5s)
- 少数 Manager 故障: 集群正常
- 多数 Manager 故障: 集群只读(不能写入)

### 18.4.2 调度算法

```
调度步骤:
1. 过滤(Filtering)
   - 节点状态(active/pause/drain)
   - 资源满足(CPU/内存)
   - 约束满足(label)
   - 端口不冲突

2. 排序(Ranking)
   - 资源最少的节点优先(填满式)
   - 或最空闲的节点优先(平衡式)

3. 选择最佳节点
```

**节点状态**:

| 状态 | 接收新任务 | 现有任务 | 说明 |
|------|-----------|---------|------|
| Active | ✓ | 运行 | 正常 |
| Pause | ✗ | 运行 | 不调度新任务 |
| Drain | ✗ | 迁移 | 维护模式 |

```bash
# 维护节点
docker node update --availability drain node-1
# 任务自动迁移到其他节点
```

### 18.4.3 路由网格(Routing Mesh)

```
                    客户端请求
                        │
                        ▼ :80
              ┌─────────────────┐
              │ 任意节点        │
              │ (ingress 网格)  │
              └────────┬────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼
    Node-1:web     Node-2:web     Node-3:web
    (容器)         (容器)         (容器)
```

**原理**:
- 每个节点都监听 service 暴露的端口
- 收到请求后,通过 IPVS 负载均衡到所有容器
- 即使本节点无容器,也会转发

**注意**:
- 路由网格会增加一跳延迟(~1ms)
- 性能敏感场景可用 `--endpoint-mode dnsrr`

### 18.4.4 Service 滚动更新

```bash
docker service create \
  --name web \
  --replicas 6 \
  --update-parallelism 2 \       # 每次更新 2 个
  --update-delay 30s \           # 间隔 30s
  --update-failure-action rollback \  # 失败回滚
  --update-order start-first \   # 先启新再停旧
  nginx:1.25

# 更新镜像
docker service update --image nginx:1.26 web
```

**流程**:
```
1. 启动 2 个新版本容器(v2)
2. 等待健康检查通过
3. 停止 2 个旧版本容器(v1)
4. 等待 30s
5. 重复直到全部更新
```

### 18.4.5 Service 与 Task 状态机

```
Task 状态:
  New → Allocated → Assigned → Accepted → Preparing → Ready → Starting → Running
                                                                          │
                                                                          ▼
                                                              Completed / Failed

Service 状态:
  Created → Running → Updating → Running
```

---

## 18.5 代码实现

### 18.5.1 集群搭建

```bash
# 1. 初始化 Manager
docker swarm init --advertise-addr 192.168.1.10:2377

# 输出加入命令:
# docker swarm join --token SWMTKN-xxx 192.168.1.10:2377

# 2. Worker 加入
docker swarm join --token SWMTKN-xxx 192.168.1.10:2377

# 3. 查看节点
docker node ls

# 4. 获取 Manager 加入命令(若需扩 Manager)
docker swarm join-token manager

# 5. 提升 Worker 为 Manager
docker node promote node-2

# 6. 离开集群
docker swarm leave        # Worker
docker swarm leave --force # Manager(谨慎,集群可能丢)
```

### 18.5.2 Service 操作

```bash
# 1. 创建 Service
docker service create \
  --name web \
  --replicas 3 \
  --publish 80:80 \
  --network my-net \
  --env ENV=prod \
  --mount type=volume,src=data,dst=/data \
  --health-cmd "curl -f http://localhost/health" \
  --health-interval 10s \
  --constraint node.labels.env==prod \
  --reserve-cpu 1 --reserve-memory 512m \
  --limit-cpu 2 --limit-memory 1g \
  --update-parallelism 1 \
  --update-delay 30s \
  --restart-condition on-failure \
  --restart-max-attempts 3 \
  nginx:1.25

# 2. 查看 Service
docker service ls
docker service ps web
docker service inspect web

# 3. 扩缩容
docker service scale web=6

# 4. 更新镜像
docker service update --image nginx:1.26 web

# 5. 更新配置
docker service update --env-add DEBUG=true web
docker service update --publish-rm 80:80 --publish-add 8080:80 web

# 6. 回滚
docker service rollback web

# 7. 删除
docker service rm web
```

### 18.5.3 Stack 部署

`docker-compose.yml`:

```yaml
version: "3.9"

services:
  web:
    image: nginx:1.25
    deploy:
      replicas: 3
      update_config:
        parallelism: 1
        delay: 30s
        failure_action: rollback
        order: start-first
      rollback_config:
        parallelism: 0  # 一次性回滚
      restart_policy:
        condition: on-failure
        max_attempts: 3
      resources:
        limits:
          cpus: "1"
          memory: 512M
        reservations:
          cpus: "0.5"
          memory: 256M
      placement:
        constraints:
          - node.labels.env == prod
    ports:
      - "80:80"
    networks:
      - frontend
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost/health"]
      interval: 10s
      timeout: 3s
      retries: 3

  redis:
    image: redis:7-alpine
    deploy:
      replicas: 1
      placement:
        constraints:
          - node.role == manager
    networks:
      - backend
    volumes:
      - redis-data:/data

volumes:
  redis-data:

networks:
  frontend:
    driver: overlay
  backend:
    driver: overlay
    internal: true  # 内部网络,不暴露
```

```bash
# 部署 Stack
docker stack deploy -c docker-compose.yml myapp

# 查看
docker stack ls
docker stack services myapp
docker stack ps myapp

# 删除
docker stack rm myapp
```

### 18.5.4 Secret 与 Config

```bash
# 1. 创建 Secret
echo "mydbpassword" | docker secret create db_password -

# 2. 创建 Config
docker config create nginx.conf ./nginx.conf

# 3. Service 使用
docker service create \
  --name web \
  --secret db_password \
  --config source=nginx.conf,target=/etc/nginx/nginx.conf \
  nginx
```

```yaml
# docker-compose.yml
services:
  web:
    image: nginx
    secrets:
      - db_password
    configs:
      - source: nginx_conf
        target: /etc/nginx/nginx.conf

secrets:
  db_password:
    external: true

configs:
  nginx_conf:
    external: true
```

**底层**:
- Secret: /run/secrets/<name>(临时文件,内存 tmpfs)
- Config: /etc/<name>(只读文件)
- Swarm 自动分发到所有运行该 service 的节点

### 18.5.5 多阶段 Stack(开发/生产)

```yaml
# docker-compose.yml(基础)
version: "3.9"
services:
  web:
    image: myapp:${TAG:-latest}
    ports:
      - "80:80"
```

```yaml
# docker-compose.prod.yml(生产 override)
version: "3.9"
services:
  web:
    deploy:
      replicas: 6
      update_config:
        parallelism: 2
        delay: 30s
      resources:
        limits:
          cpus: "2"
          memory: 1G
    environment:
      ENV: production
```

```bash
# 部署
docker stack deploy \
  -c docker-compose.yml \
  -c docker-compose.prod.yml \
  myapp
```

---

## 18.6 配置示例

### 18.6.1 节点标签管理

```bash
# 给节点打标签
docker node update --label-add env=prod node-1
docker node update --label-add env=stg node-2
docker node update --label-add env=dev node-3
docker node update --label-add role=db node-1
docker node update --label-add role=web node-2

# 查看
docker node inspect node-1 | jq '.[0].Spec.Labels'
```

### 18.6.2 网络隔离

```yaml
# 三层网络隔离
networks:
  frontend:        # 前端,对外
    driver: overlay
  app:             # 应用层,仅 frontend 可访问
    driver: overlay
    internal: true
  db:              # 数据库层,仅 app 可访问
    driver: overlay
    internal: true

services:
  nginx:
    networks: [frontend, app]
  api:
    networks: [app, db]
  postgres:
    networks: [db]
```

### 18.6.3 健康检查与重启

```yaml
services:
  web:
    image: myapp
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost/health"]
      interval: 10s
      timeout: 3s
      retries: 3
      start_period: 30s
    deploy:
      restart_policy:
        condition: on-failure
        delay: 5s
        max_attempts: 3
        window: 120s
```

### 18.6.4 日志驱动

```bash
# 全局配置(/etc/docker/daemon.json)
{
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "50m",
    "max-file": "5"
  }
}

# 单 service 配置
docker service create \
  --log-driver fluentd \
  --log-opt fluentd-address=localhost:24224 \
  --log-opt tag="docker.{{.ServiceName}}" \
  nginx
```

---

## 18.7 工业案例与基准数据

### 18.7.1 案例 1: Docker 公司 Swarm 战略失误

**时间线**:
- 2014: Kubernetes 发布
- 2016: Docker 1.12 集成 Swarm(standalone → swarm mode)
- 2017: K8s 占据主流,Docker 公司战略调整
- 2020: Docker 企业版卖给 Mirantis
- 2024: K8s 占容器编排 95%+ 份额

**失误原因**:
1. **生态投入不足**: K8s 有 CNCF 生态,Swarm 孤立
2. **功能滞后**: 没有 CRD/operator 等扩展机制
3. **多集群管理弱**: K8s 有 Cluster API,Swarm 无
4. **云厂商力推 K8s**: AWS/Azure/GCP 都主推 K8s
5. **社区分裂**: Docker 公司与社区矛盾

**经验教训**:
- 技术选型不仅看功能,看生态
- 简单不等于赢(够用即可的边界难把握)
- 标准化比私有方案更重要

### 18.7.2 案例 2: Swarm 在边缘计算的新生命

**场景**: 边缘节点资源受限(2C/4G),K8s 过重。

**Swarm 优势**:
- 二进制 50MB,内存占用 < 100MB
- 启动快(< 5s)
- 配置简单(yaml 即可)

**案例**: 某 IoT 公司 1000+ 边缘节点用 Swarm:
- 镜像分发: P2P + 增量
- 配置管理: docker config
- 远程管理: Manager 集中
- 故障自愈: 自动重启

### 18.7.3 性能对比

**资源占用对比**(空集群):

| 编排器 | 内存 | CPU | 二进制大小 |
|--------|------|-----|-----------|
| Swarm | 100MB | 1% | 50MB |
| K8s(完整) | 2GB | 5% | 500MB+ |
| K3s(轻量 K8s) | 500MB | 3% | 100MB |

**部署延迟对比**(3 副本 nginx):

| 操作 | Swarm | K8s |
|------|-------|-----|
| 创建 service | 2s | 5s |
| 扩容到 10 副本 | 5s | 10s |
| 滚动更新 | 30s | 60s |

**调度性能**(1000 节点):

| 操作 | Swarm | K8s |
|------|-------|-----|
| 启动 1000 Pod | 60s | 90s |
| 节点故障感知 | 5s | 30s |
| API 响应 | 50ms | 200ms |

### 18.7.4 大厂选型

| 厂商 | 编排器 | 原因 |
|------|--------|------|
| 阿里 | K8s(ACK) | 规模大,需要 K8s 生态 |
| 字节 | K8s(自研) | 自定义需求强 |
| Netflix | Titus(基于 K8s) | 与 AWS 集成 |
| Google | Borg → K8s | 自家产品 |
| Docker Hub | Swarm(内部小工具) | 简单场景 |
| 部分边缘厂 | Swarm/K3s | 资源受限 |

**结论**: 大厂无例外选择 K8s,Swarm 仅在边缘/小规模场景有零星应用。

---

## 18.8 故障复盘

### 18.8.1 故障 1: Manager 多数故障导致集群不可用

**背景**: 2024-03,某公司 3 Manager 集群,2 个同时故障,集群不可用。

**现象**:
- 无法调度新任务
- 现有容器仍运行(但无法管理)
- `docker service ls` 卡住

**根因**:
- 3 个 Manager 在同一物理机(违反高可用原则)
- 物理机故障,2 个 Manager 宕机
- Raft 失去多数派(只剩 1/3),无法写入

**修复过程**:
1. 紧急恢复 1 个 Manager(凑齐 2/3 多数派)
2. 重新分布 Manager 到不同物理机
3. 备份 Raft 数据,验证一致性

**预防措施**:
- **Manager 跨物理机**(P0)
- **Manager 跨可用区**(生产)
- **Manager 数为奇数**(3/5/7)
- **Raft 备份**: 定期备份 `/var/lib/docker/swarm`

### 18.8.2 故障 2: 路由网格延迟过高

**背景**: 2024-04,某公司 API 延迟从 5ms 升到 15ms,排查发现是 Swarm 路由网格。

**现象**:
- 客户端请求任意节点,被转发到容器所在节点
- 多一跳,延迟 +10ms
- 高 QPS 下,转发节点 CPU 高

**根因**:
- 路由网格默认启用
- 所有流量经过 ingress 网络
- 转发节点成为瓶颈

**修复过程**:
```bash
# 改用 dnsrr 模式(直接 DNS 轮询)
docker service create \
  --endpoint-mode dnsrr \
  --name web \
  nginx
```

但 dnsrr 模式下端口发布复杂,需配合外部 LB。

**预防措施**:
- **高 QPS 用 dnsrr + 外部 LB**
- **低延迟用 host 模式**
- **生产引入外部 LB**(HAProxy/Nginx)

### 18.8.3 故障 3: Stack 更新卡住

**背景**: 2024-05,某公司 Stack 滚动更新卡住,长时间不完成。

**现象**:
- `docker stack deploy` 后,部分 task 一直 Preparing
- 实际镜像拉取超时
- update_config 串行,卡一个全部等

**根因**:
- 镜像 1GB+,节点网络慢
- `--update-parallelism 1` 串行
- 单 task 超时未失败,卡死队列

**修复过程**:
1. 改用并行更新
   ```yaml
   deploy:
     update_config:
       parallelism: 3
       delay: 10s
       failure_action: rollback
   ```
2. 镜像瘦身(1GB → 50MB)
3. 配置镜像拉取超时
   ```bash
   docker service update --update-delay 0 --task-max-attempts 1 web
   ```

**预防措施**:
- **镜像必须瘦身**(P0)
- **并行更新**(parallelism > 1)
- **failure_action: rollback**(自动回滚)
- **超时配置合理**

---

## 18.9 最佳实践

### 18.9.1 Swarm 适用场景

**适合**:
- 单集群 < 50 节点
- 业务简单(无状态服务)
- 团队无 K8s 经验
- 边缘计算/IoT
- PoC/测试环境

**不适合**:
- 大规模(> 100 节点)
- 复杂业务(需 operator/CRD)
- 多集群管理
- 强生态需求(需 Prometheus/Grafana/...)

### 18.9.2 Manager 高可用

- **3 或 5 个 Manager**(奇数)
- **跨物理机**(防单机故障)
- **跨可用区**(生产)
- **Manager 不运行业务容器**(分离)
- **Raft 备份**(每日)

### 18.9.3 网络设计

```
┌─────────────────────────────────────┐
│ 外部 LB(HAProxy/Nginx)            │
└─────────────┬───────────────────────┘
              │
      ┌───────┴───────┐
      │  Swarm 节点    │
      │ (dnsrr 模式)  │
      └───────────────┘
```

- 生产用外部 LB + dnsrr(避免路由网格延迟)
- 内部通信用 overlay
- 数据库用 internal 网络

### 18.9.4 升级与维护

```bash
# 1. 节点维护
docker node update --availability drain node-1
# 等待任务迁移
docker node ps node-1
# 维护...
docker node update --availability active node-1

# 2. 镜像滚动更新
docker service update \
  --image nginx:1.26 \
  --update-parallelism 2 \
  --update-delay 30s \
  --update-failure-action rollback \
  web

# 3. Stack 更新
docker stack deploy -c docker-compose.yml myapp
```

### 18.9.5 监控

- **node-exporter**: 主机指标
- **cadvisor**: 容器指标
- **docker metrics**: daemon 内置(/metrics 端口)
- **Prometheus**: 集中采集

```bash
# 启用 daemon metrics
# /etc/docker/daemon.json
{
  "metrics-addr": "0.0.0.0:9323"
}
```

---

## 18.10 常见陷阱

### 18.10.1 陷阱 1: 单 Manager 集群

**问题**: 单 Manager,故障即集群不可用。

**解决**: 至少 3 Manager。

### 18.10.2 陷阱 2: compose 文件混用

**问题**: 把 `docker-compose up` 用 Swarm 部署。

**解决**:
- `docker-compose up`: 单机
- `docker stack deploy`: Swarm

### 18.10.3 陷阱 3: 全局 Service 误用

**问题**: Global 模式 + 扩容,无效。

**解决**:
- Replicated: 可扩缩容
- Global: 每节点一个,不能扩缩

### 18.10.4 陷阱 4: 滚动更新无回滚

**问题**: 更新失败,卡在中间状态。

**解决**:
```bash
docker service update \
  --update-failure-action rollback \
  web
```

### 18.10.5 陷阱 5: Secret 泄露到镜像

**问题**: 把 secret 写到镜像里。

**解决**: 用 docker secret,运行时挂载。

### 18.10.6 陷阱 6: 节点标签丢失

**问题**: 节点重启后标签丢失,调度失败。

**解决**:
- 标签写在 daemon.json(持久化)
- 或用 ansible 自动化恢复

---

## 18.11 面试题

### Q1: Swarm 和 K8s 怎么选?

**答**:
- **Swarm**: 小规模(< 50 节点)、简单业务、边缘计算、无 K8s 经验团队
- **K8s**: 大规模、复杂业务、需要生态、多集群

**一句话**: 99% 场景选 K8s,Swarm 仅适合极小规模或边缘。

### Q2: Swarm 的 Raft 共识怎么工作?

**答**:
- Manager 节点间用 Raft 保证一致性
- 写入需多数派确认(N/2 + 1)
- Leader 故障自动选举(< 5s)
- 多数 Manager 故障则集群只读

**关键**: Manager 数为奇数(3/5/7),跨物理机/AZ。

### Q3: Swarm 路由网格是什么?

**答**:
- 任意节点访问 service 端口,自动路由到容器
- 基于 IPVS 负载均衡
- 即使本节点无容器也转发

**代价**: 多一跳,延迟 +1-10ms。

**替代**: dnsrr 模式 + 外部 LB(高性能场景)。

### Q4: Service 的 Replicated 和 Global 区别?

**答**:
- **Replicated**: 指定副本数,调度器分布
- **Global**: 每节点一个,自动随节点数变化

**适用**:
- Replicated: 业务服务
- Global: 日志采集/监控(每节点都需要)

### Q5: Swarm 怎么实现滚动更新?

**答**:
```bash
docker service update \
  --image nginx:1.26 \
  --update-parallelism 2 \    # 每次更新 2 个
  --update-delay 30s \         # 间隔 30s
  --update-failure-action rollback \
  web
```

流程: 启动新 → 健康检查 → 停旧 → 重复。

### Q6: Swarm 的 Secret 怎么工作?

**答**:
- 加密存储在 Raft log
- 自动分发到运行该 service 的节点
- 容器内挂载为 /run/secrets/<name>(tmpfs)
- 不会出现在镜像、环境变量、docker inspect

### Q7: Swarm 集群最大能多大?

**答**:
- 官方推荐 < 100 节点
- 实测 1000 节点也能跑,但调度变慢
- Raft 在 > 7 Manager 时性能下降

**结论**: 大规模选 K8s。

### Q8: Swarm 怎么跨集群通信?

**答**:
- Swarm 原生不支持跨集群
- 需用 overlay 网络互联(手动配置)
- 或引入服务网格(Istio/Linkerd)

**结论**: 多集群选 K8s + Cluster API。

### Q9: Swarm 怎么备份?

**答**:
```bash
# 备份 Manager
docker swarm unlock-key  # 解锁密钥
cp -r /var/lib/docker/swarm/ backup/

# 恢复
docker swarm init --force-new-cluster
cp -r backup/swarm/ /var/lib/docker/swarm/
systemctl restart docker
```

### Q10: Swarm 还值得学吗?

**答**:
**学习价值**:
- 理解编排基本概念(Service/Task/Scheduling)
- 与 K8s 对比,理解 K8s 设计
- 简单场景仍可用(本地/边缘)

**生产价值**:
- 大厂无例外用 K8s
- Swarm 仅在边缘/IoT 零星应用

**结论**: 了解概念即可,精力放 K8s。

---

## 18.12 总结

### 18.12.1 核心要点

1. **Swarm 是 Docker 原生编排**,内置于 Docker Engine
2. **架构简单**: Manager/Worker + Raft
3. **核心概念**: Node/Service/Task/Stack
4. **路由网格**: 任意节点访问,自动路由
5. **滚动更新**: parallelism + delay + rollback
6. **生态劣势**: 不如 K8s 丰富
7. **适用场景**: 小规模/简单/边缘

### 18.12.2 工业定位

- **大厂**: 99% 用 K8s
- **小厂/边缘**: 部分用 Swarm/K3s
- **趋势**: K8s 一统天下,Swarm 逐渐边缘化
- **价值**: 学习概念 + 极简场景

### 18.12.3 决策树

```
你的场景:
├─ 大规模(> 50 节点) → K8s
├─ 复杂业务(需 operator) → K8s
├─ 多集群 → K8s
├─ 边缘/IoT(资源受限) → Swarm / K3s
├─ 本地开发 → Docker Compose
└─ 简单生产(几节点) → Swarm(可考虑) / K3s(更推荐)
```

### 18.12.4 与其他章节联系

- **[07-Docker-Compose](./07-Docker-Compose.md)**: Compose 是 Swarm 的部署单元
- **[11-OCI规范与运行时](./11-OCI规范与运行时.md)**: Swarm 与 K8s 都用 OCI 运行时
- **[17-生产实践](./17-生产实践.md)**: 生产环境高可用实践
- **[19-生态对比](./19-生态对比.md)**: 与 K8s/Nomad 详细对比

---

## 18.13 参考资料

### 官方文档
- [Docker Swarm](https://docs.docker.com/engine/swarm/)
- [Swarm mode overview](https://docs.docker.com/engine/swarm/key-concepts/)
- [docker service](https://docs.docker.com/engine/reference/commandline/service/)
- [docker stack](https://docs.docker.com/engine/reference/commandline/stack/)

### 工业实践
- [Docker 公司战略回顾](https://www.infoq.com/articles/docker-swarm-kubernetes/)
- [Swarm vs Kubernetes](https://logz.io/blog/docker-swarm-vs-kubernetes/)
- [Swarm at edge](https://www.hpe.com/insights/edge-computing.html)

### 替代方案
- [K3s](https://k3s.io/) - 轻量 K8s
- [Nomad](https://www.nomadproject.io/) - HashiCorp 编排
- [MicroK8s](https://microk8s.io/) - 单机 K8s

### 学习资源
- [Swarm tutorial](https://docs.docker.com/engine/swarm/swarm-tutorial/)
- [Play with Docker Swarm](https://labs.play-with-docker.com/)

---

> 下一章: [19-生态对比](./19-生态对比.md) - Docker 生态全景与编排器对比

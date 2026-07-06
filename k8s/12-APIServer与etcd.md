# 12 - APIServer 与 etcd

> APIServer 是 K8s 的网关,etcd 是 K8s 的真相源。两者构成 K8s 的"大脑"。本章从 REST 流水线到 etcd Raft 实现,源码级剖析这对核心组件。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- APIServer 的请求流水线(Authn → Authz → Admission → etcd)细节?
- etcd 的 Raft 实现?Leader 选举、日志复制、快照?
- watch cache 怎么工作?为什么降低 etcd 压力?
- APF(API Priority and Fairness)怎么防雪崩?
- 大规模集群 etcd 怎么扩展?

### 1.2 不解决什么

- 不讲 kubectl 实现(见第 03 章)
- 不讲 kubelet 内部(见第 13 章)
- 不讲 controller-manager(见第 09 章)

---

## 2. 直觉解释

### 2.1 "前台 + 档案室"类比

```
   ┌─────────────────────────────────────────┐
   │  APIServer(前台)                      │
   │  - 接待所有访客                         │
   │  - 验身份(Authn)                      │
   │  - 验权限(Authz)                      │
   │  - 改格式(Mutating Admission)         │
   │  - 校验(Validating Admission)         │
   │  - 存档(写入 etcd)                    │
   │  - 推送通知(Watch)                    │
   └────────────────┬────────────────────────┘
                    │
                    ▼
   ┌─────────────────────────────────────────┐
   │  etcd(档案室)                         │
   │  - 唯一真相源                           │
   │  - Raft 多数派一致                      │
   │  - MVCC 多版本                          │
   │  - Watch 订阅                           │
   └─────────────────────────────────────────┘
```

### 2.2 关键设计

- **APIServer 无状态**:可水平扩展
- **etcd 有状态**:Raft 强一致
- **唯一写 etcd**:APIServer,其他组件通过 APIServer
- **watch cache**:APIServer 缓存热点对象,降低 etcd 读

---

## 3. APIServer 详解

### 3.1 整体架构

```
   ┌─────────────────────────────────────────────────────┐
   │ kube-apiserver                                      │
   │                                                      │
   │ ┌─────────────────────────────────────────────────┐ │
   │ │ HTTP Server (TLS)                                │ │
   │ │  - 6443 (HTTPS,对外)                            │ │
   │ │  - 8080 (HTTP,本机,不安全,默认关)             │ │
   │ └─────────────────────────────────────────────────┘ │
   │                                                      │
   │ ┌─────────────────────────────────────────────────┐ │
   │ │ Request Pipeline                                 │ │
   │ │  1. Authn(认证)                                │ │
   │ │  2. Authz(授权)                                │ │
   │ │  3. APF(优先级与公平性,限流)                  │ │
   │ │  4. Mutating Admission                          │ │
   │ │  5. Object Validation                           │ │
   │ │  6. Validating Admission                         │ │
   │ │  7. etcd Write                                  │ │
   │ │  8. Watch Push                                   │ │
   │ └─────────────────────────────────────────────────┘ │
   │                                                      │
   │ ┌─────────────────────────────────────────────────┐ │
   │ │ Storage Layer                                   │ │
   │ │  - etcd client                                   │ │
   │ │  - watch cache                                   │ │
   │ │  - storage versioning                            │ │
   │ └─────────────────────────────────────────────────┘ │
   └─────────────────────────────────────────────────────┘
```

### 3.2 认证(Authentication)

#### 3.2.1 多种认证方式

| 方式 | 配置 | 适用 |
|------|------|------|
| **客户端证书** | --client-ca-file | 默认,kubelet / kubectl |
| **Bearer Token** | Static Token File / Bootstrap Token | kubelet bootstrap |
| **OIDC** | --oidc-issuer-url / --oidc-client-id | 企业 SSO |
| **Webhook** | --authentication-webhook-config-file | 外部认证 |
| **Service Account Token** | 自动 | Pod 内访问 |
| **Anonymous** | --anonymous-auth=true | 默认 false |

#### 3.2.2 OIDC 配置示例

```yaml
# apiserver 启动参数
--oidc-issuer-url=https://dex.example.com
--oidc-client-id=kubernetes
--oidc-username-claim=email
--oidc-username-prefix=oidc:
--oidc-groups-claim=groups
--oidc-groups-prefix=oidc:
--oidc-ca-file=/etc/kubernetes/pki/dex-ca.crt
```

OIDC 流程:
```
   1. 用户从 OIDC Provider 拿 id_token
   2. 配置 kubectl 用 token
   3. kubectl 带 Authorization: Bearer <token>
   4. apiserver 验签 + 提取用户信息
   5. 后续走 Authz
```

#### 3.2.3 Service Account Token

```yaml
# Pod 自动挂载 SA Token
spec:
  serviceAccountName: my-sa
  automountServiceAccountToken: true   # 默认 true
  containers:
  - name: app
    # 自动挂载到 /var/run/secrets/kubernetes.io/serviceaccount/
    # 包含: token / ca.crt / namespace
```

**Token 格式**(1.24+ BoundToken):
- JWT 格式
- 包含: iss / sub / aud / exp / pod name / SA name
- 绑定到 Pod(删除 Pod,Token 失效)

### 3.3 授权(Authorization)

#### 3.3.1 4 种模式

| 模式 | 配置 | 适用 |
|------|------|------|
| **RBAC**(默认) | --authorization-mode=Node,RBAC | 标准 |
| **ABAC** | --authorization-policy-file=... | 简单,已不推荐 |
| **Node** | --authorization-mode=Node | kubelet 特权 |
| **Webhook** | --authorization-webhook-config-file | 外部授权 |

#### 3.3.2 RBAC 详解

```yaml
# Role(单 namespace)
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  namespace: production
  name: pod-reader
rules:
- apiGroups: [""]
  resources: ["pods", "pods/log"]
  verbs: ["get", "list", "watch"]
  resourceNames: ["my-pod"]    # 可选,限定特定资源
---
# ClusterRole(全局)
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: node-reader
rules:
- apiGroups: [""]
  resources: ["nodes"]
  verbs: ["get", "list", "watch"]
---
# RoleBinding(绑定 Role 到 User/SA)
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  namespace: production
  name: read-pods
subjects:
- kind: User
  name: alice
  apiGroup: rbac.authorization.k8s.io
- kind: ServiceAccount
  name: my-sa
  namespace: production
roleRef:
  kind: Role
  name: pod-reader
  apiGroup: rbac.authorization.k8s.io
---
# ClusterRoleBinding(全局绑定)
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: read-nodes
subjects:
- kind: Group
  name: ops-team
  apiGroup: rbac.authorization.k8s.io
roleRef:
  kind: ClusterRole
  name: node-reader
  apiGroup: rbac.authorization.k8s.io
```

**Verbs**:
- get / list / watch / create / update / patch / delete / deletecollection
- *(所有)
- escalate(允许提权)

**RBAC 决策**:
- Deny by default(默认拒绝)
- 任一 RoleBinding 允许 → 允许
- 显式 deny 不存在(用 admission 补充)

### 3.4 Admission Controller

#### 3.4.1 Mutating(变更)

修改对象。常用:
- **DefaultNamespace**:默认 namespace
- **DefaultTolerationSeconds**:默认容忍时间
- **ServiceAccount**:自动挂 SA
- **NodeRestriction**:kubelet 只能改自己的 Node
- **MutatingWebhook**:业务自定义

#### 3.4.2 Validating(验证)

只允许/拒绝。常用:
- **LimitRanger**:资源限制范围
- **ResourceQuota**:配额
- **ValidatingWebhook**:业务自定义
- **ImagePolicyWebhook**:镜像白名单

#### 3.4.3 内置 Admission 顺序

```
   1. NamespaceExists / NamespaceLifecycle
   2. ServiceAccount(自动加 SA)
   3. NodeRestriction(kubelet 限制)
   4. TaintToleration
   5. ... 其他内置
   6. MutatingWebhook(用户自定义,串行)
   7. Object Schema Validation
   8. ValidatingWebhook(用户自定义,并行)
   9. ResourceQuota(最后检查配额)
```

#### 3.4.4 Webhook 配置

```yaml
apiVersion: admissionregistration.k8s.io/v1
kind: MutatingWebhookConfiguration
metadata:
  name: istio-sidecar-injection
webhooks:
- name: sidecar-injector.istio.io
  clientConfig:
    service:
      name: istiod
      namespace: istio-system
      path: "/inject"
      port: 443
    caBundle: <base64-ca>
  rules:
  - operations: ["CREATE"]
    apiGroups: [""]
    apiVersions: ["v1"]
    resources: ["pods"]
  failurePolicy: Fail        # webhook 挂了拒绝(慎用)
  matchPolicy: Equivalent
  namespaceSelector:
    matchLabels:
      istio-injection: enabled
  sideEffects: None
  timeoutSeconds: 10         # 超时 10s
```

**failurePolicy**:
- Fail:webhook 不可达时拒绝(严格,但风险大)
- Ignore:webhook 不可达时放行(宽松,但安全风险)

### 3.5 API Priority and Fairness(APF,1.20+)

#### 3.5.1 解决什么问题

- 旧版 max-requests-inflight 全局限制,单一客户端可打满
- 关键请求(kubelet heartbeat)被普通请求挤掉

#### 3.5.2 概念

- **PriorityClass**:优先级
- **FlowSchema**:按规则分流到 PriorityClass
- **PriorityLevelConfiguration**:每个优先级的并发与排队策略

#### 3.5.3 默认 FlowSchema

| FlowSchema | PriorityLevel | 描述 |
|-----------|---------------|------|
| system | exempt | 系统 exempt,无限制 |
| node-high | node-high | kubelet 上报,高优 |
| system-master | workload-high | master 上的请求 |
| workload-high | workload-high | controller 等 |
| workload-low | workload-low | 一般业务 |
| global-default | global-default | 兜底 |

#### 3.5.4 配置示例

```yaml
apiVersion: flowcontrol.apiserver.k8s.io/v1beta3
kind: FlowSchema
metadata:
  name: critical-pods
spec:
  priorityLevelConfiguration:
    name: exempt
  matchPrecedence: 1
  distinguisherMethod:
    type: ByUser
  rules:
  - subjects:
    - kind: ServiceAccount
      namespace: kube-system
    resourceRules:
    - apiGroups: [""]
      resources: ["pods"]
      verbs: ["create"]
```

### 3.6 Watch Cache

#### 3.6.1 问题

- 1k 客户端 Watch Pod,每次都连 etcd
- etcd 单 Watch 连接成本高
- 全量 List 也走 etcd,压力大

#### 3.6.2 解决

```
   ┌─────────────────────────────────────────┐
   │ APIServer                                │
   │                                          │
   │ ┌─────────────────────────────────────┐ │
   │ │ Watch Cache(内存)                  │ │
   │ │  - 每种资源一个 cache               │ │
   │ │  - List 从 cache 读(不查 etcd)    │ │
   │ │  - Watch 从 cache 推(不连 etcd)   │ │
   │ └─────────────────────────────────────┘ │
   │                                          │
   │ ┌─────────────────────────────────────┐ │
   │ │ etcd Watch(单连接)                 │ │
   │ │  - APIServer 一个 Watch 拿所有变更  │ │
   │ │  - 推到 cache                       │ │
   │ └─────────────────────────────────────┘ │
   └─────────────────────────────────────────┘
```

#### 3.6.3 配置

```yaml
--default-watch-cache-size=100         # 默认 100 个对象
--watch-cache-sizes=pods#10000,secrets#1000   # 按资源配置
```

---

## 4. etcd 详解

### 4.1 Raft 协议

#### 4.1.1 三种角色

- **Follower**:被动接收 Leader 日志
- **Candidate**:选举中,争取成为 Leader
- **Leader**:接收客户端请求,复制日志

#### 4.1.2 Leader 选举

```
   T=0    集群启动,都是 Follower
          │
   T=随机 ▼
          超时未收到 Leader 心跳,Follower → Candidate
          │
   T=候选 ▼
          Candidate 自增 term,投自己一票
          向其他节点发 RequestVote RPC
          │
   T=选举 ▼
          其他节点检查:
          - 候选人 term > 自己
          - 候选人日志 >= 自己
          满足 → 投票
          │
   T=多数派 ▼
          Candidate 拿到多数派 → Leader
          发心跳维持 Leader 地位
```

**关键参数**:
- `--heartbeat-interval=100ms`(Leader 心跳间隔)
- `--election-timeout=1000ms`(Follower 超时,触发选举)
- 随机化超时,避免多个节点同时竞选

#### 4.1.3 日志复制

```
   T=0    Client → Leader: 写请求
          │
   T=0.1  ▼
          Leader 把写请求作为日志条目,append 到本地 log
          │
   T=0.1  ▼
          Leader 发 AppendEntries RPC 给所有 Follower
          │
   T=10ms ▼
          Follower 收到,append 到本地 log,返回 ACK
          │
   T=20ms ▼
          Leader 收到多数派 ACK → Commit(标记 committed)
          │
   T=20ms ▼
          Leader 应用到状态机(写 KV store)
          │
   T=20ms ▼
          Leader 返回 Client 成功
          │
   T=21ms ▼
          Leader 下次 AppendEntries 携带 commit index
          Follower 也 commit + apply
```

**关键**:
- 多数派 ACK 才 commit,保证一致性
- Commit 后才返回客户端
- Follower 落后 Leader,Leader 会补发

#### 4.1.4 脑裂防护

```
   集群 5 节点,分裂成 3 + 2
   
   3 节点分区:
     - 多数派,选 Leader,可写
   
   2 节点分区:
     - 不到多数派,无法选 Leader
     - 不可写
   
   合并:
     - 2 节点的日志被 3 节点的覆盖
     - 数据一致
```

#### 4.1.5 etcd Raft 优化

- **PreVote**:选举前先探询,避免网络抖动触发选举
- **Leader Transfer**:主动转移 Leader(维护时用)
- **Joint Consensus**:配置变更时安全

### 4.2 数据模型

#### 4.2.1 KV 存储

```
   key: /registry/pods/default/my-pod
   value: <protobuf 序列化的 Pod 对象>
   
   key: /registry/services/default/my-svc
   value: <protobuf 序列化的 Service 对象>
```

**为什么用扁平 KV**:
- 简单,无 schema
- 范围查询(prefix)/registry/pods/default/ 拿一个 namespace 所有 Pod

#### 4.2.2 MVCC 多版本

```
   T=1    Pod 创建,revision=1,version=1
   T=2    Pod 更新,revision=2,version=2
   T=3    Pod 又更新,revision=3,version=3
   T=4    另一个 Pod 创建,revision=4
   
   查询:
     最新:revision=3
     历史某个时刻:revision=2
   
   Watch:
     从 revision=2 开始,推送之后的变化
```

- **revision**:全局单调递增,每次写自增
- **version**:同 key 的版本
- **resourceVersion**:K8s 暴露给客户端的版本号(就是 revision)

#### 4.2.3 压缩(Compaction)

```
   压缩前:
     revision 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
     (保留所有历史)
   
   压缩后(retention=1h):
     删除 1h 前的 revision
     保留最近 1h 的 revision
   
   压缩命令:
     etcdctl compact <rev>
```

**配置**:
```
--auto-compaction-mode=periodic       # 按时间
--auto-compaction-retention=1h        # 保留 1h
```

#### 4.2.4 Defrag(碎片整理)

```
   压缩后,etcd 数据文件有空洞(B+ 树碎片)
   
   defrag:
     重写数据文件,去除空洞
     释放磁盘空间
   
   注意:
     - 阻塞写入(defrag 期间不可写)
     - 逐节点 evict leader + defrag
     - v3.5+ 支持 online defrag(影响小)
```

### 4.3 etcd 性能基线

| 集群规模 | 节点数 | QPS | P99 延迟 | 磁盘 |
|---------|--------|-----|---------|------|
| 小型 | 3 | 1k | < 10ms | 1-3GB |
| 中型 | 5 | 5k | < 20ms | 5-15GB |
| 大型 | 5+SSD | 15k | < 50ms | 20-50GB |
| 超大 | 7+local SSD | 30k+ | < 100ms | 50-100GB |

---

## 5. 操作流程与命令

### 5.1 检查 APIServer 健康

```bash
# 健康检查
kubectl get --raw='/livez'
kubectl get --raw='/readyz'

# 详细健康检查
kubectl get --raw='/readyz?verbose'

# 查看版本
kubectl version
kubectl get --raw='/version'

# 查看 API 资源
kubectl api-resources
kubectl api-versions
```

### 5.2 检查 etcd

```bash
# 查看成员
ETCDCTL_API=3 etcdctl member list \
  --endpoints=https://10.0.0.10:2379 \
  --cacert=/etc/etcd/ca.crt \
  --cert=/etc/etcd/peer.crt \
  --key=/etc/etcd/peer.key

# 端点健康
ETCDCTL_API=3 etcdctl endpoint health --cluster \
  --endpoints=https://10.0.0.10:2379 \
  --cacert=/etc/etcd/ca.crt \
  --cert=/etc/etcd/peer.crt \
  --key=/etc/etcd/peer.key \
  --write-out=table

# 状态
ETCDCTL_API=3 etcdctl endpoint status --cluster \
  --endpoints=https://10.0.0.10:2379 \
  --cacert=/etc/etcd/ca.crt \
  --cert=/etc/etcd/peer.crt \
  --key=/etc/etcd/peer.key \
  --write-out=table

# 性能测试
ETCDCTL_API=3 etcdctl check perf \
  --endpoints=https://10.0.0.10:2379 \
  --cacert=/etc/etcd/ca.crt \
  --cert=/etc/etcd/peer.crt \
  --key=/etc/etcd/peer.key \
  --load="s" \
  --prefix="health-check"
```

### 5.3 etcd 备份恢复

```bash
# 备份
ETCDCTL_API=3 etcdctl snapshot save /backup/etcd-$(date +%Y%m%d).db \
  --endpoints=https://10.0.0.10:2379 \
  --cacert=/etc/etcd/ca.crt \
  --cert=/etc/etcd/peer.crt \
  --key=/etc/etcd/peer.key

# 验证备份
ETCDCTL_API=3 etcdctl snapshot status /backup/etcd-20260705.db --write-out=table

# 灾难恢复
# 1. 停所有 etcd
sudo systemctl stop etcd
sudo rm -rf /var/lib/etcd

# 2. 在每个节点恢复
ETCDCTL_API=3 etcdctl snapshot restore /backup/etcd-20260705.db \
  --data-dir=/var/lib/etcd \
  --name=etcd-01 \
  --initial-cluster=etcd-01=https://10.0.0.10:2380,etcd-02=https://10.0.0.11:2380,etcd-03=https://10.0.0.12:2380 \
  --initial-cluster-token=etcd-cluster-prod \
  --initial-advertise-peer-urls=https://10.0.0.10:2380

# 3. 启动
sudo chown -R etcd:etcd /var/lib/etcd
sudo systemctl start etcd
```

### 5.4 压缩与 defrag

```bash
# 查看当前 revision
ETCDCTL_API=3 etcdctl endpoint status --write-out=json | jq '.Status.header.revision'

# 压缩(到当前 revision)
ETCDCTL_API=3 etcdctl compact <rev>

# defrag(逐节点,避免影响写入)
for ep in 10.0.0.10 10.0.0.11 10.0.0.12; do
  ETCDCTL_API=3 etcdctl defrag \
    --endpoints=https://$ep:2379 \
    --cacert=/etc/etcd/ca.crt \
    --cert=/etc/etcd/peer.crt \
    --key=/etc/etcd/peer.key
done
```

### 5.5 查看 APIServer metrics

```bash
# 请求延迟
kubectl get --raw='/metrics' | grep apiserver_request_duration_seconds

# 请求总数
kubectl get --raw='/metrics' | grep apiserver_request_total

# Watch 缓存
kubectl get --raw='/metrics' | grep watch_cache

# etcd 请求
kubectl get --raw='/metrics' | grep etcd_request_duration

# APF
kubectl get --raw='/metrics' | grep apiserver_flow
```

---

## 6. 底层原理

### 6.1 APIServer 请求处理流程(源码级)

```
   1. HTTP Request 进入
      ├─ TLS 终止
      └─ 路由到 handler
   
   2. Authn(认证)
      ├─ 多个 authenticator 串行
      ├─ 任一成功 → user info
      └─ 全失败 → 401
   
   3. Authz(授权)
      ├─ 多个 authorizer 串行
      ├─ 任一允许 → 允许
      ├─ 任一拒绝 → 拒绝(403)
      └─ 全不决 → 拒绝
   
   4. APF 限流
      ├─ FlowSchema 匹配
      ├─ PriorityLevel 配额
      └─ 超限 → 429
   
   5. Decode
      ├─ 解析 body 为 K8s 对象
      └─ 校验 API group/version
   
   6. Admission(变更)
      ├─ 内置 mutating 串行
      ├─ MutatingWebhook 串行
      └─ 每步可能修改对象
   
   7. Validation
      ├─ OpenAPI Schema 校验
      ├─ 内置 validating
      └─ ValidatingWebhook 并行
   
   8. Storage
      ├─ 乐观锁(resourceVersion)
      ├─ etcd Put(写 KV)
      ├─ Raft 复制
      └─ Commit
   
   9. Response
      ├─ 返回 201 Created / 200 OK
      └─ 序列化为 JSON / Protobuf
   
   10. Watch Push(异步)
       ├─ etcd Watch 推给 APIServer
       └─ APIServer 推给所有 Watcher
```

### 6.2 etcd 写入流程

```
   1. APIServer 调用 etcdclient.Txn(条件事务)
      - 条件:modRevision == 客户端提供的 rv(乐观锁)
      - 操作:Put /registry/pods/default/my-pod <data>
   
   2. etcd Leader 收到请求
      ├─ 检查条件
      ├─ 生成 WAL 日志
      ├─ 写入 WAL 文件(fsync)
      └─ 发 AppendEntries 给 Followers
   
   3. Followers 复制
      ├─ 写 WAL
      └─ ACK Leader
   
   4. Leader 收到多数派 ACK
      ├─ Commit
      ├─ Apply 到 KV store
      ├─ 写 bucket(数据)
      └─ 返回客户端成功
   
   5. Leader 下次 AppendEntries 携带 commit index
      Followers 也 commit + apply
   
   关键:fsync WAL 是延迟瓶颈
     - 每次写都要 fsync(保证持久化)
     - SSD P99 < 5ms,HDD P99 > 50ms
     - 共享磁盘会争抢,导致延迟飙升
```

### 6.3 Watch 机制

```
   etcd Watch:
   1. 客户端订阅 prefix(如 /registry/pods/)
   2. etcd 维护 watch 连接
   3. 任何 key 变化,推送事件给客户端
   4. 客户端维护 revision,断线重连用 revision 续
   
   APIServer Watch Cache:
   1. APIServer 启动时,etcd Watch 所有资源
   2. 把事件写入本地 cache(按资源类型)
   3. 客户端 Watch APIServer,从 cache 推
   4. 客户端 List 从 cache 读(不查 etcd)
   
   优势:
     - 1k 客户端 Watch,etcd 只 1 个 Watch
     - List 不走 etcd,降压力
```

### 6.4 乐观锁(resourceVersion)

```go
// 伪代码:并发更新 Pod
func updatePod(pod *Pod) error {
    for {
        // 1. 读最新版本
        current := apiserver.Get(pod.Name)
        
        // 2. 修改 spec
        current.Spec.Replicas = pod.Spec.Replicas
        
        // 3. 写入(带 resourceVersion)
        err := apiserver.Update(current)
        
        if errors.IsConflict(err) {
            // 4. 冲突,重试
            continue
        }
        return err
    }
}
```

**etcd 端**:
```go
// etcd Txn(条件事务)
resp, _ := client.Txn(ctx).
    If(clientv3.Compare(clientv3.ModRevision(key), "==", expectedRV)).
    Then(clientv3.OpPut(key, value)).
    Else().
    Commit()

if !resp.Succeeded {
    return errors.Conflict    // 409
}
```

---

## 7. 配置示例

### 7.1 生产级 APIServer 配置

```yaml
# /etc/kubernetes/manifests/kube-apiserver.yaml
apiVersion: v1
kind: Pod
metadata:
  name: kube-apiserver
  namespace: kube-system
spec:
  hostNetwork: true
  priorityClassName: system-node-critical
  containers:
  - name: kube-apiserver
    image: registry.k8s.io/kube-apiserver:v1.30.2
    command:
    - kube-apiserver
    # 1. 监听
    - --advertise-address=10.0.0.10
    - --bind-address=0.0.0.0
    - --secure-port=6443
    
    # 2. etcd
    - --etcd-servers=https://10.0.0.10:2379,https://10.0.0.11:2379,https://10.0.0.12:2379
    - --etcd-cafile=/etc/kubernetes/pki/etcd/ca.crt
    - --etcd-certfile=/etc/kubernetes/pki/etcd/apiserver-etcd-client.crt
    - --etcd-keyfile=/etc/kubernetes/pki/etcd/apiserver-etcd-client.key
    
    # 3. 认证授权
    - --client-ca-file=/etc/kubernetes/pki/ca.crt
    - --authorization-mode=Node,RBAC
    - --enable-admission-plugins=NodeRestriction,ServiceAccount,DefaultStorageClass,MutatingAdmissionWebhook,ValidatingAdmissionWebhook,ResourceQuota,LimitRanger
    
    # 4. ServiceAccount
    - --service-account-key-file=/etc/kubernetes/pki/sa.pub
    - --service-account-signing-key-file=/etc/kubernetes/pki/sa.key
    - --service-account-issuer=https://kubernetes.default.svc.cluster.local
    - --api-audiences=kubernetes.default.svc.cluster.local
    
    # 5. 网络
    - --service-cluster-ip-range=10.96.0.0/12
    - --feature-gates=APIPriorityAndFairness=true
    
    # 6. 性能调优
    - --max-requests-inflight=1000
    - --max-mutating-requests-inflight=400
    - --default-watch-cache-size=200
    - --watch-cache-sizes=pods#10000,services#5000,secrets#5000,configmaps#5000
    - --request-timeout=60s
    
    # 7. 审计
    - --audit-policy-file=/etc/kubernetes/audit-policy.yaml
    - --audit-log-path=/var/log/kubernetes/audit.log
    - --audit-log-maxage=30
    - --audit-log-maxbackup=10
    - --audit-log-maxsize=100
    
    # 8. OIDC(可选)
    - --oidc-issuer-url=https://dex.example.com
    - --oidc-client-id=kubernetes
    - --oidc-username-claim=email
    - --oidc-username-prefix=oidc:
    - --oidc-groups-claim=groups
    - --oidc-groups-prefix=oidc:
    - --oidc-ca-file=/etc/kubernetes/pki/dex-ca.crt
    
    resources:
      requests:
        cpu: 1000m
        memory: 2Gi
      limits:
        cpu: 4000m
        memory: 8Gi
    livenessProbe:
      httpGet:
        path: /livez
        port: 6443
        scheme: HTTPS
      initialDelaySeconds: 45
      timeoutSeconds: 15
```

### 7.2 审计策略

```yaml
# /etc/kubernetes/audit-policy.yaml
apiVersion: audit.k8s.io/v1
kind: Policy
rules:
# 不记录 GET / LIST / WATCH
- level: None
  verbs: ["get", "list", "watch"]

# 不记录 kubelet 的 heartbeat
- level: None
  users: ["system:kube-proxy"]
  verbs: ["get", "list", "watch"]
- level: None
  userGroups: ["system:nodes"]
  verbs: ["get", "list", "watch"]

# Secret 请求记录元数据(不记 body)
- level: Metadata
  resources:
  - group: ""
    resources: ["secrets"]

# 写操作记录 RequestResponse(完整请求 + 响应)
- level: RequestResponse
  verbs: ["create", "update", "patch", "delete"]

# 其他记录 Request
- level: Request
```

### 7.3 生产级 etcd 配置

```yaml
# systemd unit
[Unit]
Description=etcd
After=network.target

[Service]
Type=exec
User=etcd
ExecStart=/usr/local/bin/etcd \
  --name=etcd-01 \
  --data-dir=/var/lib/etcd \
  --wal-dir=/var/lib/etcd/wal \
  --snapshot-count=10000 \
  --heartbeat-interval=100 \
  --election-timeout=1000 \
  --initial-cluster-token=etcd-cluster-prod \
  --initial-cluster=etcd-01=https://10.0.0.10:2380,etcd-02=https://10.0.0.11:2380,etcd-03=https://10.0.0.12:2380 \
  --initial-cluster-state=new \
  --initial-advertise-peer-urls=https://10.0.0.10:2380 \
  --listen-peer-urls=https://0.0.0.0:2380 \
  --listen-client-urls=https://0.0.0.0:2379 \
  --advertise-client-urls=https://10.0.0.10:2379 \
  --cert-file=/etc/etcd/server.crt \
  --key-file=/etc/etcd/server.key \
  --trusted-ca-file=/etc/etcd/ca.crt \
  --peer-cert-file=/etc/etcd/peer.crt \
  --peer-key-file=/etc/etcd/peer.key \
  --peer-trusted-ca-file=/etc/etcd/ca.crt \
  --peer-client-cert-auth=true \
  --client-cert-auth=true \
  --auto-compaction-mode=periodic \
  --auto-compaction-retention=1h \
  --quota-backend-bytes=8589934592 \
  --max-request-bytes=33554432 \
  --max-wals=5 \
  --max-snapshots=5

Restart=always
RestartSec=5
LimitNOFILE=65536
OOMScoreAdjust=-1000
TimeoutStopSec=0

[Install]
WantedBy=multi-user.target
```

---

## 8. 常见陷阱与调优 ⚠️

### 8.1 APIServer 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **Watch 断连** | kubectl 偶尔卡 | LB idle timeout 短 | LB 配置 ≥ 1h |
| **请求被限流** | 409 Too Many Requests | max-requests-inflight 满 | 启用 APF |
| **慢查询** | List Pods > 5s | 不带 selector 全量拉 | 用 labelSelector + 分页 |
| **admission webhook 超时** | 创建 Pod 慢 | webhook 服务不可达 | timeout=10s + 失败策略 |
| **证书过期** | 集群突然不可用 | 1 年过期未轮换 | cert-manager + 自动 |
| **anonymous 默认开** | 安全风险 | 默认配置 | --anonymous-auth=false |

### 8.2 etcd 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **磁盘满** | 全集群不可写 | 历史版本不压缩 | 启用 auto-compaction |
| **fsync 慢** | etcd P99 > 100ms | 共享磁盘 IO 争抢 | 独占 SSD |
| **脑裂** | Leader 频繁切 | 跨机房网络抖动 | 同机房部署 |
| **大对象写入** | apply 失败 | ConfigMap > 1MB | 拆分 / OCI 镜像 |
| **defrag 卡死** | 集群不可写 | 阻塞主线程 | v3.5+ 在线 defrag |
| **quota 满** | 无法写入 | 默认 2GB quota | 调高 + 监控 |

### 8.3 性能陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **List 风暴** | apiserver CPU 100% | 客户端频繁 List | 用 Informer + Watch |
| **大量 Watch 连接** | apiserver 内存涨 | 每客户端独立 Watch | 共享 Informer |
| **etcd 读压力** | etcd QPS 高 | watch cache 未启用 | 启用 + 增大 cache |

---

## 9. 工业案例与基准数据

### 9.1 阿里:超大规模 etcd 优化

**场景**:单集群 5k 节点,etcd QPS 15k+。

**优化**:
1. **etcd 分片**:按资源类型分到不同 etcd 集群
   - Pods / ConfigMap 各自独立
2. **watch cache 增大**:`--watch-cache-sizes=pods#50000`
3. **APF 严格**:关键请求 exempt,普通请求限流
4. **etcd 7 副本**:跨 AZ + local SSD
5. **压缩频率**:每 5min 压缩一次

**性能**:
- etcd P99 < 30ms
- apiserver P99 < 100ms
- 单集群 5k 节点 + 100k Pod

### 9.2 字节:多 etcd 集群分片

**场景**:8k 节点集群,单 etcd 撑不住。

**架构**:
- 自研 KubeZoo(apiserver 多租户分片)
- 不同 namespace 走不同 etcd 集群
- 大 namespace(>10k Pod)单独 etcd

**性能**:
- etcd QPS 分散到 10+ 集群
- apiserver P99 < 100ms
- Pod 启动 P99 < 8s

### 9.3 Google:GKE etcd 优化

**场景**:GKE 单集群 15k 节点。

**特色**:
- etcd 7 副本 + local SSD
- Bigtable 备份
- 自动压缩 + defrag
- etcd 自愈(自动恢复)

### 9.4 etcd 性能基线

| 配置 | 写 QPS | P99 延迟 | 适用规模 |
|------|--------|---------|---------|
| 3 节点 HDD | 200 | 100ms | < 100 节点 |
| 3 节点 SSD | 1k | 10ms | < 1k 节点 |
| 5 节点 SSD | 5k | 20ms | < 5k 节点 |
| 5 节点 NVMe | 15k | 5ms | < 5k 节点 |
| 7 节点 NVMe | 30k | 10ms | < 15k 节点 |

---

## 10. 与其他方案的关系

### 10.1 etcd vs ZooKeeper

| 维度 | etcd | ZK |
|------|------|-----|
| 协议 | Raft | ZAB(Paxos 变种) |
| API | gRPC + HTTP/2 | 自定义 |
| Watch | 原生支持 | 需重新注册 |
| 多版本 | MVCC | 单版本 |
| 易理解 | 高 | 中 |
| K8s 集成 | 原生 | 不友好 |

### 10.2 etcd vs Consul

| 维度 | etcd | Consul |
|------|------|--------|
| 定位 | KV 存储 | 服务发现 + KV |
| 协议 | Raft | Raft |
| 多 DC | 否(需 federation) | 是(原生 WAN) |
| 健康检查 | 无 | 有 |
| K8s 集成 | 原生 | 不友好 |

### 10.3 etcd vs SQL

| 维度 | etcd | SQL |
|------|------|-----|
| 模型 | KV | 关系表 |
| 事务 | 简单(单 KV) | 完整 ACID |
| 查询 | prefix / range | SQL |
| 一致性 | 强一致 | 强一致 |
| 性能 | 高(简单) | 中(复杂) |

---

## 11. 面试速答 ⭐

| 问题 | 一句话答案 |
|------|----------|
| APIServer 请求流水线? | Authn → Authz → APF → Mutating Admission → Validation → Validating Admission → etcd |
| 为什么 APIServer 无状态? | 状态全在 etcd,APIServer 可水平扩展 |
| etcd 用什么协议? | Raft,Leader 写、多数派确认 |
| Raft 三种角色? | Follower / Candidate / Leader |
| Raft 怎么脑裂防护? | 多数派写,少数派分区无法选 Leader,不可写 |
| resourceVersion 是什么? | etcd 的 revision,全局单调递增,用于乐观锁 |
| Watch Cache 作用? | APIServer 缓存热点对象,降低 etcd 读压力 |
| APF 解决什么? | 防止单客户端打满 apiserver,按优先级分流 |
| etcd 为什么用 SSD? | fsync WAL 是延迟瓶颈,SSD P99 < 5ms |
| 大集群 etcd 怎么扩? | 分片(按资源类型 / namespace) |

---

## 12. 综合面试题

### 12.1 基础题

**Q1**: 描述 APIServer 的请求流水线。

**答题要点**:
1. HTTP Request → TLS → 路由
2. Authn(认证):客户端证书 / Token / OIDC
3. Authz(授权):RBAC / Node
4. APF 限流:FlowSchema → PriorityLevel
5. Decode:解析 body
6. Mutating Admission:DefaultNamespace / ServiceAccount / Webhook
7. Validation:Schema + ValidatingWebhook
8. etcd Write:乐观锁 + Raft 复制
9. Response
10. Watch Push(异步)

**Q2**: Raft 协议的 Leader 选举流程?

**答题要点**:
- 初始都是 Follower
- 随机超时未收到 Leader 心跳 → Follower → Candidate
- Candidate 自增 term,投自己,发 RequestVote
- 其他节点检查 term + 日志,满足则投票
- 多数派 → Leader,发心跳维持
- 任一时刻最多一个 Leader(多数派保证)

### 12.2 进阶题

**Q3**: APIServer 怎么扩展支持大规模集群?

**答题要点**:
1. **APIServer 多副本**:无状态,水平扩展
2. **etcd 优化**:
   - 多副本(5/7)+ local SSD
   - 启用压缩 + defrag
   - 增大 watch cache
3. **APF 限流**:防止雪崩
4. **etcd 分片**:按资源类型 / namespace 分到不同 etcd
5. **客户端优化**:用 Informer 共享 cache,避免频繁 List
6. **监控**:apiserver P99 / etcd P99 / watch cache 命中率

**Q4**: 一个 etcd 节点磁盘满了,怎么处理?

**答题要点**:
1. **影响**:
   - 该节点无法写 WAL,会被踢出集群
   - 集群仍可用(多数派)
   - 但只剩 2/3,再挂一个就不可用
2. **紧急**:
   - 清理磁盘(日志 / 临时文件)
   - 压缩 etcd(compact + defrag)
3. **根治**:
   - 启用 auto-compaction(retention=1h)
   - 监控磁盘使用率(70% 告警)
   - 大对象拆分
   - 扩容磁盘
4. **恢复节点**:
   - 重启 etcd
   - 从 Leader 同步数据

### 12.3 高级题

**Q5**: 设计一个 K8s 多租户方案,要求 APIServer 隔离。

**答题要点**:
- **方案 1:Namespace 隔离**(简单)
  - 每个租户独立 namespace
  - RBAC 限制访问
  - ResourceQuota + LimitRange 限资源
  - NetworkPolicy 网络隔离
  - 缺点:APIServer 共享,可能互相影响
- **方案 2:Virtual Cluster**(中等)
  - 每租户独立 K8s(在主集群跑)
  - vCluster / Capsule
  - APIServer 逻辑隔离
- **方案 3:KubeZoo**(字节)
  - APIServer 多租户分片
  - 不同租户走不同 etcd
  - 强隔离
- **方案 4:多集群**(强)
  - 每租户独立集群
  - 完全隔离
  - 成本高
- **选择**:
  - < 100 租户:Namespace
  - 100-1000:vCluster
  - > 1000:KubeZoo
  - 强合规:多集群

**Q6**: APIServer 限流怎么设计?APF 原理?

**答题要点**:
- 旧版 max-requests-inflight 全局限制,问题:
  - 单客户端可打满
  - 关键请求被挤
- APF:
  - **PriorityLevel**:并发配额(如 exempt / node-high / workload)
  - **FlowSchema**:按规则匹配请求,分流到 PriorityLevel
  - **Queue**:超限排队,带 backpressure
- 默认 FlowSchema:
  - system:exempt(无限制)
  - node-high:kubelet 上报高优
  - workload-high:controller 等
  - workload-low:普通业务
  - global-default:兜底
- 监控:apiserver_flow_dispatched_requests / apiserver_flow_rejected_requests

### 12.4 设计题

**Q7**: 设计一个 K8s 集群的灾备方案,要求 RTO < 30min。

**答题要点**:
- **etcd 备份**:
  - 每小时快照 + 异地保存(S3)
  - 自动化脚本(cron)
  - 保留 7 天
- **APIServer 配置备份**:
  - 静态 Pod YAML 在版本控制
  - 证书单独备份(加密)
- **恢复流程**:
  1. 准备新集群(脚本化,15min)
     - kubeadm init + join
     - 安装 CNI / CSI / CCM
  2. etcd 恢复(5min)
     - 停 etcd
     - snapshot restore
     - 启动 etcd
  3. 验证(5min)
     - kubectl get nodes
     - kubectl get pods
  4. 业务验证(5min)
- **演练**:
  - 季度灾难恢复演练
  - 模拟全集群丢失
  - 度量 RTO/RPO
- **多集群**(可选):
  - 跨 region 双活
  - DNS 切换
  - 数据库复制

---

## 13. 参考与延伸

### 13.1 官方文档

- [kube-apiserver](https://kubernetes.io/docs/reference/command-line-tools-reference/kube-apiserver/)
- [Kubernetes API](https://kubernetes.io/docs/concepts/overview/kubernetes-api/)
- [Admission Controllers](https://kubernetes.io/docs/reference/access-authn-authz/admission-controllers/)
- [API Priority and Fairness](https://kubernetes.io/docs/concepts/cluster-administration/flow-control/)
- [etcd docs](https://etcd.io/docs/)
- [Raft paper](https://raft.github.io/raft.pdf)

### 13.2 源码入口

- apiserver: `staging/src/k8s.io/apiserver/pkg/server/handler.go`
- admission: `staging/src/k8s.io/apiserver/pkg/admission/`
- etcd v3 client: `vendor/go.etcd.io/etcd/client/v3/`
- watch cache: `staging/src/k8s.io/apiserver/pkg/storage/cacher/`

### 13.3 经典论文

- **Raft: In Search of an Understandable Consensus Algorithm**(USENIX ATC 2014)
- **Borg, Omega, and Kubernetes**(ACM Queue 2016)
- **etcd: A Distributed, Reliable Key-Value Store**(etcd docs)

### 13.4 跨文件链接

- 上一章: [11 - 滚动更新与发布策略](./11-滚动更新与发布策略.md)
- 下一章: [13 - kubelet 与 Pod 生命周期](./13-kubelet与Pod生命周期.md)
- 详见: [02 - 架构与组件](./02-架构与组件.md) / [03 - 安装与部署](./03-安装与部署.md) / [09 - 控制器模式](./09-控制器模式.md)
- 参考平行模块: [分布式系统/05 - 复制](../分布式系统/05-复制.md) / [分布式系统/05 - 共识](../分布式系统/05-共识-Paxos.md)

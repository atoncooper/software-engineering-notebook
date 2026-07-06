# 17. RBAC 与认证授权

> 关键词:Authentication、Authorization、RBAC、ServiceAccount、OIDC、Webhook、ABAC、Node Authorizer

------

## 17.1 问题定义

K8s 集群中,谁可以 **访问哪些资源**、能执行 **什么操作**?

- kubectl 用 kubeconfig 访问集群 → 是谁?
- Pod 内应用调 APIServer → 是谁?
- 不同团队共用集群 → 怎么隔离?
- 外部 IdP(企业 SSO)→ 怎么集成?

**核心问题**:

> K8s 如何实现 **认证(Authn,你是谁)** 与 **授权(Authz,你能做什么)** 的双层安全模型?

------

## 17.2 直觉解释

把 K8s 鉴权想象成 **公司门禁系统**:

| 公司门禁 | K8s |
|---------|-----|
| 工牌刷卡认证(你是谁) | Authentication |
| 部门权限判断(能进哪) | Authorization |
| 不同工牌不同权限 | ServiceAccount/User |
| 部门对应可访问区域 | Role |
| 把工牌分配到部门 | RoleBinding |
| 全公司通用权限 | ClusterRole |
| 全公司通用绑定 | ClusterRoleBinding |
| 与企业 SSO 集成 | OIDC |
| 临时访客证 | ServiceAccount Token |

关键点:K8s 没有内置 User 对象,**User 是外部概念**(证书/Token/OIDC 提供方),ServiceAccount 是 K8s 内部对象。

------

## 17.3 核心概念

### 17.3.1 认证(Authentication)机制

K8s 支持多种认证方式,**同时启用,任一通过即可**:

```
请求 → APIServer
       ↓
       ┌─ Client Certificate(X.509)
       ├─ Bearer Token(Static/Bootstrap)
       ├─ ServiceAccount Token
       ├─ OIDC(OpenID Connect)
       ├─ Webhook Token
       ├─ Authorization Header(Basic)
       └─ Anonymous(若开启)
       ↓
   任一通过 → user.Info 对象
       ↓
   授权阶段
```

### 17.3.2 授权(AuthORIZATION)机制

K8s 支持多种授权模式,**同时启用,任一通过即可**:

```
请求 → Authn → user.Info
              ↓
              ┌─ Node Authorizer(kubelet 专用)
              ├─ RBAC(主流)
              ├─ ABAC(已弃用)
              ├─ Webhook(外部授权)
              └─ AlwaysAllow/AlwaysDeny
              ↓
          任一通过 → 放行
```

### 17.3.3 RBAC 四大对象

```
Role:           命名空间级权限
  kind: Role
  metadata.namespace: production
  rules: [verbs × resources × resourceNames]

ClusterRole:    集群级权限
  kind: ClusterRole
  rules: [verbs × resources]

RoleBinding:    把 Role 绑定到 User/SA
  kind: RoleBinding
  subjects: [User/Group/ServiceAccount]
  roleRef: Role

ClusterRoleBinding: 把 ClusterRole 绑定到 User/SA
  kind: ClusterRoleBinding
  subjects: [User/Group/ServiceAccount]
  roleRef: ClusterRole
```

**层级关系**:
```
ClusterRole ──── ClusterRoleBinding ──── User/SA(集群级)
ClusterRole ──── RoleBinding        ──── User/SA(命名空间级,降级使用)
Role         ──── RoleBinding        ──── User/SA(命名空间级)
```

### 17.3.4 ServiceAccount

```yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: my-sa
  namespace: production
  
# K8s 1.24+ 不再自动生成 Secret
# Token 通过 TokenRequest API 动态生成(短期、可绑定期限)
# 或用 projection volume 投射到 Pod
```

**Pod 使用 SA**:
```yaml
spec:
  serviceAccountName: my-sa
  # K8s 1.22+ 默认自动投射 token
  containers:
  - name: app
    volumeMounts:
    - name: token
      mountPath: /var/run/secrets/tokens
  volumes:
  - name: token
    projected:
      sources:
      - serviceAccountToken:
          path: token
          expirationSeconds: 3600
          audience: vault   # 绑定用途
```

### 17.3.5 verbs 与 resources

```
verbs:
  - get / list / watch(读)
  - create / update / patch / delete(写)
  - deletecollection(批量删)
  - * (所有)

resources:
  - pods / services / deployments
  - pods/log(Pod 日志)
  - pods/exec(进入容器)
  - pods/portforward(端口转发)
  - ingresses / networkpolicies
  - */*(所有资源)

resourceNames:
  - 限定具体资源名(如只允许 get pod/nginx)

apiGroups:
  - ""(core 组,Pod/Service 等)
  - apps(Deployment/StatefulSet)
  - networking.k8s.io(Ingress)
  - rbac.authorization.k8s.io(RBAC 资源)
  - *(所有组)
```

### 17.3.6 OIDC 集成

```
企业 IdP(Google/Azure AD/Keycloak/Auth0)
  ↓ OIDC
APIServer:
  --oidc-issuer-url=https://idp.example.com
  --oidc-client-id=kubernetes
  --oidc-username-claim=email
  --oidc-groups-claim=groups
  
用户登录:
  1. kubectl 用 oidc login 获取 ID Token
  2. ID Token 含:email、groups、exp
  3. 请求 APIServer,Bearer Token = ID Token
  4. APIServer 验证签名、过期、claims
  5. 提取 username/groups 用于 RBAC
```

### 17.3.7 Node Authorizer

```
专门给 kubelet 用的授权器:
  - 认证:用 client certificate,CN=system:node:<nodeName>, O=system:nodes
  - 授权:Node Authorizer 自动允许 kubelet 操作本节点资源
    * 读取本节点 Pod / Secret / ConfigMap
    * 上报本节点 NodeStatus
    * 写 PodStatus
  - 限制:仅本节点,不能操作其他节点
```

------

## 17.4 操作流程

### 17.4.1 kubectl 请求处理流程

```
1. kubectl 读取 kubeconfig
   - server: https://api.example.com
   - client-certificate: /path/to/cert.pem
   - client-key: /path/to/key.pem
   或
   - token: <JWT>

2. 建立 TLS 连接,双向认证
   - APIServer 验证客户端证书(由 CA 签发?)
   - 提取 CN(用户名)、O(组)

3. 发送请求
   GET /api/v1/namespaces/production/pods
   Authorization: Bearer <token>(或证书)

4. APIServer 处理:
   a. Authentication(认证)
      - ClientCertificate 或 TokenReview
      - 输出:user.Info{Username, UID, Groups}
   b. Authorization(授权)
      - RBAC: 查 RoleBinding/ClusterRoleBinding
      - 匹配 user/组 → Role → verbs/resources
   c. Admission(准入,见 12 章)
      - Mutating → Validation → Validating
   d. etcd 写入
   e. 返回结果

5. kubectl 显示结果
```

### 17.4.2 Pod 内应用调 APIServer

```
1. Pod 自动获得 SA Token(投射卷)
   /var/run/secrets/kubernetes.io/serviceaccount/token
   /var/run/secrets/kubernetes.io/serviceaccount/ca.crt
   /var/run/secrets/kubernetes.io/serviceaccount/namespace

2. 应用读取 token + ca.crt

3. 通过 Downward API 获取 APIServer 地址:
   env:
   - name: KUBERNETES_SERVICE_HOST
     valueFrom:
       fieldRef: spec.serviceAccountName

4. 应用用 SDK(client-go/python-client)调 APIServer:
   - 配置:ca.crt 校验 + Bearer token
   - 自动 token 轮转(K8s 1.21+)

5. APIServer 处理:
   a. Authn: ServiceAccount token → user=system:serviceaccount:<ns>:<sa>
   b. Authz: RBAC 查 SA 是否有权限
   c. 后续流程同上
```

### 17.4.3 OIDC 登录流程

```
1. 用户安装 kubelogin 插件
   kubectl krew install oidc_login

2. kubeconfig 配置:
   users:
   - name: oidc-user
     user:
       exec:
         apiVersion: client.authentication.k8s.io/v1
         command: kubelogin
         args:
         - get-token
         - --oidc-issuer-url=https://idp.example.com
         - --oidc-client-id=kubernetes

3. kubectl 触发:
   a. kubelogin 启动,引导浏览器登录
   b. 用户在 IdP 登录,授权
   c. IdP 返回 ID Token(含 groups)
   d. kubelogin 把 Token 给 kubectl
   e. kubectl 用 Token 调 APIServer

4. APIServer:
   a. 验证 OIDC Token 签名(IdP 公钥)
   b. 提取 username/email、groups
   c. RBAC 授权(基于 groups)

5. Token 过期后自动刷新
```

------

## 17.5 底层原理

### 17.5.1 Authentication Chain

```
APIServer 启动时配置多个 Authenticator:
  --client-ca-file=...
  --token-auth-file=...
  --service-account-key-file=...
  --oidc-issuer-url=...
  --authorization-webhook-config-file=...

请求进入后:
  for authenticator in [Cert, Token, OIDC, Webhook]:
    user, ok := authenticator.AuthenticateRequest(req)
    if ok:
      return user  # 任一通过即放行
  return Unauthorized
```

### 17.5.2 Authorization Chain

```
APIServer 配置多个 Authorizer:
  --authorization-mode=Node,RBAC,Webhook

请求进入后:
  for authorizer in [Node, RBAC, Webhook]:
    decision := authorizer.Authorize(user, verb, resource)
    if decision == Allow:
      return Allow
    if decision == Deny:
      return Deny  # 显式拒绝立即终止
    # NoOpinion 继续下一个
  return Deny  # 全部 NoOpinion → 拒绝
```

### 17.5.3 RBAC 实现细节

```
RBAC Authorizer 内部:
  1. 收集 user 的所有 RoleBinding/ClusterRoleBinding:
     - 按 user.name 匹配
     - 按 user.groups 匹配
  
  2. 收集所有绑定的 Role/ClusterRole 的 rules
  
  3. 对每个 rule,检查是否匹配请求:
     - apiGroups 包含?
     - resources 包含?
     - verbs 包含?
     - resourceNames 包含?(若 rule 指定)
     - 非 resource URL?(如 /healthz)
  
  4. 任一 rule 匹配 → Allow
     全部不匹配 → NoOpinion

性能优化:
  - RBAC 规则缓存(默认 5 分钟)
  - 按 user/组索引
  - 增量更新(Informers)
```

### 17.5.4 ServiceAccount Token 演进

```
K8s 1.21 之前(legacy):
  - 创建 SA 自动生成 Secret
  - Secret 含永久 token
  - Pod 挂载 Secret 用 token
  - 风险:token 永久有效,泄露后无法撤销

K8s 1.21-1.23(过渡):
  - LegacyTokenServiceAccount = false(默认)
  - 新 SA 不自动生成 Secret
  - 用 TokenRequest API 生成短期 token
  - Pod 用 projected volume 投射 token

K8s 1.24+(现代):
  - 完全移除自动 Secret
  - token 默认 1 小时过期,自动轮转
  - 支持绑定 audience(限制用途)
  - 支持 bound object(绑定 Pod,Pod 删除即失效)

Token 结构(JWT):
  Header: {alg: RS256, kid: ...}
  Payload: {
    iss: https://kubernetes.default.svc,
    sub: system:serviceaccount:default:my-sa,
    aud: ["https://kubernetes.default.svc", "vault"],
    exp: 1690000000,
    iat: 1689996400,
    kubernetes.io: {
      namespace: default,
      serviceaccount: {name: my-sa, uid: ...},
      pod: {name: my-pod, uid: ...}  // bound object
    }
  }
```

### 17.5.5 Node Authorizer 工作机制

```
kubelet 用 client certificate:
  CN=system:node:node-a
  O=system:nodes

Node Authorizer 检查:
  1. user 是否在 system:nodes 组?
  2. user.CN 是否匹配 system:node:<nodeName>?
  3. 提取 nodeName
  
  自动允许:
  - 读取本节点 Pod / Secret / ConfigMap
  - 上报 NodeStatus / PodStatus
  - 写 events(本节点相关)
  
  拒绝:
  - 访问其他节点资源
  - 创建/删除 Pod(只能上报状态)
  - 操作非本节点的 Secret
```

### 17.5.6 Webhook Authorization

```yaml
# APIServer 配置
--authorization-webhook-config-file=/etc/kubernetes/authz-webhook.yaml
--authorization-webhook-cache-unauthorized-ttl=30s
--authorization-webhook-cache-authorized-ttl=5m

# Webhook 配置(同 kubeconfig 格式)
apiVersion: v1
kind: Config
clusters:
- name: authz-webhook
  cluster:
    server: https://authz.example.com/authorize
    certificate-authority: /path/to/ca.pem
users:
- name: apiserver
  user:
    client-certificate: /path/to/client.pem
    client-key: /path/to/client-key.pem
```

**Webhook 请求体**:
```json
{
  "apiVersion": "authorization.k8s.io/v1beta1",
  "kind": SubjectAccessReview,
  "spec": {
    "resourceAttributes": {
      "namespace": "production",
      "verb": "get",
      "resource": "pods",
      "name": "my-pod"
    },
    "user": "alice@example.com",
    "groups": ["developers", "production-admins"]
  }
}
```

**响应**:
```json
{
  "status": {
    "allowed": true,
    "reason": "user is in production-admins group"
  }
}
```

### 17.5.7 准入 vs 授权

```
授权(Authz):
  - 决定 user 是否能做某操作
  - 不关心对象内容
  - 例:alice 能否 create pods?

准入(Admission):
  - 在授权通过后执行
  - 决定对象是否符合规范
  - 可修改对象(Mutating)
  - 可拒绝请求(Validating)
  - 例:Pod 的 image 是否来自可信仓库?
```

------

## 17.6 配置示例

### 17.6.1 RBAC 完整示例

```yaml
# 1. 命名空间
apiVersion: v1
kind: Namespace
metadata:
  name: production
---
# 2. ServiceAccount
apiVersion: v1
kind: ServiceAccount
metadata:
  name: app-sa
  namespace: production
---
# 3. Role(命名空间级)
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: pod-reader
  namespace: production
rules:
- apiGroups: [""]
  resources: ["pods", "pods/log"]
  verbs: ["get", "list", "watch"]
- apiGroups: [""]
  resources: ["pods/exec"]
  verbs: ["create"]
---
# 4. RoleBinding
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: app-sa-pod-reader
  namespace: production
subjects:
- kind: ServiceAccount
  name: app-sa
  namespace: production
- kind: User
  name: alice@example.com
  apiGroup: rbac.authorization.k8s.io
- kind: Group
  name: developers
  apiGroup: rbac.authorization.k8s.io
roleRef:
  kind: Role
  name: pod-reader
  apiGroup: rbac.authorization.k8s.io
---
# 5. ClusterRole(集群级或通用权限)
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: node-reader
rules:
- apiGroups: [""]
  resources: ["nodes"]
  verbs: ["get", "list", "watch"]
---
# 6. ClusterRoleBinding
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: alice-node-reader
subjects:
- kind: User
  name: alice@example.com
  apiGroup: rbac.authorization.k8s.io
roleRef:
  kind: ClusterRole
  name: node-reader
  apiGroup: rbac.authorization.k8s.io
```

### 17.6.2 ClusterRole 降级使用(命名空间级绑定)

```yaml
# ClusterRole 定义通用权限(可复用)
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: pod-reader-common
rules:
- apiGroups: [""]
  resources: ["pods"]
  verbs: ["get", "list", "watch"]
---
# 通过 RoleBinding 绑定到命名空间(降级)
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: alice-pod-reader
  namespace: production   # 仅在此命名空间生效
subjects:
- kind: User
  name: alice@example.com
  apiGroup: rbac.authorization.k8s.io
roleRef:
  kind: ClusterRole       # ← ClusterRole
  name: pod-reader-common
  apiGroup: rbac.authorization.k8s.io
```

**好处**:ClusterRole 定义一次,在多个命名空间复用。

### 17.6.3 OIDC 配置

```bash
# APIServer 启动参数
kube-apiserver \
  --oidc-issuer-url=https://idp.example.com \
  --oidc-client-id=kubernetes \
  --oidc-client-secret=... \
  --oidc-username-claim=email \
  --oidc-username-prefix=oidc: \
  --oidc-groups-claim=groups \
  --oidc-groups-prefix=oidc: \
  --oidc-required-claim="kubernetes.io: true" \
  --oidc-ca-file=/etc/kubernetes/oidc-ca.pem
```

**含义**:
- `--oidc-issuer-url`:IdP 的 Issuer URL,APIServer 会从 `/.well-known/openid-configuration` 获取配置
- `--oidc-username-claim`:用哪个 claim 作为用户名(默认 sub)
- `--oidc-username-prefix`:用户名前缀,避免与本地用户冲突(默认 OIDC:)
- `--oidc-groups-claim`:用哪个 claim 作为组
- `--oidc-groups-prefix`:组前缀
- `--oidc-required-claim`:必须包含的 claim(额外校验)

### 17.6.4 kubeconfig OIDC 配置

```yaml
# ~/.kube/config
apiVersion: v1
kind: Config
clusters:
- name: prod
  cluster:
    server: https://api.prod.example.com
    certificate-authority-data: ...
users:
- name: oidc
  user:
    exec:
      apiVersion: client.authentication.k8s.io/v1
      command: kubelogin
      args:
      - get-token
      - --oidc-issuer-url=https://idp.example.com
      - --oidc-client-id=kubernetes
      - --oidc-client-secret=...
      - --oidc-extra-scope=groups
contexts:
- name: prod
  context:
    cluster: prod
    user: oidc
    namespace: default
current-context: prod
```

### 17.6.5 SA Token 投射卷(K8s 1.24+)

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: app
spec:
  serviceAccountName: app-sa
  containers:
  - name: app
    image: app:v1
    volumeMounts:
    - name: token
      mountPath: /var/run/secrets/tokens
  volumes:
  - name: token
    projected:
      sources:
      - serviceAccountToken:
          path: token           # 文件名
          expirationSeconds: 3600  # 1 小时过期
          audience: vault       # 绑定用途
```

### 17.6.6 kubectl auth 命令

```bash
# 检查权限
kubectl auth can-i create pods -n production
kubectl auth can-i list secrets --as=alice -n production

# 列举权限
kubectl auth can-i --list -n production

# 模拟其他用户
kubectl auth can-i delete pods --as=system:serviceaccount:default:app-sa -n production

# 创建 Role/Binding
kubectl create role pod-reader --verb=get --verb=list --resource=pods
kubectl create rolebinding alice-pod-reader --role=pod-reader --user=alice

# 创建 ClusterRole
kubectl create clusterrole node-reader --verb=get,list,watch --resource=nodes
kubectl create clusterrolebinding alice-node-reader --clusterrole=node-reader --user=alice
```

### 17.6.7 默认 ClusterRole/ClusterRoleBinding

K8s 自带常用 ClusterRole(可继承):

| ClusterRole | 用途 |
|-------------|------|
| cluster-admin | 超级管理员 |
| admin | 命名空间管理员(几乎全部权限) |
| edit | 编辑(可部署应用) |
| view | 只读 |
| system:node | kubelet 用 |
| system:controller:* | 控制器用 |
| system:masters | 紧急运维 |

```yaml
# 把 alice 绑到 edit
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: alice-edit
  namespace: production
subjects:
- kind: User
  name: alice@example.com
  apiGroup: rbac.authorization.k8s.io
roleRef:
  kind: ClusterRole
  name: edit
  apiGroup: rbac.authorization.k8s.io
```

### 17.6.8 ResourceNames 限制

```yaml
# 仅允许 alice 操作特定 ConfigMap
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: config-updater
  namespace: production
rules:
- apiGroups: [""]
  resources: ["configmaps"]
  verbs: ["get", "update", "patch"]
  resourceNames: ["app-config", "feature-flags"]
```

### 17.6.9 Aggregated ClusterRole(聚合)

```yaml
# ClusterRole 聚合:多个 ClusterRole 合并
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: monitoring-admin
aggregationRule:
  clusterRoleSelectors:
  - matchLabels:
      rbac.example.com/aggregate-to-monitoring: "true"
rules: []  # 由聚合自动填充
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: monitoring-pods
  labels:
    rbac.example.com/aggregate-to-monitoring: "true"
rules:
- apiGroups: [""]
  resources: ["pods"]
  verbs: ["get", "list", "watch"]
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: monitoring-services
  labels:
    rbac.example.com/aggregate-to-monitoring: "true"
rules:
- apiGroups: [""]
  resources: ["services", "endpoints"]
  verbs: ["get", "list", "watch"]
```

**好处**:新增带 label 的 ClusterRole 自动并入 monitoring-admin。

### 17.6.10 APIServer 配置(完整)

```bash
kube-apiserver \
  # 认证
  --client-ca-file=/etc/kubernetes/pki/ca.pem \
  --service-account-key-file=/etc/kubernetes/pki/sa.pub \
  --service-account-issuer=https://kubernetes.default.svc \
  --service-account-signing-key-file=/etc/kubernetes/pki/sa.key \
  --oidc-issuer-url=https://idp.example.com \
  --oidc-client-id=kubernetes \
  --oidc-username-claim=email \
  --oidc-groups-claim=groups \
  --token-auth-file=/etc/kubernetes/tokens.csv \
  # 授权
  --authorization-mode=Node,RBAC \
  # 准入
  --enable-admission-plugins=NodeRestriction,ServiceAccount,MutatingAdmissionWebhook,ValidatingAdmissionWebhook,ResourceQuota,LimitRanger \
  --disable-admission-plugins=...
```

------

## 17.7 常见陷阱

| # | 陷阱 | 后果 | 解决 |
|---|------|------|------|
| 1 | 给 SA 绑 cluster-admin | 提权风险 | 最小权限 |
| 2 | K8s 1.24+ 仍用旧 SA Secret | Pod 无法获取 token | 改用 TokenRequest |
| 3 | OIDC username 无前缀 | 与本地用户冲突 | 加 oidc: 前缀 |
| 4 | RoleBinding 跨命名空间引用 Role | 失败 | Role 仅命名空间级 |
| 5 | ClusterRoleBinding 绑 Role | 失败 | ClusterRoleBinding 只能绑 ClusterRole |
| 6 | RBAC 规则过大 | 性能下降 | 用聚合 ClusterRole |
| 7 | Pod exec 权限过大 | 容器逃逸 | 严格限制 pods/exec |
| 8 | 默认 SA 权限太大 | 应用可越权 | 显式绑最小权限 |
| 9 | SA token 泄露 | 横向移动 | 短期 token + 受众绑定 |
| 10 | RBAC 缓存未及时更新 | 权限生效慢 | 等 5 分钟或重启 apiserver |
| 11 | Webhook Authz 超时 | 集群不可用 | 加 cache + 超时 |
| 12 | User 不存在 K8s 对象 | 难审计 | OIDC + 日志 |
| 13 | groups claim 配置错 | 群组授权失效 | 校验 IdP claim 名 |
| 14 | Node Authorizer 未启用 | kubelet 权限失控 | authorization-mode=Node,RBAC |
| 15 | kubeconfig 含长期 token | 凭证泄露 | 改用 OIDC + 短期 token |
| 16 | ResourceQuota 与 RBAC 混淆 | 资源超额 | RBAC 控权限,Quota 控数量 |
| 17 | 跨命名空间 SA 引用 | 失败 | SA 必须同命名空间 |

------

## 17.8 工业案例

### 17.8.1 阿里 ACK:RAM 集成

**场景**:阿里云 RAM(企业 IdP)与 K8s RBAC 集成。

**方案**:
- ACK 通过 webhook 把 RAM 用户映射为 K8s User
- RAM Role 映射为 K8s Group
- 用 ack-ram-authenticator 校验 RAM Token

**权限模型**:
- RAM 用户 alice@example.com
- K8s RoleBinding 把 alice@example.com 绑到 pod-reader
- 或把 RAM Group "developers" 绑到 edit

### 17.8.2 字节跳动:多租户 RBAC 体系

**场景**:全公司共享 K8s 集群,5 个 BU 共用,需要严格隔离。

**方案**:
1. 每个 BU 独立命名空间前缀(`bu1-*`, `bu2-*`)
2. 每个命名空间独立 SA + Role
3. ClusterRole 模板化(用 Kustomize)
4. OIDC 集成企业 SSO(Staff SSO)
5. Webhook Authz 校验额外策略(如禁止 delete namespace)
6. K8s 12345 端口审计日志,接入 SIEM

**特色**:
- 准入控制:禁止跨命名空间引用资源
- 网络策略:命名空间默认 deny
- 资源配额:每命名空间独立

### 17.8.3 Google GKE:Cloud IAM 集成

**场景**:GCP Cloud IAM 与 K8s RBAC 双层授权。

**机制**:
1. GCP 用户必须先有 IAM 权限(container.clusters.get)
2. 还需 K8s RBAC 授权
3. 两层都通过才能访问

**Config Connector**:
- 用 K8s CRD 管理 GCP 资源(IAM Policy 等)
- 统一 K8s API 体验

### 17.8.4 AWS EKS:IRSA(IAM Roles for SA)

**场景**:Pod 需访问 AWS 资源(S3/DynamoDB),传统方式用节点 IAM Role,无隔离。

**IRSA 方案**:
1. 创建 OIDC Provider:EKS 集群 OIDC URL 注册到 IAM
2. 创建 IAM Role:信任策略指定 SA
3. SA 注解:指定 IAM Role ARN
4. Pod 启动时:EKS 自动投射 AWS STS Token
5. Pod 用 STS Token 调 AWS API

```yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: my-sa
  namespace: production
  annotations:
    eks.amazonaws.com/role-arn: arn:aws:iam::123456789012:role/my-role
```

**优势**:
- Pod 级 IAM 隔离
- 短期 STS Token(1 小时)
- 自动轮转
- CloudTrail 审计到 Pod 级

### 17.8.5 Netflix:动态 RBAC

**场景**:权限需频繁变更,人工运维成本高。

**方案**:
1. 自研 RBAC Manager Operator
2. CRD 定义 GroupSync(从 LDAP 同步用户组)
3. CRD 定义 RoleTemplate(模板化 Role)
4. 自动生成 RoleBinding
5. GitOps:RBAC 配置存 Git,自动同步

**收益**:
- RBAC 变更从工单到分钟级
- 审计:Git history 即变更记录
- 回滚:Git revert

------

## 17.9 与其他方案关系

### 17.9.1 RBAC vs ABAC

| 维度 | RBAC | ABAC |
|------|------|------|
| 模型 | 角色-用户-权限 | 属性策略 |
| 灵活度 | 中(基于角色) | 高(基于属性) |
| 管理 | 简单 | 复杂 |
| K8s 支持 | 主流 | 已弃用 |
| 性能 | 好 | 一般 |

K8s 早期支持 ABAC,但策略文件难维护,1.6+ 默认 RBAC。

### 17.9.2 K8s RBAC vs Linux 文件权限

| 维度 | Linux | K8s RBAC |
|------|-------|----------|
| 对象 | 文件/目录 | K8s 资源 |
| 主体 | user/group | User/Group/SA |
| 权限 | rwx | verbs(get/list/create/...) |
| 模式 | ugo+x | Role/RoleBinding |

### 17.9.3 K8s RBAC vs AWS IAM

| 维度 | AWS IAM | K8s RBAC |
|------|---------|----------|
| 模型 | User/Role/Policy | User/Group/Role |
| 资源 | ARN | K8s resources |
| 策略 | JSON | YAML |
| 联邦 | SAML/OIDC | OIDC |
| 适用 | AWS 资源 | K8s 资源 |

**关系**:EKS IRSA 把两者打通,Pod 用 SA 拿到 AWS Role。

### 17.9.4 K8s RBAC 与 OPA/Gatekeeper

```
RBAC: 控制谁能做什么
  - 例:alice 能 create pods

OPA/Gatekeeper: 控制对象内容是否符合策略
  - 例:Pod 的 image 必须来自 registry.example.com

互补:
  - RBAC = 操作授权
  - OPA = 内容校验
  - 两者结合实现完整安全
```

### 17.9.5 与 Auth0/Keycloak 集成

```
Auth0/Keycloak 作为 OIDC IdP:
  - 用户管理在 IdP
  - K8s 信任 IdP 的 Token
  - 用户登录 IdP,获取 Token
  - Token 含 groups claim
  - K8s RBAC 按 groups 授权

优势:
  - 单点登录
  - MFA 支持
  - 集中审计
```

------

## 17.10 面试速答

**Q1: K8s 认证与授权的区别?**

认证(Authn)回答"你是谁",通过证书/Token/OIDC 等识别用户身份。授权(Authz)回答"你能做什么",通过 RBAC/Node/Webhook 判断权限。先认证后授权。

**Q2: RBAC 四大对象?**

Role(命名空间权限)、ClusterRole(集群权限)、RoleBinding(命名空间绑定)、ClusterRoleBinding(集群绑定)。Role/ClusterRole 定义权限,Binding 把权限绑到 User/SA。

**Q3: User 与 ServiceAccount 区别?**

User 是外部概念(K8s 没有内置 User 对象),由证书/OIDC/Token 提供方管理。ServiceAccount 是 K8s 内部对象,给 Pod 用,有 namespace 属性。

**Q4: K8s 1.24+ SA Token 变化?**

不再自动生成 Secret 持久 token,改用 TokenRequest API 生成短期 token(默认 1 小时),通过 projected volume 投射到 Pod,自动轮转,可绑 audience。

**Q5: ClusterRole 通过 RoleBinding 绑定是什么效果?**

ClusterRole 通过 RoleBinding 绑定后,仅在 RoleBinding 所在命名空间生效(降级)。常用于复用 ClusterRole 模板到多个命名空间。

**Q6: OIDC 在 K8s 中如何工作?**

IdP 颁发 ID Token,APIServer 验证签名(用 IdP 公钥),提取 username/groups claim,用于 RBAC 授权。kubelogin 等工具负责获取与刷新 Token。

**Q7: Node Authorizer 干什么?**

专门给 kubelet 用的授权器,基于 client certificate(CN=system:node:<name>, O=system:nodes)自动授权 kubelet 操作本节点资源,限制跨节点访问。

**Q8: Pod 内应用如何调 APIServer?**

Pod 自动获得 SA Token(投射卷),应用读 token + ca.crt,用 SDK(client-go)调 APIServer,APIServer 验证 token 后用 SA 身份做 RBAC 授权。

**Q9: IRSA 是什么?**

AWS EKS 的 IAM Roles for SA。EKS 集成 OIDC,把 K8s SA 与 IAM Role 绑定,Pod 自动获得 STS Token 访问 AWS 资源,实现 Pod 级 IAM 隔离。

**Q10: RBAC 与 NetworkPolicy 区别?**

RBAC 控制对 K8s API 的访问(谁能调 APIServer),NetworkPolicy 控制 Pod 间网络流量(谁的网络能通)。两者正交,共同保障安全。

------

## 17.11 综合面试题

### 题 1:设计多租户 K8s 集群的 RBAC 体系

```
需求:5 个 BU 共享集群,需严格隔离,最小权限

设计:
1. 命名空间隔离:
   - 每 BU 独立命名空间前缀(bu1-*, bu2-*)
   - ResourceQuota 限制每命名空间资源
   
2. 三层角色:
   - BU Admin:命名空间管理员(继承 admin ClusterRole)
   - Developer:开发者(继承 edit ClusterRole)
   - Viewer:只读(继承 view ClusterRole)
   
3. SA 管理:
   - 每命名空间独立 SA
   - Pod 用命名空间内 SA
   - IRSA(若 EKS)绑 AWS IAM Role
   
4. 集成 OIDC:
   - 企业 SSO 对接 Keycloak
   - groups claim = BU 角色组
   - 例:bu1-admins, bu2-developers
   
5. Webhook Authz:
   - 额外校验(如禁止删除 namespace)
   - 与配置管理中心集成
   
6. 审计:
   - audit log 接 SIEM
   - RBAC 变更走 GitOps
   - 定期权限审查
   
7. 紧急运维:
   - break-glass 用户(仅 breakglass@company.com)
   - 双人审批
   - 全程录像
```

### 题 2:Pod 无法调 APIServer,如何排查?

```
1. 进入 Pod 检查 token:
   kubectl exec -it <pod> -- cat /var/run/secrets/kubernetes.io/serviceaccount/token
   - token 是否存在?
   
2. 检查 SA:
   kubectl get sa <sa-name> -n <ns>
   - SA 是否存在?
   
3. 测试 APIServer 连通:
   kubectl exec -it <pod> -- curl -k https://kubernetes.default.svc/api
   - 网络是否通?
   
4. 检查 RBAC:
   kubectl auth can-i list pods --as=system:serviceaccount:<ns>:<sa> -n <ns>
   - SA 是否有权限?
   
5. 看 kube-apiserver 日志:
   journalctl -u kube-apiserver | grep <sa>
   - 是否有 forbidden?
   
6. 检查 token 过期:
   - 解析 JWT,看 exp
   - K8s 1.24+ token 1 小时过期,应自动轮转
   
7. 检查 audience:
   - token 是否绑定了特定 audience?
   - APIServer 是否接受?
```

### 题 3:解释 OIDC 流程及配置

```
1. IdP 配置:
   - 注册 client_id=kubernetes
   - 配置 redirect_uri(可选,CLI 流程)
   - 定义 groups claim

2. APIServer 配置:
   --oidc-issuer-url=https://idp.example.com
   --oidc-client-id=kubernetes
   --oidc-username-claim=email
   --oidc-username-prefix=oidc:
   --oidc-groups-claim=groups
   --oidc-groups-prefix=oidc:

3. 用户登录流程:
   a. kubectl 触发 kubelogin
   b. kubelogin 启动浏览器
   c. 用户在 IdP 登录
   d. IdP 返回 ID Token(含 email、groups)
   e. kubelogin 把 Token 给 kubectl
   f. kubectl 用 Bearer Token 调 APIServer
   
4. APIServer 处理:
   a. 从 Issuer URL 获取公钥(JWKS)
   b. 验证 Token 签名
   c. 验证 iss、aud、exp
   d. 提取 username=oidc:alice@example.com
   e. 提取 groups=[oidc:developers, oidc:admins]
   f. RBAC 按 username/groups 授权

5. Token 刷新:
   - ID Token 通常 1 小时过期
   - kubelogin 用 refresh_token 自动刷新
   - 用户无感
```

### 题 4:RBAC 与 NetworkPolicy 如何配合?

```
攻击场景:攻击者拿到 Pod shell,试图横向移动

RBAC 防护:
  - Pod 的 SA 仅能 get/list configmaps(本命名空间)
  - 不能 list secrets
  - 不能 create pods
  - 不能 get pods/exec(防止进入其他 Pod)
  
NetworkPolicy 防护:
  - 默认 deny-all ingress + egress
  - 仅允许访问特定 Pod(如本命名空间内的 db)
  - 仅允许访问 kube-dns
  - 禁止访问元数据服务(169.254.169.254)
  - 禁止访问其他命名空间 Pod

两者互补:
  - RBAC 防 API 调用
  - NetworkPolicy 防网络扫描
  - 攻击者拿到 shell 也只能干有限事
```

### 题 5:如何实现 Pod 级 IAM 权限?

```
方案 1:EKS IRSA
1. EKS 集群配置 OIDC Provider
2. AWS IAM 创建 Role,信任策略:
   {
     "Principal": {
       "Federated": "oidc-provider-arn"
     },
     "Condition": {
       "StringEquals": {
         "oidc-provider:sub": "system:serviceaccount:default:my-sa"
       }
     }
   }
3. SA 注解:eks.amazonaws.com/role-arn=...
4. Pod 自动获得 STS Token

方案 2:GKE Workload Identity
类似 IRSA,通过 GCP IAM 绑定 K8s SA

方案 3:自建(Vault 等)
1. Vault 通过 K8s Auth 后端验证 SA
2. 应用调 Vault 获取云凭证
3. Vault Token 短期有效

通用原则:
- Pod 级 IAM 隔离
- 短期凭证
- 自动轮转
- 审计到 Pod 级
```

### 题 6:解释 SubjectAccessReview

```
SubjectAccessReview(SAR):
  - K8s API 对象,用于查询"某 user 是否能做某操作"
  - 也可用于 Webhook Authz
  
用途:
1. kubectl auth can-i 内部用 SAR
2. 自定义 controller 检查权限后再操作
3. APIServer 通过 Webhook Authz 调外部服务

示例:
  kubectl auth can-i create pods --as=alice -n production
  ↓
  APIServer 创建 SAR:
    spec.user: alice
    spec.resourceAttributes: {namespace: production, verb: create, resource: pods}
  ↓
  Authz 链处理,返回 allowed: true/false
  
程序化使用:
  sar := &authorizationv1.SubjectAccessReview{...}
  clientset.AuthorizationV1().SubjectAccessReviews().Create(ctx, sar, opts)
  if sar.Status.Allowed { ... }
```

### 题 7:如何审计 K8s 权限?

```
1. Audit Log:
   - APIServer 启用 --audit-log-path
   - 记录所有 API 调用(user、verb、resource、time)
   - 接 SIEM(Splunk/ELK)
   
2. 定期权限审查:
   - 列举所有 RoleBinding/ClusterRoleBinding
   - 检查 cluster-admin 绑定数(应最少)
   - 检查过期用户
   
3. 工具:
   - rbac-lookup:谁有什么权限
   - kubectl-who-can:谁能做某操作
   - kube-hunter:漏洞扫描
   - kube-bench:CIS 基线检查
   
4. GitOps:
   - RBAC 配置存 Git
   - 变更走 PR
   - 自动 review + approve
   - Git history 即审计记录
   
5. 异常检测:
   - 监控异常 API 调用(批量 delete、list secrets)
   - 监控权限提升
   - 监控 break-glass 用户使用
```

------

## 17.12 故障复盘

### 案例 1:误绑 cluster-admin 导致数据丢失

**故障时间**:2023-10-25

**故障现象**:
- 开发误删生产 namespace
- 5 个生产服务下线

**根因**:
- 测试 SA 绑了 cluster-admin(方便调试)
- 应用 bug 误调用 delete namespace API
- RBAC 无限制

**修复**:
1. 紧急从备份恢复
2. 严格 RBAC,SA 仅命名空间级权限
3. 加 OPA 策略:禁止删除带 label production=true 的 namespace
4. 重要操作加 admission webhook 双人审批

**经验**:绝不为图方便绑 cluster-admin。

### 案例 2:SA Token 泄露导致横向移动

**故障时间**:2024-02-14

**故障现象**:
- 安全审计发现异常 API 调用
- 攻击者获取多个 namespace 的 Pod 列表

**根因**:
- 应用日志误打 SA Token
- 日志被攻击者获取
- Token 长期有效(K8s 1.20 旧版本)
- SA 权限过大(可 list 所有 namespace 的 Pod)

**修复**:
1. 立即撤销 token(删除 SA Secret)
2. 升级到 K8s 1.24+,启用短期 token
3. 限制 SA 权限(仅本命名空间)
4. 日志脱敏

**经验**:SA Token 是凭证,与密码同等保护。

### 案例 3:OIDC 公钥过期导致全集群不可用

**故障时间**:2024-04-08

**故障现象**:
- 所有 OIDC 用户无法访问集群
- 错误:token signature invalid

**根因**:
- IdP 旋转签名密钥
- APIServer 缓存旧公钥,未刷新
- 所有新 Token 验证失败

**修复**:
1. 重启 APIServer 触发 JWKS 刷新
2. 与 IdP 团队建立密钥轮转通知机制
3. 监控 IdP JWKS 端点变化

**经验**:OIDC 集成需考虑密钥轮转。

### 案例 4:RBAC 规则爆炸导致 APIServer 慢

**故障时间**:2024-06-20

**故障现象**:
- APIServer 响应慢,P99 5s+
- RBAC 评估耗时

**根因**:
- 集群 10000+ RoleBinding
- 每次请求遍历所有 binding
- RBAC 缓存命中率低

**修复**:
1. 用 ClusterRole 聚合减少 binding 数
2. 用 Group 而非 User 减少绑定
3. 升级 K8s 1.27+(RBAC 评估优化)
4. 监控 RBAC 评估耗时

**经验**:大规模集群需定期清理冗余 RBAC 规则。

### 案例 5:Webhook Authz 超时集群不可用

**故障时间**:2024-09-03

**故障现象**:
- 集群全部 API 调用失败
- 错误:authorization webhook timeout

**根因**:
- 自研 Authz Webhook 服务过载
- 超时 10s 后才返回
- APIServer 累积请求,内存爆满

**修复**:
1. 紧急扩容 Webhook 服务
2. 配置 cache-unauthorized-ttl=30s,cache-authorized-ttl=5m
3. Webhook 失败时降级到 RBAC(配置 fallback)

**经验**:Webhook 必须有 cache + 超时 + fallback。

------

## 17.13 参考与延伸

### 官方文档
- [Authentication](https://kubernetes.io/docs/reference/access-authn-authz/authentication/)
- [Authorization](https://kubernetes.io/docs/reference/access-authn-authz/authorization/)
- [Using RBAC Authorization](https://kubernetes.io/docs/reference/access-authn-authz/rbac/)
- [OIDC](https://kubernetes.io/docs/reference/access-authn-authz/authentication/#openid-connect-tokens)
- [Service Accounts](https://kubernetes.io/docs/concepts/security/service-accounts/)

### KEP
- [KEP-1393: Bound Service Account Tokens](https://github.com/kubernetes/enhancements/tree/master/keps/sig-auth/1393-bound-serviceaccount-tokens)
- [KEP-3325: Self Subject Review](https://github.com/kubernetes/enhancements/tree/master/keps/sig-auth/3325-self-subject-attributes-review-api)

### 源码导航
- `kubernetes/pkg/kubeapiserver/authenticator/` - 认证器
- `kubernetes/plugin/pkg/auth/authorizer/rbac/` - RBAC 授权器
- `kubernetes/pkg/registry/rbac/` - RBAC 资源存储
- `kubernetes/pkg/serviceaccount/` - SA token 生成与校验

### 相关章节
- [12-APIServer与etcd.md](./12-APIServer与etcd.md) - 请求 pipeline
- [15-CNI与网络模型.md](./15-CNI与网络模型.md) - NetworkPolicy 实现
- [18-NetworkPolicy与流量管控.md](./18-NetworkPolicy与流量管控.md) - 网络层安全
- [19-Pod安全.md](./19-Pod安全.md) - 准入控制
- [20-策略与治理.md](./20-策略与治理.md) - 策略即代码

### 推荐阅读
- [Securing Kubernetes Clusters](https://kubernetes.io/blog/2023/04/17/securing-clusters-with-psp-replacement/)
- [Kubernetes Security Review](https://github.com/kubernetes/community/blob/master/sig-security/sig-security-review.md)
- [RBAC Lookups](https://github.com/FairwindsOps/rbac-lookup)
- [CIS Kubernetes Benchmark](https://www.cisecurity.org/benchmark/kubernetes)

### 工具
- `kubectl auth can-i` - 权限检查
- `kubectl auth who-can` - 谁能做某操作
- `rbac-lookup` - 用户/SA 权限查询
- `kubelogin` - OIDC 登录
- `rakkess` - 权限矩阵
- `audit2rbac` - 从审计日志生成 RBAC

### 进阶主题
- **Bound ServiceAccount Tokens**:绑定 Pod/Secret 的短期 token
- **Pod Identity**:Workload Identity(IRSA/GKE)
- **SPIFFE/SPIRE**:跨集群工作负载身份
- **Authz Webhook**:自研授权
- **Dual Authorization**:双层授权
- **Just-in-Time Access**:临时提权
- **External KMS**:密钥管理集成

# 07 - ConfigMap 与 Secret

> 配置即代码。K8s 用 ConfigMap 管普通配置,Secret 管敏感数据,把它们与镜像解耦。本章讲清两者区别、注入方式、热更新、加密方案,以及工业级外部密钥管理。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- ConfigMap 与 Secret 的区别与用法?
- 4 种注入方式(env / envFrom / volume / csi driver)的取舍?
- 配置热更新怎么工作?哪些会更新,哪些不会?
- Secret 真的安全吗?怎么加密?怎么对接外部密钥管理(Vault / KMS)?
- 大配置 / 敏感配置 / 多环境配置怎么管?

### 1.2 不解决什么

- 不讲 RBAC(见第 17 章)
- 不讲 Pod 安全策略(见第 19 章)
- 不讲 GitOps(见第 27 章)

---

## 2. 直觉解释

### 2.1 "配置文件 vs 保险柜"类比

- **ConfigMap**:公司公告栏,配置文件公开,谁都能看
- **Secret**:公司保险柜,敏感数据加密,需要授权
- **Volume 注入**:把公告/保险柜内容复印一份,放进员工工位
- **env 注入**:把公告/保险柜内容,做成小纸条贴在员工工牌上
- **热更新**:公告栏内容变了,工位的复印版会同步(但工牌的纸条不会,要重新发牌)

### 2.2 为什么需要 ConfigMap / Secret

```
传统方式:
  把配置打进镜像 → 一个环境一个镜像,版本管理混乱
  把配置挂 NFS → 多环境共享,难隔离
  把配置用 -e 传 → 命令行历史泄漏

K8s 方式:
  ConfigMap / Secret → 配置与镜像分离,环境隔离
  ConfigMap:
    - 普通 YAML / JSON / Properties
    - 不加密(base64 编码,但不算加密)
    - 单对象 ≤ 1MB
  Secret:
    - 敏感数据(密码 / 证书 / Token)
    - base64 编码 + etcd 加密(可选)
    - 单对象 ≤ 1MB
    - kubelet 把 Secret 挂到 tmpfs,不落盘
```

---

## 3. 核心概念

### 3.1 ConfigMap

#### 3.1.1 创建方式

```bash
# 1. 从字面值创建
kubectl create configmap my-config \
  --from-literal=key1=value1 \
  --from-literal=key2=value2

# 2. 从文件创建
kubectl create configmap my-config \
  --from-file=app.properties \
  --from-file=configs/

# 3. 从 YAML 创建
kubectl apply -f - <<EOF
apiVersion: v1
kind: ConfigMap
metadata:
  name: my-config
  namespace: production
data:
  app.properties: |
    server.port=8080
    db.host=mysql
    db.port=3306
  LOG_LEVEL: "info"
  config.yaml: |
    database:
      host: mysql
      port: 3306
    cache:
      redis: redis:6379
EOF
```

#### 3.1.2 4 种注入方式

##### 方式 1:env(单值)

```yaml
spec:
  containers:
  - name: app
    image: my-app
    env:
    - name: LOG_LEVEL
      valueFrom:
        configMapKeyRef:
          name: my-config
          key: LOG_LEVEL
    - name: DB_HOST
      valueFrom:
        configMapKeyRef:
          name: my-config
          key: db.host
          optional: false         # 默认 false,ConfigMap 不存在则 Pod 启动失败
```

##### 方式 2:envFrom(全部)

```yaml
spec:
  containers:
  - name: app
    image: my-app
    envFrom:
    - configMapRef:
        name: my-config
        optional: true
    - prefix: APP_                # 加前缀,避免冲突
      configMapRef:
        name: another-config
```

##### 方式 3:volume(挂文件)

```yaml
spec:
  containers:
  - name: app
    image: my-app
    volumeMounts:
    - name: config
      mountPath: /etc/app
      readOnly: true
  volumes:
  - name: config
    configMap:
      name: my-config
      defaultMode: 0644
      items:                       # 可选,只挂部分 key
      - key: app.properties
        path: app.properties
        mode: 0644
      - key: config.yaml
        path: config.yaml
```

##### 方式 4:subPath(单文件覆盖)

```yaml
spec:
  containers:
  - name: app
    image: my-app
    volumeMounts:
    - name: config
      mountPath: /etc/nginx/nginx.conf
      subPath: nginx.conf          # 只覆盖这个文件,不影响其他
  volumes:
  - name: config
    configMap:
      name: nginx-config
```

#### 3.1.3 热更新机制

```yaml
# ConfigMap 更新后,挂 volume 的 Pod 会自动看到新内容
# 但 env / envFrom 的 Pod 不会更新,需要重启 Pod
```

**热更新流程**:
```
   1. kubectl edit configmap my-config
   2. kubelet Watch 到 ConfigMap 变化
   3. kubelet 更新 volume 内容(写入 /var/lib/kubelet/pods/.../volumes/...)
   4. Pod 内的文件内容变化(symlink + atomic 写)
   5. 应用是否感知,取决于应用是否监听文件变化
```

**注意**:
- env / envFrom 注入的不会热更新(进程内 env 不变)
- subPath 注入的不会热更新(快照)
- 只有 volume 挂载(不带 subPath)才会热更新
- 应用层需要监听文件变化(inotify / fsnotify)才能感知

#### 3.1.4 触发 Pod 滚动更新

```yaml
# 让 ConfigMap 变化触发 Deployment 滚动
spec:
  template:
    metadata:
      annotations:
        # 把 ConfigMap 的 hash 作为 annotation
        checksum/config: ${sha256_of_configmap}
    spec:
      containers:
      - name: app
        envFrom:
        - configMapRef:
            name: my-config
```

```bash
# 计算并更新 hash
HASH=$(kubectl get configmap my-config -o yaml | sha256sum | cut -d' ' -f1)
kubectl patch deployment my-app -p "{\"spec\":{\"template\":{\"metadata\":{\"annotations\":{\"checksum/config\":\"$HASH\"}}}}}"
```

### 3.2 Secret

#### 3.2.1 类型

| 类型 | 用途 | 自动挂载 |
|------|------|---------|
| **Opaque** | 通用 KV(默认) | 否 |
| **kubernetes.io/service-account-token** | SA Token | 是(/var/run/secrets/kubernetes.io/serviceaccount) |
| **kubernetes.io/dockerconfigjson** | 镜像仓库认证 | 否 |
| **kubernetes.io/tls** | TLS 证书 | 否 |
| **kubernetes.io/basic-auth** | 用户名密码 | 否 |
| **kubernetes.io/ssh-auth** | SSH 密钥 | 否 |
| **bootstrap.kubernetes.io/token** | 节点 bootstrap | 否 |

#### 3.2.2 创建 Opaque Secret

```bash
# 1. 从字面值
kubectl create secret generic my-secret \
  --from-literal=username=admin \
  --from-literal=password='S3cr3t!'

# 2. 从文件
kubectl create secret generic my-secret \
  --from-file=ssh-privatekey=~/.ssh/id_rsa \
  --from-file=ssh-publickey=~/.ssh/id_rsa.pub

# 3. 从 YAML(base64 编码)
kubectl apply -f - <<EOF
apiVersion: v1
kind: Secret
metadata:
  name: my-secret
type: Opaque
data:
  username: YWRtaW4=          # echo -n admin | base64
  password: UzNjcjN0IQ==     # echo -n 'S3cr3t!' | base64
stringData:                    # stringData 不需 base64,会自动编码
  api_key: "sk-1234567890"
EOF
```

#### 3.2.3 创建 TLS Secret

```bash
kubectl create secret tls my-tls \
  --cert=server.crt \
  --key=server.key
```

```yaml
# 用法
apiVersion: networking.k8s.io/v1
kind: Ingress
spec:
  tls:
  - hosts:
    - api.example.com
    secretName: my-tls
```

#### 3.2.4 创建 dockerconfigjson Secret

```bash
kubectl create secret docker-registry regcred \
  --docker-server=registry.example.com \
  --docker-username=admin \
  --docker-password='S3cr3t!' \
  --docker-email=admin@example.com

# 用法
spec:
  imagePullSecrets:
  - name: regcred
```

#### 3.2.5 Secret 注入方式

```yaml
# 1. env
env:
- name: DB_PASSWORD
  valueFrom:
    secretKeyRef:
      name: db-secret
      key: password

# 2. volume(挂到 tmpfs,不落盘)
volumes:
- name: secrets
  secret:
    secretName: db-secret
    defaultMode: 0400           # 文件权限
    items:
    - key: password
      path: db-password
volumeMounts:
- name: secrets
  mountPath: /etc/secrets
  readOnly: true

# 3. imagePullSecrets(镜像拉取)
imagePullSecrets:
- name: regcred
```

### 3.3 Secret 的安全性

#### 3.3.1 默认存储

- etcd 中 base64 编码(不算加密)
- 任何能 get secrets 的用户都能解码
- 默认 RBAC:namespace 内用户可读

#### 3.3.2 启用 etcd 静态加密(1.13+)

```yaml
# /etc/kubernetes/encryption-config.yaml
apiVersion: apiserver.config.k8s.io/v1
kind: EncryptionConfiguration
resources:
- resources:
  - secrets
  providers:
  - aescbc:
      keys:
      - name: key1
        secret: <base64-encoded-32-byte-key>
  - identity: {}                # 兜底,允许读未加密的旧数据
```

```yaml
# apiserver 启动参数
--encryption-provider-config=/etc/kubernetes/encryption-config.yaml
```

```bash
# 重写所有 Secret(加密)
kubectl get secrets --all-namespaces -o json | \
  kubectl replace -f -

# 验证
etcdctl get /registry/secrets/default/my-secret --print-value-only | xxd | head
# 加密后是二进制乱码,不再是 base64
```

#### 3.3.3 外部密钥管理(生产推荐)

##### 方式 1:Vault + External Secrets Operator

```yaml
# ExternalSecret:从 Vault 拉取
apiVersion: external-secrets.io/v1beta1
kind: ExternalSecret
metadata:
  name: my-app-secret
spec:
  refreshInterval: 1h
  secretStoreRef:
    name: vault-backend
    kind: SecretStore
  target:
    name: my-app-secret          # 生成的 K8s Secret 名
    creationPolicy: Owner
  data:
  - secretKey: password
    remoteRef:
      key: secret/data/my-app
      property: password
  - secretKey: api-key
    remoteRef:
      key: secret/data/my-app
      property: api-key
```

##### 方式 2:阿里云 KMS / AWS Secrets Manager

```yaml
apiVersion: external-secrets.io/v1beta1
kind: SecretStore
metadata:
  name: alibaba-kms
spec:
  provider:
    alibaba:
      auth:
        secretRef:
          accessKeyIDSecretRef:
            name: alicloud-credentials
            key: access-key-id
          accessKeySecretSecretRef:
            name: alicloud-credentials
            key: access-key-secret
      regionID: cn-hangzhou
```

##### 方式 3:Sealed Secrets(GitOps 友好)

```yaml
# 加密后的 Secret,可放 Git
apiVersion: bitnami.com/v1alpha1
kind: SealedSecret
metadata:
  name: my-secret
spec:
  encryptedData:
    password: AgB...
  template:
    metadata:
      name: my-secret
    type: Opaque
```

**Sealed Secrets 工作流**:
```
   1. kubeseal --cert pub-cert.pem < my-secret.yaml > my-sealed-secret.yaml
   2. 提交 my-sealed-secret.yaml 到 Git
   3. 集群内 Sealed Secrets Controller 解密,生成真实 Secret
   4. 私钥仅 Controller 有,加密数据可放 Git
```

#### 3.3.4 Secret 的 RBAC 控制

```yaml
# 限制谁可以读 Secret
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  namespace: production
  name: secret-reader
rules:
- apiGroups: [""]
  resources: ["secrets"]
  verbs: ["get"]
  resourceNames: ["my-app-secret"]    # 仅限特定 Secret
```

### 3.4 不可变 ConfigMap / Secret(1.21+)

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: my-config
immutable: true                  # 不可变
data:
  key: value
```

**优势**:
- kubelet 不再 Watch,大幅降低 apiserver 压力
- 适合大量 ConfigMap(>1000)的集群

**劣势**:
- 必须删除重建才能改

---

## 4. 操作流程与命令

### 4.1 创建与查看

```bash
# 创建 ConfigMap
kubectl create configmap app-config \
  --from-literal=LOG_LEVEL=info \
  --from-literal=DB_HOST=mysql

# 查看
kubectl get cm
kubectl describe cm app-config
kubectl get cm app-config -o yaml

# 创建 Secret
kubectl create secret generic db-secret \
  --from-literal=username=admin \
  --from-literal=password='S3cr3t!'

# 查看(默认不显示 data)
kubectl get secret db-secret -o yaml
# 解码
kubectl get secret db-secret -o jsonpath='{.data.password}' | base64 -d
```

### 4.2 在 Pod 内验证

```bash
# env 注入
kubectl exec -it my-pod -- env | grep LOG_LEVEL

# volume 注入
kubectl exec -it my-pod -- cat /etc/app/app.properties

# Secret 注入
kubectl exec -it my-pod -- cat /etc/secrets/db-password
```

### 4.3 加密 etcd Secret

```bash
# 1. 生成 32 字节密钥
head -c 32 /dev/urandom | base64

# 2. 写 encryption-config.yaml
cat > /etc/kubernetes/encryption-config.yaml <<EOF
apiVersion: apiserver.config.k8s.io/v1
kind: EncryptionConfiguration
resources:
- resources:
  - secrets
  providers:
  - aescbc:
      keys:
      - name: key1
        secret: <上面生成的 key>
  - identity: {}
EOF

# 3. 修改 apiserver 静态 Pod
# 加 --encryption-provider-config=/etc/kubernetes/encryption-config.yaml

# 4. 重启 apiserver
sudo systemctl restart kubelet    # 静态 Pod 自动重启

# 5. 重写所有 Secret(让它们用新加密)
kubectl get secrets --all-namespaces -o json | kubectl replace -f -

# 6. 验证
ETCDCTL_API=3 etcdctl get /registry/secrets/default/db-secret --print-value-only | hexdump -C | head
# 加密后是二进制,不是 base64
```

### 4.4 安装 External Secrets Operator

```bash
helm repo add external-secrets https://charts.external-secrets.io
helm install external-secrets external-secrets/external-secrets \
  -n external-secrets \
  --create-namespace \
  --set installCRDs=true

# 配置 SecretStore(连 Vault)
kubectl apply -f - <<EOF
apiVersion: external-secrets.io/v1beta1
kind: SecretStore
metadata:
  name: vault-backend
spec:
  provider:
    vault:
      server: https://vault.example.com
      path: secret
      version: v2
      auth:
        kubernetes:
          mountPath: kubernetes
          role: external-secrets
EOF

# 创建 ExternalSecret
kubectl apply -f external-secret.yaml

# 查看自动生成的 K8s Secret
kubectl get secret my-app-secret
```

---

## 5. 底层原理

### 5.1 ConfigMap Volume 的实现

```
   1. kubelet Watch 到 Pod + 关联的 ConfigMap
   2. 在 Node 上创建目录:/var/lib/kubelet/pods/<uid>/volumes/kubernetes.io~configmap/<name>/
   3. 把 ConfigMap 的每个 key 写成文件
   4. 用 symlink + atomic rename 实现热更新:
      - 写新文件到 ..data_tmp
      - rename ..data_tmp → ..data
      - symlink:..data → ..data_<timestamp>
   5. Pod 内 mount 到 /etc/app,实际指向 ..data
   6. ConfigMap 更新 → kubelet 重新写 → Pod 内文件变化
```

**热更新延迟**:
- kubelet 默认每 1min 检查 ConfigMap 变化
- 可配 `--sync-frequency=30s` 缩短

### 5.2 Secret Volume 的实现

```
   1. kubelet 调用 apiserver 拿 Secret
   2. 在 Node 上创建 tmpfs(tmpfs 在内存,不落盘)
   3. 把 Secret 的 key 写成文件
   4. Pod mount 到 tmpfs
   5. Pod 删除 → tmpfs 释放
```

**安全特性**:
- tmpfs 仅内存,Pod 删除立即消失
- Node 磁盘上不残留
- defaultMode 0400(仅 owner 可读)

### 5.3 etcd 加密流程

```
   写 Secret:
     apiserver → EncryptionConfiguration → aescbc 加密 → etcd
   
   读 Secret:
     etcd → apiserver → aescbc 解密 → 返回给客户端
   
   注:identity {} 兜底,允许读未加密的旧数据
       新写的 Secret 用 aescbc 加密
       重写后所有 Secret 都加密
```

---

## 6. 配置示例

### 6.1 生产级 ConfigMap + Deployment(完整)

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: app-config
  namespace: production
data:
  application.yml: |
    server:
      port: 8080
      tomcat:
        max-threads: 200
        accept-count: 100
    spring:
      datasource:
        url: jdbc:mysql://mysql:3306/mydb
        driver-class-name: com.mysql.cj.jdbc.Driver
        hikari:
          maximum-pool-size: 20
          minimum-idle: 5
      redis:
        host: redis
        port: 6379
        timeout: 3000
    logging:
      level:
        root: INFO
        com.example: DEBUG
  logback.xml: |
    <configuration>
      <appender name="STDOUT" class="ch.qos.logback.core.ConsoleAppender">
        <encoder>
          <pattern>%d{yyyy-MM-dd HH:mm:ss} [%thread] %-5level %logger{36} - %msg%n</pattern>
        </encoder>
      </appender>
      <root level="info">
        <appender-ref ref="STDOUT" />
      </root>
    </configuration>
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: my-app
spec:
  replicas: 3
  selector:
    matchLabels:
      app: my-app
  template:
    metadata:
      labels:
        app: my-app
      annotations:
        # ConfigMap 变化触发滚动
        checksum/config: "${sha256_of_configmap}"
    spec:
      containers:
      - name: app
        image: my-app:v1.5.0
        ports:
        - containerPort: 8080
        envFrom:
        - configMapRef:
            name: app-config
        env:
        - name: DB_PASSWORD
          valueFrom:
            secretKeyRef:
              name: db-secret
              key: password
        volumeMounts:
        - name: config
          mountPath: /etc/app
          readOnly: true
        - name: secrets
          mountPath: /etc/secrets
          readOnly: true
        resources:
          requests:
            cpu: 500m
            memory: 512Mi
          limits:
            cpu: 1000m
            memory: 1Gi
      volumes:
      - name: config
        configMap:
          name: app-config
          defaultMode: 0644
      - name: secrets
        secret:
          secretName: db-secret
          defaultMode: 0400
```

### 6.2 多环境配置(production / staging / dev)

```yaml
# dev
apiVersion: v1
kind: ConfigMap
metadata:
  name: app-config
  namespace: dev
data:
  LOG_LEVEL: "debug"
  DB_HOST: "dev-mysql"
---
# staging
apiVersion: v1
kind: ConfigMap
metadata:
  name: app-config
  namespace: staging
data:
  LOG_LEVEL: "info"
  DB_HOST: "staging-mysql"
---
# production
apiVersion: v1
kind: ConfigMap
metadata:
  name: app-config
  namespace: production
data:
  LOG_LEVEL: "warn"
  DB_HOST: "prod-mysql"
```

**部署用 Helm / Kustomize**:

```yaml
# Kustomize: base + overlay
# base/deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: my-app
spec:
  template:
    spec:
      containers:
      - name: app
        envFrom:
        - configMapRef:
            name: app-config

# overlays/production/kustomization.yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: production
resources:
- ../../base
configMapGenerator:
- name: app-config
  literals:
  - LOG_LEVEL=warn
  - DB_HOST=prod-mysql
```

### 6.3 Vault + ExternalSecret 完整模板

```yaml
# 1. SecretStore(连接 Vault)
apiVersion: external-secrets.io/v1beta1
kind: SecretStore
metadata:
  name: vault
  namespace: production
spec:
  provider:
    vault:
      server: https://vault.example.com
      path: secret
      version: v2
      auth:
        kubernetes:
          mountPath: kubernetes
          role: production-app
---
# 2. ExternalSecret(从 Vault 拉数据,自动生成 K8s Secret)
apiVersion: external-secrets.io/v1beta1
kind: ExternalSecret
metadata:
  name: my-app-secrets
  namespace: production
spec:
  refreshInterval: 1h            # 每小时刷新
  secretStoreRef:
    name: vault
    kind: SecretStore
  target:
    name: my-app-secret          # 生成的 K8s Secret 名
    creationPolicy: Owner
    template:
      type: Opaque
      data:
        DB_PASSWORD: "{{ .db_password }}"
        API_KEY: "{{ .api_key }}"
        JWT_SECRET: "{{ .jwt_secret }}"
  data:
  - secretKey: db_password
    remoteRef:
      key: production/my-app
      property: db_password
  - secretKey: api_key
    remoteRef:
      key: production/my-app
      property: api_key
  - secretKey: jwt_secret
    remoteRef:
      key: production/my-app
      property: jwt_secret
```

---

## 7. 常见陷阱与调优 ⚠️

### 7.1 ConfigMap 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **env 不热更新** | 改 ConfigMap 后 env 不变 | env 注入无热更新 | 重启 Pod 或改 volume 注入 |
| **subPath 不热更新** | subPath 挂的文件不变 | subPath 是快照 | 不用 subPath 或加 checksum |
| **ConfigMap 太大** | apiserver 拒绝 | 单对象 > 1MB | 拆分 / 用 OCI 镜像存配置 |
| **应用不感知** | 文件变了应用还用旧值 | 应用未监听文件 | 用 fsnotify / 重启 Pod |
| **checksum 算错** | 滚动不触发 | hash 计算方式不一致 | 用 `kubectl get cm -o yaml | sha256sum` |

### 7.2 Secret 陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **base64 误以为加密** | 用户以为安全 | base64 不是加密 | 启用 etcd 加密 + 外部密钥管理 |
| **Secret 在 git 中泄漏** | 代码审计发现 | 直接提交 Secret YAML | 用 Sealed Secrets / External Secrets |
| **SA Token 自动挂载** | Pod 默认有 SA Token | 默认行为 | `automountServiceAccountToken: false` |
| **权限过宽** | 任何 Pod 可读 Secret | RBAC 配置松 | 严格 RBAC + resourceNames |
| **解码失败** | base64 -d 报错 | 字符串有特殊字符 | 用 stringData 避免手动编码 |

### 7.3 加密陷阱

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| **etcd 加密后无法读旧 Secret** | 部分旧 Secret 读不出 | 加密配置错 | identity 兜底,允许读旧 |
| **密钥丢失** | 无法解密 Secret | 备份缺失 | 密钥多重备份 |
| **密钥泄漏** | 加密形同虚设 | 配置文件权限松 | 限制 encryption-config.yaml 访问 |
| **轮换困难** | 不知道怎么换密钥 | 文档少 | 加新 key 在前 + 重写 Secret |

---

## 8. 工业案例与基准数据

### 8.1 阿里:多环境配置管理

**场景**:某中型公司,3 环境(dev/staging/prod),100+ 微服务。

**方案**:
- Helm + 多 values.yaml 管理
- 敏感配置:阿里云 KMS + External Secrets
- 配置变更:GitOps(Argo CD)

**经验**:
1. **Helm 模板**:统一配置结构,values 区分环境
2. **ExternalSecret**:Vault 存敏感数据,自动同步到 K8s Secret
3. **配置变更触发滚动**:checksum annotation
4. **审计**:所有配置变更走 Git PR

### 8.2 字节:大规模 ConfigMap 性能优化

**场景**:8k 节点集群,某 namespace 10k ConfigMap。

**问题**:
- kubelet 每分钟 Watch ConfigMap,apiserver 压力大
- ConfigMap 变化触发大量 Pod 重启

**优化**:
1. **immutable ConfigMap**:大量只读配置标记不可变
2. **CSI Secret Store**:动态挂载,不走 etcd
3. **配置中心**:大配置走 Apollo / Nacos,不用 ConfigMap
4. **分层 ConfigMap**:基础配置 + 业务配置分开

### 8.3 Netflix:Vault 集中密钥管理

**场景**:全球 100+ 微服务,密钥数千个。

**方案**:
- HashiCorp Vault 集中管理
- External Secrets Operator 同步到 K8s
- 动态密钥:数据库账号动态生成,1 小时过期
- 审计:Vault 记录所有访问

**优势**:
- 密钥不进 Git,不进 etcd
- 动态密钥降低泄漏风险
- 集中审计,易追溯

### 8.4 各方案对比

| 方案 | 复杂度 | 安全性 | 适合 |
|------|--------|--------|------|
| 直接 Secret | 低 | 低(base64) | 测试 |
| etcd 加密 | 中 | 中 | 生产基础 |
| Vault + ESO | 高 | 高 | 大型企业 |
| Sealed Secrets | 中 | 中 | GitOps |
| 云 KMS | 中 | 高 | 上云生产 |

---

## 9. 与其他方案的关系

### 9.1 K8s ConfigMap vs Spring Cloud Config

| 维度 | ConfigMap | Spring Cloud Config |
|------|-----------|---------------------|
| 范围 | K8s 集群 | 跨集群 |
| 协议 | K8s API | HTTP |
| 热更新 | Volume 注入(自动) | Spring Bus / Webhook |
| 多语言 | 通用 | Java 为主 |
| 适合 | K8s 内应用 | Spring 生态 |

### 9.2 K8s Secret vs Vault

| 维度 | K8s Secret | Vault |
|------|-----------|-------|
| 存储 | etcd | Vault 后端 |
| 加密 | 可选(aescbc) | 强加密 |
| 动态密钥 | 否 | 是 |
| 审计 | K8s audit | Vault audit |
| 多集群 | 否 | 是 |
| 复杂度 | 低 | 高 |

### 9.3 Sealed Secrets vs External Secrets

| 维度 | Sealed Secrets | External Secrets |
|------|---------------|-----------------|
| 存储 | Git | Vault / KMS |
| 解密 | 集群内 Controller | 拉取生成 |
| 适合 | GitOps(配置即代码) | 中心化密钥管理 |
| 轮换 | 重加密 | 自动刷新 |

---

## 10. 面试速答 ⭐

| 问题 | 一句话答案 |
|------|----------|
| ConfigMap 与 Secret 区别? | ConfigMap 普通配置,Secret 敏感数据(base64 + 可加密) |
| Secret 安全吗? | 默认仅 base64,不算加密;启用 etcd 加密 + 外部密钥管理才安全 |
| ConfigMap 4 种注入方式? | env / envFrom / volume / subPath |
| 哪些注入会热更新? | 仅 volume(不带 subPath);env / envFrom / subPath 都不会 |
| 单 ConfigMap 大小限制? | 1MB(etcd 限制) |
| etcd 加密怎么启用? | --encryption-provider-config + aescbc + 重写 Secret |
| Sealed Secrets 干什么? | 加密后可放 Git,集群内 Controller 解密生成真实 Secret |
| ExternalSecret 干什么? | 从 Vault / KMS 拉数据,自动生成 K8s Secret |
| SA Token 默认挂载吗? | 是,/var/run/secrets/kubernetes.io/serviceaccount,可关闭 |
| immutable ConfigMap 优势? | kubelet 不再 Watch,降低 apiserver 压力 |

---

## 11. 综合面试题

### 11.1 基础题

**Q1**: ConfigMap 的 4 种注入方式,哪些会热更新?

**答题要点**:
- env:不会(env 注入是进程启动时读取,不更新)
- envFrom:不会(同 env)
- volume(不带 subPath):会(kubelet 自动更新 volume 内容)
- subPath:不会(快照,固定为挂载时内容)
- 应用是否感知:取决于应用是否监听文件变化

**Q2**: Secret 真的安全吗?怎么提升安全性?

**答题要点**:
- 默认仅 base64 编码,任何能 get secrets 的用户能解码
- 提升安全性:
  1. 启用 etcd 加密(--encryption-provider-config + aescbc)
  2. 严格 RBAC(限制 namespace + resourceNames)
  3. 外部密钥管理(Vault + External Secrets Operator)
  4. 动态密钥(短期 Token,自动轮换)
  5. 审计(K8s audit log + Vault audit)
  6. Secret 不进 Git(Sealed Secrets 或 ESO)

### 11.2 进阶题

**Q3**: 怎么让 ConfigMap 变化触发 Deployment 滚动?

**答题要点**:
- env / envFrom 注入不会触发滚动(配置在 Pod 内但不更新)
- 解决:加 checksum annotation
  ```yaml
  spec:
    template:
      metadata:
        annotations:
          checksum/config: <sha256-of-configmap>
  ```
- ConfigMap 变化 → 计算新 hash → patch Deployment annotation → 触发滚动
- 工具:Helm 的 checksum 函数 / Argo CD 的配置同步

**Q4**: 一个大型集群,ConfigMap 数量很多(>1000),apiserver 压力大,怎么优化?

**答题要点**:
1. **immutable ConfigMap**:大量只读配置标记不可变,kubelet 不再 Watch
2. **CSI Secret Store**:动态挂载,不走 etcd
3. **配置中心**:大配置走 Apollo / Nacos,不用 ConfigMap
4. **分层**:基础配置 + 业务配置分开,减少变更频率
5. **优化 kubelet**:`--sync-frequency=5m` 降低 ConfigMap 同步频率
6. **监控**:apiserver 请求 QPS + Watch 连接数

**Q5**: 怎么实现多环境配置管理(dev/staging/prod)?

**答题要点**:
- 方案 1:Helm + 多 values.yaml
  - 同一 chart,不同环境不同 values
  - `helm install app ./chart -f values-prod.yaml`
- 方案 2:Kustomize + overlay
  - base 共用,overlay 区分环境
  - `kustomize build overlays/production | kubectl apply -f -`
- 方案 3:Namespace + 环境变量
  - 同一 ConfigMap 名,不同 namespace 不同内容
- 敏感数据:External Secrets + 不同 Vault 路径
- 部署:GitOps(Argo CD),各环境自动同步

### 11.3 高级题

**Q6**: 设计一个 K8s 集群的密钥管理方案,要求支持轮换 + 审计 + 多集群。

**答题要点**:
- **中心化存储**:Vault 集群(多副本 + 跨机房)
- **接入 K8s**:External Secrets Operator(ESO)
  - SecretStore 配置 Vault 连接
  - ExternalSecret 定义拉取规则
  - 自动生成 K8s Secret + 定期刷新
- **轮换**:
  - Vault 动态密钥(数据库账号 1h 过期)
  - ESO refreshInterval 定期同步
  - 长期密钥定期手动轮换
- **审计**:
  - Vault audit log(所有访问)
  - K8s audit log(Secret 操作)
  - 集中收集(Splunk / ELK)
- **多集群**:
  - Vault 多 cluster 模式
  - 各 K8s 集群独立 ESO + Vault 端点
  - 跨集群密钥同步(Vault replication)
- **加密**:
  - etcd 启用 aescbc 加密
  - Vault 后端加密(Transit Engine)
- **RBAC**:
  - 各环境独立 SA + Role
  - 限制 Secret 访问范围

**Q7**: 应用读取配置后,如何在不重启 Pod 的情况下让配置生效?

**答题要点**:
- 方式 1:**应用监听文件变化**
  - ConfigMap 用 volume 注入(自动热更新)
  - 应用用 fsnotify / inotify 监听
  - 配置变化时重新加载(如 Spring Cloud Kubernetes)
- 方式 2:**长轮询 / 推送**
  - 配置中心(Apollo / Nacos)推送
  - 应用订阅配置变化
- 方式 3:**重新加载信号**
  - Sidecar 监听 ConfigMap,变化时发 SIGHUP / HTTP /reload
  - 如 nginx-ingress 的 reload
- 方式 4:**滚动重启**
  - checksum annotation 触发 Deployment 滚动
  - 简单但有损

### 11.4 设计题

**Q8**: 设计一个 GitOps 工作流,要求 Secret 也能在 Git 中管理。

**答题要点**:
- 方案:Sealed Secrets 或 SOPS
- **Sealed Secrets**:
  1. 集群内安装 Sealed Secrets Controller,生成密钥对
  2. 开发者用 kubeseal 加密 Secret → SealedSecret(可放 Git)
  3. Argo CD 同步 SealedSecret 到集群
  4. Controller 解密,生成真实 K8s Secret
  5. 私钥仅 Controller 持有,Git 中只有密文
- **SOPS**(Mozilla):
  1. 用 GPG / KMS 加密 Secret 的 value
  2. 加密后的 YAML 提交 Git
  3. 部署时用 sops-decrypt 解密
- **结合 KMS**:
  - 加密密钥存云 KMS(AWS KMS / 阿里 KMS)
  - 集群内 ServiceAccount 通过 IRSA / RAM Role 访问 KMS
- **流程**:
  1. 开发者写 Secret 明文(本地)
  2. 加密提交 Git
  3. Argo CD 同步
  4. Controller / Operator 解密生成真实 Secret
  5. Pod 使用
- **轮换**:
  - 密钥定期轮换
  - 重新加密所有 SealedSecret

---

## 12. 故障复盘

### 12.1 案例 1:Secret 误提交 Git 导致数据泄漏

**业务影响**:2023 年某公司,开发者把生产数据库密码的 Secret YAML 误提交到 GitHub 公开仓库,密码泄漏,被攻击者利用,数据库被勒索。

**根因**:
- 没有预提交钩子检查
- 用了 kubectl create secret --dry-run=client -o yaml 生成 YAML 后误提交
- 没有用 Sealed Secrets 或 ESO

**修复过程**:
1. 紧急:旋转所有泄漏的密码(数据库 / API Key / Token)
2. 删除 Git 历史(git filter-branch)
3. 强制 push,通知 GitHub 清缓存
4. 引入 Sealed Secrets + 预提交钩子
5. 安全审计,排查是否被攻击

**防范**:
- Secret 不进 Git(Sealed Secrets / ESO)
- 预提交钩子检查(truffleHog / git-secrets)
- Git 仓库私有 + 访问控制
- 定期扫描 Git 中的敏感信息

### 12.2 案例 2:ConfigMap 变更未生效导致业务异常

**业务影响**:2022 年某公司,改 ConfigMap 后业务行为未变,因为 env 注入未热更新,3 小时未发现。

**根因**:
- 用 env 注入而非 volume
- 开发者以为改了就生效
- 没有监控验证

**修复过程**:
1. 紧急:重启所有 Pod
2. 改造:env → volume 注入
3. 加 checksum annotation,ConfigMap 变化触发滚动
4. 加监控:配置变更后验证生效

**防范**:
- 统一配置注入规范(volume)
- 配置变更走 GitOps,自动触发滚动
- 监控配置版本与预期是否一致

### 12.3 案例 3:Vault 不可达导致应用启动失败

**业务影响**:2024 年某公司,Vault 集群网络抖动,ESO 无法刷新 Secret,新 Pod 拿不到 Secret 启动失败,持续 30 分钟。

**根因**:
- ESO 依赖 Vault,Vault 不可达 → Secret 无法刷新
- 已有 K8s Secret 仍可用(刷新失败不影响存量)
- 但新创建的 Pod(滚动更新)读不到 Secret

**修复过程**:
1. 紧急:Vault 网络恢复
2. 加缓存:ESO 配置失败回退(用旧 Secret)
3. 加监控:Vault 不可达告警
4. 高可用:Vault 多副本 + 跨机房

**防范**:
- Vault 集群高可用(3+ 副本 + 跨机房)
- ESO 容错:Vault 不可达时用已缓存 Secret
- 监控:Vault 健康 + ESO 同步状态
- 应急预案:Vault 故障时降级(本地 Secret)

---

## 13. 参考与延伸

### 13.1 官方文档

- [ConfigMaps](https://kubernetes.io/docs/concepts/configuration/configmap/)
- [Secrets](https://kubernetes.io/docs/concepts/configuration/secret/)
- [Configure a Pod to Use a ConfigMap](https://kubernetes.io/docs/tasks/configure-pod-container/configure-pod-configmap/)
- [Configure a Pod to Use a Secret](https://kubernetes.io/docs/tasks/inject-data-application/distribute-credentials-secure/)
- [Encrypting Secret Data at Rest](https://kubernetes.io/docs/tasks/administer-cluster/encrypt-data/)
- [Immutable ConfigMaps and Secrets](https://kubernetes.io/docs/concepts/configuration/secret/#secret-immutable)

### 13.2 工具与项目

- **External Secrets Operator**:对接 Vault / KMS / Secrets Manager
- **Sealed Secrets**:Bitnami 出品,GitOps 友好
- **SOPS**:Mozilla 出品,加密 YAML
- **Vault**:HashiCorp 密钥管理
- **CSI Secret Store**:CSI 驱动方式挂载 Secret

### 13.3 跨文件链接

- 上一章: [06 - 存储](./06-存储.md)
- 下一章: [08 - 调度器](./08-调度器.md)
- 详见: [12 - APIServer 与 etcd](./12-APIServer与etcd.md) / [17 - RBAC 与认证授权](./17-RBAC与认证授权.md) / [27 - GitOps 与持续部署](./27-GitOps与持续部署.md)
- 参考平行模块: [云计算安全 - 密钥管理](../云计算安全/README.md)

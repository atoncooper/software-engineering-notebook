# 07 - Docker Compose

> 单机多容器编排的事实标准。从开发环境到 CI 测试再到小规模生产,Compose 都是性价比最高的方案。

---

## 1. 问题定义与边界

### 1.1 本章解决什么

- 把 `compose.yaml` 的 **完整语法** 讲到能写生产级配置
- 把 **多容器依赖** 管理讲到能避免"启动顺序"陷阱
- 把 **开发 / 测试 / 生产** 多环境配置讲到位(override 机制)
- 把 Compose 的 **能力边界** 说清(什么时候该上 K8s)
- 把 **工业实践** 讲到能落地(团队 Compose 规范)

### 1.2 本章不解决什么

- 不讲 K8s 编排(见 [19-容器生态对比](./19-容器生态对比.md))
- 不讲 Swarm 编排(见 [18-Docker Swarm入门](./18-Docker-Swarm入门.md))
- 不讲 Compose 内部实现(基于 Python,已被 Go 重写)
- 不讲 CI/CD 流水线(见 [16-Docker与CI-CD](./16-Docker与CI-CD.md))

> **关键认知**:Compose 不是"轻量级 K8s",而是"声明式多容器管理工具"。它解决的是单机多容器的编排,K8s 解决的是多机多容器的调度。

---

## 2. 直觉解释

### 2.1 Compose 类比:乐谱

```
   单独 docker run                  docker compose
   ────────────────                  ─────────────
   每个容器一行命令                   一个 YAML 描述所有容器
   依赖关系靠脚本                     依赖关系靠 depends_on
   网络卷手动创建                     自动创建
   
   docker run -d --name db ...       ┌────────────────┐
   docker run -d --name app ...      │ compose.yaml   │
   docker run -d --name web ...      │  services:     │
                                     │    db: ...     │
                                     │    app: ...    │
                                     │    web: ...    │
                                     └────────────────┘
                                            │
                                            ▼
                                     docker compose up
                                     (一键起所有)
```

### 2.2 Compose 的定位

```
   复杂度 / 规模
   ▲
   │                  K8s
   │                  ───
   │              Swarm
   │              ─────
   │          Docker Compose
   │          ─────────────
   │      docker run
   │      ─────────────
   │
   └──────────────────────────→ 单机 ──→ 多机
```

**适用场景**:
- ✅ 开发环境(本地起全套依赖)
- ✅ CI 测试环境(一键起 + 测试 + 销毁)
- ✅ 小规模生产(单机,3-10 容器)
- ✅ 演示 / POC
- ❌ 多机集群(用 K8s)
- ❌ 复杂调度(用 K8s)

---

## 3. 核心概念与架构

### 3.1 Compose 文件结构

```yaml
# compose.yaml 顶层结构
name: myapp                    # 项目名(默认是目录名)

services:                      # 必填:服务定义
  web:
    image: nginx:1.25
    ...
  api:
    build: ./api
    ...

networks:                     # 可选:网络声明
  frontend:
  backend:

volumes:                      # 可选:卷声明
  db-data:
  app-logs:

secrets:                      # 可选:secret 声明
  db-password:
    file: ./secrets/db.txt

configs:                      # 可选:配置声明
  nginx-conf:
    file: ./nginx.conf
```

### 3.2 Compose 命令

```bash
# 启动(前台)
docker compose up

# 启动(后台)
docker compose up -d

# 构建并启动
docker compose up -d --build

# 指定文件
docker compose -f compose.prod.yaml up -d

# 多文件(后覆盖前)
docker compose -f compose.yaml -f compose.override.yaml up -d

# 停止并删除
docker compose down

# 停止但保留容器
docker compose stop

# 启动已存在
docker compose start

# 重启
docker compose restart

# 查看状态
docker compose ps

# 查看日志
docker compose logs -f
docker compose logs -f web api

# 进入容器
docker compose exec web sh

# 在运行的服务里执行命令
docker compose run --rm web npm test

# 拉取镜像
docker compose pull

# 构建镜像
docker compose build

# 查看配置(渲染后的 YAML)
docker compose config
```

### 3.3 项目名与容器命名

```
项目名:myapp(默认是目录名,或 name: 字段)

容器命名规则:<项目名>-<服务名>-<序号>
例:myapp-web-1, myapp-db-1

网络命名:myapp_frontend, myapp_backend
卷命名:myapp_db-data
```

---

## 4. 操作流程与命令

### 4.1 一个完整的 Web 应用 Compose

```yaml
# compose.yaml
name: myapp

services:
  # ── 前端 ──
  web:
    image: nginx:1.25-alpine
    ports:
      - "8080:80"
    volumes:
      - ./web/dist:/usr/share/nginx/html:ro
      - ./nginx/nginx.conf:/etc/nginx/nginx.conf:ro
    depends_on:
      api:
        condition: service_healthy
    networks:
      - frontend
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "wget", "-q", "--spider", "http://localhost/"]
      interval: 10s
      timeout: 3s
      retries: 3

  # ── 后端 API ──
  api:
    build:
      context: ./api
      dockerfile: Dockerfile
      args:
        VERSION: 1.2.3
    image: myapp-api:1.2.3
    environment:
      - ENV=production
      - DB_HOST=db
      - DB_PASSWORD_FILE=/run/secrets/db-password
      - REDIS_HOST=redis
    depends_on:
      db:
        condition: service_healthy
      redis:
        condition: service_started
    networks:
      - frontend
      - backend
    volumes:
      - app-logs:/var/log/app
    secrets:
      - db-password
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-fsS", "http://localhost:8000/health"]
      interval: 10s
      timeout: 3s
      retries: 3
      start_period: 30s
    deploy:
      resources:
        limits:
          cpus: "2.0"
          memory: 2g
        reservations:
          cpus: "0.5"
          memory: 512m

  # ── 数据库 ──
  db:
    image: postgres:16
    environment:
      POSTGRES_DB: myapp
      POSTGRES_USER: app
      POSTGRES_PASSWORD_FILE: /run/secrets/db-password
    volumes:
      - db-data:/var/lib/postgresql/data
      - ./db/init:/docker-entrypoint-initdb.d:ro
    networks:
      - backend
    secrets:
      - db-password
    restart: unless-stopped
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U app -d myapp"]
      interval: 10s
      timeout: 5s
      retries: 5

  # ── 缓存 ──
  redis:
    image: redis:7-alpine
    command: redis-server --maxmemory 256mb --maxmemory-policy allkeys-lru
    volumes:
      - redis-data:/data
    networks:
      - backend
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 3s
      retries: 3

networks:
  frontend:
    driver: bridge
  backend:
    driver: bridge
    internal: true   # 内部网络,不能访问外网

volumes:
  db-data:
  app-logs:
  redis-data:

secrets:
  db-password:
    file: ./secrets/db-password.txt
```

### 4.2 多环境:override 机制

```yaml
# compose.yaml(基础,开发环境)
services:
  web:
    image: nginx:1.25
    ports:
      - "8080:80"
  api:
    build: ./api
    volumes:
      - ./api:/app    # 开发挂载源码
    environment:
      - ENV=development
      - DEBUG=true
```

```yaml
# compose.prod.yaml(生产覆盖)
services:
  web:
    ports:
      - "80:443"      # 生产用 80
    restart: always
  api:
    build:
      context: ./api
      dockerfile: Dockerfile.prod
    volumes: !reset []  # 清空开发挂载
    environment:
      - ENV=production
      - DEBUG=false
    deploy:
      resources:
        limits:
          cpus: "2"
          memory: 2g
```

```bash
# 开发
docker compose up -d

# 生产
docker compose -f compose.yaml -f compose.prod.yaml up -d
```

### 4.3 默认 override

Compose 默认会加载 `compose.yaml` + `compose.override.yaml`(后者存在则合并)。开发用 override,生产用 `-f` 显式指定。

---

## 5. 底层原理(简略)

### 5.1 `docker compose up` 做了什么

```
1. 解析 compose.yaml
2. 创建项目网络(myapp_frontend, myapp_backend)
3. 创建卷(myapp_db-data, ...)
4. 按 depends_on 拓扑排序服务
5. 对每个服务:
   a. 拉取 / 构建镜像
   b. 创建容器(用 docker run 的参数)
   c. 等待 healthcheck 通过(若 depends_on.condition: service_healthy)
   d. 启动容器
6. 监听 SIGINT/SIGTERM,收到信号后 down
```

### 5.2 depends_on 的三种 condition

```yaml
depends_on:
  db:
    condition: service_started      # 容器启动即继续(默认)
  redis:
    condition: service_healthy      # 健康检查通过才继续
  cache:
    condition: service_completed    # 容器执行完才继续(如初始化脚本)
```

> **关键**:`depends_on` 只控制 **启动顺序**,不保证服务就绪。`service_healthy` 才是真就绪。

---

## 6. 代码与配置示例

### 6.1 开发环境热重载

```yaml
services:
  api:
    build: ./api
    volumes:
      - ./api:/app                    # 源码挂载
      - /app/node_modules             # 排除 node_modules(匿名卷)
    command: npm run dev              # nodemon 热重载
    environment:
      - CHOKIDAR_USEPOLLING=true      # 文件监听(Mac/Win 必须)
    ports:
      - "3000:3000"
      - "9229:9229"                   # 调试端口
```

### 6.2 CI 测试环境

```yaml
# compose.test.yaml
services:
  test:
    build:
      context: .
      dockerfile: Dockerfile.test
    depends_on:
      db:
        condition: service_healthy
    environment:
      - DB_HOST=db
      - DB_PASSWORD=test
    command: npm test
    # 不重启,跑完就退
    restart: "no"

  db:
    image: postgres:16
    environment:
      POSTGRES_DB: test
      POSTGRES_USER: test
      POSTGRES_PASSWORD: test
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U test"]
      interval: 2s
      timeout: 2s
      retries: 10
```

```bash
# CI 脚本
docker compose -f compose.test.yaml up --abort-on-container-exit --exit-code-from test
# 测试容器退出后整个 stack 退出,退出码 = test 容器的退出码
```

### 6.3 多服务扩容

```yaml
services:
  worker:
    build: ./worker
    deploy:
      replicas: 4     # 启动 4 个副本
```

```bash
docker compose up -d
# Creating myapp-worker-1 ... done
# Creating myapp-worker-2 ... done
# Creating myapp-worker-3 ... done
# Creating myapp-worker-4 ... done

# 扩缩容
docker compose up -d --scale worker=8
```

### 6.4 Compose Profile(可选服务)

```yaml
services:
  web:
    # 默认启动

  db:
    # 默认启动

  debug:
    profiles: ["debug"]    # 仅 debug profile 启动
    image: busybox
    command: sleep 3600

  load-test:
    profiles: ["test"]
    image: grafana/k6
    volumes:
      - ./load-test.js:/test.js
```

```bash
# 默认启动(web + db)
docker compose up -d

# 启动 debug 服务
docker compose --profile debug up -d

# 启动测试
docker compose --profile test run load-test
```

---

## 7. 常见陷阱与调优

### 7.1 陷阱:`depends_on` 不等于就绪

```yaml
# ❌ db 启动了但还没接受连接,api 连不上
services:
  api:
    depends_on:
      - db
    # db 容器启动即继续,但 PostgreSQL 还在初始化

# ✓ 等 healthcheck 通过
services:
  api:
    depends_on:
      db:
        condition: service_healthy
  db:
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U app"]
      interval: 2s
      retries: 20
```

### 7.2 陷阱:卷覆盖镜像内容

```yaml
# ❌ /app 整个被空卷覆盖,镜像内代码消失
services:
  api:
    volumes:
      - app-data:/app
```

**修复**:
- 开发:用 bind mount 挂源码(`./api:/app`),不是命名卷
- 生产:不用挂载 /app,代码在镜像里

### 7.3 陷阱:环境变量优先级

```
优先级(高 → 低):
1. docker compose run -e VAR=x
2. compose.yaml 的 environment
3. compose.yaml 的 env_file
4. Dockerfile 的 ENV
5. 镜像继承的基础镜像 ENV
```

### 7.4 陷阱:`build` 与 `image` 同时用

```yaml
services:
  api:
    build: ./api           # 从源码构建
    image: myrepo/api:1.0  # 构建后打这个 tag
```

**含义**:构建后镜像名为 `myrepo/api:1.0`,可推送到 registry。

### 7.5 陷阱:端口冲突

```yaml
# ❌ 多副本不能用相同宿主端口
services:
  web:
    ports:
      - "8080:80"
    deploy:
      replicas: 3   # 3 个副本都要 8080,冲突
```

**修复**:
- 用 `8080-8082:80` 端口范围
- 或不指定宿主端口,用负载均衡

### 7.6 陷阱:`!reset` 与 `!override`

```yaml
# Compose 2.20+ 支持的新语法
services:
  api:
    volumes:
      - ./code:/app
    environment:
      - DEBUG=true

# override 文件
services:
  api:
    volumes: !reset []        # 清空基础文件的 volumes
    environment:
      DEBUG: false            # 覆盖
```

### 7.7 陷阱:`docker compose down` 删卷

```bash
# ❌ 会删卷(数据丢失!)
docker compose down -v

# ✓ 不删卷(默认)
docker compose down
```

---

## 8. 工业案例与基准数据

### 8.1 Compose vs K8s:选型对比

| 维度 | Docker Compose | K8s |
|------|----------------|-----|
| 学习曲线 | 低(YAML) | 高(多概念) |
| 部署复杂度 | 单二进制 | 多组件 |
| 多机支持 | 否(Swarm 可) | 是 |
| 自愈 | 弱(restart) | 强(控制器) |
| 滚动更新 | 弱 | 强 |
| 服务发现 | DNS | kube-dns |
| 网络策略 | 无 | NetworkPolicy |
| 存储 | volume | PV/PVC/CSI |
| 监控 | 外部 | 集成 |
| 适用规模 | < 10 容器 | > 50 容器 |

**决策树**:
```
单机 + 容器 < 10?    → Compose
单机 + 容器 10-50?   → Compose + 监控
多机 + 容器 > 50?    → K8s
多机 + 容器 < 50?    → Swarm(慎用,生态萎缩)或 K8s
```

### 8.2 大厂 Compose 使用场景

- **开发环境**:几乎所有公司都用 Compose 起本地依赖(MySQL + Redis + Kafka)
- **CI 测试**:GitHub Actions / GitLab CI 大量用 Compose 起测试栈
- **小规模生产**:边缘节点、IoT、内部工具(< 10 容器)
- **POC / Demo**:快速验证想法

### 8.3 Compose 启动时间基准

**测试条件**:5 服务(web + api + db + redis + worker),冷启动。

| 场景 | 启动时间 |
|------|----------|
| 镜像已在本地 | 3-5 秒 |
| 需拉镜像(50 MB) | 10-20 秒 |
| 需构建(多阶段) | 1-3 分钟 |
| + healthcheck | + 10-30 秒 |

---

## 9. 与其他方案的关系

### 9.1 Compose vs Swarm vs K8s

| 维度 | Compose | Swarm | K8s |
|------|---------|-------|-----|
| 单机 | ✓ | ✓ | ✓ |
| 多机 | ✗ | ✓ | ✓ |
| Compose 文件 | 原生 | 兼容 | 不兼容(需 kompose 转换) |
| 编排 | 简单 | 中等 | 复杂 |
| 生态 | 大 | 萎缩 | 极大 |

### 9.2 Compose vs Helm

| 维度 | Compose | Helm |
|------|---------|------|
| 目标 | 单机多容器 | K8s 应用打包 |
| 模板 | YAML + 变量 | Go template |
| 仓库 | OCI registry | Helm repo |
| 适用 | 单机 | K8s 集群 |

### 9.3 `docker compose` v1 vs v2

| 维度 | v1(Python) | v2(Go) |
|------|--------------|---------|
| 命令 | `docker-compose`(中划线) | `docker compose`(空格) |
| 实现 | Python 脚本 | Go 二进制(docker 插件) |
| 性能 | 慢 | 快 5-10 倍 |
| 维护 | 已停止 | 活跃 |
| 新特性 | 无 | profiles / !reset / etc. |

> **基线**:用 v2,即 `docker compose`(空格)。

---

## 10. 面试速答

| 问题 | 一句话答案 |
|------|-----------|
| Compose 解决什么? | 单机多容器编排,声明式 YAML 一键起整套服务。 |
| Compose 与 K8s 区别? | Compose 单机、轻量;K8s 多机、强调度、自愈。 |
| `depends_on` 保证服务就绪吗? | 默认只保证启动顺序;`condition: service_healthy` 才保证就绪。 |
| 多环境配置怎么管理? | 多文件 override:`-f compose.yaml -f compose.prod.yaml`。 |
| Compose v1 与 v2 区别? | v1 是 Python(中划线命令),v2 是 Go 插件(空格命令),v2 快 5-10 倍。 |
| `docker compose down -v` 危险在哪? | 会删所有卷,数据丢失;默认 down 不删卷。 |
| profiles 干什么? | 可选服务分组,默认不启动,`--profile xxx` 启用。 |
| `build` + `image` 同时用什么? | 构建后打指定 tag,便于推送 registry。 |
| Compose 能多机吗? | 单机 Compose 不能;Swarm 模式可以(`docker stack deploy`)。 |
| `!reset` 干什么? | override 文件里清空基础配置(如 volumes: []),Compose 2.20+ 支持。 |

---

## 11. 综合面试题

### 题 1(基础)
**问**:Compose 中如何保证 db 就绪后 api 才启动?

**答题要点**:
- db 加 healthcheck(pg_isready)
- api 的 depends_on 用 `condition: service_healthy`
- 设置合理的 interval / retries
- 退出码:api 启动失败应有重试机制

### 题 2(实战)
**问**:写一个 Compose 配置,包含前端、后端、数据库,要求生产级。

**答题要点**:
- 三服务 + 三网络(frontend / backend internal)
- 数据库密码用 secret 文件
- 所有服务 healthcheck
- restart: unless-stopped
- 资源限制(limits / reservations)
- 日志限制(log_driver + max-size)
- 非 root 用户
- 只读根文件系统 + tmpfs

### 题 3(多环境)
**问**:同一份 Compose 配置如何同时支持开发与生产?

**答题要点**:
- 基础 compose.yaml(开发)
- compose.prod.yaml override
- 开发挂源码,生产不挂
- 开发用环境变量,生产用 secret
- 开发无资源限制,生产有
- 开发 DEBUG=true,生产 false
- 启动:`-f compose.yaml -f compose.prod.yaml`

### 题 4(故障)
**问**:`docker compose up` 后 api 容器一直重启,如何排查?

**答题要点**:
- `docker compose logs api` 看日志
- `docker compose ps` 看状态
- 是不是依赖未就绪(db / redis)
- 是不是配置错(环境变量、文件路径)
- 是不是资源不足(OOM)
- 是不是健康检查失败
- 临时:`docker compose run api sh` 进容器调试

### 题 5(架构)
**问**:为什么 Compose 不适合大规模生产?

**答题要点**:
- 单机限制:不能跨主机
- 无自愈:容器挂了只重启,不重调度
- 无负载均衡:Service 概念弱
- 无滚动更新:停机更新
- 无配置管理:ConfigMap / Secret 弱
- 无网络策略:不能限制容器间通信
- 适合:< 10 容器,单机,内部工具

### 题 6(工业)
**问**:CI 中用 Compose 跑集成测试,如何设计?

**答题要点**:
- compose.test.yaml:测试服务 + 依赖
- 测试服务跑完即退(restart: no)
- `--abort-on-container-exit`:任一容器退出全停
- `--exit-code-from test`:退出码 = 测试容器
- 测试后 `docker compose down -v`:清理
- 镜像缓存:CI runner 缓存 / buildx cache
- 并行:不同 job 用不同 project name

### 题 7(深度)
**问**:Compose 的 depends_on 在多机(Swarm)下还有效吗?

**答题要点**:
- 单机 Compose:有效
- Swarm 模式(`docker stack deploy`):**无效**
- Swarm 是声明式,不保证启动顺序
- 替代:应用层重试、readiness probe
- 教训:不要依赖 depends_on 做生产逻辑

### 题 8(性能)
**问**:`docker compose up` 启动慢,如何优化?

**答题要点**:
- 镜像预拉(本地有镜像最快)
- BuildKit 缓存:`--build` 用 cache-from
- 减少构建:用 `image:` 而非 `build:`
- 并行启动:去掉不必要的 depends_on
- 简化 healthcheck:start_period 长,interval 短
- 资源够:CI runner 别太弱

### 题 9(安全)
**问**:Compose 里如何安全地传数据库密码?

**答题要点**:
- 不用 environment 明文
- 用 secrets(文件挂载到 /run/secrets)
- 应用读 password_file 而非 password
- 或:运行时通过 Vault 注入
- secrets 文件不入 git
- 生产用 K8s Secret / 云 KMS

### 题 10(综合)
**问**:设计一个微服务项目的 Compose 配置,涵盖开发、测试、生产。

**答题要点**:
- 三份 YAML:compose.yaml(开发)+ compose.test.yaml + compose.prod.yaml
- 服务:web / api / worker / db / redis
- 网络:frontend(对外)+ backend(internal)
- 卷:db-data / redis-data / app-logs
- secrets:db-password / api-key
- 开发:源码挂载 + 热重载 + DEBUG
- 测试:测试容器 + abort-on-exit
- 生产:资源限制 + restart + 日志限制 + 非 root
- CI:buildx 缓存 + 镜像推送 + 跨架构

---

## 12. 故障复盘

### 案例 1:depends_on 误用导致启动失败

**现象**:某项目 api 容器启动报 `Connection refused: db:5432`,重试 5 次后退出。

**根因**:
- `depends_on: db` 只保证 db 容器启动,不保证 PostgreSQL 就绪
- api 启动时 db 还在初始化(3-5 秒)

**修复**:
```yaml
services:
  api:
    depends_on:
      db:
        condition: service_healthy
  db:
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U app"]
      interval: 2s
      retries: 20
```

**防范**:
- 所有依赖都用 service_healthy
- 应用层加重试(连接失败 sleep 1s 重试)

### 案例 2:`down -v` 误删数据库

**现象**:某开发者执行 `docker compose down -v` 清理测试环境,误删了生产数据库卷。

**根因**:
- 测试与生产用同一台机器(违规)
- 执行 `down -v` 没看清楚项目名
- 卷被删,数据丢失

**修复**:
- 测试与生产严格隔离
- 生产用 `down`(不带 -v)
- 卷定期备份

**防范**:
- 生产环境禁止 -v
- 卷备份脚本
- 监控卷删除事件

### 案例 3:override 文件配置覆盖错误

**现象**:某团队生产环境 api 容器内存超限 OOM,但配置写的是 4g。

**根因**:
- compose.yaml 写 memory 4g
- compose.prod.yaml 写 memory 2g(override)
- 实际生效的是 2g

**修复**:
```yaml
# compose.prod.yaml
services:
  api:
    deploy:
      resources:
        limits:
          memory: 4g    # 显式覆盖
```

**防范**:
- `docker compose config` 看渲染后的最终配置
- override 文件用 `!reset` 清空不需要的
- Code review 检查 override

### 案例 4:端口冲突导致服务起不来

**现象**:某团队 `docker compose up` 报 `port is already allocated`。

**根因**:
- 多个 Compose 项目用相同端口(8080)
- 或宿主机已有服务用 8080

**修复**:
- 不同项目用不同端口范围
- 用环境变量参数化端口
- 用 Traefik / Nginx 反向代理,容器用随机端口

### 案例 5:CI 中 Compose 残留导致下次失败

**现象**:CI 每隔几次就失败,报端口冲突或容器名冲突。

**根因**:
- 上次测试 `docker compose up` 后没 `down`
- 容器残留,占用端口与名字

**修复**:
```bash
# CI 脚本末尾必加
trap "docker compose down -v" EXIT

# 或用 --force-recreate
docker compose up --force-recreate --abort-on-container-exit
```

**防范**:
- CI 用临时 project name:`-p ci-$BUILD_ID`
- 每次开始先 `down -v` 清理
- 超时机制:超时自动 down

---

## 13. 参考与延伸

### 官方文档

- Compose file reference — https://docs.docker.com/compose/compose-file/
- Compose CLI — https://docs.docker.com/compose/reference/
- Compose vs Swarm — https://docs.docker.com/compose/swarm/

### 工具

- kompose — Compose 转 K8s
- docker compose config — 渲染最终配置
- Compose Watch(v2.22+) — 文件变更自动同步

### 相关模块

- [05-容器网络](./05-容器网络.md) — Compose 网络配置
- [06-数据存储与卷](./06-数据存储与卷.md) — Compose 卷配置
- [16-Docker与CI-CD](./16-Docker与CI-CD.md) — CI 中的 Compose
- [18-Docker-Swarm入门](./18-Docker-Swarm入门.md) — 多机 Compose
- [19-容器生态对比](./19-容器生态对比.md) — Compose vs K8s
- [infra开发](../infra开发/) — 服务编排

---

> **下一章**:[08-底层原理-namespaces](./08-底层原理-namespaces.md)

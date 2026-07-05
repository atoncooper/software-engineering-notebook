# 15. Dockerfile 生产模板

> 章节定位: 生产实战篇 · 第一章
> 前置章节: [14-监控与日志](./14-监控与日志.md)
> 后续章节: [16-CI-CD与Docker](./16-CI-CD与Docker.md)

---

## 15.1 思维导图

```
                生产级 Dockerfile
                      │
        ┌─────────────┼─────────────┐
        │             │             │
     基础镜像       构建优化       安全加固
        │             │             │
   ┌────┴────┐   ┌────┴────┐   ┌────┴────┐
   │         │   │         │   │         │
 distroless  多阶段构建   非 root 用户
 alpine      BuildKit    最小权限
 scratch     缓存挂载    只读文件系统
 debian-slim 层合并     删除 setuid
        │             │             │
        └─────────────┼─────────────┘
                      │
                      ▼
              生产模板库(按语言)
                      │
        ┌─────────────┼─────────────┐
        │             │             │
     Go/Java      Python/Node   静态站点
     Rust/C++     Ruby/PHP     Nginx/JS
```

**生产 Dockerfile 5 大目标**:

| 目标 | 衡量指标 | 优先级 |
|------|---------|-------|
| 镜像最小 | 体积 < 100MB | P0 |
| 构建最快 | 缓存命中 > 90% | P1 |
| 安全最高 | CVE 数 = 0 | P0 |
| 可重现 | 同一 commit 同一镜像 | P0 |
| 可调试 | 必要时能进容器排查 | P2 |

---

## 15.2 章节简介

Dockerfile 是镜像的"源代码",直接决定镜像质量。生产环境中,劣质 Dockerfile 导致的问题包括: 镜像 1GB+ 拖慢部署、包含高危 CVE、构建 20 分钟卡 CI、不同环境构建结果不一致。

本章从工业实战出发,沉淀 5 大语言(Go/Java/Python/Node/静态站点)的生产级 Dockerfile 模板,每个模板都经过以下验证:
- 镜像体积优化到极致
- BuildKit 缓存最大化
- 非 root 用户运行
- 健康检查与信号处理
- 多阶段构建分离构建环境与运行环境

**本章工业焦点**:
- Google distroless 实践(无 shell 镜像)
- 阿里 / 字节 Dockerfile 模板规范
- 字节镜像体积优化案例(Go 1.2GB→20MB)
- 供应链安全: SBOM 与镜像签名集成

---

## 15.3 核心概念

### 15.3.1 生产 Dockerfile 设计原则

**1. 单一职责**
```
错误: 一个 Dockerfile 同时构建 API + Worker + Cron
正确: 3 个 Dockerfile,共用 base image
```

**2. 分层缓存友好**
```dockerfile
# 错误: 改一行代码,所有层失效
COPY . /app
RUN pip install -r requirements.txt

# 正确: 依赖与代码分离
COPY requirements.txt /app/
RUN pip install -r requirements.txt
COPY . /app
```

**3. 最小权限**
```dockerfile
USER 65532:65532    # 非 root
```

**4. 不可变 + 可重现**
```dockerfile
# 错误: latest 浮动
FROM golang:latest

# 正确: 锁定版本 + digest
FROM golang:1.21.5-alpine3.18@sha256:abc123...
```

**5. 显式声明**
```dockerfile
EXPOSE 8080
HEALTHCHECK ...
ENTRYPOINT [...]
```

### 15.3.2 基础镜像选型决策树

```
是否需要 shell 调试?
├─ 是 → 是否极度在意体积?
│       ├─ 是 → alpine (5MB)
│       └─ 否 → debian-slim (80MB)
└─ 否 → 是否需要 glibc 兼容?
        ├─ 是 → distroless (20-50MB)
        └─ 否 → scratch (0MB,需静态编译)
```

**基础镜像对比**:

| 镜像 | 体积 | 包管理 | shell | glibc | 安全 | 适用 |
|------|------|-------|-------|-------|------|------|
| ubuntu | 78MB | apt | ✓ | ✓ | 中 | 通用 |
| debian-slim | 80MB | apt | ✓ | ✓ | 中高 | 通用生产 |
| alpine | 5MB | apk | ✓ | musl | 高 | 体积敏感 |
| distroless | 20-50MB | 无 | ✗ | ✓ | 极高 | 安全优先 |
| scratch | 0MB | 无 | ✗ | - | 极高 | 静态二进制 |
| ubi-minimal | 100MB | microdnf | ✓ | ✓ | 高 | 企业/RHEL |

**alpine 陷阱**: musl libc 与 glibc 不兼容,某些 Python C 扩展 / Java JNI 可能异常。Python 用 alpine 编译 numpy 等需 30 分钟,不推荐。

### 15.3.3 多阶段构建(Multi-stage)

```dockerfile
# 阶段 1: 构建
FROM golang:1.21 AS builder
WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -o app -ldflags="-s -w" .

# 阶段 2: 运行
FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=builder /app/app /app
USER nonroot:nonroot
EXPOSE 8080
ENTRYPOINT ["/app"]
```

**核心收益**:
- 构建工具(go/cargo/maven)不进入运行镜像
- 镜像体积降 90%+
- 攻击面大幅减小(无编译器/调试器)

### 15.3.4 BuildKit 缓存挂载

```dockerfile
# syntax=docker/dockerfile:1.6
FROM golang:1.21
COPY . .
RUN --mount=type=cache,target=/root/.cache/go-build \
    --mount=type=cache,target=/go/pkg/mod \
    go build -o app .
```

| 挂载类型 | 用途 | 跨构建共享 |
|---------|------|-----------|
| cache | 包仓库/编译缓存 | ✓ |
| bind | 只读挂载(替代 COPY) | - |
| tmpfs | 临时目录(RAM) | - |
| secret | 敏感信息(不进镜像) | - |
| ssh | SSH 密钥(拉私有仓库) | - |

**secret 挂载示例**:
```dockerfile
# syntax=docker/dockerfile:1.6
RUN --mount=type=secret,id=npmrc,target=/root/.npmrc \
    npm install
```

```bash
# 构建时传入 secret(不进镜像 layer)
DOCKER_BUILDKIT=1 docker build --secret id=npmrc,src=$HOME/.npmrc .
```

### 15.3.5 非 root 用户

```dockerfile
# 方式 1: 创建专用用户
RUN groupadd -r app && useradd -r -g app app
USER app

# 方式 2: 使用数字 UID(K8s 兼容性好)
USER 65532:65532

# 方式 3: distroless 内置 nonroot
FROM gcr.io/distroless/static-debian12:nonroot
# 已是 nonroot,无需手动指定
```

**为什么数字 UID**: K8s `securityContext.runAsUser` 需要数字,字符串名解析失败。

### 15.3.6 健康检查与信号处理

```dockerfile
# 健康检查
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD ["/app/healthcheck"]   # 退出码 0=健康 1=不健康

# 或用 curl/wget(需镜像含此工具)
HEALTHCHECK CMD curl -f http://localhost:8080/health || exit 1
```

**信号处理**(参考 [04-容器运行与生命周期](./04-容器运行与生命周期.md)):

```dockerfile
# 错误: shell 形式(信号传不到应用)
CMD /app/server

# 正确: exec 形式(应用成为 PID 1)
CMD ["/app/server"]

# Java 应用必须用 exec 形式才能接收 SIGTERM
ENTRYPOINT ["java", "-jar", "/app/app.jar"]
```

---

## 15.4 底层原理

### 15.4.1 镜像分层与缓存

```
Dockerfile 指令          镜像层                  缓存键
─────────────────────────────────────────────────────────
FROM golang:1.21      → base layer            → golang:1.21 digest
WORKDIR /app          → metadata(无层)        → 字符串 /app
COPY go.mod go.sum    → 新层(go.mod 内容)     → 文件内容 hash
RUN go mod download   → 新层(依赖)            → 命令 + 上层 hash
COPY . .              → 新层(代码)            → 文件内容 hash
RUN go build          → 新层(二进制)          → 命令 + 上层 hash
```

**缓存失效规则**:
- 任一层的缓存键变化,该层及**之后所有层**缓存失效
- `COPY . .` 任何文件改动 → 后续全部失效
- 因此**把变化频率低的放前面,变化高的放后面**

### 15.4.2 镜像 digest 与可重现

```bash
# 同一 Dockerfile + 同一 base digest + 同一代码
# 应该产生相同 image digest
$ docker build -t myapp:v1 .
$ docker images --digests myapp
myapp   v1   sha256:abc123def456...

# 但实际上,以下因素会导致 digest 不同:
# 1. base image 用 tag 而非 digest(tag 内容可能变)
# 2. RUN apt-get install(包版本可能变)
# 3. 时间戳(构建时间)
# 4. BuildKit 并行构建顺序
```

**生产建议**:
```dockerfile
# 锁定 base image digest
FROM golang:1.21.5-alpine3.18@sha256:abc123...

# 包管理锁版本
RUN apk add --no-cache ca-certificates=20230506-r0
```

### 15.4.3 多阶段构建的层复制

```dockerfile
COPY --from=builder /app/app /app
```

**底层**:
- `--from=builder` 从命名阶段复制
- 仅复制文件,**不复制层的元数据**
- 文件权限保留,但需注意 owner

```dockerfile
# 关键: --chown 修正权限
COPY --from=builder --chown=65532:65532 /app/app /app
```

### 15.4.4 镜像层合并

```dockerfile
# 错误: 5 个层
RUN apt-get update
RUN apt-get install -y curl
RUN apt-get install -y git
RUN rm -rf /var/lib/apt/lists/*
RUN apt-get clean

# 正确: 1 个层
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      curl git && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean
```

**为什么**: 每个 RUN 是一层,中间状态会保留(即使后面删除,前层仍有该文件)。合并为一个 RUN 确保中间产物不进入镜像。

---

## 15.5 代码实现

### 15.5.1 Go 应用生产模板

`Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.6

# ===== 阶段 1: 构建 =====
FROM golang:1.21.5-alpine3.18 AS builder

# 安装必要工具(ca-certificates 用于 TLS)
RUN apk add --no-cache git ca-certificates

WORKDIR /app

# 1. 依赖优先(缓存友好)
COPY go.mod go.sum ./
RUN --mount=type=cache,target=/root/.cache/go-build \
    --mount=type=cache,target=/go/pkg/mod \
    go mod download

# 2. 复制代码
COPY . .

# 3. 构建参数(版本信息)
ARG VERSION=dev
ARG COMMIT=unknown
ARG BUILD_DATE=unknown

# 4. 静态编译
RUN --mount=type=cache,target=/root/.cache/go-build \
    --mount=type=cache,target=/go/pkg/mod \
    CGO_ENABLED=0 GOOS=linux GOARCH=amd64 \
    go build \
      -ldflags="-s -w \
        -X main.version=${VERSION} \
        -X main.commit=${COMMIT} \
        -X main.buildDate=${BUILD_DATE}" \
      -o /out/app \
      .

# ===== 阶段 2: 运行 =====
FROM gcr.io/distroless/static-debian12:nonroot

# 元数据
LABEL org.opencontainers.image.title="myapp" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.source="https://github.com/me/myapp" \
      org.opencontainers.image.licenses="MIT"

# 复制二进制 + 证书
COPY --from=builder /out/app /app
COPY --from=builder /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/

# 非 root 用户(distroless:nonroot 已是 65532)
USER 65532:65532

EXPOSE 8080 9090

# 健康检查(distroless 无 shell,需二进制)
HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD ["/app", "-healthcheck"]

ENTRYPOINT ["/app"]
```

**构建命令**:

```bash
DOCKER_BUILDKIT=1 docker build \
  --build-arg VERSION=v1.0.0 \
  --build-arg COMMIT=$(git rev-parse HEAD) \
  --build-arg BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ) \
  -t myapp:v1.0.0 \
  -t myapp:latest \
  .
```

**镜像体积**: ~20MB(原始 1.2GB,优化 98%)

### 15.5.2 Java Spring Boot 模板

`Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.6

# ===== 阶段 1: 构建 =====
FROM maven:3.9.5-eclipse-temurin-17 AS builder

WORKDIR /build

# 1. 依赖优先
COPY pom.xml .
RUN --mount=type=cache,target=/root/.m2 \
    mvn dependency:go-offline -B

# 2. 复制源码并构建
COPY src ./src
RUN --mount=type=cache,target=/root/.m2 \
    mvn clean package -DskipTests -B

# 3. 提取 layers(Spring Boot 优化)
RUN java -Djarmode=layertools -jar target/*.jar extract

# ===== 阶段 2: 运行 =====
FROM eclipse-temurin:17.0.9_11-jre-alpine

# 安装必要工具(可选,dumb-init 处理信号)
RUN apk add --no-cache dumb-init ca-certificates tzdata && \
    addgroup -S app && adduser -S app -G app

WORKDIR /app

# Spring Boot 分层复制(依赖/应用分离,缓存友好)
COPY --from=builder --chown=app:app /build/dependencies/ ./
COPY --from=builder --chown=app:app /build/spring-boot-loader/ ./
COPY --from=builder --chown=app:app /build/snapshot-dependencies/ ./
COPY --from=builder --chown=app:app /build/application/ ./

USER app:app

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
  CMD wget -qO- http://localhost:8080/actuator/health | grep -q UP || exit 1

# dumb-init 作为 PID 1(正确转发信号)
ENTRYPOINT ["dumb-init", "--"]
CMD ["java", "org.springframework.boot.loader.launch.JarLauncher"]
```

**Spring Boot Layered Jar**:

```xml
<!-- pom.xml 启用分层 -->
<plugin>
  <groupId>org.springframework.boot</groupId>
  <artifactId>spring-boot-maven-plugin</artifactId>
  <configuration>
    <layers>
      <enabled>true</enabled>
    </layers>
  </configuration>
</plugin>
```

**分层效果**:
- dependencies: 第三方依赖(变化少)
- snapshot-dependencies: SNAPSHOT 依赖
- spring-boot-loader: 启动器
- application: 业务代码(变化频繁)

**镜像体积**: ~200MB(原始 800MB)

### 15.5.3 Python 模板

`Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.6

# ===== 阶段 1: 构建(编译 wheels) =====
FROM python:3.12-slim AS builder

# 系统依赖(编译 Python C 扩展用)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 创建虚拟环境(隔离依赖)
RUN python -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# 依赖优先
COPY requirements.txt .
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --no-cache-dir --upgrade pip && \
    pip install --no-cache-dir -r requirements.txt

# ===== 阶段 2: 运行 =====
FROM python:3.12-slim AS runtime

# 仅运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* && \
    groupadd -r app && useradd -r -g app app

# 复制虚拟环境
COPY --from=builder --chown=app:app /opt/venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH" \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

WORKDIR /app

# 复制代码
COPY --chown=app:app . .

USER app:app

EXPOSE 8000

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD curl -f http://localhost:8000/health || exit 1

# gunicorn 生产级启动
CMD ["gunicorn", "--bind", "0.0.0.0:8000", "--workers", "4", \
     "--threads", "2", "--timeout", "60", \
     "--access-logfile", "-", "--error-logfile", "-", \
     "app:app"]
```

`requirements.txt`(锁版本):

```
flask==3.0.0
gunicorn==21.2.0
psycopg[binary]==3.1.13
redis==5.0.1
structlog==23.2.0
prometheus-client==0.19.0
```

**镜像体积**: ~150MB(原始 1GB+)

### 15.5.4 Node.js 模板

`Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.6

# ===== 阶段 1: 依赖(全量,含 dev) =====
FROM node:20.10-alpine3.18 AS deps
WORKDIR /app
COPY package*.json ./
RUN --mount=type=cache,target=/root/.npm \
    npm ci

# ===== 阶段 2: 构建 =====
FROM node:20.10-alpine3.18 AS builder
WORKDIR /app
COPY --from=deps /app/node_modules ./node_modules
COPY . .
ARG NODE_ENV=production
ENV NODE_ENV=production
RUN npm run build && \
    npm prune --production

# ===== 阶段 3: 运行 =====
FROM node:20.10-alpine3.18 AS runtime

RUN apk add --no-cache dumb-init ca-certificates tzdata && \
    addgroup -S node && adduser -S node -G node

WORKDIR /app

# 仅复制运行时必要文件
COPY --from=builder --chown=node:node /app/node_modules ./node_modules
COPY --from=builder --chown=node:node /app/dist ./dist
COPY --from=builder --chown=node:node /app/package.json ./

USER node:node

ENV NODE_ENV=production \
    PORT=3000

EXPOSE 3000

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD wget -qO- http://localhost:3000/health || exit 1

ENTRYPOINT ["dumb-init", "--"]
CMD ["node", "dist/main.js"]
```

**npm ci vs npm install**:
- `npm ci`: 删 node_modules,严格按 package-lock.json 安装(更快,可重现)
- `npm install`: 可能修改 package-lock.json

**镜像体积**: ~120MB

### 15.5.5 静态站点模板(Nginx)

`Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.6

# ===== 阶段 1: 构建 SPA =====
FROM node:20.10-alpine3.18 AS builder
WORKDIR /app
COPY package*.json ./
RUN --mount=type=cache,target=/root/.npm npm ci
COPY . .
RUN npm run build

# ===== 阶段 2: 运行 =====
FROM nginxinc/nginx-unprivileged:1.25.3-alpine

# 移除默认配置
RUN rm /etc/nginx/conf.d/default.conf

# 复制自定义 nginx 配置
COPY nginx.conf /etc/nginx/conf.d/

# 复制构建产物
COPY --from=builder /app/dist /usr/share/nginx/html

# 非 root 用户(nginx-unprivileged 已是 101)
USER 101

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD wget -qO- http://localhost:8080/health || exit 1
```

`nginx.conf`:

```nginx
server {
    listen 8080;
    root /usr/share/nginx/html;
    index index.html;

    # SPA 路由
    location / {
        try_files $uri $uri/ /index.html;
    }

    # 静态资源长缓存
    location ~* \.(js|css|png|jpg|jpeg|gif|ico|svg|woff2?)$ {
        expires 1y;
        add_header Cache-Control "public, immutable";
    }

    # 健康检查
    location = /health {
        access_log off;
        return 200 "ok";
        add_header Content-Type text/plain;
    }

    # 安全头
    add_header X-Frame-Options "SAMEORIGIN";
    add_header X-Content-Type-Options "nosniff";
    add_header X-XSS-Protection "1; mode=block";
    add_header Referrer-Policy "strict-origin-when-cross-origin";

    # gzip
    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1024;
}
```

**镜像体积**: ~50MB

### 15.5.6 Rust 模板

`Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.6

# ===== 阶段 1: 构建 =====
FROM rust:1.74-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    pkg-config libssl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 依赖优先(Cargo.lock)
COPY Cargo.toml Cargo.lock ./
RUN mkdir src && echo "fn main() {}" > src/main.rs
RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/app/target \
    cargo build --release && \
    rm -rf target/release/deps/myapp*

# 实际代码构建
COPY . .
RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/app/target \
    cargo build --release && \
    cp target/release/myapp /out/

# ===== 阶段 2: 运行 =====
FROM debian:12.2-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 && \
    rm -rf /var/lib/apt/lists/* && \
    useradd -r -m -u 1000 app

COPY --from=builder --chown=app:app /out/myapp /app/myapp

USER app

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD /app/myapp healthcheck

ENTRYPOINT ["/app/myapp"]
```

**镜像体积**: ~80MB(原始 1.5GB)

---

## 15.6 配置示例

### 15.6.1 .dockerignore(必配)

```
# .dockerignore

# 版本控制
.git
.gitignore

# 依赖(避免覆盖容器内已安装)
node_modules
venv
.venv
__pycache__

# 构建产物
dist
build
target
*.pyc

# 测试与文档
test
tests
*_test.go
*.md
docs

# IDE
.idea
.vscode
*.swp

# 环境变量(敏感)
.env
.env.*
!.env.example

# Docker 自身
Dockerfile*
docker-compose*.yml
.dockerignore

# CI/CD
.github
.gitlab-ci.yml
Jenkinsfile
```

**收益**:
- 构建上下文从 500MB → 50MB
- 构建速度提升 50%+
- 避免敏感文件进入镜像

### 15.6.2 镜像标签规范

```
镜像命名: <registry>/<namespace>/<name>:<tag>@<digest>

推荐方案:
├─ myapp:v1.2.3              # 语义版本(生产)
├─ myapp:v1.2.3-20260704     # 版本+日期(可追溯)
├─ myapp:sha-abc1234         # git commit short hash
├─ myapp:branch-feature-x    # 分支名(测试)
└─ myapp:latest              # ❌ 禁止生产使用

最佳实践:
1. 生产用语义版本 + digest 锁定
2. 测试用 git commit hash
3. latest 仅本地开发
```

### 15.6.3 镜像元数据(OCI 标准)

```dockerfile
LABEL org.opencontainers.image.title="myapp" \
      org.opencontainers.image.description="Order service" \
      org.opencontainers.image.version="1.2.3" \
      org.opencontainers.image.revision="${COMMIT}" \
      org.opencontainers.image.created="${BUILD_DATE}" \
      org.opencontainers.image.source="https://github.com/me/myapp" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.vendor="MyCorp" \
      org.opencontainers.image.authors="dev@mycorp.com"
```

**查询**:
```bash
$ docker inspect myapp:v1.2.3 | jq '.[0].Config.Labels'
```

### 15.6.4 SBOM 与镜像签名

```bash
# 1. 生成 SBOM(软件物料清单)
$ docker buildx build --sbom=true -t myapp:v1 .

# 2. 用 Cosign 签名(参考 13 章)
$ cosign sign --key cosign.key myapp:v1

# 3. 用 Trivy 扫描
$ trivy image myapp:v1

# 4. 验证签名(K8s 准入)
$ cosign verify --key cosign.pub myapp:v1
```

```dockerfile
# Dockerfile 集成 SBOM 生成
# syntax=docker/dockerfile:1.6
FROM ...
COPY . .
RUN --mount=type=cache,target=/root/.cache \
    go build -o /app .

# 暴露 SBOM(可选)
COPY sbom.spdx.json /sbom.spdx.json
LABEL org.opencontainers.image.sbom="/sbom.spdx.json"
```

### 15.6.5 BuildKit 构建优化

```bash
# 1. 并行构建多阶段
DOCKER_BUILDKIT=1 docker build .

# 2. 跨构建缓存(本地)
docker build --cache-from=type=local,src=/tmp/.buildx-cache \
             --cache-to=type=local,dest=/tmp/.buildx-cache-new \
             -t myapp:v1 .

# 3. 跨构建缓存(远程 registry)
docker buildx build \
  --cache-from type=registry,ref=myorg/cache \
  --cache-to type=registry,ref=myorg/cache,mode=max \
  -t myapp:v1 \
  --push .

# 4. 多平台构建
docker buildx build --platform linux/amd64,linux/arm64 \
  -t myapp:v1 --push .

# 5. BuildKit 内联 SSH 拉私有仓库
docker build --ssh default=$SSH_AUTH_SOCK \
  -t myapp:v1 .
```

```dockerfile
# 用 SSH 拉私有 Go 依赖
# syntax=docker/dockerfile:1.6
FROM golang:1.21
RUN --mount=type=ssh \
    GOPRIVATE=git.mycorp.com \
    go mod download
```

---

## 15.7 工业案例与基准数据

### 15.7.1 案例 1: 字节跳动 Go 镜像优化

**初始状态**:
- 镜像: 1.2GB
- 基础: ubuntu + 完整 toolchain
- 构建: 25 分钟

**优化过程**:

| 阶段 | 优化项 | 镜像体积 | 构建时长 |
|------|-------|---------|---------|
| 初始 | ubuntu + go build | 1.2GB | 25min |
| 1 | 多阶段构建 | 350MB | 18min |
| 2 | 改 alpine | 80MB | 15min |
| 3 | distroless | 25MB | 15min |
| 4 | BuildKit cache mount | 25MB | 4min |
| 5 | 远程缓存(跨 CI) | 25MB | 90s |

**关键收益**:
- 体积降 98%(1.2GB → 25MB)
- 构建快 17x(25min → 90s)
- 部署快 5x(拉取 25MB vs 1.2GB)

### 15.7.2 案例 2: Google distroless 推广

**distroless 特点**:
- 无 shell(防止攻击者交互)
- 无包管理器(不能 apt install)
- 仅含运行时(JRE/Python) + 必要库(glibc/openssl)
- 体积小: distroless/cc 20MB, distroless/java 75MB

**Google 内部数据**:
- 100% 生产镜像用 distroless
- 容器逃逸 CVE 影响降低 90%
- 镜像体积平均降 70%

**distroless 系列镜像**:

| 镜像 | 体积 | 包含 |
|------|------|------|
| static-debian12 | 2MB | 无(仅 libc) |
| base-debian12 | 20MB | + glibc + openssl |
| cc-debian12 | 30MB | + libgcc + libstdc++ |
| python3-debian12 | 50MB | + Python 3 |
| nodejs18-debian12 | 75MB | + Node.js 18 |
| java17-debian12 | 200MB | + JRE 17 |

### 15.7.3 案例 3: 阿里 Dockerfile 规范

**强制规范**(摘自内部文档):

```
1. 基础镜像必须锁定 digest
   FROM golang:1.21.5@sha256:abc...  ✓
   FROM golang:latest                 ✗

2. 必须使用多阶段构建
3. 必须非 root 运行
4. 必须 HEALTHCHECK
5. 必须有 .dockerignore
6. 必须有 OCI labels
7. 必须扫描 CVE 通过
8. 必须签名(Cosign)
9. 必须生成 SBOM
10. 必须 < 200MB(Java < 300MB)
```

**自动化检查**(CI 集成):

```yaml
# .gitlab-ci.yml
dockerfile-lint:
  image: hadolint/hadolint:2.12.0
  script:
    - hadolint Dockerfile

image-scan:
  image: aquasec/trivy:0.48.0
  script:
    - trivy image --severity HIGH,CRITICAL --exit-code 1 myapp:$CI_COMMIT_SHA

image-size-check:
  script:
    - SIZE=$(docker image inspect myapp:$CI_COMMIT_SHA --format='{{.Size}}')
    - [ $SIZE -lt 209715200 ] || exit 1  # 200MB
```

### 15.7.4 性能基准对比

**镜像体积对比**(同一 Go 应用):

| 方案 | 体积 | 说明 |
|------|------|------|
| ubuntu + go | 1.2GB | 原始 |
| alpine + go | 350MB | 多阶段 |
| alpine 静态 | 25MB | CGO_DISABLED=0 |
| distroless | 20MB | 无 shell |
| scratch | 12MB | 静态二进制 |

**构建时长对比**(同一 Go 应用,首次 vs 二次):

| 方案 | 首次 | 二次(缓存) |
|------|------|------------|
| 无 BuildKit | 25min | 8min |
| BuildKit | 18min | 4min |
| BuildKit + cache mount | 15min | 90s |
| BuildKit + 远程缓存 | 15min | 30s |

**拉取时长对比**(1Gbps 网络):

| 镜像体积 | 拉取时长 | 1000 节点并发 |
|---------|---------|--------------|
| 1.2GB | 12s | 10min(P2P)/60min(直连) |
| 200MB | 2s | 3min(P2P)/15min(直连) |
| 20MB | 0.2s | 30s(P2P)/3min(直连) |

### 15.7.5 大厂基础镜像选型表

| 厂商 | Go | Java | Python | Node |
|------|----|----|--------|------|
| 阿里 | distroless/static | eclipse-temurin-jre | python-slim | node-alpine |
| 字节 | alpine + upx | dragonwell | python-slim | node-alpine |
| 腾讯 | distroless | korfin-jre | python-slim | node-alpine |
| Google | distroless/cc | distroless/java | distroless/python | distroless/nodejs |
| Netflix | alpine | eclipse-temurin | python-slim | node-alpine |

---

## 15.8 故障复盘

### 15.8.1 故障 1: latest 标签导致生产故障

**背景**: 2024-01,某公司凌晨生产故障,API 大量 500。

**现象**:
- 凌晨 3 点开始 5xx 飙升
- 应用启动失败,日志 `symbol lookup error`
- 同一 commit 构建的镜像,行为不一致

**根因**:
```dockerfile
# Dockerfile
FROM node:latest    # ❌ latest 浮动

# 凌晨 node:latest 从 20.10 更新到 21.0
# Node 21 移除了某些 API,应用未适配
```

**修复过程**:
1. **临时**: 修改本地 node:20.10 重新 tag 为 latest
2. **彻底**: 改用精确版本 + digest
   ```dockerfile
   FROM node:20.10.0-alpine3.18@sha256:abc123...
   ```

**预防措施**:
- **基础镜像必须锁定版本**(P0)
- **关键镜像锁定 digest**(防 registry 被篡改)
- **基础镜像更新走流程**: 测试 → 灰度 → 全量
- **依赖更新用 Renovate/Dependabot**: 自动 PR,人工 review

### 15.8.2 故障 2: apt-get install 引入 CVE

**背景**: 2024-03,某公司安全扫描发现生产镜像含高危 CVE(CVE-2024-3094 xz 后门)。

**现象**:
- Trivy 扫描显示 xz-utils 5.6.0 含后门
- 该 CVE 是供应链攻击,极度危险
- 应用本身未直接用 xz,但 apt-get install 间接拉入

**根因**:
```dockerfile
RUN apt-get update && apt-get install -y \
    curl git vim   # ← 间接依赖 xz-utils
```

**修复过程**:
1. 紧急升级到 xz-utils 5.4.5
2. 用 `--no-install-recommends` 减少间接依赖
   ```dockerfile
   RUN apt-get update && apt-get install -y --no-install-recommends \
       curl git && \
       rm -rf /var/lib/apt/lists/*
   ```
3. 改用 distroless(无包管理器,避免类似问题)
4. CI 加扫描门禁: HIGH/CRITICAL CVE 阻断构建

**预防措施**:
- **CI 强制 Trivy 扫描**(P0)
- **定期重建基础镜像**(获取安全补丁)
- **使用 distroless 替代完整 OS**(减少攻击面)
- **订阅 CVE 通告**: 关键依赖第一时间响应

### 15.8.3 故障 3: .dockerignore 缺失导致泄露

**背景**: 2024-04,某公司安全审计发现镜像内含 .env 文件(含数据库密码)。

**现象**:
- 镜像 layers 中能找到 .env 文件
- 即使应用不用,仍被 COPY . . 复制进镜像
- 密码泄露给所有能 pull 镜像的人

**根因**:
```dockerfile
COPY . .   # ← 没有 .dockerignore,所有文件都进镜像
```

**修复过程**:
1. **紧急**: 旋转所有泄露的密码/密钥
2. **删除镜像**: 强制删除所有版本,重新构建
3. **加 .dockerignore**:
   ```
   .env
   .env.*
   *.pem
   *.key
   credentials/
   ```
4. **CI 检查**: `dive` 工具检查镜像 layers,发现敏感文件失败

**预防措施**:
- **必须有 .dockerignore**(P0)
- **敏感文件单独挂载**: 不进镜像,运行时挂载
- **构建时用 secret mount**: 不进 layer
  ```dockerfile
  RUN --mount=type=secret,id=npmrc,target=/root/.npmrc \
      npm install
  ```
- **镜像扫描**: 用 trivy/dive 定期检查

### 15.8.4 故障 4: 应用收不到 SIGTERM

**背景**: 2024-02,某公司滚动更新时大量请求失败,持续 30s。

**现象**:
- K8s 滚动更新,旧 Pod 优雅退出
- 但实际请求还在处理就被强杀
- 应用日志无 shutdown 痕迹

**根因**:
```dockerfile
# Dockerfile
CMD "/app/server"   # ← shell 形式,信号传不到应用

# 实际执行: /bin/sh -c "/app/server"
# PID 1 是 sh,不是 server
# SIGTERM 被 sh 忽略,server 收不到
```

**修复过程**:
```dockerfile
# 改为 exec 形式
CMD ["/app/server"]

# 或用 entrypoint 脚本(注意最后用 exec)
COPY entrypoint.sh /
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
```

`entrypoint.sh`:

```bash
#!/bin/sh
# 前置初始化
echo "Starting..."
# 关键: exec 替换进程,使应用成为 PID 1
exec "$@"
```

**预防措施**:
- **CMD/ENTRYPOINT 必须用 exec 形式**(P0)
- **应用实现优雅退出**: 捕获 SIGTERM,停止接收新请求,等待旧请求
- **K8s preStop + terminationGracePeriodSeconds**: 留足退出时间
  ```yaml
  spec:
    terminationGracePeriodSeconds: 60
    containers:
      - name: app
        lifecycle:
          preStop:
            exec:
              command: ["sh", "-c", "sleep 10"]
  ```

### 15.8.5 故障 5: 镜像体积过大导致部署超时

**背景**: 2024-05,某公司扩容 1000 实例,部署耗时 45min,超时失败。

**现象**:
- 镜像 2.3GB(Java 完整 JDK)
- 单节点拉取 3min
- 1000 节点并发拉取,registry 拥塞
- 部署 45min 仍未完成

**根因**:
```dockerfile
# 用了完整 JDK 而非 JRE
FROM openjdk:17
# 包含编译器、调试工具、文档
```

**修复过程**:
1. **改用 JRE**:
   ```dockerfile
   FROM eclipse-temurin:17-jre-alpine
   ```
   2.3GB → 200MB
2. **多阶段构建 + Spring Boot 分层**: 200MB → 180MB
3. **接入 P2P 分发**(参考 [13 章](./13-镜像仓库与分发.md)): 部署 45min → 5min

**预防措施**:
- **镜像必须 < 200MB**(Java < 300MB)
- **CI 检查镜像体积**: 超阈值失败
- **大集群用 P2P 分发**: Dragonfly/Kraken
- **预热镜像**: 关键镜像提前推送到所有节点

---

## 15.9 最佳实践

### 15.9.1 Dockerfile 编写 10 条铁律

1. **锁定基础镜像版本**(必须)
2. **多阶段构建**(必须)
3. **依赖文件优先 COPY**(缓存友好)
4. **合并 RUN 指令**(减少层)
5. **清理中间产物**(apt cache, /tmp)
6. **非 root 用户**(必须)
7. **HEALTHCHECK**(必须)
8. **exec 形式 CMD/ENTRYPOINT**(信号处理)
9. **OCI 标准元数据**(LABEL)
10. **.dockerignore**(必须)

### 15.9.2 镜像分层策略

```
层 1 (变化最少):  基础镜像 + 系统依赖
层 2:             运行时(JRE/Python)
层 3:             应用依赖(requirements.txt / package.json)
层 4 (变化最多):  应用代码
层 5:             配置(可被 K8s ConfigMap 覆盖)
```

**收益**: 改业务代码,仅层 4-5 失效,层 1-3 命中缓存。

### 15.9.3 构建性能优化

| 优化项 | 收益 | 实施难度 |
|-------|------|---------|
| BuildKit cache mount | 5-10x | 低 |
| 远程缓存(registry) | 跨机器共享 | 中 |
| 多阶段并行构建 | 2-3x | 中 |
| 镜像分层优化 | 缓存命中 90%+ | 低 |
| .dockerignore | 减少 context 50%+ | 低 |
| 包管理器并行 | 1.5x | 低 |

### 15.9.4 安全加固清单

```dockerfile
# 1. 非 root 用户
USER 65532:65532

# 2. 只读文件系统(K8s 配合)
# K8s: securityContext.readOnlyRootFilesystem: true

# 3. 删除 setuid/setgid
RUN find / -perm /6000 -type f -exec chmod a-s {} \; || true

# 4. 最小 capabilities
# K8s: securityContext.capabilities.drop: [ALL]

# 5. 禁用特权
# K8s: securityContext.privileged: false

# 6. 资源限制
# K8s: resources.limits / requests

# 7. SELinux/AppArmor
# K8s: securityContext.seLinuxOptions

# 8. 网络 egress 控制
# NetworkPolicy
```

### 15.9.5 可调试性权衡

**问题**: distroless/scratch 无 shell,出问题无法进入排查。

**解决方案**:

1. **调试用镜像**: 同一 commit 构建两个镜像
   - `myapp:v1`(distroless,生产)
   - `myapp:v1-debug`(alpine + 工具,排查)

2. ** ephemeral container**(K8s 1.25+):
   ```yaml
   kubectl debug -it mypod --image=busybox --target=myapp
   ```

3. **诊断 sidecar**: 常驻一个 alpine sidecar 用于排查

4. **进程注入**: 用 `docker exec` 进入(需镜像含 sh)

**生产推荐**: distroless + ephemeral container,既安全又可调试。

---

## 15.10 常见陷阱

### 15.10.1 陷阱 1: COPY 缓存失效

**问题**:
```dockerfile
COPY . .
RUN go build
```

任何文件改动(包括 README)→ 后续所有层失效。

**解决**:
```dockerfile
# 依赖优先
COPY go.mod go.sum ./
RUN go mod download

# 代码后置
COPY . .
RUN go build
```

### 15.10.2 陷阱 2: ENV 与 ARG 混淆

**问题**:
```dockerfile
ARG ENV=production
RUN echo $ENV    # ✗ ARG 不能在 RUN 中直接用
```

**解决**:
```dockerfile
ARG ENV=production
ENV APP_ENV=$ENV   # ✓ 转为 ENV
RUN echo $APP_ENV
```

**区别**:
- ARG: 构建时变量,不进入运行时(可在 image history 看到)
- ENV: 运行时变量,容器内可用

### 15.10.3 陷阱 3: WORKDIR 不存在

**问题**:
```dockerfile
WORKDIR /app      # 自动创建
COPY . .          # 复制到 /app
```

**陷阱**: WORKDIR 会自动创建,但 owner 是 root。如果之前已 `USER app`,后续 COPY 的文件 owner 可能错乱。

**解决**:
```dockerfile
USER root
WORKDIR /app
COPY --chown=app:app . .
USER app
```

### 15.10.4 陷阱 4: 健康检查误判

**问题**:
```dockerfile
HEALTHCHECK CMD curl -f http://localhost:8080/health || exit 1
```

distroless 无 curl,HEALTHCHECK 永远失败。

**解决**:
```dockerfile
# 方案 1: 用二进制
HEALTHCHECK CMD ["/app/healthcheck"]

# 方案 2: 用 wget(alpine 内置)
HEALTHCHECK CMD wget -qO- http://localhost:8080/health || exit 1

# 方案 3: K8s livenessProbe 替代(推荐)
# 不依赖容器内工具
```

### 15.10.5 陷阱 5: 时区错误

**问题**: 容器默认 UTC,日志时间与本地差 8 小时。

**解决**:
```dockerfile
# alpine
RUN apk add --no-cache tzdata && \
    cp /usr/share/zoneinfo/Asia/Shanghai /etc/localtime && \
    echo "Asia/Shanghai" > /etc/timezone

# debian
RUN apt-get install -y tzdata && \
    ln -snf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime && \
    echo "Asia/Shanghai" > /etc/timezone
```

**推荐**: 全程 UTC,前端展示时转换(参考 [14 章](./14-监控与日志.md))。

### 15.10.6 陷阱 6: 多阶段命名混淆

**问题**:
```dockerfile
FROM golang:1.21 AS builder
FROM node:20 AS builder   # ❌ 重名
```

**解决**: 阶段名唯一,语义清晰。

```dockerfile
FROM golang:1.21 AS go-builder
FROM node:20 AS node-builder
FROM alpine AS runtime
```

---

## 15.11 面试题

### Q1: 为什么用多阶段构建?有什么好处?

**答**:
1. **分离构建与运行**: 编译器/构建工具不进入运行镜像
2. **镜像更小**: Go 1.2GB → 20MB,降 98%
3. **更安全**: 无 shell/编译器/调试器,攻击面小
4. **缓存友好**: 不同阶段独立缓存
5. **可重现**: 同一 Dockerfile 输出一致镜像

### Q2: COPY 和 ADD 的区别?

**答**:
- **COPY**: 仅复制本地文件/目录到镜像(推荐)
- **ADD**: 额外功能:
  - 自动解压 tar 包(`ADD app.tar.gz /app/`)
  - 支持 URL(`ADD http://... /app/`)
  - 但行为不可预测,不推荐

**最佳实践**: 始终用 COPY,需要解压用 `RUN tar`。

### Q3: CMD 和 ENTRYPOINT 区别?

**答**:

| 维度 | CMD | ENTRYPOINT |
|------|-----|-----------|
| 用途 | 默认参数 | 固定入口 |
| 是否可被覆盖 | `docker run image cmd` 覆盖 | 不会被覆盖 |
| 配合使用 | 给 ENTRYPOINT 传参 | 真正执行 |

```dockerfile
# 推荐: ENTRYPOINT + CMD(默认参数)
ENTRYPOINT ["/app/server"]
CMD ["--config", "/etc/app.conf"]

# docker run myapp                    # 用默认参数
# docker run myapp --config other.conf # 覆盖参数
```

### Q4: 镜像如何做到可重现?

**答**:
1. **基础镜像锁 digest**:
   ```dockerfile
   FROM golang:1.21.5@sha256:abc123...
   ```
2. **包管理锁版本**: pip/npm/go mod 锁文件
3. **消除时间依赖**: 构建时间用 ARG 注入,不依赖 `date`
4. **消除网络依赖**: 包仓库锁定版本,镜像内置
5. **消除随机性**: BuildKit `--build-arg BUILDKIT_SYNTAX` 锁定

```bash
# 用 reproducible build 标志
docker build --build-arg SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) ...
```

### Q5: 怎么减小镜像体积?

**答**:
1. **多阶段构建**(最大收益,降 80%+)
2. **选小基础镜像**(distroless/alpine/scratch)
3. **静态编译**(CGO_ENABLED=0)
4. **合并 RUN**(减少层 + 清理中间产物)
5. **去除调试工具**(无 shell/curl/vim)
6. **二进制 strip**(`-ldflags="-s -w"`)
7. **用 upx 压缩二进制**(再降 50%)
8. **.dockerignore**(减少构建上下文)

### Q6: 为什么 distroless 比 alpine 更安全?

**答**:
1. **无 shell**: 攻击者无法 exec sh
2. **无包管理器**: 不能 apt/apk install 装工具
3. **无编译器**: 不能现场编译 exploit
4. **最小依赖**: 仅含运行时 + glibc/openssl
5. **Google 维护**: 安全补丁及时

**对比**:
- alpine: 5MB,有 shell,有 apk
- distroless/static: 2MB,无 shell

### Q7: BuildKit 缓存挂载和镜像层缓存的区别?

**答**:

| 维度 | 镜像层缓存 | BuildKit cache mount |
|------|-----------|---------------------|
| 范围 | 同一 Dockerfile 内 | 跨构建、跨机器 |
| 失效 | 上层变化即失效 | 永不失效(手动清) |
| 位置 | 镜像 layer | 主机目录 |
| 进入镜像 | 是 | 否 |

```dockerfile
# 镜像层缓存
COPY go.mod .
RUN go mod download   # go.mod 变则失效

# cache mount
RUN --mount=type=cache,target=/go/pkg/mod \
    go mod download   # 永远命中缓存
```

### Q8: .dockerignore 为什么必须配?

**答**:
1. **减少上下文**: 500MB → 50MB,构建快
2. **缓存友好**: 避免 .git 等变动导致缓存失效
3. **安全**: 防止 .env / 密钥进入镜像
4. **可重现**: 排除本地配置文件

**必配项**:
- 版本控制(.git)
- 依赖(node_modules/venv)
- 构建产物(dist/build)
- IDE 配置(.idea/.vscode)
- 敏感文件(.env/*.pem)

### Q9: HEALTHCHECK 和 K8s livenessProbe 区别?

**答**:

| 维度 | HEALTHCHECK | livenessProbe |
|------|-------------|---------------|
| 作用域 | 单容器(Docker) | K8s Pod |
| 工具依赖 | 容器内(需 curl/wget) | K8s 提供(kubelet) |
| 灵活性 | 低(仅命令退出码) | 高(HTTP/TCP/Exec) |
| 推荐 | Docker Compose | K8s |

**K8s 场景**: 用 livenessProbe/readinessProbe/startupProbe,不依赖容器内工具。

### Q10: 怎么对镜像做安全扫描与签名?

**答**:
1. **扫描**: Trivy / Grype / Snyk
   ```bash
   trivy image --severity HIGH,CRITICAL myapp:v1
   ```
2. **签名**: Cosign / Notary
   ```bash
   cosign sign --key cosign.key myapp:v1
   ```
3. **SBOM**: 软件物料清单
   ```bash
   syft myapp:v1 -o spdx-json > sbom.spdx.json
   ```
4. **准入控制**: Kyverno / OPA Gatekeeper
   ```yaml
   # 仅允许已签名镜像
   apiVersion: kyverno.io/v1
   kind: ClusterPolicy
   spec:
     rules:
       - validate:
           message: "Image must be signed"
           pattern:
             spec:
               containers:
                 - image: "*@sha256:*"
   ```

---

## 15.12 总结

### 15.12.1 核心要点

1. **生产 Dockerfile 5 目标**: 小、快、安全、可重现、可调试
2. **基础镜像选型**: 静态 → scratch,动态 → distroless,体积敏感 → alpine
3. **多阶段构建是基石**: 分离构建与运行,体积降 90%+
4. **缓存友好分层**: 依赖前,代码后,变化频率递增
5. **BuildKit cache mount**: 跨构建共享缓存,构建快 10x+
6. **非 root + distroless**: 安全最佳组合
7. **OCI 元数据 + SBOM + 签名**: 供应链安全三件套

### 15.12.2 模板选型决策

```
你的应用是?
├─ 静态二进制(Go/Rust) → distroless/static 或 scratch
├─ Java → eclipse-temurin-jre-alpine + Spring Boot 分层
├─ Python → python-slim + venv 多阶段
├─ Node.js → node-alpine + npm ci + prune
├─ 静态站点 → nginx-unprivileged
└─ C/C++ 动态链接 → distroless/cc
```

### 15.12.3 工业实践要点

1. **阿里**: 10 条铁律 + CI 强制扫描 + digest 锁定
2. **字节**: Go 1.2GB→25MB,构建 25min→90s
3. **Google**: 100% distroless,CVE 影响降 90%
4. **Netflix**: alpine + 严格模板规范
5. **统一**: 锁版本 + 多阶段 + 非 root + 健康检查 + SBOM + 签名

### 15.12.4 与其他章节联系

- **[03-镜像原理与Dockerfile](./03-镜像原理与Dockerfile.md)**: 镜像分层与 Dockerfile 基础
- **[04-容器运行与生命周期](./04-容器运行与生命周期.md)**: PID 1 信号处理
- **[12-安全与隔离](./12-安全与隔离.md)**: 非 root / capabilities / distroless 安全
- **[13-镜像仓库与分发](./13-镜像仓库与分发.md)**: SBOM / Cosign 签名 / Kyverno 准入
- **[14-监控与日志](./14-监控与日志.md)**: HEALTHCHECK 与应用日志规范
- **[16-CI-CD与Docker](./16-CI-CD与Docker.md)**: 模板在 CI 流水线中的应用

---

## 15.13 参考资料

### 官方文档
- [Dockerfile reference](https://docs.docker.com/engine/reference/builder/)
- [BuildKit](https://docs.docker.com/build/buildkit/)
- [Dockerfile best practices](https://docs.docker.com/develop/develop-images/dockerfile_best-practices/)
- [Build secrets](https://docs.docker.com/develop/develop-images/build_secrets/)

### 基础镜像
- [Google distroless](https://github.com/GoogleContainerTools/distroless)
- [Alpine Linux](https://hub.docker.com/_/alpine)
- [debian-slim](https://hub.docker.com/_/debian-slim)
- [Red Hat UBI](https://catalog.redhat.com/software/container-stacks)

### 工具与项目
- [hadolint](https://github.com/hadolint/hadolint) - Dockerfile linter
- [dive](https://github.com/wagoodman/dive) - 镜像分层分析
- [Trivy](https://github.com/aquasec/trivy) - 漏洞扫描
- [Syft](https://github.com/anchore/syft) - SBOM 生成
- [Cosign](https://github.com/sigstore/cosign) - 镜像签名
- [Renovate](https://github.com/renovatebot/renovate) - 依赖更新

### 工业实践
- [Google: distroless containers](https://github.com/GoogleContainerTools/distroless/blob/main/README.md)
- [Alibaba Dockerfile 规范](https://developer.aliyun.com/article/782015)
- [字节跳动: 镜像优化实践](https://bytedance.feishu.cn/docs/)
- [Netflix: Docker in production](https://netflixtechblog.com/the-evolution-of-container-technology-at-netflix-3d3c1c6b3b6e)
- [Uber: Build system at scale](https://www.uber.com/blog/the-architecture-of-ubers-build-system/)

### 语言特定
- [Go production Dockerfile](https://github.com/golang-templates/seed)
- [Spring Boot Docker](https://spring.io/guides/gs/spring-boot-docker/)
- [Python Docker best practices](https://pythonspeed.com/docker/)
- [Node.js Docker](https://nodejs.org/en/docs/guides/nodejs-docker-webapp/)

### 标准与规范
- [OCI Image Format](https://github.com/opencontainers/image-spec)
- [SPDX SBOM](https://spdx.dev/)
- [SLSA Framework](https://slsa.dev/)
- [NIST Supply Chain Security](https://csrc.nist.gov/projects/supply-chain-risk-management)

---

> 下一章: [16-CI-CD与Docker](./16-CI-CD与Docker.md) - 模板在 CI/CD 流水线中的落地实践

# 20. FAQ - 常见问题与疑难解答

> 章节定位: 编排生态篇 · 第三章
> 前置章节: [19-生态对比](./19-生态对比.md)
> 后续章节: [21-大厂流水线](./21-大厂流水线.md)

---

## 20.1 思维导图

```
                  Docker FAQ
                      │
        ┌─────────────┼─────────────┐
        │             │             │
     安装配置        日常使用       故障排查
        │             │             │
   ┌────┴────┐   ┌────┴────┐   ┌────┴────┐
   │         │   │         │   │         │
 安装失败   镜像问题   容器无法启动
 daemon    容器问题   网络不通
 配置      网络问题   数据丢失
 权限      存储问题   性能问题
        │             │             │
        └─────────────┼─────────────┘
                      │
                      ▼
              按场景分类 FAQ
                      │
   ┌──────────────────┼──────────────────┐
   │                  │                  │
 开发环境           CI/CD              生产环境
   │                  │                  │
 Mac/Windows      构建慢             OOM
 WSL2             镜像大             卡死
 Desktop          缓存               重启循环
   │                  │                  │
 网络问题          推送失败           集群故障
 磁盘占用          私有仓库           滚动卡住
```

**FAQ 整理原则**:
- 高频问题(社区/面试)
- 实战陷阱(生产事故)
- 概念辨析(易混淆)
- 操作技巧(提高效率)

---

## 20.2 章节简介

本章汇总 Docker 使用中的高频问题,按场景分类解答。每个问题包含:
- **现象**: 具体症状
- **根因**: 为什么会这样
- **解决**: 具体操作步骤
- **预防**: 如何避免

覆盖 100+ 实战问题,是 Docker 用户的案头速查手册。

---

## 20.3 安装与配置 FAQ

### Q1: Docker 安装后 `docker` 命令需要 sudo,如何免 sudo?

**现象**: 普通用户执行 `docker ps` 报 `permission denied`。

**根因**: docker daemon 用 root 启动,socket 文件 `/var/run/docker.sock` 属于 root:docker 组。

**解决**:
```bash
# 1. 加入 docker 组
sudo usermod -aG docker $USER

# 2. 重新登录(或 newgrp docker)
newgrp docker

# 3. 验证
docker ps
```

**安全提示**: docker 组等价于 root(可用挂载文件提权),生产环境慎用。

### Q2: Windows Docker Desktop 启动失败怎么办?

**现象**: Docker Desktop 启动报错 `WSL 2 installation is incomplete`。

**根因**: WSL2 未正确安装或版本过低。

**解决**:
```powershell
# 1. 启用 WSL 功能
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart

# 2. 重启电脑

# 3. 安装 WSL2 内核更新
# 下载: https://wslstorestorage.blob.core.windows.net/wslblob/wsl_update_x64.msi

# 4. 设置 WSL2 为默认
wsl --set-default-version 2

# 5. 启动 Docker Desktop
```

### Q3: Docker daemon.json 在哪?修改后不生效?

**位置**:
- Linux: `/etc/docker/daemon.json`
- Docker Desktop: Settings → Docker Engine
- Rootless: `~/.config/docker/daemon.json`

**修改后不生效原因**:
```bash
# 1. 没重启 docker
sudo systemctl restart docker

# 2. JSON 格式错误(用 jq 验证)
cat /etc/docker/daemon.json | jq .

# 3. 配置项写错(查看日志)
journalctl -u docker -n 100
```

### Q4: 公司内网无法访问 Docker Hub 怎么办?

**方案 1: 镜像加速**:
```json
// /etc/docker/daemon.json
{
  "registry-mirrors": [
    "https://mirror.registry.aliyuncs.com",
    "https://docker.mirrors.ustc.edu.cn"
  ]
}
```

**方案 2: 私有 Harbor**:
```bash
# 推送到私有仓库
docker tag nginx:latest my-harbor.com/library/nginx:latest
docker push my-harbor.com/library/nginx:latest
```

**方案 3: 离线导入**:
```bash
# 在有网机器
docker pull nginx:latest
docker save nginx:latest -o nginx.tar

# 在内网机器
docker load -i nginx.tar
```

### Q5: Docker 占用磁盘越来越大怎么清理?

```bash
# 1. 查看占用
docker system df

# 2. 清理未使用
docker system prune -a --volumes
# -a: 删除所有未使用的镜像(不仅是 dangling)
# --volumes: 删除未使用的卷

# 3. 清理构建缓存
docker builder prune -af

# 4. 清理特定
docker image prune -a      # 镜像
docker container prune     # 容器
docker volume prune        # 卷
docker network prune       # 网络

# 5. 定时清理(cron)
0 3 * * * docker system prune -af --filter "until=168h"
```

---

## 20.4 镜像 FAQ

### Q6: 镜像 tag 和 digest 区别?哪个更可靠?

**Tag**: 可变标签(如 `nginx:1.25`)
**Digest**: 镜像内容的 SHA256(如 `nginx@sha256:abc...`)

**区别**:
- tag 可被覆盖(同一 tag 可能指向不同镜像)
- digest 不可变(内容固定)

**生产建议**: 关键镜像用 digest 锁定,防 registry 被篡改。

```dockerfile
FROM nginx:1.25.3@sha256:abc123def456...
```

### Q7: 镜像为什么 latest 不能用于生产?

**风险**:
- latest 内容随时变(可能引入 breaking change)
- 不同时间构建,latest 行为不一致
- 难以追溯(不知道具体版本)

**正确做法**:
- 用语义版本 `nginx:1.25.3`
- 或用 git commit hash `myapp:sha-abc1234`
- 锁定 digest `nginx@sha256:...`

### Q8: 怎么查看镜像分层?

```bash
# 1. docker history
docker history nginx:1.25

# 2. dive(更直观)
dive nginx:1.25

# 3. docker inspect
docker inspect nginx:1.25 | jq '.[0].RootFS.Layers'
```

### Q9: 镜像构建特别慢怎么优化?

**优化顺序**(参考 [15 章](./15-Dockerfile生产模板.md)):
1. **多阶段构建**(降 80%+)
2. **BuildKit cache mount**(`--mount=type=cache`)
3. **依赖文件前置**(缓存命中)
4. **.dockerignore**(减少上下文)
5. **远程缓存**(跨机器共享)
6. **预编译基础镜像**

### Q10: 怎么把镜像从一个 registry 迁移到另一个?

```bash
# 方案 1: pull + retag + push(简单)
docker pull old-registry/app:v1
docker tag old-registry/app:v1 new-registry/app:v1
docker push new-registry/app:v1

# 方案 2: crane(无需 docker)
crane copy old-registry/app:v1 new-registry/app:v1

# 方案 3: skopeo(支持多平台)
skopeo copy docker://old-registry/app:v1 docker://new-registry/app:v1

# 方案 4: registry 同步(Harbor replication)
```

### Q11: 镜像里怎么查看文件(不启动容器)?

```bash
# 1. docker create + cp
docker create --name tmp nginx:1.25
docker cp tmp:/etc/nginx/nginx.conf ./nginx.conf
docker rm tmp

# 2. dive(交互式浏览)
dive nginx:1.25

# 3. skopeo + tar
skopeo copy docker://nginx:1.25 docker-archive:nginx.tar
tar -xf nginx.tar
```

---

## 20.5 容器运行 FAQ

### Q12: 容器启动后立即退出怎么办?

**排查**:
```bash
# 1. 查看退出码
docker ps -a
# 0: 正常退出
# 1: 应用错误
# 125: docker 错误
# 126: 命令不可执行
# 127: 命令未找到
# 137: SIGKILL(OOM 或手动)
# 139: 段错误
# 143: SIGTERM(正常停止)

# 2. 查看日志
docker logs <container>

# 3. 查看详情
docker inspect <container>
```

**常见原因**:
- 应用是 daemon 进程(后台运行),docker 需要前台运行
- CMD 错误(命令不存在)
- 应用启动失败(配置/依赖问题)

**解决**:
```bash
# nginx: 用 daemon off
docker run nginx nginx -g "daemon off;"

# systemd 服务: 容器内不能用 systemd,改为直接运行二进制
```

### Q13: 容器内时间不对怎么改?

```bash
# 1. 挂载宿主机时区
docker run -v /etc/localtime:/etc/localtime:ro \
           -v /etc/timezone:/etc/timezone:ro \
           nginx

# 2. 设置 TZ 环境变量
docker run -e TZ=Asia/Shanghai nginx

# 3. Dockerfile 内设置
ENV TZ=Asia/Shanghai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
```

**推荐**: 全程 UTC,前端展示时转换。

### Q14: 容器内为什么 ping 不通外网?

**排查步骤**:
```bash
# 1. 进入容器
docker exec -it <c> sh

# 2. 测试 DNS
nslookup google.com

# 3. 测试 IP
ping 8.8.8.8

# 4. 查看路由
ip route

# 5. 查看 /etc/resolv.conf
cat /etc/resolv.conf
```

**常见原因**:
- DNS 配置错误 → 修改 daemon.json 的 dns
- iptables 规则异常 → 重启 docker
- 容器网络冲突 → 改 bip
- 防火墙拦截 → 检查 firewalld

### Q15: 容器怎么访问宿主机服务?

**方案 1: host.docker.internal**(Docker Desktop)
```bash
docker run -e API_URL=http://host.docker.internal:8080 app
```

**方案 2: 网桥 IP**(Linux)
```bash
# 获取网桥 IP
ip addr show docker0  # 通常 172.17.0.1

docker run -e API_URL=http://172.17.0.1:8080 app
```

**方案 3: host 网络**
```bash
docker run --network host app
# 容器直接用宿主机网络
```

### Q16: 容器内为什么没法用 systemd?

**原因**:
- systemd 需要 PID 1,但容器 PID 1 是应用
- systemd 需要 cgroup 文件系统,容器默认隔离
- systemd 需要 D-Bus,容器无

**解决**:
- 容器内不运行 systemd
- 一个容器一个进程(12-factor)
- 必要时用 supervisord / s6 / dumb-init

### Q17: 容器 OOM 怎么排查?

```bash
# 1. 查看 OOM 事件
docker inspect <c> | jq '.[0].State.OOMKilled'
# true 表示被 OOM 杀过

# 2. 查看主机 dmesg
dmesg | grep -i "killed process"

# 3. 查看容器内存使用
docker stats <c>

# 4. 应用级分析
# JVM: jmap -dump:format=b,file=heap.hprof PID
# Go: pprof
# Python: tracemalloc
```

**常见原因**:
- memory limit 过小
- 内存泄漏
- 大对象一次性加载
- JVM 堆未适配容器(参考 [17 章](./17-生产实践.md))

### Q18: 怎么进入运行中的容器?

```bash
# 1. exec(推荐)
docker exec -it <c> sh
docker exec -it <c> bash

# 2. nsenter(无 bash/sh 时)
PID=$(docker inspect -f '{{.State.Pid}}' <c>)
nsenter -t $PID -m -u -i -n -p

# 3. docker attach(会同步 stdin/stdout)
docker attach <c>
# 注意: Ctrl+P Ctrl+Q 退出,不要 Ctrl+C
```

---

## 20.6 网络 FAQ

### Q19: 端口映射不生效?

**排查**:
```bash
# 1. 查看端口
docker port <c>

# 2. 查看监听
ss -tlnp | grep <port>

# 3. 查看容器 IP
docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' <c>

# 4. 测试
curl http://localhost:<port>
```

**常见原因**:
- 应用监听 127.0.0.1 而非 0.0.0.0
- 端口冲突
- 防火墙拦截

**解决**: 应用配置监听 0.0.0.0
```bash
docker run -p 8080:80 nginx
# nginx.conf: listen 80; (默认 0.0.0.0)
```

### Q20: 容器间怎么通信?

**方式 1: 用户定义网络**(推荐)
```bash
docker network create my-net
docker run -d --network my-net --name app1 nginx
docker run -d --network my-net --name app2 nginx
# app1 容器内可用 app2 域名访问 app2
```

**方式 2: 容器间链接**(已弃用)
```bash
docker run --link app1:app1 nginx  # ❌ legacy
```

**方式 3: Compose 网络**
```yaml
services:
  web:
    networks: [app-net]
  redis:
    networks: [app-net]
networks:
  app-net:
```

### Q21: 自定义 bridge 网络和默认 bridge 区别?

| 维度 | 默认 bridge | 自定义 bridge |
|------|------------|--------------|
| DNS | ✗(需 --link) | ✓(容器名解析) |
| 隔离 | 弱 | 强 |
| 配置 | 简单 | 灵活 |

**生产建议**: 用自定义网络。

### Q22: overlay 网络跨主机不通?

**排查**:
```bash
# 1. 检查 Swarm 状态
docker node ls

# 2. 检查端口(必须开放)
# 2377/tcp: 集群管理
# 7946/tcp+udp: 节点发现
# 4789/udp: VXLAN

# 3. 防火墙
sudo ufw allow 2377/tcp
sudo ufw allow 7946/tcp
sudo ufw allow 7946/udp
sudo ufw allow 4789/udp

# 4. MTU 问题(云上常见)
docker network create -d overlay --opt com.docker.network.driver.mtu=1400 my-net
```

### Q23: 容器内 DNS 慢怎么优化?

```bash
# 1. ndots 调整(默认 5,过多查询)
docker run --dns-opt ndots:2 nginx

# 2. 自定义 DNS
docker run --dns 223.5.5.5 --dns 223.6.6.6 nginx

# 3. 应用层 DNS 缓存
# JVM: -Dnetworkaddress.cache.ttl=60
# Go: 使用 cachedresolver
```

---

## 20.7 存储 FAQ

### Q24: volume 和 bind mount 区别?

| 维度 | volume | bind mount | tmpfs |
|------|--------|-----------|-------|
| 位置 | Docker 管理(/var/lib/docker/volumes) | 任意宿主路径 | 内存 |
| 创建 | docker volume create | 自动(挂载点) | 自动 |
| 跨平台 | ✓ | ✗(路径依赖宿主) | ✓ |
| 备份 | docker volume export | 直接复制 | 不需要 |
| 适用 | 生产数据 | 配置文件 | 临时数据 |

### Q25: volume 数据怎么备份?

```bash
# 1. 备份
docker run --rm -v my-volume:/data -v $(pwd):/backup alpine \
  tar czf /backup/data.tar.gz -C /data .

# 2. 恢复
docker run --rm -v my-volume:/data -v $(pwd):/backup alpine \
  tar xzf /backup/data.tar.gz -C /data
```

### Q26: 容器删除后数据还在吗?

- **volume**: 在(独立于容器)
- **bind mount**: 在(在宿主机)
- **容器内文件系统**: 不在(随容器删除)

**结论**: 重要数据必须用 volume 或 bind mount。

### Q27: 多容器共享数据怎么处理?

```bash
# 方式 1: 共享 volume
docker volume create shared-data
docker run -v shared-data:/data app1
docker run -v shared-data:/data app2

# 方式 2: NFS
docker volume create --driver local \
  --opt type=nfs \
  --opt o=addr=nfs.server,rw \
  --opt device=:/path/to/dir \
  nfs-volume

# 方式 3: 分布式存储(Ceph/GlusterFS)
```

### Q28: 容器磁盘满了怎么办?

```bash
# 1. 查看占用
docker system df -v

# 2. 清理
docker system prune -a --volumes

# 3. 找大文件
du -sh /var/lib/docker/* | sort -h

# 4. 日志清理(常见元凶)
find /var/lib/docker/containers -name "*.log" -size +1G -exec truncate -s 0 {} \;

# 5. 配置日志轮转(daemon.json)
{
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "100m",
    "max-file": "5"
  }
}
```

---

## 20.8 Docker Compose FAQ

### Q29: docker-compose up 和 docker compose 区别?

- **docker-compose**(V1): Python 实现,独立命令
- **docker compose**(V2): Go 实现,作为 docker CLI 插件

**推荐**: 用 V2(`docker compose`),无需单独安装,性能更好。

### Q30: Compose 怎么等待依赖服务就绪?

```yaml
# depends_on 仅等待启动,不等待就绪
services:
  web:
    depends_on:
      db:
        condition: service_healthy
  db:
    image: mysql
    healthcheck:
      test: ["CMD", "mysqladmin", "ping"]
      interval: 5s
      retries: 10
```

**或用 wait-for-it 脚本**:
```yaml
web:
  command: ["./wait-for-it.sh", "db:3306", "--", "python", "app.py"]
```

### Q31: Compose 怎么覆盖配置(多环境)?

```bash
# 默认 + override
docker compose -f docker-compose.yml -f docker-compose.prod.yml up
```

```yaml
# docker-compose.yml
services:
  web:
    image: myapp
    environment:
      ENV: dev
```

```yaml
# docker-compose.prod.yml
services:
  web:
    environment:
      ENV: prod
    deploy:
      replicas: 3
```

### Q32: Compose 怎么传递变量?

```yaml
# docker-compose.yml
services:
  web:
    image: myapp:${TAG:-latest}
    environment:
      DB_PASSWORD: ${DB_PASSWORD}
```

```bash
# .env 文件(自动加载)
TAG=v1.2.3
DB_PASSWORD=secret

# 或命令行
TAG=v1.2.3 docker compose up

# 或 --env-file
docker compose --env-file prod.env up
```

---

## 20.9 性能 FAQ

### Q33: 容器性能比裸机慢多少?

**实测**(CPU 密集型):
- 计算密集: 慢 0-2%(几乎无影响)
- IO 密集: 慢 5-15%
- 网络密集: 慢 5-10%(bridge 模式)

**优化**:
- 用 host 网络模式
- 用巨页
- CPU 静态绑核
- NUMA 亲和

### Q34: docker stats 显示的 CPU 100% 什么意思?

- 单核机器: 100% = 1 核打满
- 4 核机器: 100% = 1 核打满,400% = 4 核打满
- 容器 limit=2: 200% 才是 limit

### Q35: 容器 CPU throttling 怎么排查?

```bash
# 查看 throttle 次数
cat /sys/fs/cgroup/cpu/docker/<id>/cpu.stat
# nr_periods: 总周期数
# nr_throttled: 被 throttle 的周期数
# throttled_time: throttle 总时间

# PromQL
rate(container_cpu_cfs_throttled_periods_total[5m])
  / rate(container_cpu_cfs_periods_total[5m])
```

**解决**:
- 调大 CPU limit
- 应用 GC 优化(避免短峰)
- CPU manager static 模式

### Q36: 容器网络延迟高怎么优化?

| 模式 | 延迟 | 适用 |
|------|------|------|
| bridge | 1-2ms | 默认 |
| macvlan | 0.5ms | 高性能 |
| ipvlan | 0.5ms | 高性能 |
| host | 0.1ms | 极致(无隔离) |

**优化**:
- 用 macvlan/ipvlan 替代 bridge
- 调内核参数(`net.core.somaxconn` 等)
- 用 SR-IOV(硬件虚拟化)

### Q37: 镜像拉取慢怎么加速?

```bash
# 1. 镜像加速器
{
  "registry-mirrors": ["https://mirror.registry.aliyuncs.com"]
}

# 2. P2P 分发(Dragonfly)
# 3. 延迟加载(Nydus/Stargz)
# 4. 预拉取(cron 拉到所有节点)
```

---

## 20.10 安全 FAQ

### Q38: 容器逃逸是什么?怎么防?

**逃逸**: 容器内进程突破隔离,访问宿主机。

**常见途径**:
- 内核漏洞(CVE)
- privileged 模式
- 挂载敏感目录(/、/proc、/var/run/docker.sock)
- capabilities 过多

**防御**:
- 不用 privileged
- 非 root 运行
- 删除所有 capabilities
- 用 gVisor/Kata 强隔离
- 及时升级内核

### Q39: docker.sock 挂载到容器有什么风险?

**风险**: 等价于 root 权限,可控制宿主机所有容器。

```bash
# 危险示例
docker run -v /var/run/docker.sock:/var/run/docker.sock ...
# 容器内可:
# docker run --privileged -v /:/host alpine  → 完全控制宿主机
```

**原则**:
- 不挂载 docker.sock
- 必要时用 socket proxy(过滤请求)

### Q40: 容器镜像怎么扫描漏洞?

```bash
# Trivy(推荐)
trivy image myapp:v1

# Grype
grype myapp:v1

# Docker Scout
docker scout cves myapp:v1

# Snyk(商业)
snyk container test myapp:v1
```

**CI 集成**:
```yaml
- uses: aquasecurity/trivy-action@master
  with:
    image-ref: myapp:v1
    severity: HIGH,CRITICAL
    exit-code: 1
```

### Q41: 怎么防止镜像被篡改?

```bash
# 1. 锁定 digest
FROM nginx:1.25.3@sha256:abc123...

# 2. Cosign 签名
cosign sign --key cosign.key myapp:v1

# 3. K8s 准入验证
# Kyverno / OPA Gatekeeper

# 4. registry 启用不可变 tag
# Harbor: 项目设置 → Tag Immutability
```

---

## 20.11 K8s 集成 FAQ

### Q42: K8s 1.24 后还能用 Docker 吗?

**能**,但含义变了:
- K8s 不再直接调用 Docker
- 节点用 containerd/CRI-O
- 镜像仍兼容(OCI 标准)
- Docker CLI 仍可用(但与 K8s 无关)

**影响**:
- 旧集群需迁移到 containerd
- 镜像无需重新构建
- docker 命令在 Pod 内仍可用(不推荐)

### Q43: Pod 内多个容器怎么共享数据?

```yaml
spec:
  containers:
  - name: app
    volumeMounts:
    - name: shared
      mountPath: /shared
  - name: sidecar
    volumeMounts:
    - name: shared
      mountPath: /shared
  volumes:
  - name: shared
    emptyDir: {}
```

### Q44: Pod 一直 Pending 怎么排查?

```bash
# 1. 查看 Pod 事件
kubectl describe pod <pod>

# 常见原因:
# - Unschedulable: 资源不足
# - FailedScheduling: nodeSelector/Affinity 不匹配
# - ImagePullBackOff: 镜像拉取失败
# - Insufficient cpu/memory: 资源不够

# 2. 查看节点资源
kubectl top nodes

# 3. 查看节点污点
kubectl describe node <node> | grep Taint
```

### Q45: Pod 一直 ContainerCreating 怎么办?

```bash
# 1. 查看事件
kubectl describe pod <pod>

# 常见原因:
# - 镜像拉取慢/失败
# - 挂载卷失败
# - 网络配置失败
# - secret/configmap 不存在

# 2. 查看 kubelet 日志
journalctl -u kubelet | grep <pod>

# 3. 查看容器运行时
crictl ps -a | grep <pod>
crictl logs <container>
```

### Q46: Pod 频繁重启怎么排查?

```bash
# 1. 查看重启次数
kubectl get pod <pod>
# RESTARTS 列

# 2. 查看上次终止原因
kubectl describe pod <pod> | grep -A 5 "Last State"

# 3. 查看日志
kubectl logs <pod> --previous

# 常见原因:
# - livenessProbe 太严
# - OOMKilled
# - 应用异常退出
# - 资源不足
```

---

## 20.12 生产环境 FAQ

### Q47: 容器化后怎么排查问题?

**工具箱**:
```bash
# 1. 进入容器
kubectl exec -it <pod> -- sh
# 或
docker exec -it <c> sh

# 2. 网络调试
# nslookup, curl, telnet, tcpdump

# 3. 性能
# top, htop, iostat, vmstat

# 4. 容器无这些工具时
# 用 ephemeral container(K8s 1.25+)
kubectl debug -it <pod> --image=nicolaka/netshoot --target=<container>
```

### Q48: 怎么实现零停机部署?

**关键点**(参考 [16 章](./16-CI-CD与Docker.md)):
1. maxUnavailable=0
2. readinessProbe
3. preStop hook
4. 应用优雅退出
5. terminationGracePeriodSeconds 足够

### Q49: 滚动更新卡住怎么处理?

```bash
# 1. 查看 rollout 状态
kubectl rollout status deployment/<name>

# 2. 查看事件
kubectl get events --sort-by='.lastTimestamp'

# 常见原因:
# - 新 Pod readiness 检查失败
# - 资源不足
# - 镜像拉取失败

# 3. 回滚
kubectl rollout undo deployment/<name>

# 4. 暂停/恢复
kubectl rollout pause deployment/<name>
kubectl rollout resume deployment/<name>
```

### Q50: 怎么备份 K8s 集群?

```bash
# 1. 资源备份(用 Velero)
velero install --provider aws --bucket k8s-backup
velero backup create full-cluster --include-cluster-resources=true

# 2. etcd 备份(控制面)
ETCDCTL_API=3 etcdctl snapshot save etcd.db \
  --endpoints=https://127.0.0.1:2379 \
  --cacert=/etc/kubernetes/pki/etcd/ca.crt \
  --cert=/etc/kubernetes/pki/etcd/server.crt \
  --key=/etc/kubernetes/pki/etcd/server.key

# 3. 数据备份(数据库)
# mysqldump / pg_dump / redis RDB
```

### Q51: 怎么实现多集群管理?

**方案**:
1. **Cluster API**: 创建/管理多集群
2. **ArgoCD 联邦**: 多集群部署
3. **Karmada**: 多集群调度
4. **Submariner**: 跨集群网络
5. **服务网格**: Istio multi-cluster

### Q52: 容器化后成本反而升高怎么办?

**原因**:
- 资源 request 过大(浪费)
- 镜像过大(存储/带宽)
- 没有弹性伸缩
- 单节点容器密度低

**优化**(参考 [17 章](./17-生产实践.md)):
1. 基于 metric 调 request
2. 镜像瘦身
3. HPA + Cluster Autoscaler
4. 在线/离线混部
5. Spot/Reserved 实例

---

## 20.13 开发环境 FAQ

### Q53: Mac/Windows Docker Desktop 占用资源大?

**优化**:
- 限制 CPU/内存: Settings → Resources
- 用 WSL2 backend(Windows)
- 关闭不用的功能(Kubernetes)
- 定期清理: `docker system prune -a`

### Q54: WSL2 内怎么访问 Docker Desktop?

```bash
# WSL2 内
# Docker Desktop 启用 WSL Integration
# 默认 docker 命令可用

# 验证
docker version
docker info
```

### Q55: 怎么在容器内调试 VSCode?

```json
// .devcontainer/devcontainer.json
{
  "image": "mcr.microsoft.com/devcontainers/python:3.12",
  "forwardPorts": [8000],
  "extensions": ["ms-python.python"],
  "postCreateCommand": "pip install -r requirements.txt"
}
```

```bash
# VSCode: Reopen in Container
```

### Q56: 本地怎么起 K8s 测试?

```bash
# 方案 1: kind(K8s in Docker)
kind create cluster --name test

# 方案 2: k3d(K3s in Docker)
k3d cluster create test

# 方案 3: minikube
minikube start

# 方案 4: Docker Desktop 内置 K8s
# Settings → Kubernetes → Enable
```

---

## 20.14 故障排查 FAQ

### Q57: docker 命令卡住不响应?

```bash
# 1. 检查 daemon
sudo systemctl status docker

# 2. 重启 daemon(谨慎,影响所有容器)
sudo systemctl restart docker

# 3. 查看日志
journalctl -u docker -n 100

# 常见原因:
# - 磁盘满
# - 网络问题(registry 不通)
# - daemon 死锁
# - 容器数量过多
```

### Q58: 容器日志看不到?

```bash
# 1. 检查 log driver
docker inspect <c> | jq '.[0].HostConfig.LogConfig'

# 2. json-file 驱动: 查看日志文件
ls /var/lib/docker/containers/<id>/

# 3. fluentd/gelf 驱动: docker logs 不可用
# 需要到日志系统查询

# 4. 应用未输出到 stdout
# 检查应用日志配置
```

### Q59: 镜像构建失败,网络拉依赖失败?

```dockerfile
# 1. 用国内镜像源
RUN pip config set global.index-url https://mirrors.aliyun.com/pypi/simple/ \
    && pip install -r requirements.txt

# 2. 离线安装(预先下载)
COPY packages/ /packages/
RUN pip install --no-index --find-links=/packages/ -r requirements.txt

# 3. BuildKit secret(私有仓库)
RUN --mount=type=secret,id=npmrc,target=/root/.npmrc \
    npm install
```

### Q60: 容器启动报 `no space left on device`?

```bash
# 1. 查看磁盘
df -h

# 2. 清理 docker
docker system prune -a --volumes

# 3. 清理日志
find /var/lib/docker/containers -name "*.log" -size +1G -exec truncate -s 0 {} \;

# 4. 配置日志轮转
# daemon.json: max-size + max-file
```

### Q61: K8s Pod `ImagePullBackOff`?

```bash
# 1. 查看错误
kubectl describe pod <pod>

# 常见原因:
# - 镜像不存在/拼写错误
# - 镜像私有未配 secret
# - registry 不可达
# - 拉取速率限制(Docker Hub)

# 2. 配置 imagePullSecrets
kubectl create secret docker-registry regcred \
  --docker-server=registry.com \
  --docker-username=user \
  --docker-password=pwd \
  --docker-email=x@x.com

# Pod 配置
spec:
  imagePullSecrets:
  - name: regcred
```

---

## 20.15 概念辨析 FAQ

### Q62: Image 和 Container 区别?

- **Image**: 静态模板(类)
- **Container**: 运行实例(对象)

类比:
```
Image : Container = Class : Object
```

### Q63: Docker 和 containerd 区别?

- **Docker**: 完整平台(CLI + daemon + containerd + runc + buildkit + compose)
- **containerd**: 仅容器运行时(Docker 的子集)

### Q64: docker build 和 buildah 区别?

- **docker build**: 需 daemon,经典构建
- **buildah**: 无 daemon,脚本化

### Q65: Pod 和 Container 区别?

- **Container**: 单个容器
- **Pod**: K8s 概念,1+ 容器共享网络/存储

### Q66: K8s Service 和 Ingress 区别?

- **Service**: L4 负载均衡(TCP/UDP)
- **Ingress**: L7 路由(HTTP/HTTPS)

### Q67: StatefulSet 和 Deployment 区别?

- **Deployment**: 无状态,Pod 可互换
- **StatefulSet**: 有状态,Pod 有持久身份(名称/存储)

### Q68: ConfigMap 和 Secret 区别?

- **ConfigMap**: 非敏感配置(明文)
- **Secret**: 敏感配置(base64 编码,可加密)

---

## 20.16 命令速查 FAQ

### Q69: 最常用的 docker 命令?

```bash
# 镜像
docker images
docker pull nginx
docker build -t myapp .
docker push myapp
docker rmi nginx

# 容器
docker ps
docker ps -a
docker run -d --name web nginx
docker stop web
docker start web
docker restart web
docker rm web
docker logs web
docker exec -it web sh
docker stats

# 系统
docker system df
docker system prune -a
docker info
docker version
```

### Q70: 最常用的 docker compose 命令?

```bash
docker compose up -d
docker compose down
docker compose ps
docker compose logs -f
docker compose exec web sh
docker compose build
docker compose pull
docker compose restart web
docker compose stop
docker compose config  # 验证配置
```

### Q71: 最常用的 kubectl 命令?

```bash
kubectl get pods -A
kubectl get svc -n prod
kubectl describe pod <pod>
kubectl logs -f <pod> -c <container>
kubectl exec -it <pod> -- sh
kubectl apply -f manifest.yaml
kubectl delete -f manifest.yaml
kubectl rollout status deployment/<name>
kubectl rollout undo deployment/<name>
kubectl scale deployment <name> --replicas=5
kubectl top pods
kubectl top nodes
kubectl get events --sort-by='.lastTimestamp'
```

### Q72: 怎么查看容器内进程?

```bash
# 1. docker top
docker top <c>

# 2. 进入容器
docker exec -it <c> ps aux

# 3. 宿主机视角
ps -ef | grep <c>
```

### Q73: 怎么查看容器资源占用?

```bash
# 单容器
docker stats <c>

# 所有容器
docker stats

# K8s
kubectl top pod <pod>
kubectl top pod <pod> --containers

# 容器内
docker exec -it <c> top
```

---

## 20.17 高级技巧 FAQ

### Q74: 怎么调试 Dockerfile?

```bash
# 1. 逐步构建
docker build --target builder -t myapp:debug .
docker run -it myapp:debug sh

# 2. 查看中间层
docker history myapp

# 3. dive 工具
dive myapp
```

### Q75: 怎么实现镜像多平台?

```bash
# 1. 创建 builder
docker buildx create --name multi --use

# 2. 启用 QEMU
docker run --privileged --rm tonistiigi/binfmt --install all

# 3. 构建
docker buildx build --platform linux/amd64,linux/arm64 -t myapp:v1 --push .
```

### Q76: 怎么减小镜像传输大小?

```bash
# 1. 镜像压缩(docker save + gzip)
docker save myapp | gzip > myapp.tar.gz

# 2. 用 crane(更高效)
crane export myapp:v1 myapp.tar

# 3. 增量传输(只传差异层)
# registry 自动处理

# 4. 用 Stargz/Nydus(延迟加载)
```

### Q77: 怎么监控容器内部?

```bash
# 1. cAdvisor(容器指标)
docker run -p 8080:8080 -v /:/rootfs:ro cadvisor/cadvisor

# 2. node-exporter(主机指标)
docker run -p 9100:9100 -v /:/host:ro prom/node-exporter

# 3. Prometheus + Grafana
# 参考第 14 章
```

### Q78: 怎么用 docker 实现开发环境?

```yaml
# docker-compose.dev.yml
version: "3.9"
services:
  dev:
    image: mcr.microsoft.com/devcontainers/python:3.12
    volumes:
      - ..:/workspace
      - ~/.ssh:/root/.ssh:ro
    command: sleep infinity
    network_mode: host
```

```bash
# VSCode: Dev Containers 扩展
# Reopen in Container
```

---

## 20.18 面试高频问题

### Q79: Docker 和虚拟机区别?

| 维度 | Docker | VM |
|------|--------|-----|
| 隔离 | 进程级(namespace) | 硬件级(虚拟化) |
| 启动 | 秒级 | 分钟级 |
| 体积 | MB | GB |
| 性能 | 接近原生 | 5-15% 损耗 |
| 资源 | 低 | 高 |
| 隔离强度 | 弱(共享内核) | 强(独立内核) |

### Q80: Docker 5 大核心组件?

1. **镜像**(Image): 只读模板
2. **容器**(Container): 运行实例
3. **仓库**(Registry): 镜像存储
4. **网络**(Network): 通信
5. **卷**(Volume): 持久化

### Q81: namespace 和 cgroup 区别?

- **namespace**: 隔离(让容器看不到主机)
- **cgroup**: 限制(让容器用有限资源)

### Q82: UnionFS 是什么?

联合文件系统,将多个层叠在一起,呈现为单一文件系统。Docker 用 overlay2 实现。

### Q83: 容器为什么轻量?

- 共享主机内核(无 guest OS)
- 进程级隔离(无虚拟化开销)
- UnionFS 层复用(镜像小)

### Q84: 怎么保证镜像可重现?

1. 锁 base image digest
2. 锁依赖版本(package-lock.json / Cargo.lock)
3. 消除时间/网络依赖
4. 用 BuildKit reproducible

### Q85: GitOps 是什么?

Git 作为唯一真相源,部署工具(ArgoCD/Flux)自动同步 Git 到 K8s。

### Q86: 微服务为什么用容器?

1. 环境一致(开发=生产)
2. 资源隔离(避免互相影响)
3. 弹性伸缩(快速扩缩)
4. 标准化交付(镜像即制品)
5. 易于 CI/CD

---

## 20.19 总结

### 20.19.1 FAQ 高频问题分布

```
安装配置: 10%
镜像: 15%
容器运行: 20%
网络: 15%
存储: 10%
Compose: 5%
性能: 10%
安全: 5%
K8s 集成: 5%
故障排查: 5%
```

### 20.19.2 排查思路通用框架

```
1. 现象收集
   ├─ 错误信息
   ├─ 时间点
   └─ 影响范围

2. 信息获取
   ├─ docker logs
   ├─ docker inspect
   ├─ docker events
   ├─ kubectl describe
   └─ 监控指标

3. 根因分析
   ├─ 最近变更
   ├─ 资源是否够
   ├─ 配置是否对
   └─ 网络/存储/应用

4. 解决方案
   ├─ 临时: 重启/扩容
   ├─ 彻底: 修配置/代码
   └─ 预防: 监控/告警/规范

5. 复盘总结
   ├─ 文档化
   ├─ 加 FAQ
   └─ 改流程
```

### 20.19.3 学习路径建议

```
入门(1 周):
├─ 安装 Docker
├─ docker run / build
├─ docker compose
└─ 理解镜像/容器概念

进阶(1 月):
├─ Dockerfile 优化
├─ 网络与存储
├─ 多阶段构建
└─ BuildKit

生产(3 月):
├─ K8s 基础
├─ 监控告警
├─ CI/CD
└─ 安全加固

高级(6 月+):
├─ K8s operator
├─ 性能调优
├─ 多集群管理
└─ 混沌工程
```

### 20.19.4 与其他章节联系

- **[01-基础与核心概念](./01-基础与核心概念.md)**: 基础概念
- **[04-容器运行与生命周期](./04-容器运行与生命周期.md)**: 容器生命周期
- **[12-安全与隔离](./12-安全与隔离.md)**: 安全相关 FAQ
- **[14-监控与日志](./14-监控与日志.md)**: 监控排查
- **[17-生产实践](./17-生产实践.md)**: 生产实践 FAQ

---

## 20.20 参考资料

### 官方文档
- [Docker FAQ](https://docs.docker.com/faq/)
- [Docker Troubleshooting](https://docs.docker.com/config/troubleshooting/)
- [K8s Troubleshooting](https://kubernetes.io/docs/tasks/debug/)

### 社区资源
- [Stack Overflow: Docker](https://stackoverflow.com/questions/tagged/docker)
- [Docker Forums](https://forums.docker.com/)
- [K8s Slack](https://kubernetes.slack.com/)

### 工具
- [dive](https://github.com/wagoodman/dive) - 镜像分层分析
- [crane](https://github.com/google/go-containerregistry) - registry 工具
- [skopeo](https://github.com/containers/skopeo) - 镜像同步
- [lazydocker](https://github.com/jesseduffield/lazydocker) - TUI
- [ctop](https://github.com/bcicen/ctop) - 容器 top

### 故障排查
- [Docker troubleshooting guide](https://docs.docker.com/config/troubleshooting/)
- [K8s debugging guide](https://kubernetes.io/docs/tasks/debug-application-cluster/)
- [Common Docker issues](https://github.com/wsargent/docker-cheat-sheet)

### 学习资源
- [Play with Docker](https://training.play-with-docker.com/)
- [KillerCoda](https://killercoda.com/)
- [K8s Examples](https://github.com/kubernetes/examples)

---

> 下一章: [21-大厂流水线](./21-大厂流水线.md) - 大厂容器化流水线深度剖析

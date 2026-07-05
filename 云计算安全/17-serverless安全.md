# 第 17 章 Serverless 安全

> 章节定位：Serverless（FaaS、BaaS）架构下的安全模型、事件注入、函数 IAM 与可观测性。
> 前置章节：[§2 IAM](./02-身份与访问管理.md)、[§5 工作负载安全](./05-工作负载安全.md)、[§10 DevSecOps](./10-DevSecOps.md)、[§16 供应链安全](./16-供应链安全.md)。
> 后置章节：[§18 AI 云安全](./18-ai云安全.md)、[§19 灾难恢复](./19-灾难恢复与业务连续性.md)。

---

## 〇 思维导图

```
Serverless 安全
│
├── 1. 服务模型
│   ├── FaaS：Lambda、Functions、Cloud Functions
│   ├── BaaS：DynamoDB、Cosmos DB、Firestore
│   ├── CaaS：Fargate、Cloud Run、ACI
│   └── DaaS：Aurora Serverless、Snowflake
│
├── 2. 责任模型
│   ├── 厂商：物理、Hypervisor、Runtime、自动扩缩
│   ├── 客户：函数代码、IAM 配置、事件源、数据
│   └── 边界：Runtime 与代码之间
│
├── 3. 攻击面
│   ├── 事件注入（API Gateway、SQS、S3 trigger）
│   ├── 函数 IAM 越权
│   ├── 冷启动泄露（共享 Runtime 残留）
│   ├── 依赖漏洞（npm/PyPI 包）
│   ├── 配置缺陷（公开 URL、弱环境变量）
│   └── DDoS（费用耗尽攻击）
│
├── 4. IAM 模式
│   ├── 最小权限 Execution Role
│   ├── 资源级条件（限定源 ARN）
│   ├── 临时凭证（STS，无长期密钥）
│   └── 跨服务传递（PassRole）
│
├── 5. 可观测
│   ├── 分布式追踪（X-Ray、Cloud Trace）
│   ├── 结构化日志（JSON、关联 ID）
│   ├── 冷启动监控
│   └── 函数调用指标
│
└── 6. 工业案例
    ├── Capital One（2019）：SSRF + Lambda 凭证外泄
    ├── Tesla K8s（2018）：未授权 Kubernetes API
    ├── 多起 S3 触发器公开访问
    └── DDoS 费用耗尽案例
```

---

## 一 问题定义

### 1.1 业务痛点

1. **事件驱动复杂**：单个函数可能由 API Gateway、S3、SQS、EventBridge 等多个源触发，输入验证难。
2. **权限边界模糊**：函数 Execution Role 权限过宽（如 `s3:*`），代码漏洞即权限滥用。
3. **冷启动不可控**：函数实例可能复用，全局变量残留敏感数据。
4. **可观测碎片化**：函数调用日志散落 CloudWatch Logs，链路追踪需手动埋点。
5. **DDoS 费用耗尽**：攻击者高频触发函数，账单爆炸（Lambda 按调用计费）。

### 1.2 Serverless 服务模型

| 模型 | AWS | Azure | GCP | 阿里云 | 华为云 |
|------|-----|-------|-----|--------|--------|
| FaaS | Lambda | Functions | Cloud Functions | 函数计算 | FunctionGraph |
| CaaS | Fargate | ACI | Cloud Run | ECI | CCI |
| BaaS（DB） | DynamoDB | Cosmos DB | Firestore | Tablestore | GaussDB NoSQL |
| BaaS（API） | API Gateway | API Management | API Gateway | API 网关 | APIG |
| BaaS（事件） | EventBridge | Event Grid | Eventarc | EventBridge | EG |
| Serverless DB | Aurora Serverless | Azure SQL Serverless | Cloud SQL Serverless | PolarDB Serverless | TaurusDB Serverless |

### 1.3 反模式

- ❌ 函数 Execution Role 用 `*` 权限（`s3:*` / `dynamodb:*`）。
- ❌ 函数环境变量明文存密钥（应用层加密但不轮转）。
- ❌ API Gateway 不验证输入，函数直接处理 SQL。
- ❌ S3 触发器配置为 `*` 源，任何桶都可触发。
- ❌ 全局变量缓存敏感数据，跨调用复用。
- ❌ 函数返回详细错误信息（堆栈、SQL）。
- ❌ CloudWatch Logs 不脱敏，含 PII。

---

## 二 核心概念与术语

| 术语 | 英文 | 定义 |
|------|------|------|
| FaaS | Function as a Service | 函数即服务 |
| Cold Start | 冷启动 | 新函数实例首次加载延迟 |
| Warm Instance | 暖实例 | 已加载函数实例 |
| Execution Role | 执行角色 | 函数运行时使用的 IAM Role |
| Event Source Mapping | 事件源映射 | 事件源与函数的关联 |
| Trigger | 触发器 | 启动函数的事件配置 |
| Invocation Type | 调用类型 | Sync / Async / Stream |
| Timeout | 超时 | 函数最大执行时间（Lambda 15 分钟） |
| Memory | 内存 | 函数分配内存（CPU 按比例分配） |
| Concurrency | 并发 | 同时执行函数实例数 |
| Provisioned Concurrency | 预置并发 | 预先准备暖实例 |
| Reserved Concurrency | 保留并发 | 函数最大并发上限 |
| Layer | 层 | 共享代码 / 依赖 |
| Custom Runtime | 自定义运行时 | 自带语言运行时 |
| Dead Letter Queue | DLQ | 失败消息队列 |
| Destination | 目的地 | 异步调用成功/失败通知 |
| VPC Config | VPC 配置 | 函数接入 VPC 访问私有资源 |
| IAM PassRole | 传递角色 | 允许服务使用某 Role |
| SSRF | Server-Side Request Forgery | 服务端请求伪造 |
| Event Injection | 事件注入 | 攻击者操纵事件触发函数 |

---

## 三 原理与机制

### 3.1 Lambda 执行模型 🎓

**冷启动 vs 暖实例**：

```
Cold Start（首次或扩容）：
1. 下载函数代码（S3 → Worker）
2. 创建 Execution Context（容器 / microVM）
3. 加载 Runtime（Node.js / Python）
4. 加载函数代码
5. 执行 init 代码（全局变量）
6. 调用 handler
   总延迟：100-1000ms

Warm Instance（复用）：
1. 调用 handler（已有 Execution Context）
   延迟：1-50ms
```

**关键洞察**：
- 全局变量在多次调用间复用（同一 worker）。
- `/tmp` 目录内容在多次调用间复用。
- 网络连接（DB pool）可跨调用复用。

**安全风险**：
- 全局变量缓存 DB 密码 → 泄露风险。
- /tmp 残留前一次请求文件 → 数据泄露。
- DB 连接池未关闭 → 连接耗尽。

### 3.2 事件源映射 🎓

**事件源类型**：
1. **Push 模型**：事件源直接调用函数（API Gateway、S3、SNS）。
2. **Pull 模型**：Lambda 主动拉取（Kinesis、DynamoDB Streams、SQS）。
3. **Sync 调用**：调用方等待返回（API Gateway、ALB、SDK）。
4. **Async 调用**：事件入队后异步处理（SNS、EventBridge）。

**事件结构（S3 触发器）**：

```json
{
  "Records": [{
    "eventSource": "aws:s3",
    "eventName": "ObjectCreated:Put",
    "s3": {
      "bucket": {"name": "my-bucket"},
      "object": {"key": "path/file.txt", "size": 1024}
    }
  }]
}
```

**安全风险**：
- 攻击者上传恶意文件名 `../../../etc/passwd`，函数处理时路径穿越。
- 攻击者上传超大文件，函数 OOM。
- 攻击者构造特殊 S3 事件（伪触发）。

### 3.3 函数 IAM 模型 🎓

**Execution Role 最小权限**：

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": ["s3:GetObject"],
      "Resource": "arn:aws:s3:::my-specific-bucket/*",
      "Condition": {
        "StringEquals": {
          "s3:prefix": "input/*"
        }
      }
    }
  ]
}
```

**资源级条件**：
- `s3:prefix`：限定前缀
- `lambda:FunctionArn`：限定调用方 ARN
- `aws:SourceArn`：限定事件源 ARN
- `aws:SourceAccount`：限定事件源账号

**PassRole 风险**：
- Lambda 假设 Role 时需 `iam:PassRole` 权限。
- 攻击者拿到 `iam:PassRole` 可任意假设 Role，提权。

### 3.4 事件注入攻击 🎓

**Capital One 事件流程**：

```
1. 攻击者扫描 AWS WAF 配置
2. SSRF 漏洞：WAF 的 metadata endpoint 可访问
3. 通过 SSRF 访问 169.254.169.254（IMDSv1）
4. 获取 EC2 IAM Role 凭证
5. 凭证外泄到攻击者服务器（境外 IP）
6. 用该凭证 ListBuckets、GetObject
7. 下载 1 亿条信用卡申请数据
```

**Lambda 事件注入模式**：

| 攻击类型 | 描述 | 防御 |
|---------|------|------|
| 路径穿越 | 文件名 `../etc/passwd` | 输入验证 + 沙箱 |
| SQL 注入 | API Gateway 直接拼 SQL | 参数化查询 |
| 命令注入 | `os.system(input)` | 避免 shell 调用 |
| XXE | XML 解析器加载外部实体 | 禁用外部实体 |
| 反序列化 | pickle.loads(user_input) | 白名单类 |
| SSRF | 函数 fetch 用户输入 URL | 出站白名单 |
| 文件上传 | 上传 .php 伪装图片 | 类型校验 + 病毒扫描 |

### 3.5 冷启动安全 🎓

**Execution Context 复用风险**：

```python
# 反例：全局变量缓存敏感数据
import boto3
import os

DB_PASSWORD = None  # 全局变量

def handler(event, context):
    global DB_PASSWORD
    if DB_PASSWORD is None:
        DB_PASSWORD = get_secret("db/password")  # 首次加载
    # 后续调用复用 DB_PASSWORD
```

**风险**：
- DB_PASSWORD 在 Execution Context 内长期存在。
- Lambda worker 共享，攻击者可能拿到内存 dump。
- Secrets 轮转后 DB_PASSWORD 不更新。

**最佳实践**：
- Secrets 缓存 TTL < 轮转周期 / 2。
- 使用 AWS Parameters and Secrets Lambda Extension（自动缓存）。
- 不在全局变量放敏感数据。
- /tmp 文件使用后删除。

---

## 四 算法/流程

### 4.1 Serverless 输入验证

```
function VALIDATE_EVENT(event, expected_schema):
    # 1. Schema 验证
    try:
        validate(event, expected_schema)
    except ValidationError as e:
        return {"statusCode": 400, "error": "Invalid input"}
    
    # 2. 输入净化
    for field in event:
        if isinstance(event[field], str):
            event[field] = sanitize(event[field])  # 去除特殊字符
    
    # 3. 文件名验证
    if "filename" in event:
        if not re.match(r"^[a-zA-Z0-9_\-\.]+$", event["filename"]):
            return {"statusCode": 400, "error": "Invalid filename"}
        if ".." in event["filename"]:
            return {"statusCode": 400, "error": "Path traversal"}
    
    # 4. 大小限制
    if event.get("content_length", 0) > MAX_SIZE:
        return {"statusCode": 413, "error": "Too large"}
    
    return event
```

### 4.2 函数 IAM 最小权限分析

```
function LAMBDA_LEAST_PRIVILEGE(function_name, lookback_days=90):
    # 1. 获取函数当前权限
    role = lambda.get_function_configuration(function_name).Role
    policy_docs = iam.list_attached_role_policies(role)
    allowed_actions = parse_policies(policy_docs)
    
    # 2. 分析 CloudTrail 中实际调用
    used_actions = set()
    for event in cloudtrail.query(
        identity=role,
        lookback=lookback_days,
    ):
        used_actions.add(f"{event.eventSource}:{event.eventName}")
    
    # 3. 差异
    unused = allowed_actions - used_actions
    over_privilege = []
    for action in unused:
        if action.endswith("*"):
            over_privilege.append({"action": action, "severity": "HIGH"})
        else:
            over_privilege.append({"action": action, "severity": "MEDIUM"})
    
    return over_privilege
```

### 4.3 DDoS 费用防护

```
function LAMBDA_DDoS_PROTECTION():
    # 1. 限流（API Gateway Throttling）
    api_gateway.set_throttle(rate_limit=1000, burst_limit=2000)
    
    # 2. Reserved Concurrency（函数上限）
    lambda.put_function_concurrency(
        FunctionName="my-fn",
        ReservedConcurrentExecutions=100,  # 最大 100 并发
    )
    
    # 3. WAF 规则
    waf.add_rule(
        name="rate-limit",
        action="BLOCK",
        rate_limit=2000,  # 5 分钟 2000 次
    )
    
    # 4. CloudWatch Alarm（费用异常）
    cloudwatch.put_metric_alarm(
        AlarmName="Lambda-Cost-Anomaly",
        MetricName="EstimatedCharges",
        Threshold=100,  # $100
        Period=300,
        EvaluationPeriods=1,
        AlarmActions=[sns_topic],
    )
```

### 4.4 分布式追踪

```python
from aws_xray_sdk.core import xray_recorder
from aws_xray_sdk.core import patch_all

patch_all()  # 自动埋点 boto3、requests 等

@xray_recorder.capture('process_order')
def process_order(order_id):
    # 子段
    subsegment = xray_recorder.begin_subsegment('validate_order')
    validate(order_id)
    xray_recorder.end_subsegment()
    
    # 注解（可搜索）
    xray_recorder.put_annotation("order_id", order_id)
    
    # 元数据（不可搜索）
    xray_recorder.put_metadata("order_details", {...})
    
    return {"status": "ok"}
```

---

## 五 工业实现对照

### 5.1 FaaS 平台对照

| 维度 | AWS Lambda | Azure Functions | GCP Cloud Functions | 阿里云 函数计算 | 华为云 FunctionGraph |
|------|-----------|-----------------|---------------------|----------------|---------------------|
| 运行时 | Node/Python/Java/Go/Custom | 同左 + .NET | Node/Python/Java/Go/Python | Node/Python/Java/Go | Node/Python/Java/Go |
| 超时上限 | 15 分钟 | 10 分钟 | 60 分钟 | 10 分钟 | 15 分钟 |
| 内存 | 128MB-10GB | 128MB-1.5GB | 128MB-4GB | 128MB-3GB | 128MB-3GB |
| 冷启动 | 50-500ms | 100-500ms | 100-500ms | 50-300ms | 50-300ms |
| 并发 | 1000/账号/区域 | 200/实例 | 1000/函数 | 300/函数 | 1000/账号 |
| 价格（百万次） | $0.20 + GB-s | $0.20 + GB-s | $0.40 + GB-s | ¥1.43 | ¥1.40 |
| VPC 接入 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 私有镜像 | 支持（Container Image） | Docker | 支持 | 容器镜像 | 容器镜像 |
| Provisioned Concurrency | ✅ | Premium Plan | min-instances | 预留实例 | 预留实例 |
| Layers | ✅ | ❌ | ❌ | 层 | 层 |
| 自定义 Runtime | ✅ | ✅ | ✅ | ✅ | ✅ |

### 5.2 Serverless 安全平台

| 平台 | 类型 | 优势 | 劣势 |
|------|------|------|------|
| AWS Lambda Insights | 监控 | CloudWatch 集成 | 仅 AWS |
| Datadog Serverless | 监控 | 多云、链路 | 商业 |
| Thundra | 监控 + 安全 | FaaS 专用 | 较小 |
| Lumigo | 监控 | 可视化 | 商业 |
| PureSec (Now Palo Alto) | 安全 | IAM 分析 | 已被收购 |
| Protego (Now Nuweba) | 安全 | 运行时保护 | 商业 |
| Snyk Serverless | 安全 | 依赖扫描 + IAM | 商业 |
| Orca Serverless | 安全 | 旁路扫描 | 商业 |

---

## 六 代码/配置示例

### 6.1 教学示例：安全 Lambda 函数

```python
import json
import os
import re
import logging
import boto3
from botocore.exceptions import ClientError
from aws_xray_sdk.core import xray_recorder, patch_all

# 配置日志
logger = logging.getLogger()
logger.setLevel(logging.INFO)

# 自动埋点
patch_all()

# 全局变量（仅在 Execution Context 内复用）
_SSM_CLIENT = None
_DB_PASSWORD = None
_DB_PASSWORD_TIME = 0

def get_db_password():
    """带 TTL 的 secrets 缓存"""
    global _DB_PASSWORD, _DB_PASSWORD_TIME, _SSM_CLIENT
    import time
    now = time.time()
    if _DB_PASSWORD is None or (now - _DB_PASSWORD_TIME) > 300:  # 5 分钟 TTL
        if _SSM_CLIENT is None:
            _SSM_CLIENT = boto3.client("ssm")
        try:
            response = _SSM_CLIENT.get_parameter(
                Name=os.environ["DB_PASSWORD_PARAM"],
                WithDecryption=True,
            )
            _DB_PASSWORD = response["Parameter"]["Value"]
            _DB_PASSWORD_TIME = now
            logger.info("DB password refreshed", extra={"param": os.environ["DB_PASSWORD_PARAM"]})
        except ClientError as e:
            logger.error("Failed to get DB password", extra={"error": str(e)})
            raise
    return _DB_PASSWORD

def validate_input(event):
    """严格输入验证"""
    body = event.get("body", "{}")
    if isinstance(body, str):
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            raise ValueError("Invalid JSON")
    else:
        data = body
    
    # 必填字段
    required = ["order_id", "customer_id", "amount"]
    for field in required:
        if field not in data:
            raise ValueError(f"Missing field: {field}")
    
    # 类型验证
    if not isinstance(data["order_id"], str) or not re.match(r"^ord-[a-zA-Z0-9]{8,20}$", data["order_id"]):
        raise ValueError("Invalid order_id")
    
    if not isinstance(data["amount"], (int, float)) or data["amount"] <= 0:
        raise ValueError("Invalid amount")
    
    if data["amount"] > 100000:
        raise ValueError("Amount exceeds limit")
    
    return data

def handler(event, context):
    """Lambda 入口"""
    request_id = context.aws_request_id
    logger.info("Processing request", extra={"request_id": request_id})
    
    try:
        # 1. 输入验证
        data = validate_input(event)
        xray_recorder.put_annotation("order_id", data["order_id"])
        
        # 2. 业务逻辑
        password = get_db_password()
        result = process_order(data, password)
        
        # 3. 返回（不泄露内部信息）
        return {
            "statusCode": 200,
            "body": json.dumps({"status": "success", "order_id": data["order_id"]}),
            "headers": {
                "Content-Type": "application/json",
                "Strict-Transport-Security": "max-age=31536000",
                "X-Content-Type-Options": "nosniff",
            },
        }
    
    except ValueError as e:
        logger.warning("Input validation failed", extra={"error": str(e), "request_id": request_id})
        return {
            "statusCode": 400,
            "body": json.dumps({"error": "Bad request"}),
        }
    except Exception as e:
        # 不返回详细错误
        logger.error("Internal error", extra={"error": str(e), "request_id": request_id}, exc_info=True)
        return {
            "statusCode": 500,
            "body": json.dumps({"error": "Internal server error"}),
        }
```

### 6.2 生产级 Terraform：完整 Serverless 应用

```hcl
# variables.tf
variable "project" { type = string }
variable "env" { type = string }

# iam.tf - 最小权限 Execution Role
data "aws_iam_policy_document" "lambda_assume" {
  statement {
    actions = ["sts:AssumeRole"]
    principals {
      type        = "Service"
      identifiers = ["lambda.amazonaws.com"]
    }
  }
}

resource "aws_iam_role" "lambda" {
  name               = "${var.project}-${var.env}-lambda"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

# 仅允许写日志到自己的 log group
resource "aws_iam_role_policy" "lambda_logs" {
  name = "logs"
  role = aws_iam_role.lambda.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Action = [
        "logs:CreateLogGroup",
        "logs:CreateLogStream",
        "logs:PutLogEvents",
      ]
      Resource = "arn:aws:logs:${data.aws_region.current.name}:${data.aws_caller_identity.current.account_id}:log-group:/aws/lambda/${var.project}-${var.env}-*"
    }]
  })
}

# 仅允许读取特定桶特定前缀
resource "aws_iam_role_policy" "lambda_s3_read" {
  name = "s3-read"
  role = aws_iam_role.lambda.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Action = ["s3:GetObject"]
      Resource = "arn:aws:s3:::${var.project}-${var.env}-input/input/*"
    }]
  })
}

# Secrets Manager 读取
resource "aws_iam_role_policy" "lambda_secrets" {
  name = "secrets-read"
  role = aws_iam_role.lambda.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Action = ["secretsmanager:GetSecretValue"]
      Resource = "arn:aws:secretsmanager:${data.aws_region.current.name}:${data.aws_caller_identity.current.account_id}:secret:${var.project}/${var.env}/*"
    }]
  })
}

# lambda.tf
resource "aws_lambda_function" "api" {
  function_name = "${var.project}-${var.env}-api"
  role          = aws_iam_role.lambda.arn
  runtime       = "python3.11"
  handler       = "app.handler"
  filename      = data.archive_file.app.output_path
  source_code_hash = data.archive_file.app.output_base64sha256
  
  memory_size = 512
  timeout     = 30
  
  reserved_concurrent_executions = 100  # DDoS 防护
  
  environment {
    variables = {
      DB_PASSWORD_PARAM = aws_ssm_parameter.db_password.name
      LOG_LEVEL = "INFO"
      ENVIRONMENT = var.env
    }
  }
  
  vpc_config {
    subnet_ids         = data.aws_subnets.private.ids
    security_group_ids = [aws_security_group.lambda.id]
  }
  
  tracing_config { mode = "Active" }  # X-Ray
  
  dead_letter_config { target_arn = aws_sqs_queue.dlq.arn }
  
  snap_start {
    apply_on = "PublishedVersions"
  }
  
  depends_on = [aws_iam_role_policy.lambda_logs]
}

# VPC SG
resource "aws_security_group" "lambda" {
  name        = "${var.project}-${var.env}-lambda"
  description = "Lambda function SG"
  vpc_id      = var.vpc_id
  
  egress {
    from_port   = 443
    to_port     = 443
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]  # 仅 HTTPS 出站
  }
  
  egress {
    from_port   = 5432
    to_port     = 5432
    protocol    = "tcp"
    security_groups = [aws_security_group.rds.id]  # 仅 DB
  }
}

# api_gateway.tf - 限流 + WAF
resource "aws_api_gateway_rest_api" "api" {
  name = "${var.project}-${var.env}-api"
  endpoint_configuration { types = ["REGIONAL"] }
}

resource "aws_api_gateway_method_settings" "api" {
  rest_api_id = aws_api_gateway_rest_api.api.id
  stage_name  = aws_api_gateway_stage.api.stage_name
  method_path = "*/*"
  
  settings {
    metrics_enabled = true
    logging_level   = "INFO"
    throttling_burst_limit = 200
    throttling_rate_limit  = 100  # 100 RPS
  }
}

# WAF
resource "aws_wafv2_web_acl" "api" {
  name  = "${var.project}-${var.env}-api"
  scope = "REGIONAL"
  
  default_action { allow {} }
  
  # 速率限制
  rule {
    name     = "rate-limit"
    priority = 1
    action { block {} }
    statement {
      rate_based_statement {
        limit              = 2000  # 5 分钟 2000 次
        aggregate_key_type = "IP"
      }
    }
    visibility_config {
      cloudwatch_metrics_enabled = true
      metric_name               = "rate-limit"
      sampled_requests_enabled   = true
    }
  }
  
  # SQLi
  rule {
    name     = "sql-injection"
    priority = 2
    action { block {} }
    statement {
      sqli_match_statement {
        text_transformation { type = "URL_DECODE" priority = 0 }
        field_to_match { all_query_arguments {} }
      }
    }
    visibility_config {
      cloudwatch_metrics_enabled = true
      metric_name               = "sqli"
      sampled_requests_enabled   = true
    }
  }
  
  visibility_config {
    cloudwatch_metrics_enabled = true
    metric_name               = "${var.project}-${var.env}-api"
    sampled_requests_enabled   = true
  }
}

resource "aws_wafv2_web_acl_association" "api" {
  resource_arn = aws_api_gateway_stage.api.arn
  web_acl_arn  = aws_wafv2_web_acl.api.arn
}

# DLQ
resource "aws_sqs_queue" "dlq" {
  name                       = "${var.project}-${var.env}-lambda-dlq"
  message_retention_seconds = 1209600  # 14 天
  kms_master_key_id         = "alias/aws/sqs"
}

# CloudWatch Alarm
resource "aws_cloudwatch_metric_alarm" "errors" {
  alarm_name          = "${var.project}-${var.env}-lambda-errors"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 2
  metric_name         = "Errors"
  namespace           = "AWS/Lambda"
  period              = 60
  statistic           = "Sum"
  threshold           = 5
  alarm_description   = "Lambda error rate high"
  alarm_actions       = [aws_sns_topic.alerts.arn]
  dimensions = {
    FunctionName = aws_lambda_function.api.function_name
  }
}
```

### 6.3 Lambda Container Image + Provisioned Concurrency

```dockerfile
# Dockerfile
FROM public.ecr.aws/lambda/python:3.11

# 安装依赖（layer 替代）
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 业务代码
COPY app.py ${LAMBDA_TASK_DIR}/

# 设置 handler
CMD ["app.handler"]
```

```hcl
resource "aws_lambda_function" "container" {
  function_name = "${var.project}-${var.env}-container"
  role          = aws_iam_role.lambda.arn
  package_type  = "Image"
  image_uri     = "${aws_ecr_repository.lambda.repository_url}:latest"
  
  memory_size = 1024
  timeout     = 60
}

# Provisioned Concurrency（消除冷启动）
resource "aws_lambda_alias" "live" {
  name             = "live"
  function_name    = aws_lambda_function.container.function_name
  function_version = aws_lambda_function.container.version
}

resource "aws_lambda_provisioned_concurrency" "live" {
  function_name                     = aws_lambda_function.container.function_name
  qualifier                         = aws_lambda_alias.live.name
  provisioned_concurrent_executions = 10
}
```

### 6.4 Lambda Layers 共享依赖

```hcl
resource "aws_lambda_layer_version" "common" {
  layer_name  = "${var.project}-common"
  description = "Common dependencies"
  
  filename         = data.archive_file.common.output_path
  source_code_hash = data.archive_file.common.output_base64sha256
  
  compatible_runtimes = ["python3.11"]
  compatible_architectures = ["x86_64"]
}

resource "aws_lambda_function" "api" {
  # ... 其他配置
  layers = [
    aws_lambda_layer_version.common.arn,
    "arn:aws:lambda:${data.aws_region.current.name}:017000801446:layer:AWSParametersAndSecretsLambdaExtension:11",  # Secrets 缓存
  ]
}
```

### 6.5 异步调用 + Destination

```hcl
resource "aws_lambda_function_event_invoke_config" "async" {
  function_name = aws_lambda_function.api.function_name
  
  maximum_retry_attempts = 2
  maximum_event_age_in_seconds = 3600  # 1 小时
  
  destination_config {
    on_failure {
      destination = aws_sqs_queue.dlq.arn
    }
    on_success {
      destination = aws_sns_topic.success.arn
    }
  }
}
```

### 6.6 CloudFormation SAM 模板

```yaml
# template.yaml
AWSTemplateFormatVersion: '2010-09-09'
Transform: AWS::Serverless-2016-10-31

Resources:
  ApiFunction:
    Type: AWS::Serverless::Function
    Properties:
      FunctionName: !Sub "${ProjectName}-${Env}-api"
      CodeUri: ./src
      Handler: app.handler
      Runtime: python3.11
      MemorySize: 512
      Timeout: 30
      ReservedConcurrentExecutions: 100
      Tracing: Active
      Environment:
        Variables:
          DB_PASSWORD_PARAM: !Ref DBPasswordParam
      Events:
        Api:
          Type: Api
          Properties:
            Path: /orders
            Method: post
            RestApiId: !Ref Api
      Policies:
        - Statement:
            - Effect: Allow
              Action: [secretsmanager:GetSecretValue]
              Resource: !Sub "arn:aws:secretsmanager:${AWS::Region}:${AWS::AccountId}:secret:${ProjectName}/${Env}/*"
        - Statement:
            - Effect: Allow
              Action: [s3:GetObject]
              Resource: !Sub "arn:aws:s3:::${ProjectName}-${Env}-input/input/*"
      VpcConfig:
        SubnetIds: !Ref PrivateSubnets
        SecurityGroupIds: [!Ref LambdaSG]
  
  Api:
    Type: AWS::Serverless::Api
    Properties:
      StageName: !Ref Env
      MethodSettings:
        - ResourcePath: "/*"
          HttpMethod: "*"
          ThrottlingBurstLimit: 200
          ThrottlingRateLimit: 100
```

---

## 七 常见陷阱与最佳实践

### 7.1 陷阱表 ⚠️

| 陷阱 | 描述 | 后果 | 规避 |
|------|------|------|------|
| Execution Role 用 `*` | 资源 `*` + Action `*` | 函数越权 | 资源级 ARN + 条件 |
| 环境变量明文密钥 | 应用层加密但不轮转 | 密钥泄露 | Secrets Manager + 参数引用 |
| 全局变量缓存密钥 | Execution Context 复用 | 密钥不轮转 | TTL 缓存 + Extension |
| /tmp 残留文件 | 跨调用复用 | 数据泄露 | 使用后删除 + 加密 |
| API Gateway 不验证 | 函数直接处理输入 | 事件注入 | 输入验证 + WAF |
| 错误返回堆栈 | stack trace 给客户端 | 信息泄露 | 通用错误 + 日志详细 |
| 不限 Reserved Concurrency | 无上限 | DDoS 费用耗尽 | 100-1000 上限 |
| CloudWatch Logs 不脱敏 | 含 PII | 合规违规 | RedactingFormatter |
| 不开 X-Ray | 链路不可见 | 难调试 | X-Ray + 结构化日志 |
| S3 触发器无 SourceArn | 任何桶可触发 | 越权触发 | 限定 SourceArn |
| PassRole 过宽 | iam:PassRole:* | 提权风险 | 限定 Role ARN |
| 冷启动慢 | 每次新实例加载 | 性能差 | Provisioned Concurrency |

### 7.2 最佳实践

1. **最小权限 Execution Role**：资源级 ARN + 条件键（SourceArn / SourceAccount）。
2. **Secrets 托管**：Secrets Manager + Lambda Extension 自动缓存。
3. **输入验证**：API Gateway 模型验证 + 函数内严格验证（schema + 类型 + 净化）。
4. **Reserved Concurrency**：每个函数设上限，防 DDoS 费用耗尽。
5. **WAF + 限流**：API Gateway Throttling + WAF rate limit + SQLi/XSS 规则。
6. **结构化日志**：JSON 格式 + 关联 ID + 脱敏 PII。
7. **X-Ray 追踪**：所有函数开 Active tracing，跨服务链路。
8. **DLQ + Destination**：异步调用失败入 DLQ，成功/失败通知 Destination。
9. **Provisioned Concurrency**：低延迟场景预热实例。
10. **SnapStart**：Java 函数启用 SnapStart，冷启动从秒级到毫秒级。

---

## 八 与其他章节关系

| 关联章节 | 关系 | 说明 |
|----------|------|------|
| [§2 IAM](./02-身份与访问管理.md) | 应用 | Execution Role 最小权限 |
| [§3 网络](./03-网络安全.md) | 应用 | VPC Config + SG |
| [§4 数据安全](./04-数据安全.md) | 应用 | Secrets Manager |
| [§5 工作负载安全](./05-工作负载安全.md) | 应用 | 容器镜像函数 |
| [§7 CSPM](./07-云安全态势管理.md) | 工具 | Lambda 配置扫描 |
| [§8 CWPP](./08-工作负载保护平台.md) | 工具 | Inspector ECR 扫描 |
| [§10 DevSecOps](./10-DevSecOps.md) | 流程 | Lambda IaC 扫描 |
| [§11 KMS](./11-密钥与机密管理.md) | 应用 | 环境变量加密 |
| [§12 审计](./12-审计与可观测.md) | 应用 | CloudTrail + CloudWatch |
| [§13 威胁检测](./13-威胁检测与响应.md) | 检测 | GuardDuty Lambda findings |
| [§16 供应链](./16-供应链安全.md) | 应用 | 函数依赖治理 |

---

## 九 面试速答 ⭐

**Q1：Lambda 冷启动怎么优化？**
1) Provisioned Concurrency（预热实例）；2) SnapStart（Java，启动快照）；3) 减小部署包；4) Layer 共享依赖；5) 避免重 init；6) /tmp 缓存。

**Q2：Lambda Execution Role 最小权限？**
限定 Action（如 s3:GetObject 而非 s3:*）+ 限定 Resource（具体 ARN 而非 *）+ 条件键（SourceArn / SourceAccount）。CloudTrail 分析实际使用，去除未用权限。

**Q3：Capital One 事件中 Lambda 的角色？**
其实是 EC2 上的 SSRF，不是 Lambda。但 Lambda 同样面临 SSRF 风险：函数 fetch 用户输入 URL → IMDS 凭证泄露。防御：禁用 IMDSv1，强制 IMDSv2 + 出站白名单。

**Q4：Lambda DDoS 防护？**
1) Reserved Concurrency 限制函数最大并发；2) API Gateway Throttling（rate + burst）；3) WAF rate limit；4) CloudWatch 费用告警；5) Lambda Destinations 处理失败。

**Q5：Lambda 全局变量安全吗？**
Execution Context 复用，全局变量跨调用持久。可缓存非敏感数据（DB 连接、配置），但不应缓存密钥（用 Secrets Manager Extension 带 TTL 缓存）。

**Q6：异步调用失败怎么处理？**
配置 Destination on_failure → SQS DLQ；maximum_retry_attempts 2；maximum_event_age 1 小时。DLQ 14 天保留供分析。

**Q7：Lambda 容器镜像 vs ZIP？**
ZIP：小、快、限制 250MB 解压后；容器镜像：大（10GB）、灵活（自定义 base）、依赖 ECR 扫描。生产用容器镜像 + ECR scan-on-push + Cosign 签名。

**Q8：Lambda VPC 配置注意？**
1) 私有子网（非公有）；2) SG 仅允许必要出站；3) NAT Gateway 访问公网（如需）；4) VPC Endpoint 访问 AWS 服务（避免 NAT 费用）；5) Hyperplane Lit™ 减冷启动延迟。

**Q9：Lambda Layer 怎么用？**
共享代码 / 依赖，多函数复用。最多 5 层 + 函数代码 ≤ 250MB。例：common logging、AWS Parameters and Secrets Extension。层版本不可变，更新需新版本。

**Q10：Serverless 可观测怎么做？**
1) X-Ray 分布式追踪；2) CloudWatch Logs JSON 结构化；3) Lambda Insights（性能指标）；4) 自定义指标（putMetricData）；5) Datadog / Lumigo 商业平台；6) 关联 ID 贯穿调用链。

---

## 十 综合面试题

1. **设计题**：电商订单 API 用 Lambda + API Gateway + DynamoDB，如何设计安全？
   要点：1) API Gateway 模型验证 + WAF；2) Lambda 输入严格验证（schema + 类型）；3) Execution Role 仅 dynamodb:PutItem 限定表 ARN；4) DynamoDB 加密 + 备份；5) Reserved Concurrency 100；6) X-Ray 追踪；7) CloudTrail 全审计；8) DLQ + Destinations。

2. **场景题**：Lambda 函数返回详细错误给客户端，被攻击者利用，怎么改？
   要点：1) 不返回堆栈/SQL/内部错误；2) 通用错误（"Internal server error"）+ 详细日志；3) 错误码 + 关联 ID 让客户端报修；4) API Gateway 响应模板覆盖；5) CloudWatch Logs 脱敏；6) 结构化日志便于查询。

3. **原理题**：Lambda Execution Context 复用怎么影响安全？
   要点：1) 全局变量跨调用持久，缓存密钥不安全（不轮转）；2) /tmp 文件残留，需删除；3) DB 连接池可复用但需检查存活；4) 单 worker 串行处理，多 worker 并行；5) 攻击者可能拿到内存 dump（侧面泄露）。

4. **场景题**：Lambda 函数被 DDoS，账单 $10,000，怎么应急？
   要点：1) 立即设 Reserved Concurrency=0（停止新调用）；2) CloudTrail 查调用源；3) WAF block 异常 IP；4) AWS Support 申请费用豁免（DDoS）；5) 长期：API Gateway Throttling + WAF rate limit + Reserved Concurrency。

5. **设计题**：Lambda + SQS 异步处理订单，如何保证 at-least-once + 幂等？
   要点：1) SQS 可见性超时 > Lambda 超时；2) Lambda 部分批处理失败返回 batchItemFailure（避免重投成功消息）；3) 业务幂等（order_id 去重）；4) DLQ 收集失败消息；5) Destinations 通知成功/失败；6) 监控 ApproximateAgeOfOldestMessage。

6. **对比题**：Lambda 和 Fargate 怎么选？
   要点：1) Lambda：事件驱动、短任务（< 15 分钟）、自动扩缩、按调用计费；2) Fargate：长任务、有状态、固定端口、按运行时间计费；3) 低延迟 + 突发 → Lambda；4) 长连接 + 复杂应用 → Fargate；5) 安全：Lambda 无需管 OS，Fargate 需管容器。

7. **原理题**：Lambda IAM PassRole 是什么风险？
   要点：1) PassRole 允许服务使用某 Role；2) 攻击者拿到 PassRole: * 可任意假设 Role，包括 admin；3) 防御：限定 Role ARN + 条件 iam:PassedToService=lambda.amazonaws.com；4) 最小权限：每个服务用独立 Role，不共享。

8. **场景题**：Lambda 函数依赖 log4j，发现 Log4Shell，怎么应急？
   要点：1) SBOM 查询使用 log4j 的函数；2) 升级到 2.17.1+；3) 无法升级：设环境变量 `LOG4J_FORMAT_MSG_NO_LOOKUPS=true`；4) WAF 规则拦截 `${jndi:ldap}` payload；5) CloudTrail 查异常出站（攻击者可能已触发 RCE）；6) rotate 函数凭证。

9. **设计题**：跨服务 Lambda 调用链路怎么追踪？
   要点：1) X-Ray Active tracing 全开；2) AWS SDK 自动传播 trace header；3) 自定义 subsegment 标注业务逻辑；4) annotation 可搜索（order_id）；5) metadata 不可搜索但详细；6) CloudWatch ServiceLens 可视化；7) Lumigo / Datadog 商用增强。

10. **原理题**：Lambda SnapStart 怎么工作？
    要点：1) 仅 Java 8+；2) Init 阶段后拍快照（内存 + 文件系统）；3) 后续冷启动从快照恢复（< 200ms）；4) 风险：快照含随机数、单例状态，跨实例复用 → 用 uniqueId 替代；5) SnapStart 不支持 ARM、EFS；6) 安全：快照加密存储。

---

## 十一 参考与延伸

### 标准与规范 📜
- **OWASP Serverless Top 10**: https://owasp.org/www-project-serverless-top-10/
- **NIST SP 800-218 SSDF**: Secure Software Development Framework
- **CSA Serverless Security**: https://cloudsecurityalliance.org/artifacts/serverless-security-usage-guidance/
- **OWASP API Security Top 10**
- **PCI-DSS v4.0 §6**: Secure Software Development
- **MITRE ATT&CK for Cloud**: https://attack.mitre.org/matrices/enterprise/cloud/

### 厂商文档
- AWS Lambda Security: https://docs.aws.amazon.com/lambda/latest/dg/lambda-security.html
- AWS Lambda Best Practices: https://docs.aws.amazon.com/lambda/latest/dg/best-practices.html
- AWS Serverless Application Model: https://aws.amazon.com/serverless/sam/
- Azure Functions Security: https://learn.microsoft.com/azure/azure-functions/security-concepts
- GCP Cloud Functions Security: https://cloud.google.com/functions/docs/concepts/security
- 阿里云 函数计算安全: https://help.aliyun.com/product/50980.html
- 华为云 FunctionGraph: https://support.huaweicloud.com/functiongraph/

### 工具
- **AWS X-Ray**: 分布式追踪
- **AWS Parameters and Secrets Lambda Extension**: Secrets 缓存
- **AWS Lambda Insights**: CloudWatch Lambda Insights
- **AWS Serverless Application Model (SAM)**: IaC 框架
- **Datadog Serverless**: 商业监控
- **Lumigo**: 商业可视化
- **Thundra**: 商业监控 + 安全
- **Snyk Serverless**: 依赖扫描 + IAM 分析
- **PureSec**: Serverless 安全平台（Palo Alto）
- **Serverless Framework**: https://www.serverless.com/（多 clouds）
- **AWS Power Tools for Lambda**: https://github.com/aws-powertools/powertools-lambda-python

### 事故案例 🏭
- **Capital One（2019）**: SSRF + IAM 凭证外泄（虽是 EC2，启示适用 Lambda）。
- **Tesla K8s（2018）**: 未授权 Kubernetes API 被挖矿，启示无服务器也要管 API。
- **DoorDash（2019）**: 第三方员工凭证泄露，启示 IAM 严格。
- **Imperva（2019）**: AWS 客户数据库泄露，启示 RDS 加密 + IAM。
- **Twilio（2022）**: phishing 攻击员工凭证，启示 Serverless 也要防社工。

### 学术论文 🎓
- **"Serverless Security: A Survey"** (IEEE Cloud 2021)
- **"Cold Start Vulnerabilities"** (USENIX Security 2021)
- **"Event Injection in Serverless"** (CCS 2020)
- **"Serverless Performance Analysis"** (SoCC 2020)
- **"FaaS Performance Characterization"** (EuroSys 2019)

### 交叉链接
- 上承：[§10 DevSecOps](./10-DevSecOps.md)、[§16 供应链安全](./16-供应链安全.md)
- 下接：[§18 AI 云安全](./18-ai云安全.md)（AI Serverless）、[§19 灾难恢复](./19-灾难恢复与业务连续性.md)（Serverless DR）
- 横向：[§2 IAM](./02-身份与访问管理.md)（Execution Role）
- 横向：[§12 审计](./12-审计与可观测.md)（X-Ray）
- 纵向：[§20 工业案例](./20-工业案例与事故库.md)（Capital One）

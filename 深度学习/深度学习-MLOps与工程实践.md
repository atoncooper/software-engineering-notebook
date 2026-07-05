# 深度学习 - MLOps 与工程实践

> 工业化实战系列 · 第三章
>
> "模型不是产品,系统才是产品。" —— 一个能上线、能监控、能回滚的 ML 系统,才算工程项目。
>
> MLOps = DevOps + DataOps + ModelOps,涵盖数据→模型→服务的全生命周期工程化。

---

## 0. 思维导图

```
MLOps 与工程实践
├── 全生命周期
│   ├── 数据: 采集/标注/版本/特征
│   ├── 实验: 跟踪/复现/搜索
│   ├── 模型: 训练/评估/注册/版本
│   ├── 部署: 灰度/蓝绿/金丝雀
│   └── 监控: 漂移/质量/延迟/成本
├── 实验管理
│   ├── MLflow / W&B / Neptune
│   ├── 元数据: 代码/参数/指标/产物
│   └── 复现性: 随机种子/环境/数据快照
├── 数据版本
│   ├── DVC / LakeFS / Delta Lake
│   ├── 数据血缘 (Lineage)
│   └── 特征存储 (Feature Store)
├── 流水线编排
│   ├── Airflow / Prefect / Dagster
│   ├── Kubeflow / Argo Workflows
│   └── Pipeline as Code
├── CI/CD
│   ├── 代码 CI: GitHub Actions / Jenkins
│   ├── 模型 CI: 单元测试/数据测试/模型测试
│   └── 部署 CD: 蓝绿/金丝雀/影子
├── 监控
│   ├── 数据漂移 (PSI / KS / KL)
│   ├── 模型衰减 (实时 vs 离线)
│   ├── 业务指标 (CTR / 转化)
│   └── 基础设施 (GPU / 延迟 / 错误率)
├── 治理
│   ├── 模型卡 (Model Card)
│   ├── 数据卡 (Data Card)
│   ├── 审计日志
│   └── 合规 (GDPR/等保)
└── 工业 MLOps 平台
    ├── 阿里 PAI / 字节 ByteML
    ├── AWS SageMaker / GCP Vertex / Azure ML
    └── 字节 / Meta 内部平台
```

---

## 1. 问题定义

### 1.1 为什么需要 MLOps?

**传统软件工程**: 代码 + 配置 → 部署。DevOps 解决代码变更的自动化。

**机器学习工程**: 代码 + 数据 + 模型权重 + 超参 + 环境 → 部署。**多维度变更源**,任一维度漂移都导致线上效果衰减。

Google 经典数据:
- 只有 ~5% 的 ML 项目真正上线
- 上线后 ~60% 在 6 个月内效果衰减
- 数据漂移是 #1 失败原因 (40%)

### 1.2 ML 系统的隐藏技术债

Google 论文 "Hidden Technical Debt in ML Systems" (2015) 经典图:

```
ML 代码 (实际机器学习逻辑): ███████ 5%
└─ 周边基础设施: ███████████████████████████████ 95%
   ├─ 数据收集 / 特征工程
   ├─ 配置 / 流水线编排
   ├─ 监控 / 告警
   ├─ 评估 / 验证
   ├─ 部署 / 服务化
   └─ 版本管理 / 复现
```

### 1.3 MLOps 成熟度模型

Google MLOps 成熟度 3 级模型:

| 级别 | 特征 | 自动化 |
|------|------|--------|
| **L0** (手动) | Notebook 实验,手动部署 | 0% - 全人工 |
| **L1** (流水线) | 训练流水线自动化,手动部署 | 50% - 训练自动 |
| **L2** (CI/CD) | 全自动训练+部署+监控 | 90% - 全流程自动 |

**大厂实际水平**:
- 头部互联网 (字节/阿里/Google): L2
- 中型公司: L1
- 传统企业: L0 → L1 过渡

### 1.4 MLOps vs DevOps 的关键差异

| 维度 | DevOps | MLOps |
|------|--------|-------|
| 变更源 | 代码 | 代码 + 数据 + 模型 |
| 测试 | 单元/集成/端到端 | + 数据测试 + 模型测试 |
| 部署 | 二进制 | 二进制 + 模型权重 |
| 监控 | 系统指标 | + 数据/模型漂移 |
| 回滚 | 上一版本 | 上一版本 + 模型快照 |
| 复现 | 代码 + 配置 | + 数据 + 随机种子 + 环境 |

---

## 2. 直觉解释

### 2.1 ML 系统的两条流水线

```
训练流水线 (Offline):
  数据采集 → 特征工程 → 训练 → 评估 → 注册 → 部署
  ────────────────────────────────────────────────
  周期: 天/周/月
  目标: 模型质量最优
  资源: 大量 GPU,长时运行

推理流水线 (Online):
  请求 → 特征提取 → 模型推理 → 后处理 → 响应
  ────────────────────────────────────────────────
  周期: 毫秒
  目标: 低延迟高吞吐
  资源: 优化 GPU/CPU,7×24 稳定
```

**关键**: 两条流水线共享**特征工程代码**,否则训练-服务偏差 (Training-Serving Skew)。

### 2.2 实验管理的核心痛点

**场景**: 你训练了 50 个模型,3 个月后老板问"最好的模型是哪个?用什么参数?"

无实验管理:
- Notebook 散落各处
- 参数靠记忆
- 指标没记录
- **复现不可能**

有实验管理 (MLflow):
- 每次实验自动记录: 代码 commit、参数、指标、模型权重、环境
- 一键查询: "找出 F1 > 0.9 且延迟 < 100ms 的所有实验"
- 一键复现: `mlflow run` 一行命令

### 2.3 数据版本管理的挑战

**Git 不适合大文件**:
- 10GB 数据集 clone 一份 → 仓库膨胀 100GB
- 每次更新全量存储 → 磁盘爆炸
- Git LFS 仍需全量上传

**DVC (Data Version Control) 思路**:
- Git 只存元数据 (.dvc 文件,记录 hash)
- 实际数据存对象存储 (S3/OSS)
- 类似 Git,但数据流不走 Git
- `dvc push/pull` 按需同步

### 2.4 模型衰减的本质

```
时间 0:  模型上线, 准确率 95%
时间 3月: 数据分布漂移, 准确率 90%
时间 6月: 业务变更, 准确率 85% (已不可用)

漂移类型:
1. 协变量漂移 (Covariate Shift): P(X) 变, P(Y|X) 不变
   - 例: 用户画像从城市扩散到农村
2. 标签漂移 (Label Shift): P(Y) 变, P(X|Y) 不变
   - 例: 类别不平衡比例改变
3. 概念漂移 (Concept Drift): P(Y|X) 变
   - 例: 推荐系统用户偏好改变
4. 数据质量漂移: 缺失值/异常值增加
   - 例: 上游采集系统异常
```

---

## 3. 形式化推导 🎓

### 3.1 数据漂移检测

#### 3.1.1 PSI (Population Stability Index)

对特征 X,按基线分布分桶,计算新旧分布在每桶的比例:

$$
\text{PSI} = \sum_i (p_i^{\text{new}} - p_i^{\text{base}}) \cdot \ln\frac{p_i^{\text{new}}}{p_i^{\text{base}}}
$$

判断标准:
- PSI < 0.1: 稳定
- 0.1 ≤ PSI < 0.25: 轻微漂移,关注
- PSI ≥ 0.25: 显著漂移,触发重训

#### 3.1.2 KS 检验 (Kolmogorov-Smirnov)

对连续特征:

$$
D = \sup_x |F_{\text{new}}(x) - F_{\text{base}}(x)|
$$

p-value < 0.05 → 拒绝"分布相同"假设,即漂移。

#### 3.1.3 KL 散度

$$
\text{KL}(P_{\text{new}} \| P_{\text{base}}) = \sum_x P_{\text{new}}(x) \ln\frac{P_{\text{new}}(x)}{P_{\text{base}}(x)}
$$

非对称,值越大漂移越严重。

### 3.2 训练-服务偏差

训练时:
$$
\hat\theta = \arg\min_\theta \frac{1}{N}\sum_{i=1}^N \ell(h_\theta(x_i^{\text{train}}), y_i)
$$

服务时:
$$
\hat y = h_{\hat\theta}(x^{\text{serve}})
$$

偏差来源:
- $x^{\text{train}}$ 与 $x^{\text{serve}}$ 不同分布 (特征处理不一致)
- 训练标签 $y$ 与线上真实 $y$ 不一致 (标签噪声)
- 训练 batch 与线上 single 不一致 (Batch Norm 问题)

**解决方案**: 共享特征提取代码 + 在线特征存储 (Feature Store)。

### 3.3 A/B 测试样本量

$$
n = \frac{(z_{\alpha/2} + z_\beta)^2 \cdot 2\sigma^2}{\delta^2}
$$

- $\delta$: 期望提升 (最小可检测效应 MDE)
- $\sigma$: 指标标准差
- $\alpha=0.05, \beta=0.2$ → $z$ 值 1.96 + 0.84

**例**: CTR 提升 1% (δ=0.01), σ=0.05
$$n = (1.96+0.84)^2 \cdot 2 \cdot 0.05^2 / 0.01^2 = 3920$$ 每组

### 3.4 模型评估的置信区间

n 折交叉验证均值 $\bar{x}$, 标准差 $s$:

$$
\text{CI}_{95\%} = \bar{x} \pm t_{0.025, n-1} \cdot \frac{s}{\sqrt{n}}
$$

模型 A vs B 显著性: 看置信区间是否重叠,或用配对 t 检验 / Wilcoxon。

---

## 4. 算法流程

### 4.1 MLflow 实验追踪

```python
import mlflow
import mlflow.pytorch

# 设置实验
mlflow.set_experiment("llama-finetune-v1")

# 自动日志 (推荐)
mlflow.pytorch.autolog(
    log_models=False,         # 模型单独 log
    log_datasets=False,
    log_model_signatures=True,
)

with mlflow.start_run(run_name="lora-r64-bs32-lr2e-4") as run:
    # 手动记录参数
    mlflow.log_params({
        "model": "Llama-2-7b",
        "lora_r": 64,
        "lora_alpha": 16,
        "batch_size": 32,
        "learning_rate": 2e-4,
        "epochs": 3,
        "warmup_ratio": 0.03,
    })

    # 训练循环
    for epoch in range(3):
        train_loss = train_epoch(model, ...)
        eval_loss, eval_acc = evaluate(model, ...)

        # 每轮记录指标
        mlflow.log_metrics({
            "train_loss": train_loss,
            "eval_loss": eval_loss,
            "eval_accuracy": eval_acc,
        }, step=epoch)

    # 注册模型
    mlflow.pytorch.log_model(
        model,
        artifact_path="model",
        registered_model_name="llama-7b-lora",
    )

# 查询实验
runs = mlflow.search_runs(
    experiment_ids=["1"],
    filter_string="metrics.eval_accuracy > 0.9",
    order_by=["metrics.eval_accuracy DESC"],
)
```

### 4.2 DVC 数据版本管理

```bash
# 初始化
git init
dvc init

# 添加数据
dvc add data/train.csv
# 生成 train.csv.dvc (元数据), data/train.csv 加入 .gitignore

# 提交
git add data/train.csv.dvc .gitignore
git commit -m "add train data v1"

# 更新数据
cp new_train.csv data/train.csv
dvc add data/train.csv
git commit -am "update train data v2"

# 推到远程 (S3/OSS)
dvc remote add -d storage s3://my-bucket/dvc
dvc push

# 回到 v1
git checkout HEAD~1 data/train.csv.dvc
dvc checkout
# data/train.csv 现在是 v1 内容
```

### 4.3 Kubeflow Pipeline 流水线

```python
from kfp import dsl, components
from kfp.dsl import Input, Output, Dataset, Model

@dsl.component(base_image="pytorch/pytorch:2.1-cuda12.1")
def preprocess(raw_data: str, output: Output[Dataset]):
    import pandas as pd
    df = pd.read_csv(raw_data)
    df = df.dropna().drop_duplicates()
    df.to_parquet(output.path)

@dsl.component
def train(data: Input[Dataset], model: Output[Model],
          epochs: int, lr: float):
    import torch
    import pytorch_lightning as pl
    # ... 训练代码
    torch.save(model.state_dict(), model.path)

@dsl.component
def evaluate(model: Input[Model], metrics: Output[Dataset]):
    # ... 评估代码
    pass

@dsl.component
def deploy(model: Input[Model]):
    # 调用部署 API
    pass

@dsl.pipeline(name="llm-training-pipeline")
def pipeline(raw_data: str, epochs: int = 3, lr: float = 2e-4):
    prep = preprocess(raw_data=raw_data)
    tr = train(data=prep.outputs["output"], epochs=epochs, lr=lr)
    ev = evaluate(model=tr.outputs["model"])
    # 仅当指标达标才部署
    with dsl.Condition(ev.outputs["accuracy"] > 0.9):
        deploy(model=tr.outputs["model"])

# 编译 + 提交
from kfp import Client
client = Client(host="https://kubeflow.example.com")
client.create_run_from_pipeline_func(pipeline, arguments={"raw_data": "s3://..."})
```

### 4.4 GitHub Actions CI/CD for ML

```yaml
# .github/workflows/model-ci.yml
name: Model CI/CD
on:
  push:
    branches: [main]
    paths: ['model/**', 'data/**']
  pull_request:
    branches: [main]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - run: pip install -r requirements.txt
      - name: Unit tests
        run: pytest tests/unit -v --cov
      - name: Data tests
        run: pytest tests/data -v
        # 数据完整性测试: schema/缺失值/分布
      - name: Model tests
        run: pytest tests/model -v
        # 模型测试: shape/数值稳定/推理延迟

  train:
    needs: test
    if: github.ref == 'refs/heads/main'
    runs-on: self-hosted-gpu
    steps:
      - uses: actions/checkout@v4
      - name: Train model
        run: |
          dvc pull data/
          python train.py --config configs/prod.yaml
      - name: Evaluate
        run: python evaluate.py --model models/latest.pt
      - name: Register model
        run: |
          python -c "
          import mlflow
          mlflow.register_model(
              'runs:/$GITHUB_RUN_ID/model',
              'production-candidate'
          )
          "

  deploy:
    needs: train
    runs-on: ubuntu-latest
    steps:
      - name: Canary deploy
        run: |
          # 5% 流量到新模型
          python deploy.py --strategy canary --percent 5
      - name: Monitor
        run: |
          sleep 3600  # 观察 1 小时
          python check_metrics.py --threshold 0.95
      - name: Promote to production
        if: success()
        run: python deploy.py --strategy canary --percent 100
```

### 4.5 监控告警系统

```python
"""
模型监控: 检测数据漂移 + 模型衰减
"""
import numpy as np
import pandas as pd
from scipy import stats
from prometheus_client import Counter, Gauge, Histogram

# 指标
DRIFT_SCORE = Gauge('model_drift_score', 'Drift score per feature', ['feature'])
PREDICTION_DISTRIBUTION = Histogram('model_prediction_distribution', 'Predictions')
ERROR_RATE = Counter('model_errors_total', 'Total errors', ['type'])

class ModelMonitor:
    def __init__(self, baseline_data: pd.DataFrame):
        self.baseline = baseline_data
        self.feature_stats = self._compute_stats(baseline_data)

    def _compute_stats(self, df):
        stats = {}
        for col in df.columns:
            if df[col].dtype in [np.float64, np.int64]:
                stats[col] = {
                    'mean': df[col].mean(),
                    'std': df[col].std(),
                    'hist': np.histogram(df[col], bins=50),
                }
        return stats

    def detect_drift(self, new_data: pd.DataFrame) -> dict:
        """检测漂移,返回 PSI 和 KS"""
        drift_report = {}
        for col in new_data.columns:
            if col not in self.feature_stats:
                continue

            # PSI
            base_hist, base_edges = self.feature_stats[col]['hist']
            new_hist, _ = np.histogram(new_data[col], bins=base_edges)
            base_p = base_hist / base_hist.sum() + 1e-10
            new_p = new_hist / new_hist.sum() + 1e-10
            psi = np.sum((new_p - base_p) * np.log(new_p / base_p))

            # KS 检验
            ks_stat, ks_p = stats.ks_2samp(
                self.baseline[col], new_data[col]
            )

            drift_report[col] = {
                'psi': psi,
                'ks_stat': ks_stat,
                'ks_p_value': ks_p,
                'drifted': psi > 0.25 or ks_p < 0.05,
            }
            DRIFT_SCORE.labels(feature=col).set(psi)

        return drift_report

    def check_prediction_distribution(self, preds: np.ndarray):
        """预测分布监控"""
        for p in preds:
            PREDICTION_DISTRIBUTION.observe(float(p))

    def check_latency(self, latencies_ms: list):
        """延迟监控"""
        p50 = np.percentile(latencies_ms, 50)
        p99 = np.percentile(latencies_ms, 99)
        if p99 > 1000:  # 超过 1s 告警
            ERROR_RATE.labels(type='latency_violation').inc()
        return p50, p99

# 周期任务 (Airflow / Cron)
def daily_monitor():
    monitor = ModelMonitor.load('baseline_v1.pkl')
    # 拉取昨日生产数据
    new_data = fetch_production_data(date='yesterday')
    report = monitor.detect_drift(new_data)
    drifted_features = [k for k, v in report.items() if v['drifted']]
    if len(drifted_features) > 0:
        # 触发重训
        trigger_retraining(features=drifted_features)
        send_alert(f"Drift detected: {drifted_features}")
```

### 4.6 特征存储 (Feature Store)

```python
"""
特征存储: 训练和推理共享特征定义,消除训练-服务偏差
"""
from feast import FeatureStore, Entity, FeatureView, FileSource
from feast.value_type import ValueType
from datetime import timedelta

# 1. 定义特征 (feature_repo/features.py)
user = Entity(name='user_id', value_type=ValueType.INT64)

user_features = FileSource(
    path="data/user_features.parquet",
    timestamp_field='event_timestamp',
)

user_fv = FeatureView(
    name="user_features",
    entities=[user],
    ttl=timedelta(days=30),
    schema=[
        Field(name='age', dtype=ValueType.INT64),
        Field(name='gender', dtype=ValueType.STRING),
        Field(name='click_count_7d', dtype=ValueType.INT64),
        Field(name='purchase_amount_30d', dtype=ValueType.FLOAT),
    ],
    online=True,   # 在线服务
    offline=True,  # 离线训练
    source=user_features,
)

# 2. 应用定义
# feast apply

# 3. 训练时: 离线获取历史特征 (Point-in-Time Join)
store = FeatureStore(repo_path=".")
training_data = store.get_historical_features(
    entity_df=training_labels,  # 含 user_id, event_timestamp, label
    features=[
        "user_features:age",
        "user_features:click_count_7d",
        "user_features:purchase_amount_30d",
    ],
).to_df()

# 4. 推理时: 在线获取实时特征 (Redis 后端)
features = store.get_online_features(
    features=[
        "user_features:age",
        "user_features:click_count_7d",
    ],
    entity_rows=[{"user_id": 12345}],
).to_dict()
```

---

## 5. PyTorch 实现

### 5.1 完整生产训练流水线

```python
"""
生产级训练流水线: 配置驱动 + 实验追踪 + 检查点 + 评估
"""
import os
import yaml
import torch
import pytorch_lightning as pl
import mlflow
from dataclasses import dataclass
from typing import Optional

@dataclass
class TrainConfig:
    """训练配置 (类型安全 + 可序列化)"""
    # 数据
    train_data: str
    eval_data: str
    test_data: str
    # 模型
    model_name: str
    pretrained_path: Optional[str] = None
    # 训练
    batch_size: int = 32
    learning_rate: float = 2e-4
    epochs: int = 3
    warmup_ratio: float = 0.03
    weight_decay: float = 0.01
    max_grad_norm: float = 1.0
    # 系统
    gpus: int = 4
    precision: str = "bf16"
    # 实验追踪
    experiment_name: str = "default"
    run_name: str = "run-1"
    # 检查点
    checkpoint_dir: str = "./checkpoints"
    save_top_k: int = 3

class LLMModule(pl.LightningModule):
    def __init__(self, config: TrainConfig):
        super().__init__()
        self.config = config
        self.model = self._build_model()
        self.criterion = torch.nn.CrossEntropyLoss()

    def _build_model(self):
        from transformers import AutoModelForCausalLM
        model = AutoModelForCausalLM.from_pretrained(
            self.config.pretrained_path,
            torch_dtype=torch.bfloat16,
        )
        # LoRA
        if self.config.use_lora:
            from peft import LoraConfig, get_peft_model
            lora_config = LoraConfig(r=64, lora_alpha=16, ...)
            model = get_peft_model(model, lora_config)
        return model

    def training_step(self, batch, batch_idx):
        outputs = self.model(input_ids=batch['input_ids'],
                            attention_mask=batch['attention_mask'],
                            labels=batch['labels'])
        loss = outputs.loss
        self.log('train_loss', loss, prog_bar=True, on_step=True, on_epoch=True)
        # 学习率
        self.log('lr', self.optimizers().param_groups[0]['lr'], on_step=True)
        return loss

    def validation_step(self, batch, batch_idx):
        outputs = self.model(**batch)
        loss = outputs.loss
        # 计算准确率
        preds = outputs.logits.argmax(-1)
        acc = (preds == batch['labels']).float().mean()
        self.log_dict({'val_loss': loss, 'val_acc': acc}, prog_bar=True)

    def configure_optimizers(self):
        from transformers import get_cosine_schedule_with_warmup
        optimizer = torch.optim.AdamW(
            self.parameters(),
            lr=self.config.learning_rate,
            weight_decay=self.config.weight_decay,
        )
        total_steps = len(self.train_dataloader()) * self.config.epochs
        scheduler = get_cosine_schedule_with_warmup(
            optimizer,
            num_warmup_steps=int(total_steps * self.config.warmup_ratio),
            num_training_steps=total_steps,
        )
        return {
            'optimizer': optimizer,
            'lr_scheduler': {'scheduler': scheduler, 'interval': 'step'},
        }

def train_pipeline(config_path: str):
    # 1. 加载配置
    with open(config_path) as f:
        cfg_dict = yaml.safe_load(f)
    config = TrainConfig(**cfg_dict)

    # 2. MLflow 追踪
    mlflow.set_experiment(config.experiment_name)
    mlflow.start_run(run_name=config.run_name)
    mlflow.log_params(vars(config))

    # 3. 数据
    train_loader = build_dataloader(config.train_data, config.batch_size, train=True)
    val_loader = build_dataloader(config.eval_data, config.batch_size, train=False)

    # 4. 模型
    module = LLMModule(config)

    # 5. Trainer
    trainer = pl.Trainer(
        accelerator='gpu',
        devices=config.gpus,
        strategy='ddp',
        precision=config.precision,
        max_epochs=config.epochs,
        gradient_clip_val=config.max_grad_norm,
        callbacks=[
            pl.callbacks.ModelCheckpoint(
                dirpath=config.checkpoint_dir,
                save_top_k=config.save_top_k,
                monitor='val_loss',
                mode='min',
            ),
            pl.callbacks.EarlyStopping(monitor='val_loss', patience=3),
            pl.callbacks.LearningRateMonitor(),
        ],
        logger=[
            pl.loggers.MLFlowLogger(experiment_name=config.experiment_name),
            pl.loggers.TensorBoardLogger('logs/'),
        ],
    )

    # 6. 训练
    trainer.fit(module, train_loader, val_loader)

    # 7. 测试
    test_loader = build_dataloader(config.test_data, config.batch_size, train=False)
    test_results = trainer.test(module, test_loader)[0]
    mlflow.log_metrics({f"test_{k}": v for k, v in test_results.items()})

    # 8. 注册模型
    best_model_path = trainer.checkpoint_callback.best_model_path
    mlflow.pytorch.log_model(module.model, "model",
                            registered_model_name=f"{config.model_name}-prod")

    mlflow.end_run()
    return best_model_path

if __name__ == "__main__":
    import sys
    train_pipeline(sys.argv[1])
```

### 5.2 数据测试

```python
# tests/data/test_data_quality.py
import pandas as pd
import pytest
from great_expectations.dataset import PandasDataset

class TestDataQuality:
    @pytest.fixture
    def data(self):
        return pd.read_parquet("data/test.parquet")

    def test_schema(self, data):
        """schema 完整性"""
        expected_cols = {'user_id', 'age', 'gender', 'click_count'}
        assert set(data.columns) >= expected_cols

    def test_no_null_ids(self, data):
        """主键非空"""
        assert data['user_id'].notna().all()
        assert (data['user_id'] > 0).all()

    def test_age_range(self, data):
        """年龄范围合法"""
        assert data['age'].between(0, 150).all()

    def test_no_duplicates(self, data):
        """无完全重复行"""
        assert data.duplicated().sum() / len(data) < 0.01  # <1% 重复

    def test_feature_distribution(self, data):
        """特征分布稳定 (与基线比)"""
        baseline = pd.read_parquet("data/baseline.parquet")
        for col in ['age', 'click_count']:
            psi = compute_psi(baseline[col], data[col])
            assert psi < 0.25, f"{col} drift: PSI={psi}"

    def test_label_balance(self, data):
        """标签分布平衡"""
        if 'label' in data.columns:
            ratio = data['label'].mean()
            assert 0.1 < ratio < 0.9, f"Label imbalance: {ratio}"
```

### 5.3 模型测试

```python
# tests/model/test_model.py
import torch
import pytest

class TestModel:
    @pytest.fixture
    def model(self):
        return load_model("checkpoints/best.pt")

    @pytest.fixture
    def sample_input(self):
        return torch.randint(0, 32000, (4, 128))

    def test_output_shape(self, model, sample_input):
        """输出 shape 正确"""
        out = model(sample_input)
        assert out.logits.shape == (4, 128, 32000)

    def test_numerical_stability(self, model, sample_input):
        """数值稳定性: 无 NaN/Inf"""
        out = model(sample_input)
        assert not torch.isnan(out.logits).any()
        assert not torch.isinf(out.logits).any()

    def test_inference_latency(self, model, sample_input):
        """推理延迟 < 100ms (P99)"""
        import time
        model = model.cuda().eval()
        sample_input = sample_input.cuda()

        # warmup
        for _ in range(10):
            _ = model(sample_input)

        torch.cuda.synchronize()
        latencies = []
        for _ in range(100):
            start = time.time()
            _ = model(sample_input)
            torch.cuda.synchronize()
            latencies.append((time.time() - start) * 1000)

        p99 = sorted(latencies)[98]
        assert p99 < 100, f"P99 latency {p99}ms > 100ms"

    def test_deterministic(self, model, sample_input):
        """确定性输出 (相同输入相同输出)"""
        torch.manual_seed(42)
        out1 = model(sample_input)
        torch.manual_seed(42)
        out2 = model(sample_input)
        assert torch.allclose(out1.logits, out2.logits, atol=1e-5)

    def test_robustness_to_perturbation(self, model, sample_input):
        """小扰动不影响输出 (鲁棒性)"""
        out1 = model(sample_input)
        noise = torch.randn_like(sample_input.float()).long() * 0  # 微小扰动
        out2 = model(sample_input + noise)
        # Top-1 预测一致率 > 95%
        pred1 = out1.logits.argmax(-1)
        pred2 = out2.logits.argmax(-1)
        consistency = (pred1 == pred2).float().mean()
        assert consistency > 0.95
```

---

## 6. 常见陷阱 ⚠️

| # | 陷阱 | 后果 | 解决方案 |
|---|------|------|----------|
| 1 | 训练-服务特征不一致 | 线上 AUC 比 offline 低 5-15% | Feature Store + 共享特征代码 |
| 2 | 实验未追踪随机种子 | 复现失败 | 全局 set_seed + 记录到 MLflow |
| 3 | 检查点只存模型权重 | 优化器状态丢失,继续训练效果差 | 存 model + optimizer + scheduler + epoch |
| 4 | 监控只看准确率 | 业务指标下降才发现 | 业务指标 (CTR/转化) + 模型指标双轨 |
| 5 | 数据漂移未检测 | 模型静默衰减 | PSI/KS 周期检测 + 告警 |
| 6 | A/B 测试样本量不足 | 噪声判断为显著提升 | 用样本量公式预算 + 多次验证 |
| 7 | 训练-评估数据泄漏 | 评估过度乐观 | 严格时间切分 + 防泄漏测试 |
| 8 | 模型注册无版本 | 线上模型不可追溯 | Model Registry + 严格 alias (Staging/Prod) |
| 9 | CI 只测代码 | 模型 bug 上线 | 数据测试 + 模型测试 + 性能测试 |
| 10 | 流水线硬编码 | 难复现 | 配置驱动 (YAML + dataclass) |
| 11 | 大文件入 Git | 仓库膨胀 | DVC/LFS + .gitignore |
| 12 | 多人实验无协作 | 重复造轮子 | 共享实验仓库 + Run 命名规范 |
| 13 | 部署无回滚机制 | 故障延长 | 模型蓝绿部署 + 一键回滚 |
| 14 | 监控无告警路由 | 看到问题已晚 | 告警分级 + oncall 排班 |
| 15 | Notebook 当生产代码 | 不可复现 + 不可维护 | Notebook → 模块化 .py + 单测 |
| 16 | 配置散落 | 难维护 | 中心化配置 (Hydra/OmegaConf) |
| 17 | 没做模型卡 | 团队不知道模型能力边界 | 强制 Model Card 模板 |

---

## 7. 与其他方法关系

### 7.1 MLOps 全栈架构

```
┌────────────────────────────────────────────────────────────┐
│                    业务系统 (Application)                    │
├────────────────────────────────────────────────────────────┤
│  服务层 (Serving) - Triton / vLLM / TF Serving             │
├────────────────────────────────────────────────────────────┤
│  模型注册 (Registry) - MLflow / Vertex Model Registry      │
├────────────────────────────────────────────────────────────┤
│  编排层 (Orchestration) - Airflow / Kubeflow / Argo        │
├────────────────────────────────────────────────────────────┤
│  实验追踪 (Tracking) - MLflow / W&B / Neptune              │
├────────────────────────────────────────────────────────────┤
│  特征存储 (Feature Store) - Feast / Tecton / 阿里 iFeco    │
├────────────────────────────────────────────────────────────┤
│  数据版本 (Data Versioning) - DVC / LakeFS / Delta Lake    │
├────────────────────────────────────────────────────────────┤
│  基础设施 (Infrastructure) - K8s / GPU 集群 / 对象存储      │
└────────────────────────────────────────────────────────────┘
```

### 7.2 与分布式训练的关系

- [[深度学习-分布式训练]] 关注**单次训练效率** (DDP/FSDP/3D 并行)
- MLOps 关注**全生命周期工程化** (从数据到部署)
- 二者交集: 训练流水线编排、检查点管理、弹性训练

### 7.3 与推理优化的关系

- [[深度学习-推理优化与部署]] 关注**单次推理效率** (量化/PagedAttention)
- MLOps 关注**模型上线全过程** (CI/CD + 监控)
- 二者交集: 模型注册、部署策略 (蓝绿/金丝雀)、推理监控

### 7.4 与 DevOps 的对应关系

| DevOps | MLOps |
|--------|-------|
| Git | Git + DVC |
| CI (Jenkins) | CI + 数据/模型测试 |
| CD (ArgoCD) | CD + 模型注册 + 灰度 |
| Monitor (Prometheus) | Monitor + 漂移检测 |
| Logs (ELK) | Logs + 实验追踪 |
| APM (SkyWalking) | APM + 模型质量监控 |

### 7.5 主流 MLOps 平台对比

| 平台 | 强项 | 弱项 | 适用 |
|------|------|------|------|
| **MLflow** | 开源、轻量、生态好 | 大规模协同弱 | 中小团队 |
| **W&B** | 协作、可视化、超参搜索 | 收费、锁定 | 研究团队 |
| **Kubeflow** | K8s 原生、全套 | 复杂、维护成本高 | 大厂自建 |
| **SageMaker** | AWS 集成、端到端 | 厂商锁定、贵 | AWS 用户 |
| **Vertex AI** | GCP 集成、AutoML | 厂商锁定 | GCP 用户 |
| **Azure ML** | Azure 集成、企业级 | 厂商锁定 | Azure 用户 |
| **阿里 PAI** | 阿里云集成、中文好 | 国内限定 | 国内企业 |
| **字节的 MLOps** | 字节内部大规模 | 不开源 | 字节内部 |

---

## 8. 面试速答 ⭐

**Q1: 什么是 MLOps?与 DevOps 的核心区别?**

A: MLOps 是 ML 系统的全生命周期工程化实践,涵盖数据→模型→服务的自动化。与 DevOps 区别: (1) 变更源多 (代码+数据+模型), (2) 测试复杂 (数据/模型/性能), (3) 监控需漂移检测, (4) 部署含模型权重, (5) 回滚需模型版本。**核心**: ML 系统的"隐藏技术债"是 ML 代码仅占 5%,95% 是周边工程。

**Q2: 实验追踪要记录什么?**

A: 五要素:
1. **代码**: Git commit hash + diff
2. **环境**: Python/PyTorch/CUDA 版本
3. **数据**: 数据版本 (DVC hash) + 特征 schema
4. **参数**: 模型架构 + 超参 + 命令行参数
5. **指标**: 训练/验证指标 + 系统指标 (GPU 利用率)
6. **产物**: 模型权重 + 检查点 + 评估报告

**关键**: 复现 = 代码 + 环境 + 数据 + 随机种子,任一缺失即失败。

**Q3: 训练-服务偏差 (Training-Serving Skew) 的根因?**

A: 三大根因:
1. **特征处理不一致**: 训练用 Pandas,服务用 Java/Go 重写,逻辑漂移
2. **数据分布不一致**: 训练历史数据,服务实时数据,分布漂移
3. **batch 不一致**: 训练用 Batch Norm,服务单样本推理

**解决方案**: Feature Store (共享特征定义) + 在线/离线一致性测试 + 服务用 Train Norm 或 Layer Norm。

**Q4: 数据漂移检测的方法?**

A:
- **PSI**: 适合分桶特征,通用,< 0.1 稳定, > 0.25 漂移
- **KS 检验**: 连续分布,p<0.05 漂移
- **KL 散度**: 信息论视角
- **Wasserstein 距离**: 比 KL 更鲁棒
- **模型方法**: 训练二分类区分新旧数据,AUC>0.7 即漂移

**关键**: 多特征多方法组合,设阈值告警,自动触发重训。

**Q5: 模型部署有哪些策略?**

A:
1. **蓝绿部署**: 旧/新版本并存,流量瞬间切换。回滚快,资源 2×。
2. **金丝雀**: 5% → 25% → 50% → 100% 渐进,问题影响小。需监控。
3. **影子**: 新版本接收流量但不返回结果,仅观察。零风险。
4. **A/B**: 流量分组对比,统计显著后才全量。
5. **多臂老虎机**: 自适应流量分配,效果好的版本流量自动增加。

**关键**: 高风险用影子,中风险用金丝雀,效果不确定用 A/B。

**Q6: 如何保证 ML 项目的复现性?**

A: 六要素:
1. **代码**: Git commit + 干净的 working tree
2. **环境**: Docker / conda env export
3. **数据**: DVC hash 锁定 + 数据卡 (来源/处理)
4. **随机性**: `torch.manual_seed` + `numpy.random.seed` + `PYTHONHASHSEED`
5. **配置**: YAML/dataclass + Hydra
6. **硬件**: 记录 GPU 型号 + CUDA 版本 (有些算子不 deterministic)

**验证**: 用 MLflow 复跑 + 对比指标,差异 < 0.1% 视为复现成功。

**Q7: 模型监控应该监控什么?**

A: 四层监控:
1. **基础设施**: GPU 利用率/显存/温度/功耗,网络/磁盘 IO
2. **服务性能**: QPS/延迟 (P50/P99)/错误率/超时率
3. **模型质量**: 预测分布漂移,实时准确率 (有延迟标签时)
4. **业务指标**: CTR/转化率/留存 (与 A/B 联动)

**关键**: 业务指标衰减是终极告警,但滞后;数据漂移是早期信号。

**Q8: CI/CD for ML 与普通 CI/CD 的区别?**

A:
- **测试维度**: 普通只有代码测试 (单元/集成/E2E);ML 还需数据测试 (schema/分布/质量) + 模型测试 (shape/数值稳定/性能) + 公平性测试
- **构建产物**: 普通只构建二进制;ML 还构建模型权重 + 配置 + 数据快照
- **部署策略**: 普通蓝绿足够;ML 需金丝雀 + 影子 + A/B
- **回滚**: 普通回滚代码;ML 回滚模型 + 配置 + 数据快照

**Q9: Feature Store 解决什么问题?**

A: 三大问题:
1. **训练-服务偏差**: 在线/离线特征不一致,通过共享定义解决
2. **特征重复计算**: 多个团队重复造轮子,通过特征复用解决
3. **Point-in-Time 正确性**: 训练时需历史时刻特征 (防泄漏),通过 time-travel 查询解决

**主流**: Feast (开源), Tecton (商业), 阿里 iFeco, 字节 FeaturePlat。

**Q10: 模型卡 (Model Card) 应该包含什么?**

A: Google Model Card 模板:
1. **模型详情**: 开发者/日期/版本/许可证
2. **预期用途**: 主用例/二次用例/禁用例
3. **性能评估**: 数据集/指标/分群体表现
4. **伦理考量**: 偏见/风险/缓解措施
5. **局限性**: 不适用场景
6. **训练细节**: 数据/超参/计算资源

**关键**: 分群体表现 (gender/race/age) 揭示公平性问题。

---

## 9. 综合面试题

1. **[L2]** 设计一个 LLM 微调的完整 MLOps 流程,从数据采集到上线监控

2. **[L3]** 训练-服务偏差导致线上 AUC 比 offline 低 8%,如何排查?给出诊断流程

3. **[L2]** 比较 MLflow 和 W&B,在 100 人团队中如何选择?

4. **[L3]** 设计一个支持 50 个模型并发的模型注册中心,如何管理版本/权限/审计?

5. **[L2]** 实现一个数据漂移检测系统,支持 100+ 特征,每日检测,如何降本?

6. **[L3]** 模型上线后 P99 延迟从 200ms 涨到 800ms,但模型质量没变,可能原因?

7. **[L2]** 设计一个 LLM A/B 测试: 7 天, 100 万用户,如何分桶?如何评估?

8. **[L3]** 你团队的 Notebook 越积越多,代码无法复用,如何重构?

9. **[L2]** 比较 Airflow、Kubeflow、Argo Workflows 在 ML 流水线中的优劣

10. **[L3]** 设计一个特征存储系统: 1000 特征, 1 亿用户,日更 10%,如何设计?

11. **[L2]** 大模型 (LLM) 时代的 MLOps 与传统 ML 有何不同?

12. **[L3]** 如何评估一个 ML 系统的可观察性 (Observability)?给出指标体系

13. **[L2]** 设计一个 ML 团队的 CI/CD 流水线,从 PR 到生产,有哪些 stage?

14. **[L3]** 监管要求模型可审计,如何设计完整的模型审计日志系统?

15. **[L2]** 你被指派提升团队的 ML 项目上线率 (5% → 50%),给出 6 个月计划

---

## 10. 工业案例

### 10.1 字节跳动推荐系统 MLOps

**背景**: 抖音/今日头条推荐系统,日活 7 亿,模型日更 100+。

**架构**:
- **特征平台**: FeaturePlat (自研), PB 级特征,毫秒级在线
- **训练平台**: 内部基于 Ray + 自研调度,支持万亿样本
- **实验平台**: A/B 实验平台,日均 1000+ 实验
- **模型部署**: 蓝绿 + 金丝雀,P99 < 50ms
- **监控**: 实时指标 (5 分钟延迟) + 漂移检测 + 业务联动

**关键实践**:
- 特征版本化,可回滚
- 模型分级 (S/A/B/C),S 级需审批
- 实验报告自动化生成
- 失败案例自动归因

### 10.2 阿里 PAI 平台

**架构**:
- **PAI-Studio**: 可视化建模 (拖拽式)
- **PAI-DSW**: 在线 Notebook (含 GPU)
- **PAI-DLC**: 分布式训练 (基于 K8s)
- **PAI-EAS**: 模型在线服务
- **iFeco**: 特征存储
- **PAI-Rec**: 推荐系统解决方案

**特点**:
- 全流程低代码,适合传统企业
- 与阿里云深度集成 (OSS/RDS/MaxCompute)
- 中文文档完善
- 价格: 按 GPU 时计费

### 10.3 Google Vertex AI

**架构**:
- **Pipelines**: Kubeflow 升级版
- **Model Registry**: 模型注册 + 版本管理
- **Feature Store**: 全托管
- **Model Monitoring**: 自动漂移检测
- **Vertex AI Studio**: 模型微调界面

**特点**:
- 全托管,免运维
- AutoML 功能强 (表格/视觉/文本)
- 与 BigQuery 深度集成
- 价格较高,适合大企业

### 10.4 Meta FBLearner Flow

**背景**: Meta (Facebook) 内部 ML 平台,服务 1000+ 工程师。

**架构**:
- **FBLearner Flow**: 流水线编排 (类 Airflow)
- **FBLearner Explorer**: 实验管理
- **Feature Store**: 内部特征平台
- **Model Serving**: 多模态服务 (推荐/CV/NLP)

**规模**:
- 日均实验 1 万+
- 模型版本 100 万+
- 特征 10 万+

### 10.5 故障复盘: 推荐系统静默衰减

**事件**: 某电商推荐系统,3 个月内 CTR 从 4.5% 衰减到 3.2%,未被及时发现。

**根因**:
1. 数据漂移: 疫情后用户消费习惯变化
2. 监控只看模型准确率,未监控业务指标
3. 模型重训周期 1 月,响应过慢

**修复**:
1. 加业务指标实时监控 (CTR/转化率),15% 跌幅告警
2. 加 PSI 漂移检测,> 0.25 自动触发重训
3. 重训周期从 1 月 → 1 周
4. 引入在线学习 (Flink + 增量训练)
5. 上线 Model Monitor Dashboard,每日邮件

**结果**: CTR 恢复到 4.3%,衰减发现时间从 3 月 → 3 天。

### 10.6 故障复盘: 模型权重污染

**事件**: 某金融风控模型,新版本上线后误杀率从 2% → 8%。

**排查**:
1. 模型评估指标正常 (AUC 0.92 vs 0.91)
2. 检查发现新模型权重文件大小异常 (1.2GB vs 1.5GB)
3. 进一步发现权重被另一团队的实验覆盖

**根因**:
- 模型注册中心无权限控制
- 检查点目录共享,无版本隔离
- 缺乏模型签名验证

**修复**:
1. 模型注册中心加 RBAC (开发/审核/上线)
2. 模型签名 (hash) 上线前验证
3. 检查点分租户存储
4. 上线流程加双人审核
5. 灰度发布 + 影子流量对比

### 10.7 OpenAI 的模型迭代流程

**架构** (基于公开信息):
- **训练实验**: 内部平台追踪每次实验
- **模型评估**: 多维 benchmark + 人工评估
- **红队测试**: 安全/伦理/偏差专项测试
- **A/B 部署**: ChatGPT API 滚动发布
- **监控**: 用户反馈 + RLHF 数据收集

**特点**:
- 评估极其严格 (单评估耗资数万美元)
- 红队提前 6 个月介入
- 灰度上线 (1% → 10% → 50% → 100%)
- 失败回滚机制完善

### 10.8 Hugging Face 的开源 MLOps 生态

**工具栈**:
- **Transformers**: 模型库 (10 万+ 模型)
- **Datasets**: 数据集库
- **Spaces**: 模型 demo 托管
- **Inference Endpoints**: 一键部署
- **AutoTrain**: 自动微调

**影响**:
- 降低 ML 门槛,小团队也能玩大模型
- 推动开源生态,模型权重共享
- 商业模式: 免费开源 + 企业版收费

---

## 11. 参考与延伸

### 11.1 核心论文

- **Hidden Technical Debt**: Sculley et al. "Hidden Technical Debt in Machine Learning Systems" (Google, NeurIPS 2015)
- **MLOps Maturity**: Google Cloud "MLOps: Continuous delivery and automation pipelines in machine learning" (2020)
- **Model Cards**: Mitchell et al. "Model Cards for Model Reporting" (Google, FAT* 2019)
- **Datasheets for Datasets**: Gebru et al. "Datasheets for Datasets" (Microsoft, 2018)
- **Feature Store**: Lagov et al. "The Feature Store: A Foundation for ML at Scale" (Uber, 2020)
- **Continuous Training**: Polyzotis et al. "Continuous Training for Production ML in the TensorFlow Extended (TFX) Platform" (Google, 2019)

### 11.2 开源工具

- **MLflow**: https://mlflow.org (实验追踪 + 模型注册)
- **W&B**: https://wandb.ai (协作实验平台)
- **DVC**: https://dvc.org (数据版本)
- **Kubeflow**: https://kubeflow.org (K8s ML 平台)
- **Airflow**: https://airflow.apache.org (流水线编排)
- **Prefect**: https://prefect.io (现代编排)
- **Dagster**: https://dagster.io (数据编排,资产导向)
- **Feast**: https://feast.dev (特征存储)
- **Great Expectations**: https://greatexpectations.io (数据质量)
- **Evidently**: https://evidentlyai.com (漂移检测)
- **Paperspace**: https://paperspace.com (GPU + 实验平台)

### 11.3 商业平台

- **AWS SageMaker**: https://aws.amazon.com/sagemaker
- **GCP Vertex AI**: https://cloud.google.com/vertex-ai
- **Azure ML**: https://azure.microsoft.com/ml
- **阿里 PAI**: https://www.aliyun.com/product/bigdata/learn
- **Databricks**: https://databricks.com (Lakehouse + ML)
- **Weights & Biases**: https://wandb.ai
- **Comet ML**: https://comet.ml

### 11.4 教程与课程

- **Google MLOps**: https://cloud.google.com/architecture/mlops-continuous-delivery-and-automation-pipelines-in-machine-learning
- **Made With ML**: https://madewithml.com (免费实战)
- **Full Stack Deep Learning**: https://fullstackdeeplearning.com
- **Stanford CS 329**: Machine Learning Systems Engineering
- **Andrew Ng ML Ops**: Coursera 专项

### 11.5 内部链接

- [[深度学习-分布式训练]] - 训练侧工程化
- [[深度学习-推理优化与部署]] - 推理侧工程化
- [[深度学习-Transformer与注意力]] - 主流架构
- ../机器学习/监督学习-评估与调优.md - 评估方法论
- ../分布式系统/ - 系统设计基础

### 11.6 延伸阅读

- **Chip Huyen**: "Designing Machine Learning Systems" (2022) - 最权威的 MLSys 教材
- **Hannes Hapke**: "Machine Learning Operations" (2022)
- **Google SRE for ML**: SRE 系列应用版
- **martinfowler.com**: ML 文章合集

---

> **工业化核心原则**: MLOps 不是工具堆砌,而是工程文化的体现。**关键三件事**: (1) 复现性 (任何实验可重现), (2) 自动化 (减少人工操作), (3) 监控 (问题早发现)。工具是手段,文化是根本。

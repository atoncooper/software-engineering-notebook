# 深度学习

> 本目录是深度学习方向的学习与面试笔记总索引,与 [机器学习](../机器学习/) 目录互为补充。
> 机器学习笔记本覆盖数学基础、监督学习、无监督学习;本目录聚焦深度学习的核心模型与训练方法。
>
> 按照 **神经网络基础 → CNN → RNN → Transformer → 生成模型 → 优化与正则化** 的递进顺序组织,从感知机到大模型,从理论到工程。
>
> 每篇笔记遵循统一结构:**思维导图 → 问题定义 → 直觉解释 → 形式化推导 → 算法流程 → PyTorch 实现 → 常见陷阱 → 与其他方法关系 → 面试速答 → 综合面试题 → 参考**。

---

## 一、目录总览

### 1. 神经网络基础

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-神经网络基础.md` | MLP、前向/反向传播、激活函数 | Sigmoid/Tanh/ReLU、梯度消失/爆炸、Batch Norm、初始化 |

### 2. 卷积神经网络

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-卷积神经网络.md` | CNN、卷积、池化、经典架构 | LeNet/AlexNet/VGG/ResNet/Inception、感受野、U-Net |

### 3. 循环神经网络

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-循环神经网络.md` | RNN、LSTM、GRU、Seq2Seq | BPTT、门控机制、注意力、双向 RNN |

### 4. Transformer 与注意力

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-Transformer与注意力.md` | Self-Attention、位置编码、预训练 | Scaled Dot-Product、Multi-Head、BERT/GPT/T5 |

### 5. 生成模型

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-生成模型.md` | 自编码器、VAE、GAN、Diffusion | KL 散度、对抗训练、重参数化、DDPM、Stable Diffusion |

### 6. 优化与正则化

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-优化与正则化.md` | SGD/Adam、学习率调度、Dropout | Momentum/RMSProp/AdamW、Layer Norm、Early Stopping |

### 7. 工业化实战系列

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-分布式训练.md` | DDP/FSDP/Megatron/3D 并行 | Ring AllReduce、ZeRO Stage 1/2/3、张量并行、流水线并行、混合精度、MFU、弹性训练、LLaMA-2 70B 复现 |
| `深度学习-推理优化与部署.md` | vLLM/量化/蒸馏/部署 | PagedAttention、GPTQ/AWQ/SmoothQuant、Speculative Decoding、Continuous Batching、LoRA/QLoRA、Triton/TensorRT-LLM、生产部署 |
| `深度学习-MLOps与工程实践.md` | 实验管理/版本/监控/CI/CD | MLflow/W&B/DVC、Kubeflow/Airflow、Feature Store、数据漂移检测(PSI/KS)、模型卡、A/B 测试、训练-服务偏差、字节/阿里/Meta 案例 |
| `深度学习-工业案例与训练实战.md` | GPT-3/LLaMA-2/Megatron/GPT-4/AlphaFold/CLIP/SD/BERT 深度剖析 | 175B/70B/1T 训练细节、成本核算、Loss Spike、5 问复盘、调度运维、训练失败模式 |

---

## 二、推荐阅读顺序

```
入门
  │
  └─→ 深度学习-神经网络基础.md
         │
         ├─→ 深度学习-优化与正则化.md   (训练方法贯穿全程)
         │
         ▼
   ┌──────────────────────────────┐
   │  深度学习-卷积神经网络.md     │  视觉
   │  (CV: LeNet → ResNet)        │
   └──────────────┬───────────────┘
                  │
   ┌──────────────┴───────────────┐
   │  深度学习-循环神经网络.md     │  序列
   │  (NLP: RNN → LSTM → Seq2Seq) │
   └──────────────┬───────────────┘
                  │
                  ▼
   ┌──────────────────────────────┐
   │  深度学习-Transformer.md     │  统一架构
   │  (BERT/GPT/T5)               │
   └──────────────┬───────────────┘
                  │
                  ▼
   ┌──────────────────────────────┐
   │  深度学习-生成模型.md         │  生成
   │  (AE/VAE/GAN/Diffusion)      │
   └──────────────┬───────────────┘
                  │
                  ▼
   ┌──────────────────────────────────────────┐
   │           工业化实战系列 (进阶)          │
   │  ┌──────────────────────────────────┐    │
   │  │ 深度学习-分布式训练.md           │    │
   │  │ (DDP/FSDP/Megatron/3D 并行)      │    │
   │  └──────────────┬───────────────────┘    │
   │                 │                         │
   │  ┌──────────────┴───────────────────┐    │
   │  │ 深度学习-推理优化与部署.md       │    │
   │  │ (vLLM/量化/部署)                 │    │
   │  └──────────────┬───────────────────┘    │
   │                 │                         │
   │  ┌──────────────┴───────────────────┐    │
   │  │ 深度学习-MLOps与工程实践.md      │    │
   │  │ (实验/版本/监控/CI/CD)           │    │
   │  └──────────────┬───────────────────┘    │
   │                 │                         │
   │  ┌──────────────┴───────────────────┐    │
   │  │ 深度学习-工业案例与训练实战.md   │    │
   │  │ (GPT-3/LLaMA-2/AlphaFold 剖析)   │    │
   │  └──────────────────────────────────┘    │
   └──────────────────────────────────────────┘
```

**两阶段阅读法**:
- **基础阶段 (1-6 章)**: 建立深度学习核心概念框架,适合学生 / 转岗工程师
- **工业阶段 (7-10 章)**: 大模型时代的工程化实战,适合算法工程师 / 架构师 / 准备面试者

---

## 三、知识地图

```
            ┌────────────────────────────────┐
            │       神经网络基础              │
            │  感知机 → MLP → 反向传播        │
            │  激活函数 / 初始化 / 归一化     │
            └─────────────┬──────────────────┘
                          │
            ┌─────────────┴──────────────────┐
            ▼                                ▼
   ┌─────────────────┐              ┌─────────────────┐
   │   CNN           │              │   RNN           │
   │  视觉特征       │              │  序列建模       │
   │  LeNet→ResNet   │              │  LSTM/GRU       │
   └────────┬────────┘              └────────┬────────┘
            │                                │
            └────────────────┬───────────────┘
                             ▼
            ┌────────────────────────────────┐
            │      Transformer               │
            │  Self-Attention / Multi-Head   │
            │  BERT / GPT / T5               │
            └─────────────┬──────────────────┘
                          │
            ┌─────────────┴──────────────────┐
            ▼                                ▼
   ┌─────────────────┐              ┌─────────────────┐
   │   生成模型      │              │  优化与正则化   │
   │  AE/VAE/GAN/    │              │  SGD/Adam/      │
   │  Diffusion      │              │  Dropout/Norm   │
   └─────────────────┘              └─────────────────┘
```

---

## 四、每篇笔记的统一结构

1. **思维导图**:ASCII 图呈现本章知识脉络
2. **问题定义**:解决什么问题、典型应用场景
3. **直觉解释**:先讲直觉,再上公式
4. **形式化定义与公式推导**:第一性原理推导,标明每一步的几何/统计意义 🎓
5. **算法流程**:伪代码 + 复杂度分析
6. **PyTorch 实现**:从零实现 + 库 API 实现
7. **常见陷阱与调参经验**:工程化视角 ⚠️
8. **与其他方法的关系**:横向对比表
9. **面试速答 ⭐**:高频问题的一句话答案
10. **综合面试题**:由浅入深,含答题要点
11. **参考与延伸**:书籍、论文、跨文件链接

---

## 五、面试高频考点速查

### 5.1 神经网络基础
- 反向传播的链式法则
- 梯度消失/爆炸与激活函数、归一化
- Batch Norm vs Layer Norm 的适用场景
- He/Xavier 初始化的原理
- ReLU 的"神经元死亡"问题

### 5.2 CNN
- 卷积的参数共享与局部连接
- 感受野计算与网络深度
- 1×1 卷积的作用
- ResNet 残差连接的数学原理
- Depthwise Separable Conv 的参数量

### 5.3 RNN
- BPTT 与梯度消失
- LSTM 三个门的作用
- GRU 与 LSTM 的对比
- 双向 RNN 的适用场景
- Seq2Seq 的暴露偏差(exposure bias)

### 5.4 Transformer
- Self-Attention 的 Q/K/V 计算
- 缩放点积的除以 √d 原因
- Multi-Head 的并行性
- 位置编码(正弦/可学习/相对/RoPE)
- BERT vs GPT vs T5 的预训练目标
- KV Cache 与推理加速

### 5.5 生成模型
- VAE 的重参数化技巧
- ELBO 的推导
- GAN 的极小极大博弈
- Diffusion 的前向/逆向过程
- Stable Diffusion 的潜空间
- 模式崩溃与训练不稳定

### 5.6 优化与正则化
- SGD/Momentum/AdaGrad/RMSProp/Adam 演进
- AdamW 与权重衰减解耦
- 学习率预热与余弦退火
- Dropout 的推理行为
- 早停与验证集监控

### 5.7 分布式训练 (工业化)
- DDP vs DP 的区别,Ring AllReduce 原理
- ZeRO Stage 1/2/3 各优化什么
- 张量并行 (Megatron-LM column/row parallel)
- 流水线并行 GPipe vs 1F1B 的气泡差异
- 3D 并行 (DP×TP×PP) 配置选择
- 混合精度 FP16 vs BF16 的取舍
- MFU 计算 + 优化方向
- 弹性训练与故障恢复

### 5.8 推理优化与部署 (工业化)
- 推理为什么是 memory-bound
- PagedAttention 解决什么问题
- INT4 vs INT8 量化的取舍
- Speculative Decoding 数学原理
- Continuous Batching 的核心挑战
- LoRA / QLoRA 的本质
- vLLM vs TGI vs TensorRT-LLM 选型
- 推理服务 P99 延迟优化

### 5.9 MLOps 工程实践 (工业化)
- MLOps vs DevOps 的核心差异
- 训练-服务偏差的根因
- 数据漂移检测方法 (PSI/KS/KL)
- 实验追踪五要素 (代码/环境/数据/参数/指标)
- 模型部署策略 (蓝绿/金丝雀/影子/A/B)
- Feature Store 解决什么问题
- 模型卡 (Model Card) 内容

### 5.10 工业案例与训练实战 (工业化)
- GPT-3 175B 训练成本核算
- LLaMA-2 70B RLHF 三阶段
- Megatron-LM 1T 3D 并行配置
- AlphaFold 2 的 IPA 与 Self-Attention 区别
- Stable Diffusion 潜空间扩散 48× 加速原理
- Loss Spike 的应对策略
- 千卡训练故障恢复机制
- 大模型训练健康度指标

---

## 六、参考资源

### 6.1 经典教材
- 《Deep Learning》(花书)—— Goodfellow, Bengio, Courville
- 《动手学深度学习》—— 李沐
- 《神经网络与深度学习》—— 邱锡鹏
- 《Dive into Deep Learning》—— Zhang, Lipton, Li, Smola
- 《深度学习圣经》—— Goodfellow

### 6.2 经典论文(按主题)

**基础与训练**:
- Backpropagation: Rumelhart, Hinton, Williams (1986)
- Batch Normalization: Ioffe & Szegedy (2015)
- Dropout: Srivastava et al. (2014)
- Adam: Kingma & Ba (2014)
- He Initialization: He et al. (2015)

**CNN**:
- LeNet: LeCun et al. (1998)
- AlexNet: Krizhevsky et al. (2012)
- VGG: Simonyan & Zisserman (2014)
- GoogLeNet/Inception: Szegedy et al. (2015)
- ResNet: He et al. (2015)
- DenseNet: Huang et al. (2017)
- MobileNet: Howard et al. (2017)
- EfficientNet: Tan & Le (2019)

**RNN**:
- LSTM: Hochreiter & Schmidhuber (1997)
- GRU: Cho et al. (2014)
- Seq2Seq: Sutskever et al. (2014)
- Bahdanau Attention: Bahdanau et al. (2014)
- Luong Attention: Luong et al. (2015)

**Transformer**:
- Attention Is All You Need: Vaswani et al. (2017)
- BERT: Devlin et al. (2018)
- GPT-1: Radford et al. (2018)
- GPT-2: Radford et al. (2019)
- GPT-3: Brown et al. (2020)
- T5: Raffel et al. (2019)
- RoPE: Su et al. (2021)
- FlashAttention: Dao et al. (2022)

**生成模型**:
- VAE: Kingma & Welling (2013)
- GAN: Goodfellow et al. (2014)
- DCGAN: Radford et al. (2015)
- StyleGAN: Karras et al. (2018)
- DDPM: Ho et al. (2020)
- Score-Based: Song et al. (2020)
- Latent Diffusion: Rombach et al. (2022)
- Stable Diffusion: Rombach et al. (2022)

### 6.3 在线课程
- 吴恩达 Deep Learning Specialization
- 李宏毅深度学习 / 机器学习
- Stanford CS231n(CV) / CS224n(NLP) / CS25(Transformers)
- Berkeley CS285(RL) / CS288(NLP)
- HuggingFace Course
- Fast.ai

### 6.4 开源工具与生态
- PyTorch / TensorFlow / JAX
- HuggingFace Transformers / Diffusers / Datasets
- timm / torchvision
- Accelerate / DeepSpeed / Megatron-LM
- ONNX / TensorRT / vLLM / TGI
- MLflow / Weights & Biases / TensorBoard

---

## 七、笔记约定

- **语言与框架**:所有代码以 PyTorch 为主,关键算法给出从零实现 + 库 API 双版本
- **数学符号**:向量小写粗体 $\mathbf{x}$,矩阵大写 $X$,概率 $P(\cdot)$,期望 $\mathbb{E}$,梯度 $\nabla$
- **图示**:优先 ASCII 图说明结构;复杂图示标注来源
- **公式**:关键推导步骤必须标明几何/统计意义,不省略中间步骤
- **代码**:从零实现版(教学用) + 库 API 版(工程用)双版本
- **跨文件链接**:相关概念使用相对路径链接,便于跳转
- **标记**:⭐ 重点 / 🎓 学术 / ⚠️ 陷阱 / 🔥 高风险

---

## 八、与机器学习笔记本的关系

| 机器学习笔记本 | 深度学习笔记本 |
|---------------|---------------|
| 数学基础(线代/概统/优化) | 神经网络基础(用到所有数学基础) |
| 监督学习-线性模型 | 神经网络(逻辑回归是单层 NN) |
| 监督学习-评估与调优 | 优化与正则化(深度学习特有) |
| 监督学习-决策树与集成 | CNN(对比传统 ML 的特征工程) |
| 无监督学习-降维 | 自编码器 / VAE(深度降维) |
| 无监督学习-聚类 | 生成模型(无监督生成) |

深度学习是机器学习的子集但已自成体系,本目录专注深度学习特有内容,与机器学习笔记本交叉的概念(如梯度下降、正则化)在两处都有但侧重不同。

---

## 九、TODO / 待完善

- [x] 6 章基础正文全部完成
- [x] 4 章工业化实战 (分布式训练/推理优化/MLOps/工业案例) 完成
- [ ] 补充每章的真实数据集案例(MNIST / CIFAR / IMDB / WMT)
- [ ] 增加大模型专章(LLaMA / PaLM / Gemini / 多模态 CLIP/BLIP)
- [ ] 增加论文精读笔记子目录
- [ ] 增加面试真题汇编(按公司分类)
- [ ] 增加 Kaggle 实战案例索引
- [ ] RLHF / DPO / RLAIF 微调技术专章
- [ ] RAG / Agent / Tool Use 工程化专章

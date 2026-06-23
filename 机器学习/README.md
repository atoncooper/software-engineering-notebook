# 机器学习

> 本目录是机器学习方向的学习与面试笔记总索引。
> 按照 **数学基础 → 监督学习 → 无监督学习 → 深度学习 → 进阶专题 → 工程化** 的递进顺序组织，覆盖从理论到实战的完整知识体系。
>
> 每篇笔记遵循统一结构：**直觉 → 理论 → 公式推导 → 代码实现（PyTorch） → 常见陷阱 → 面试速答 → 综合面试题 → 参考**。

---

## 一、目录总览

### 1. 数学基础

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `数学基础-线性代数.md` | 矩阵、向量空间、特征值、SVD | 秩、迹、正交、投影、PCA 基础 |
| `数学基础-概率与统计.md` | 概率论、贝叶斯、常见分布、估计 | MLE/MAP、贝叶斯推断、假设检验 |
| `数学基础-微积分与优化.md` | 导数、梯度、凸优化、拉格朗日 | 梯度下降、KKT、对偶、牛顿法 |
| `数学基础-信息论.md` | 熵、KL 散度、交叉熵、互信息 | 信源编码、MDL、信息增益 |

### 2. 监督学习

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `监督学习-线性模型.md` | 线性回归、逻辑回归、正则化 | L1/L2、Ridge/Lasso、Elastic Net |
| `监督学习-决策树与集成.md` | 决策树、Bagging、Boosting | ID3/C4.5/CART、RF、GBDT、XGBoost、LightGBM |
| `监督学习-支持向量机.md` | SVM、核函数、SMO | 最大间隔、软间隔、RBF/多项式核 |
| `监督学习-贝叶斯分类.md` | 朴素贝叶斯、贝叶斯网络 | 条件独立、拉普拉斯平滑 |
| `监督学习-KNN与距离度量.md` | KNN、距离、KD 树 | 欧氏/曼哈顿/余弦、近似最近邻 |
| `监督学习-评估与调优.md` | 评估指标、交叉验证、超参搜索 | Precision/Recall/F1、ROC/AUC、过拟合/欠拟合 |

### 3. 无监督学习

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `无监督学习-聚类.md` | K-Means、层次聚类、DBSCAN | 轮廓系数、肘部法则、密度可达 |
| `无监督学习-降维.md` | PCA、LDA、t-SNE、UMAP | 方差最大化、流形学习 |
| `无监督学习-关联规则.md` | Apriori、FP-Growth | 支持度/置信度/提升度 |
| `无监督学习-异常检测.md` | 孤立森林、LOF、One-Class SVM | 密度估计、重构误差 |

### 4. 深度学习

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `深度学习-神经网络基础.md` | MLP、前向/反向传播、激活函数 | Sigmoid/Tanh/ReLU、梯度消失/爆炸、Batch Norm |
| `深度学习-卷积神经网络.md` | CNN、卷积、池化、经典架构 | LeNet/AlexNet/VGG/ResNet/Inception、感受野 |
| `深度学习-循环神经网络.md` | RNN、LSTM、GRU、Seq2Seq | BPTT、门控机制、注意力、Transformer |
| `深度学习-Transformer与注意力.md` | Self-Attention、位置编码、预训练 | Scaled Dot-Product、Multi-Head、BERT/GPT |
| `深度学习-生成模型.md` | 自编码器、VAE、GAN、Diffusion | KL 散度、对抗训练、重参数化、DDPM |
| `深度学习-优化与正则化.md` | SGD/Adam、学习率调度、Dropout | Momentum/RMSProp/AdamW、Layer Norm、Early Stopping |

### 5. 进阶专题

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `进阶-强化学习.md` | MDP、Q-Learning、Policy Gradient、Actor-Critic | 值迭代、DQN、PPO、A3C、探索-利用 |
| `进阶-图神经网络.md` | 图表示、GCN、GAT、GraphSAGE | 谱方法、消息传递、图分类 |
| `进阶-推荐系统.md` | 协同过滤、矩阵分解、深度推荐 | UserCF/ItemCF、FM/DeepFM、DSSM、双塔 |
| `进阶-迁移学习与领域自适应.md` | Fine-tune、Domain Adaptation、Prompt | 模态迁移、对抗迁移、LoRA/PEFT |
| `进阶-元学习.md` | MAML、Prototypical Network、Learning to Learn | 小样本、few-shot |
| `进阶-联邦学习.md` | FedAvg、差分隐私、安全聚合 | 横向/纵向、隐私保护 |

### 6. 工程化与部署

| 文件 | 主题 | 关键词 |
|------|------|--------|
| `工程化-特征工程.md` | 数值/类别/文本/时间特征、特征筛选 | 编码、分箱、归一化、特征重要性 |
| `工程化-模型部署.md` | 模型导出、推理优化、服务化 | ONNX/TensorRT、量化、蒸馏、Triton |
| `工程化-模型监控.md` | 数据漂移、概念漂移、A/B 测试 | PSI、KS、回滚、再训练 |
| `工程化-MLOps.md` | 实验管理、流水线、版本管理 | MLflow、DVC、Kubeflow、特征平台 |

---

## 二、推荐阅读顺序

```
入门
  │
  ├─→ 数学基础-线性代数.md
  ├─→ 数学基础-概率与统计.md
  ├─→ 数学基础-微积分与优化.md
  └─→ 数学基础-信息论.md
                          │
                          ▼
            ┌─────────────────────────┐
            │   监督学习（核心套路）   │
            │  线性模型 → 树 → SVM    │
            │  贝叶斯 → KNN → 评估    │
            └────────────┬────────────┘
                         │
                         ▼
            ┌─────────────────────────┐
            │   无监督学习             │
            │  聚类 → 降维 → 关联     │
            └────────────┬────────────┘
                         │
                         ▼
            ┌─────────────────────────┐
            │   深度学习               │
            │  基础 → CNN → RNN        │
            │       → Transformer     │
            │       → 生成模型        │
            └────────────┬────────────┘
                         │
            ┌────────────┴────────────┐
            ▼                         ▼
   ┌─────────────────┐       ┌─────────────────┐
   │   进阶专题      │       │   工程化部署    │
   │ RL/GNN/推荐/    │       │ 特征/部署/      │
   │ 迁移/元/联邦    │       │ 监控/MLOps      │
   └─────────────────┘       └─────────────────┘
```

---

## 三、知识地图

```
            ┌────────────────────────────┐
            │      数学基础              │
            │ 线代 / 概统 / 优化 / 信息  │
            └─────────────┬──────────────┘
                          │
            ┌─────────────┴──────────────┐
            ▼                            ▼
   ┌─────────────────┐          ┌─────────────────┐
   │  经典机器学习   │          │    深度学习     │
   │ 监督 / 无监督   │          │ NN/CNN/RNN/     │
   │ 树/SVM/贝叶斯   │  ──────→ │ Transformer/    │
   └────────┬────────┘          │ 生成模型       │
            │                   └────────┬────────┘
            │                            │
            └─────────────┬──────────────┘
                          ▼
            ┌────────────────────────────┐
            │      进阶专题              │
            │ RL / GNN / 推荐 / 迁移 /   │
            │ 元学习 / 联邦学习          │
            └─────────────┬──────────────┘
                          ▼
            ┌────────────────────────────┐
            │      工程化与部署          │
            │ 特征工程 / 模型部署 /      │
            │ 监控 / MLOps               │
            └────────────────────────────┘
```

---

## 四、每篇笔记的统一结构

1. **问题定义**：解决什么问题、典型应用场景
2. **直觉解释**：先讲直觉，再上公式
3. **形式化定义与公式推导**：第一性原理推导，标明每一步的几何/统计意义
4. **算法流程**：伪代码 + 复杂度分析
5. **PyTorch 实现**：从零实现 + 库 API 实现
6. **常见陷阱与调参经验**：工程化视角
7. **与其他方法的关系**：横向对比表
8. **面试速答**：高频问题的一句话答案
9. **综合面试题**：由浅入深，含答题要点
10. **参考与延伸**：书籍、论文、跨文件链接

---

## 五、面试高频考点速查

### 5.1 数学与基础
- L1 vs L2 正则化的本质区别（稀疏性、几何解释）
- MLE 与 MAP 的关系
- 凸优化与对偶问题
- KL 散度的非对称性与交叉熵
- 梯度下降的收敛性条件

### 5.2 经典机器学习
- SVM 的对偶推导与核函数的本质
- GBDT 与 XGBoost 的区别
- 随机森林的 Bagging 思想
- 决策树的信息增益 vs 增益率 vs Gini
- 过拟合的判定与缓解（正则化、数据增强、集成）
- 偏差-方差权衡
- 类别不平衡的处理方法
- AUC 的概率意义

### 5.3 深度学习
- 反向传播的链式法则
- 梯度消失/爆炸与激活函数、归一化
- Batch Norm vs Layer Norm 的适用场景
- Adam vs SGD 的优劣
- CNN 的感受野与参数共享
- RNN 的 BPTT 与长程依赖
- LSTM 三个门的作用
- Transformer 的 Self-Attention 复杂度
- 位置编码的设计
- BERT 与 GPT 的预训练目标差异
- VAE 与 GAN 的本质区别
- Diffusion 模型的前向/逆向过程

### 5.4 进阶与工程
- Q-Learning 的收敛条件
- Policy Gradient 与 Q-Learning 的优劣
- PPO 的 clipped objective
- GCN 的谱方法起源
- 推荐系统的召回-排序两阶段架构
- 协同过滤的冷启动
- 迁移学习中的负迁移
- 模型量化的 INT8 与蒸馏
- 数据漂移的检测指标
- A/B 测试的样本量计算

---

## 六、参考资源

### 6.1 经典教材
- 《Pattern Recognition and Machine Learning》—— Bishop
- 《The Elements of Statistical Learning》—— Hastie, Tibshirani, Friedman
- 《Pattern Classification》—— Duda, Hart, Stork
- 《统计学习方法》—— 李航
- 《机器学习》（西瓜书）—— 周志华
- 《Deep Learning》（花书）—— Goodfellow, Bengio, Courville
- 《动手学深度学习》—— 李沐

### 6.2 经典论文（按主题）
- SVM: Cortes & Vapnik (1995)
- Random Forest: Breiman (2001)
- GBDT: Friedman (2001)
- XGBoost: Chen & Guestrin (2016)
- ResNet: He et al. (2015)
- Attention / Transformer: Vaswani et al. (2017)
- BERT: Devlin et al. (2018)
- GPT 系列: Radford et al. / Brown et al. (2020)
- VAE: Kingma & Welling (2013)
- GAN: Goodfellow et al. (2014)
- DDPM: Ho et al. (2020)
- DQN: Mnih et al. (2013)
- PPO: Schulman et al. (2017)
- GCN: Kipf & Welling (2016)

### 6.3 在线课程
- 吴恩达 Machine Learning / Deep Learning Specialization
- 李宏毅机器学习 / 深度学习
- Stanford CS229 / CS231n / CS224n
- Berkeley CS285 (RL)
- Fast.ai

### 6.4 开源工具与生态
- PyTorch / TensorFlow / JAX
- scikit-learn / XGBoost / LightGBM
- HuggingFace Transformers / Diffusers
- MLflow / Weights & Biases / DVC
- ONNX / TensorRT / Triton

---

## 七、笔记约定

- **语言与框架**：所有深度学习代码以 PyTorch 为主；经典 ML 以 scikit-learn 为主
- **数学符号**：向量小写粗体 $\mathbf{x}$，矩阵大写 $X$，概率 $P(\cdot)$，期望 $\mathbb{E}$
- **图示**：优先 ASCII 图说明结构；复杂图示标注来源
- **公式**：关键推导步骤必须标明几何/统计意义，不省略中间步骤
- **代码**：从零实现版（教学用）+ 库 API 版（工程用）双版本
- **跨文件链接**：相关概念使用相对路径链接，便于跳转

---

## 八、TODO / 待完善

- [ ] 按章节逐篇完善内容
- [ ] 补充每篇笔记的真实数据集案例（MNIST / CIFAR / IMDB / 房价预测）
- [ ] 增加论文精读笔记子目录
- [ ] 增加面试真题汇编（按公司分类）
- [ ] 增加 Kaggle 实战案例索引

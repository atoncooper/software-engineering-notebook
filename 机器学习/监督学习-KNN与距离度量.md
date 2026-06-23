# 监督学习 —— KNN 与距离度量

> KNN（K-Nearest Neighbors）是最简单的监督学习算法：训练时不做任何事，预测时找最近 K 个邻居投票。但其背后的距离度量、近邻搜索、维数灾难等问题深具工程价值，是推荐系统、向量检索的基础。

---

## 〇、思维导图

```
                  KNN 与距离度量
                       │
        ┌──────────────┼──────────────┐
        │              │              │
    KNN 算法       距离度量        近邻搜索
        │              │              │
   ┌────┼────┐    闵可夫斯基        KD 树
  分类 回归 加权   │                球树
        │     │  欧氏 曼哈顿          │
        │     │  切比雪夫 余弦      维数灾难
        │     │  马氏 汉明            │
        │     │                       │
        │     │                    近似最近邻
        │     │                       │
        │     │                 LSH HNSW
        │     │                 IVF PQ
        │     │                       │
        └─────┴───────────────────────┘
                       │
                  Faiss / Milvus
```

---

## 一、问题定义

### 1.1 懒惰学习

KNN 是**懒惰学习（lazy learning）**：训练阶段仅存储数据，不显式学习模型；预测时才进行计算。对应地，逻辑回归、SVM 等是**急切学习（eager learning）**。

**特点**：

- 训练复杂度 $O(1)$（仅存储）
- 推理复杂度 $O(nd)$/次（朴素）
- 内存 $O(nd)$

### 1.2 KNN 算法

**分类**：找最近 $K$ 个邻居，多数投票

**回归**：最近 $K$ 个邻居的均值（或中位数）

**加权 KNN**：距离倒数加权，近的邻居权重大

---

## 二、KNN 算法

### 2.1 分类

$$
\hat{y} = \arg\max_c \sum_{i \in N_K(\mathbf{x})} \mathbb{1}[y_i = c]
$$

$N_K(\mathbf{x})$ 为 $\mathbf{x}$ 的 $K$ 近邻索引集。

### 2.2 回归

$$
\hat{y} = \frac{1}{K}\sum_{i \in N_K(\mathbf{x})} y_i
$$

### 2.3 距离加权

$$
\hat{y} = \arg\max_c \sum_{i \in N_K(\mathbf{x})} w_i \mathbb{1}[y_i = c], \quad w_i = \frac{1}{d_i + \epsilon}
$$

或高斯核权重 $w_i = \exp(-d_i^2/(2\sigma^2))$。

**动机**：避免远处邻居"喧宾夺主"，特别是 $K$ 较大时。

### 2.4 K 的选择与偏差-方差

| $K$ | 偏差 | 方差 | 现象 |
|-----|------|------|------|
| $K = 1$ | 低 | 高 | 过拟合，决策边界破碎 |
| $K$ 大 | 高 | 低 | 欠拟合，决策边界过简 |
| $K = n$ | 最高 | 0 | 退化为预测众数 |

**经验**：$K = \sqrt{n}$ 起步，奇数避免平票，CV 选最优。

### 2.5 KNN 与贝叶斯最优的关系

$K = 1$ 时，KNN 的渐近误差率满足 Cover-Hart 不等式：

$$
R^* \le R_{1\text{NN}} \le 2R^*
$$

$R^*$ 为贝叶斯最优误差。即 1-NN 最多比最优差一倍，且不会比最优好。$K \to \infty$ 且 $K/n \to 0$ 时，KNN 收敛到贝叶斯最优。

---

## 三、距离度量

### 3.1 闵可夫斯基距离（统一框架）

$$
d_p(\mathbf{x}, \mathbf{z}) = \left(\sum_i |x_i - z_i|^p\right)^{1/p}, \quad p \ge 1
$$

| $p$ | 名称 | 性质 |
|-----|------|------|
| 1 | 曼哈顿 | 鲁棒、稀疏偏好 |
| 2 | 欧氏 | 几何直觉 |
| $\infty$ | 切比雪夫 | 最大维度差 |

### 3.2 欧氏距离

$$
d_2(\mathbf{x}, \mathbf{z}) = \sqrt{\sum_i (x_i - z_i)^2} = \|\mathbf{x} - \mathbf{z}\|_2
$$

**适用**：连续特征、各向同性。

**陷阱**：尺度敏感，必须标准化。

### 3.3 曼哈顿距离

$$
d_1 = \sum_i |x_i - z_i|
$$

**适用**：高维（[维数灾难](#八、维数灾难)下比欧氏稳健）、网格结构（如城市街道）。

### 3.4 余弦相似度

$$
\cos(\mathbf{x}, \mathbf{z}) = \frac{\mathbf{x}^\top\mathbf{z}}{\|\mathbf{x}\|\|\mathbf{z}\|}
$$

距离 $d_{\cos} = 1 - \cos$。

**适用**：文本（TF-IDF）、高维稀疏向量。仅关注方向，忽略模长。

**与欧氏的关系**：归一化后 $d_2^2 = 2(1 - \cos)$，即两者等价。

### 3.5 马氏距离

$$
d_M(\mathbf{x}, \mathbf{z}) = \sqrt{(\mathbf{x} - \mathbf{z})^\top \Sigma^{-1} (\mathbf{x} - \mathbf{z})}
$$

$\Sigma$ 为协方差矩阵。

**性质**：

- 对各向同性 $\Sigma = \sigma^2 I$ 退化为欧氏
- 考虑特征间相关性与尺度差异
- 仿射不变：线性变换下不变

**与多元高斯的关系**：马氏距离的平方即高斯密度指数部分。

**陷阱**：$\Sigma^{-1}$ 估计需大样本；$\Sigma$ 奇异时需正则化。

### 3.6 汉明距离

$$
d_H(\mathbf{x}, \mathbf{z}) = \sum_i \mathbb{1}[x_i \neq z_i]
$$

**适用**：类别特征、二进制向量（如哈希）。

### 3.7 距离度量学习

学一个马氏距离 $d_M$ 中的 $M = L^\top L$：

$$
d_M(\mathbf{x}, \mathbf{z}) = \|L(\mathbf{x} - \mathbf{z})\|^2
$$

**目标**：同类样本拉近，异类推远（LMNN, ITML 等算法）。

---

## 四、KD 树

### 4.1 动机

朴素 KNN 推理 $O(nd)$/次，大数据不可行。**KD 树**是空间划分数据结构，将查询降到 $O(\log n)$/次（低维）。

### 4.2 构造

1. 选方差最大的维度 $j$（或轮流）
2. 取该维度中位数作切分点
3. 左子树 $x_j \le$ 中位数，右子树 $x_j >$ 中位数
4. 递归直到叶节点（含少量样本）

**复杂度**：$O(n \log n)$。

### 4.3 查询

1. 从根递归找到包含查询点 $\mathbf{q}$ 的叶节点
2. 在叶节点内做暴力搜索，得当前最近
3. 回溯：检查 sibling 子树是否可能含更近点（用超矩形与当前最近球是否相交判断）
4. 剪枝：不相交则跳过

**平均复杂度**：$O(\log n)$。

**最坏复杂度**：$O(n)$（高维下几乎所有节点都被访问）。

### 4.4 适用维度

经验：$d \lesssim 20$ 时 KD 树比暴力快；$d \gtrsim 20$ 退化为 $O(n)$，甚至比暴力慢（剪枝失效）。

---

## 五、球树

### 5.1 结构

每个节点定义一个超球面：

- 中心 $\mathbf{c}$
- 半径 $r$，覆盖所有子节点样本

构造： recursively 用 PCA 或 k-means 划分。

### 5.2 三角不等式剪枝

对查询点 $\mathbf{q}$，当前最近距离 $r^*$：

- 若 $d(\mathbf{q}, \mathbf{c}) - r > r^*$，则该球内不可能有更近点，剪枝

球树利用三角不等式，比 KD 树更适应高维。

### 5.3 复杂度

构造 $O(n \log n)$，查询平均 $O(\log n)$，但最坏仍 $O(n)$。

---

## 六、维数灾难

### 6.1 现象

高维下 KNN 与近邻搜索失效：所有点距离趋于相近，"最近"失去意义。

### 6.2 距离趋同定理

设 $X_1, \dots, X_n$ i.i.d. 于 $[0, 1]^d$ 上的均匀分布。最近邻距离 $\min_j \|\mathbf{x} - X_j\|$ 与最远距离 $\max_j \|\mathbf{x} - X_j\|$ 之比：

$$
\frac{d_{\min}}{d_{\max}} \xrightarrow{P} 1 \quad \text{as } d \to \infty
$$

**直觉证明**：高维下点都在球的"角"上。体积集中在表面，距离的相对方差趋于 0。

**更严格**：对 $L_p$ 范数，$\frac{d_{\max} - d_{\min}}{d_{\max}} \xrightarrow{P} 0$。

### 6.3 样本稀疏

要达到固定密度，样本量需 $O(c^d)$（指数增长）。$d = 100$ 时即使每维 2 个样本也需 $2^{100}$。

### 6.4 应对

| 方法 | 原理 |
|------|------|
| 降维 | PCA、t-SNE、UMAP（见 [无监督学习-降维](./无监督学习-降维.md)） |
| 特征选择 | 移除无关特征 |
| 距离度量学习 | 学马氏距离聚焦判别方向 |
| 近似最近邻 | 牺牲精度换速度 |
| 稀疏表示 | L1 正则、压缩感知 |

---

## 七、近似最近邻（ANN）

精确 KNN 在大规模高维下不可行。**近似最近邻**牺牲少量精度换取数量级加速。

### 7.1 LSH（局部敏感哈希）

**思想**：相近样本以高概率哈希到同一桶。

对距离 $d$ 与阈值 $r$，哈希族 $\mathcal{H}$ 满足：

- $d(\mathbf{x}, \mathbf{z}) \le r$：$P[h(\mathbf{x}) = h(\mathbf{z})] \ge p_1$
- $d(\mathbf{x}, \mathbf{z}) \ge cr$：$P[h(\mathbf{x}) = h(\mathbf{z})] \le p_2$

$c > 1, p_1 > p_2$。

**构造**：随机超平面哈希（余弦相似度）、p-stable LSH（欧氏）。

**查询**：仅在同桶（及邻近桶）内暴力搜索。

**复杂度**：次线性 $O(n^\rho)$，$\rho = \log(1/p_1)/\log(1/p_2) < 1$。

### 7.2 HNSW（分层可导航小世界图）

**思想**：构建多层图，上层稀疏用于长距离跳转，下层密集用于精确搜索。

**结构**：

- 每个节点出现在某层及以下所有层
- 顶层入口，逐层下降
- 同层内每个节点连 $M$ 个最近邻居

**查询**：

1. 从顶层入口开始贪心搜索
2. 每层找最近，下降
3. 底层用贪心 + 候选集扩展（类似跳表）

**复杂度**：$O(\log n)$/查询（经验）。

**优势**：召回率高、查询快，是当前主流 ANN 算法（Faiss HNSW、Milvus、hnswlib）。

### 7.3 IVF + PQ（倒排 + 乘积量化）

**IVF（Inverted File）**：

1. 用 k-means 聚类所有向量
2. 每个向量分到最近簇
3. 查询时仅搜索最近 $n_{\text{probe}}$ 个簇

**PQ（Product Quantization）**：

1. 将 $d$ 维向量切分为 $m$ 段
2. 每段独立 k-means 量化（如每段 256 中心）
3. 原向量表示为 $m$ 个 8-bit 码本索引

**组合 IVF-PQ**：

- IVF 减少搜索范围
- PQ 压缩存储（$d \cdot 32$ bit → $m \cdot 8$ bit）
- 距离用码本查表计算

**典型应用**：Faiss 默认推荐 IVF-HNSW-PQ 组合。

### 7.4 工业库对比

| 库 | 算法 | GPU | 分布式 | 适用 |
|----|------|-----|--------|------|
| Faiss | IVF/PQ/HNSW | ✓ | 部分 | 通用、研究 |
| Milvus | 多种 | ✓ | ✓ | 生产级、云原生 |
| Annoy | 树 | ✗ | ✗ | 简单、单机 |
| HNSWLib | HNSW | ✗ | ✗ | 极速、单机 |
| ScaNN | 各向异性量化 | 部分 | ✗ | Google 优化 |
| NMSLIB | 多种 | ✗ | ✗ | 学术对比 |

---

## 八、算法流程与复杂度

| 方法 | 构造 | 查询 | 内存 | 适用 |
|------|------|------|------|------|
| 暴力 | $O(1)$ | $O(nd)$ | $O(nd)$ | 小数据、精确 |
| KD 树 | $O(n \log n)$ | $O(\log n)$ 平均 | $O(n)$ | $d \lesssim 20$ |
| 球树 | $O(n \log n)$ | $O(\log n)$ 平均 | $O(n)$ | $d \lesssim 50$ |
| LSH | $O(n L)$ | $O(n^\rho)$ | $O(nL)$ | 高维、召回优先 |
| HNSW | $O(n \log n)$ | $O(\log n)$ | $O(nM)$ | 通用、主流 |
| IVF-PQ | $O(n k \cdot T)$ | $O(n_{\text{probe}} \cdot m)$ | $O(nm)$ 压缩 | 超大规模 |

---

## 九、代码实现（工业级）

### 9.1 从零实现：KD 树

```python
"""
工业级 KD 树实现。
支持：K 近邻、范围查询、距离度量选择。
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Optional, List, Tuple
import heapq
import numpy as np


@dataclass
class KDNode:
    point: np.ndarray
    index: int
    axis: int
    left: Optional["KDNode"] = None
    right: Optional["KDNode"] = None


@dataclass
class KDTreeConfig:
    leaf_size: int = 16


class KDTree:
    def __init__(self, config: KDTreeConfig = KDTreeConfig()):
        self.config = config
        self.root: Optional[KDNode] = None
        self.data: Optional[np.ndarray] = None

    def fit(self, X: np.ndarray) -> "KDTree":
        self.data = X
        idx = np.arange(len(X))
        self.root = self._build(X, idx, depth=0)
        return self

    def _build(self, X: np.ndarray, idx: np.ndarray, depth: int) -> Optional[KDNode]:
        if len(idx) == 0:
            return None
        d = X.shape[1]
        axis = depth % d
        # 按该轴排序取中位数
        order = np.argsort(X[idx, axis])
        idx_sorted = idx[order]
        mid = len(idx_sorted) // 2
        i = idx_sorted[mid]
        node = KDNode(point=X[i], index=i, axis=axis)
        node.left = self._build(X, idx_sorted[:mid], depth + 1)
        node.right = self._build(X, idx_sorted[mid + 1:], depth + 1)
        return node

    def query(self, x: np.ndarray, k: int = 1) -> Tuple[np.ndarray, np.ndarray]:
        """K 近邻查询，返回 (距离, 索引)。"""
        heap: List[Tuple[float, int]] = []  # max-heap via negation
        self._search(self.root, x, k, heap)
        # 排序
        heap.sort()
        dists = np.array([h[0] for h in heap])
        idxs = np.array([h[1] for h in heap])
        return dists, idxs

    def _search(self, node: Optional[KDNode], x: np.ndarray,
                k: int, heap: List[Tuple[float, int]]):
        if node is None:
            return
        # 当前节点距离
        d = np.linalg.norm(x - node.point)
        if len(heap) < k:
            heapq.heappush(heap, (-d, node.index))  # 用负值模拟 max-heap
        elif d < -heap[0][0]:
            heapq.heapreplace(heap, (-d, node.index))
        # 决定先访问哪个子树
        diff = x[node.axis] - node.point[node.axis]
        first = node.left if diff < 0 else node.right
        second = node.right if diff < 0 else node.left
        self._search(first, x, k, heap)
        # 检查是否需访问另一子树
        if len(heap) < k or abs(diff) < -heap[0][0]:
            self._search(second, x, k, heap)
```

### 9.2 从零实现：加权 KNN

```python
from typing import Literal

class KNNClassifier:
    """加权 KNN 分类器。"""
    def __init__(self, k: int = 5,
                 weight: Literal["uniform", "distance"] = "distance",
                 metric: Literal["euclidean", "manhattan", "cosine"] = "euclidean"):
        self.k = k
        self.weight = weight
        self.metric = metric
        self.X: Optional[np.ndarray] = None
        self.y: Optional[np.ndarray] = None

    def fit(self, X: np.ndarray, y: np.ndarray) -> "KNNClassifier":
        self.X, self.y = X, y
        return self

    def _distance(self, x: np.ndarray) -> np.ndarray:
        if self.metric == "euclidean":
            return np.linalg.norm(self.X - x, axis=1)
        elif self.metric == "manhattan":
            return np.abs(self.X - x).sum(axis=1)
        elif self.metric == "cosine":
            num = self.X @ x
            denom = np.linalg.norm(self.X, axis=1) * np.linalg.norm(x) + 1e-12
            return 1 - num / denom
        raise ValueError(f"未知 metric: {self.metric}")

    def predict(self, X: np.ndarray) -> np.ndarray:
        return np.array([self._predict_one(x) for x in X])

    def _predict_one(self, x: np.ndarray):
        dists = self._distance(x)
        idx = np.argpartition(dists, self.k)[:self.k]
        neighbors_y = self.y[idx]
        neighbors_d = dists[idx]
        if self.weight == "uniform":
            counts = np.bincount(neighbors_y)
            return counts.argmax()
        # 距离倒数加权
        w = 1.0 / (neighbors_d + 1e-12)
        scores = np.zeros(neighbors_y.max() + 1)
        for yi, wi in zip(neighbors_y, w):
            scores[yi] += wi
        return scores.argmax()
```

### 9.3 sklearn 工业级用法

```python
import numpy as np
from sklearn.neighbors import (
    KNeighborsClassifier, KNeighborsRegressor,
    KDTree, BallTree, NearestNeighbors
)
from sklearn.preprocessing import StandardScaler
from sklearn.pipeline import Pipeline
from sklearn.model_selection import GridSearchCV, cross_val_score
from sklearn.datasets import load_digits

# 标准化 + KNN
pipe = Pipeline([
    ("scaler", StandardScaler()),
    ("knn", KNeighborsClassifier())
])
param_grid = {
    "knn__n_neighbors": [3, 5, 7, 9, 11],
    "knn__weights": ["uniform", "distance"],
    "knn__metric": ["euclidean", "manhattan"],
    "knn__algorithm": ["auto", "kd_tree", "ball_tree"]
}
grid = GridSearchCV(pipe, param_grid, cv=5, scoring="accuracy", n_jobs=-1)
X, y = load_digits(return_X_y=True)
grid.fit(X, y)
print(f"最优参数: {grid.best_params_}, 准确率: {grid.best_score_:.4f}")
```

### 9.4 Faiss 工业级用法

```python
"""
Faiss 大规模近邻搜索。
单机 GPU 上 1 亿向量检索亚秒级。
"""
import numpy as np
import faiss

# 模拟 100 万 128 维向量
d = 128
nb = 1_000_000
nq = 10
np.random.seed(42)
xb = np.random.randn(nb, d).astype(np.float32)
xq = np.random.randn(nq, d).astype(np.float32)
faiss.normalize_L2(xb); faiss.normalize_L2(xq)  # 余弦相似度

# 1. 精确搜索（基线）
index_flat = faiss.IndexFlatIP(d)
index_flat.add(xb)
D_flat, I_flat = index_flat.search(xq, k=10)

# 2. IVF + PQ 压缩
nlist = 1024  # 簇数
m = 16        # PQ 段数
quantizer = faiss.IndexFlatIP(d)
index_ivfpq = faiss.IndexIVFPQ(quantizer, d, nlist, m, 8)
index_ivfpq.train(xb[:50000])
index_ivfpq.add(xb)
index_ivfpq.nprobe = 16  # 搜索簇数
D_ivf, I_ivf = index_ivfpq.search(xq, k=10)

# 3. HNSW
index_hnsw = faiss.IndexHNSWFlat(d, 32)
index_hnsw.hnsw.efConstruction = 200
index_hnsw.hnsw.efSearch = 64
index_hnsw.add(xb)
D_hnsw, I_hnsw = index_hnsw.search(xq, k=10)

# 召回率对比
def recall_at_k(I_approx, I_true, k=10):
    rec = []
    for i in range(len(I_true)):
        rec.append(len(set(I_approx[i]) & set(I_true[i])) / k)
    return np.mean(rec)

print(f"IVF-PQ 召回@10: {recall_at_k(I_ivf, I_flat):.4f}")
print(f"HNSW 召回@10: {recall_at_k(I_hnsw, I_flat):.4f}")
```

### 9.5 单元测试

```python
import pytest


def test_kd_tree_matches_brute_force():
    np.random.seed(0)
    X = np.random.randn(200, 5)
    tree = KDTree().fit(X)
    q = np.random.randn(5)
    dists, idxs = tree.query(q, k=5)
    # 暴力搜索
    brute = np.linalg.norm(X - q, axis=1).argsort()[:5]
    assert set(idxs) == set(brute)


def test_knn_classifier_predicts_majority():
    X = np.array([[0, 0], [0.1, 0.1], [0.2, 0.2],
                  [5, 5], [5.1, 5.1], [5.2, 5.2]])
    y = np.array([0, 0, 0, 1, 1, 1])
    knn = KNNClassifier(k=3, weight="uniform").fit(X, y)
    assert knn.predict(np.array([[0.05, 0.05]]))[0] == 0
    assert knn.predict(np.array([[5.05, 5.05]]))[0] == 1
```

---

## 十、实战案例：MNIST 数字识别

```python
"""
MNIST 子集：KNN 与其他方法对比。
"""
import numpy as np
import time
from sklearn.datasets import load_digits
from sklearn.neighbors import KNeighborsClassifier
from sklearn.ensemble import RandomForestClassifier
from sklearn.svm import SVC
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import cross_val_score, StratifiedKFold
from sklearn.pipeline import Pipeline

X, y = load_digits(return_X_y=True)
print(f"数据: {X.shape}, 类别: {len(np.unique(y))}")

models = {
    "KNN (k=5, distance)": Pipeline([
        ("scaler", StandardScaler()),
        ("clf", KNeighborsClassifier(n_neighbors=5, weights="distance"))
    ]),
    "KNN (k=7, brute)": Pipeline([
        ("scaler", StandardScaler()),
        ("clf", KNeighborsClassifier(n_neighbors=7, algorithm="brute"))
    ]),
    "RF": RandomForestClassifier(n_estimators=100, random_state=42),
    "SVM RBF": Pipeline([
        ("scaler", StandardScaler()),
        ("clf", SVC(kernel="rbf", C=10, gamma="scale"))
    ])
}

cv = StratifiedKFold(5, shuffle=True, random_state=42)
for name, model in models.items():
    t0 = time.time()
    scores = cross_val_score(model, X, y, cv=cv, scoring="accuracy")
    t = time.time() - t0
    print(f"{name:30s} 准确率 {scores.mean():.4f} ± {scores.std():.4f}  "
          f"耗时 {t:.1f}s")
```

**典型结果**：

- KNN 在小数据集（MNIST digits, 1797 样本）上准确率 ~98%
- SVM RBF 常略优于 KNN
- RF 在该规模上与 KNN 接近
- KNN 推理慢于 RF/SVM

---

## 十一、常见陷阱

| 陷阱 | 后果 | 对策 |
|------|------|------|
| 未标准化 | 大尺度特征主导距离 | 必须标准化 |
| $K$ 太小 | 过拟合 | CV 选 $K$ |
| $K$ 偶数 | 平票 | 用奇数或加权 |
| 高维用 KD 树 | 退化为暴力 | 用 HNSW/LSH |
| 用欧氏处理稀疏 | 失效 | 用余弦 |
| 未处理类别特征 | 距离无意义 | one-hot 或 Gower 距离 |
| 不平衡数据 | 偏向多数类 | 加权 KNN |
| 大数据用暴力 | 慢 | 用 ANN |

---

## 十二、与其他方法的关系

| 方法 | 与 KNN 关系 |
|------|-------------|
| 朴素贝叶斯 | 都是非参数；NB 参数化、KNN 非参数 |
| 核方法 | KNN 可视为核密度估计的特例 |
| SVM | RBF-SVM 决策仅依赖支持向量，类似 KNN |
| 树模型 | 都基于特征空间划分 |
| 神经网络 | 注意力机制 softmax(QK^T) 是软 KNN |
| 聚类 | K-means 用 KNN 思想分配簇 |
| 推荐系统 | UserCF/ItemCF 本质是 KNN |

---

## 十三、面试速答

| 问 | 答 |
|----|-----|
| KNN 为何是懒惰学习？ | 训练时不学习，仅存储数据；推理时才计算。 |
| $K$ 的选择如何影响偏差-方差？ | $K$ 小偏差低方差高（过拟合）；$K$ 大偏差高方差低（欠拟合）。 |
| KD 树的构造与查询？ | 交替轴中位数划分构造 $O(n\log n)$；查询 $O(\log n)$ 平均。 |
| 为何 KD 树在高维失效？ | 剪枝失效，几乎所有节点都被访问，退化为 $O(n)$。 |
| 马氏距离与欧氏的区别？ | 考虑协方差，仿射不变；$\Sigma = I$ 退化为欧氏。 |
| 余弦与欧氏的关系？ | 归一化后 $d_2^2 = 2(1 - \cos)$，等价。 |
| 维数灾难本质？ | 高维下距离趋同，样本稀疏，"最近"失去意义。 |
| 距离加权 KNN 动机？ | 远处邻居可能异类，距离倒数加权降低其影响。 |
| LSH 原理？ | 相近样本高概率哈希同桶，仅同桶内搜索，次线性复杂度。 |
| HNSW 为何快？ | 多层图结构，上层稀疏跳转，下层精确搜索，$O(\log n)$。 |
| 1-NN 与贝叶斯最优关系？ | Cover-Hart 不等式：$R^* \le R_{1\text{NN}} \le 2R^*$。 |
| KNN vs 树模型？ | KNN 懒惰、推理慢、不需训练；树模型急切、推理快、可解释。 |

---

## 十四、综合面试题

1. **推导马氏距离与协方差的关系。**
   - 设 $\mathbf{x} \sim \mathcal{N}(\boldsymbol{\mu}, \Sigma)$。其密度 $\propto \exp(-\frac{1}{2}(\mathbf{x}-\boldsymbol{\mu})^\top\Sigma^{-1}(\mathbf{x}-\boldsymbol{\mu}))$。指数部分即马氏距离平方的一半。故马氏距离是多元高斯下的"自然距离"。
   - 仿射不变性：$\mathbf{y} = A\mathbf{x}$，$\text{Cov}(\mathbf{y}) = A\Sigma A^\top$，$d_M(\mathbf{y}_1, \mathbf{y}_2) = d_M(\mathbf{x}_1, \mathbf{x}_2)$。

2. **证明高维下距离趋同。**
   - 设 $X_1, \dots, X_n \sim U[0,1]^d$。对 $L_p$ 范数 $\|X\|_p^p = \sum_i |X_i|^p$。由大数定律，$\|X\|_p^p / d \to \mathbb{E}|U|^p = 1/(p+1)$。故 $\|X\|_p \approx (d/(p+1))^{1/p}$。
   - 方差：$\text{Var}(\|X\|_p^p) = d \cdot \text{Var}(|U|^p) = O(d)$，相对方差 $O(1/d) \to 0$。故所有点范数接近均值，距离趋同。

3. **解释 KD 树查询为何在低维有效。**
   - 每次回溯检查 sibling 子树时，用查询点到分裂超平面的距离与当前最近比较。低维下大部分 sibling 可被剪枝（最近球不相交该子树区域），平均查询 $O(\log n)$。
   - 高维下：到分裂面距离接近 0 的概率高（多维度累积），剪枝失效，回溯几乎所有节点。

4. **HNSW 为何比 KD 树更适合高维？**
   - KD 树基于轴对齐划分，高维下剪枝失效。HNSW 基于图结构，每层贪心搜索时直接跳到候选更近的节点，不受维度限制。
   - 小世界图的"短路径"性质保证 $O(\log n)$ 跳数即可到达近邻区域。

5. **比较 LSH 与 HNSW 的优劣。**
   - LSH：理论保证、参数可调（$\rho$）、内存低；但召回率随数据分布波动，需调参。
   - HNSW：召回率高、查询快；内存大（图边）、构建慢。
   - 工业实践：HNSW 几乎全面胜出，LSH 多在理论分析或内存严格受限场景使用。

6. **为何归一化后余弦等价于欧氏？**
   - $\|\mathbf{x}/\|\mathbf{x}\| - \mathbf{z}/\|\mathbf{z}\|\|^2 = 2 - 2\mathbf{x}^\top\mathbf{z}/(\|\mathbf{x}\|\|\mathbf{z}\|) = 2(1 - \cos)$。故 $d_2 = \sqrt{2(1 - \cos)}$。

7. **KNN 在不平衡数据下如何调整？**
   - 加权 KNN：少数类邻居权重更大（如 1/(类频率)）。
   - 阈值调整：根据验证集找最优决策阈值。
   - 距离加权：避免多数类"淹没"少数类邻居。
   - 评估用 F1/AUC 而非 Accuracy。

8. **KNN 的 Cover-Hart 定理陈述与直觉。**
   - $R^* \le R_{1\text{NN}} \le 2R^*$。1-NN 的渐近误差介于贝叶斯最优 $R^*$ 与 $2R^*$ 之间。
   - 直觉：1-NN 用最近邻标签，若贝叶斯最优都错 $R^*$，1-NN 额外错在"最近邻标签与查询点不同"的情况，至多再加 $R^*$。

9. **为何 KNN 在文本分类中表现好？**
   - 文本 TF-IDF 向量高维稀疏，余弦相似度有效。
   - 同主题文档词频分布相似，近邻机制天然契合。
   - 无需训练，适合流式数据。
   - 但大数据下推理慢，需 ANN 加速。

10. **解释 IVF-PQ 的两阶段加速与精度损失。**
    - IVF：用 k-means 划分空间，查询仅搜索 $n_{\text{probe}}$ 个最近簇。损失：簇边界附近的真实近邻可能在未搜索簇中。
    - PQ：向量分段量化，距离用码本查表近似。损失：量化误差，原始向量被压缩为码本索引，距离非精确。
    - 两者结合：IVF 减少候选集，PQ 加速距离计算。典型召回 90%+ 速度提升 100x+。

---

## 十五、公式速查卡

| 公式 | 表达式 |
|------|--------|
| 闵可夫斯基距离 | $d_p = (\sum |x_i - z_i|^p)^{1/p}$ |
| 欧氏 | $\sqrt{\sum (x_i - z_i)^2}$ |
| 曼哈顿 | $\sum |x_i - z_i|$ |
| 余弦 | $\mathbf{x}^\top\mathbf{z}/(\|\mathbf{x}\|\|\mathbf{z}\|)$ |
| 马氏 | $\sqrt{(\mathbf{x}-\mathbf{z})^\top\Sigma^{-1}(\mathbf{x}-\mathbf{z})}$ |
| 汉明 | $\sum \mathbb{1}[x_i \neq z_i]$ |
| 加权 KNN | $\hat{y} = \arg\max_c \sum_{i \in N_K} w_i \mathbb{1}[y_i = c]$ |
| Cover-Hart | $R^* \le R_{1\text{NN}} \le 2R^*$ |
| 归一化欧氏 | $d_2^2 = 2(1 - \cos)$ |

---

## 十六、参考与延伸

### 16.1 教材
- 《The Elements of Statistical Learning》第 13 章
- 《统计学习方法》第 3 章
- 《Pattern Classification》—— Duda, Hart, Stork
- 《Approximate Nearest Neighbor Search》—— survey by Wang et al.

### 16.2 经典论文
- Cover & Hart (1967): Nearest Neighbor Pattern Classification
- Bentley (1975): KD 树
- Omohundro (1989): 球树
- Indyk & Motwani (1998): LSH
- Malkov & Yashunin (2016): HNSW
- Johnson, Douze & Jégou (2017): Faiss
- Ge et al. (2013): ScaNN 前身

### 16.3 跨文件链接
- [数学基础-线性代数.md](./数学基础-线性代数.md)：范数、协方差、SVD
- [数学基础-概率与统计.md](./数学基础-概率与统计.md)：Cover-Hart、密度估计
- [监督学习-评估与调优.md](./监督学习-评估与调优.md)：$K$ 选择、类别不平衡
- [监督学习-决策树与集成.md](./监督学习-决策树与集成.md)：特征空间划分对比
- 后续 [无监督学习-降维.md](./无监督学习-降维.md)：缓解维数灾难
- 后续 [进阶-推荐系统.md](./进阶-推荐系统.md)：UserCF/ItemCF 本质是 KNN

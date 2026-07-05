# 深度学习 —— Transformer 与注意力

> Transformer(2017)是深度学习近 10 年最大革命。从注意力机制到 BERT/GPT,从 ViT 到 LLaMA,Transformer 统一了 NLP、CV、语音、多模态。本章覆盖自注意力原理、架构、预训练范式、推理优化。

---

## 〇、思维导图

```
                  Transformer
                       │
        ┌──────────────┼──────────────┐
        │              │              │
     注意力          架构           预训练
        │              │              │
   ┌────┴────┐    ┌────┴────┐    ┌────┴────┐
   │         │    │         │    │         │
  缩放点积  多头  Encoder  Decoder BERT    GPT
  注意力    注意力  6 层堆叠  6 层堆叠  MLM     自回归
   │        │     │         │    │       │
  Q/K/V   并行   Self-Attn  Masked  NSP   T5
  缩放    多视角  + FFN     Self-Attn  Decoder-only
  √dk    多表达  + Residual  + Cross   PaLM
         + Norm  + LN     Attn     LLaMA
                                   │
                                推理优化
                                   │
                                KV Cache
                                FlashAttn
                                MQA/GQA
                                投机解码
```

---

## 一、问题定义

### 1.1 RNN 的痛点

- **串行计算**:时间步依赖,无法并行,GPU 利用率低
- **长程依赖**:即使 LSTM,超长序列仍困难
- **固定上下文**:Seq2Seq 用最后隐状态表示全部信息,信息瓶颈

### 1.2 注意力的核心思想

不压缩到固定向量,而是**动态**对每个输出位置,关注输入的不同位置。每个位置都可直接访问所有其他位置——长程依赖的"高速公路"。

### 1.3 Transformer 的革命

Vaswani et al. 2017 "Attention Is All You Need":
- 完全抛弃 RNN,纯注意力 + MLP
- 完全并行,GPU 友好
- 任意两位置直接交互,O(1) 路径长度
- 成为大模型统一架构

### 1.4 应用场景

- NLP:翻译、摘要、问答、对话、代码生成
- CV:ViT、DETR、Sora
- 语音:Whisper
- 多模态:CLIP、GPT-4V
- 科学:AlphaFold 2(蛋白质结构)
- 代码:GitHub Copilot

---

## 二、直觉解释

### 2.1 注意力的数据库类比

给定查询(Query)、键值对存储(Key-Value):
- Query 是"我想找什么"
- Key 是"数据库里每条记录的标签"
- Value 是"实际内容"
- 输出 = Value 的加权和,权重由 Q 与 K 相似度决定

$$
\text{Attention}(Q, K, V) = \text{softmax}\left(\frac{Q K^\top}{\sqrt{d_k}}\right) V
$$

### 2.2 自注意力的直觉

序列中每个位置同时是 Query、Key、Value 的来源。每个 token "看" 其他所有 token,加权聚合信息。比喻:开会时每个人听所有人,但根据相关性给不同人不同关注度。

### 2.3 多头的直觉

多个头并行注意,每个头学不同关系(语法、共指、语义)。类比:CNN 多通道学不同特征。

### 2.4 并行的直觉

RNN 一次只能看一个 token,Transformer 一次看完全序列——O(T) → O(1) 顺序依赖,完全并行。

---

## 三、形式化推导

### 3.1 缩放点积注意力 🎓

给定 $Q \in \mathbb{R}^{n \times d_k}$, $K \in \mathbb{R}^{m \times d_k}$, $V \in \mathbb{R}^{m \times d_v}$:

$$
\text{Attention}(Q, K, V) = \text{softmax}\left(\frac{Q K^\top}{\sqrt{d_k}}\right) V
$$

**步骤**:
1. $Q K^\top$:每对 Query-Key 内积,$\in \mathbb{R}^{n \times m}$
2. 除以 $\sqrt{d_k}$:缩放(下面解释)
3. softmax:每行归一化,得注意力权重
4. 乘 $V$:加权求和

### 3.2 为什么除以 √d_k 🎓

若 $Q, K$ 各维 i.i.d. 均值 0 方差 1,$QK^\top$ 的每个元素是 $d_k$ 个独立项之和,均值 0 方差 $d_k$。

当 $d_k$ 大(如 64),点积绝对值大,softmax 进入饱和区,梯度消失。

除以 $\sqrt{d_k}$ 把方差拉回 1,softmax 梯度健康。

### 3.3 多头注意力 🎓

把 $d_{\text{model}}$ 维向量分裂为 $h$ 个头,每头 $d_k = d_{\text{model}} / h$ 维,各做注意力,最后拼接:

$$
\begin{aligned}
\text{MultiHead}(Q, K, V) &= \text{Concat}(\text{head}_1, ..., \text{head}_h) W^O \\
\text{head}_i &= \text{Attention}(Q W_i^Q, K W_i^K, V W_i^V)
\end{aligned}
$$

**直觉**:每个头看不同子空间,学不同关系,拼接后等价于"多视角融合"。

### 3.4 自注意力复杂度 🎓

序列长 $T$,模型维度 $d$:

| 操作 | 时间复杂度 | 空间复杂度 |
|------|-----------|-----------|
| $Q K^\top$ | $O(T^2 d)$ | $O(T^2)$ |
| Attention × V | $O(T^2 d)$ | $O(T d)$ |
| 总注意力 | $O(T^2 d)$ | $O(T^2)$ |
| MLP | $O(T d^2)$ | $O(T d)$ |

**问题**:$T^2$ 长序列爆炸(64K 上下文需 16G 内存)。

**解决**:FlashAttention、稀疏注意力、线性注意力等。

### 3.5 位置编码 🎓

注意力本身是位置无关的(permutation invariant),需注入位置信息。

**正弦位置编码**(Vaswani 原版):

$$
PE_{(pos, 2i)} = \sin(pos / 10000^{2i/d}), \quad PE_{(pos, 2i+1)} = \cos(pos / 10000^{2i/d})
$$

性质:
- 每个位置有唯一编码
- 相对位置可由线性变换表示
- 可外推到训练未见的长度

**可学习位置编码**:每个位置一个可学习向量(BERT 用)
- 优点:灵活
- 缺点:不能外推

**相对位置编码**(Transformer-XL, T5):注意力计算时加入相对位置偏置

**RoPE**(Rotary Position Embedding, Su et al. 2021):
- 把 Q/K 向量视为复数,旋转角度 = 位置 × 频率
- 内积自动包含相对位置
- 长度外推性好
- LLaMA / GPT-NeoX 采用

**ALiBi**:注意力分数加距离线性偏置,外推极强

### 3.6 Transformer Encoder 块 🎓

```
input
  ↓
[Multi-Head Self-Attention]  ←┐
  ↓                            │
  + ←──── residual ───────────┘
  ↓
  LayerNorm
  ↓
[Feed-Forward: Linear → GELU → Linear]  ←┐
  ↓                                        │
  + ←──── residual ──────────────────────┘
  ↓
  LayerNorm
  ↓
output
```

**FFN**:

$$
\text{FFN}(\mathbf{x}) = \text{GELU}(\mathbf{x} W_1 + \mathbf{b}_1) W_2 + \mathbf{b}_2
$$

通常 $d_{ff} = 4 d_{model}$。

### 3.7 Transformer Decoder 块 🎓

比 Encoder 多一个 Cross-Attention:

```
target
  ↓
[Masked Self-Attention]   ← 看已生成部分
  ↓ + residual + LN
[Cross-Attention]         ← Q=tgt, K=V=encoder output
  ↓ + residual + LN
[FFN]                     ←┐
  ↓ + residual + LN        │
output                     │
```

**Masked Self-Attention**:把未来位置 mask 掉(设 -∞),保证自回归。

### 3.8 Pre-LN vs Post-LN 🎓

**Post-LN**(原版):$x' = \text{LN}(x + \text{Sublayer}(x))$
- 训练不稳定,需 warmup
- 性能稍好

**Pre-LN**(GPT-2 起主流):$x' = x + \text{Sublayer}(\text{LN}(x))$
- 训练稳定,无需 warmup
- 大模型默认

---

## 四、预训练范式

### 4.1 BERT(Encoder-only)

**Masked Language Model (MLM)**:随机 mask 15% token,预测被 mask 的

```
输入: The [MASK] sat on the [MASK] mat.
目标: cat        on
```

**Next Sentence Prediction (NSP)**:判断两句子是否相邻(后被 RoBERTa 证明无用)

**特点**:
- 双向注意力,看上下文
- 适合分类、问答、NER
- 不适合生成

**变种**:RoBERTa(更多数据,去掉 NSP)、ALBERT(参数共享)、DistilBERT(蒸馏)

### 4.2 GPT(Decoder-only)

**自回归语言模型**:给定前缀,预测下一 token

$$
P(\mathbf{w}) = \prod_t P(w_t | w_{<t})
$$

**特点**:
- 单向注意力(mask 未来)
- 适合生成
- GPT-3 起展现 in-context learning 能力

**演进**:
- GPT-1(2018):117M,预训练 + 微调
- GPT-2(2019):1.5B,zero-shot
- GPT-3(2020):175B,few-shot,提示工程
- GPT-4(2023):多模态,MoE
- GPT-4o(2024):原生多模态

### 4.3 T5(Encoder-Decoder)

**Text-to-Text 统一框架**:把所有任务转为"输入文本 → 输出文本"

```
翻译: "translate English to French: Hello" → "Bonjour"
摘要: "summarize: [文章]" → "[摘要]"
分类: "classify: [文本]" → "positive"
```

**优点**:统一接口,一个模型做所有任务。

### 4.4 现代大模型

- **PaLM/ PaLM 2**(Google):540B,MoE
- **LLaMA / LLaMA 2/3**(Meta):7B-70B,开源
- **Claude**(Anthropic):RLHF + Constitutional AI
- **GPT-4 / GPT-4o**(OpenAI):MoE,多模态
- **Gemini**(Google):原生多模态
- **Qwen / Baichuan / GLM**:中文大模型

### 4.5 预训练 vs 微调

| 阶段 | 数据 | 目标 |
|------|------|------|
| 预训练 | 海量无标注 | 通用语言能力 |
| SFT(监督微调) | 指令数据 | 跟随指令 |
| RLHF | 人类偏好 | 对齐 |
| LoRA/PEFT | 任务数据 | 高效微调 |

---

## 五、推理优化

### 5.1 KV Cache

自回归生成时,每步只多一个 token。前面 token 的 K/V 不变,缓存避免重复计算。

```
不缓存:每步重新算所有 K,V → O(T²)
缓存:  K,V 缓存,每步只算新 token → O(T)
```

显存:$O(T \cdot d \cdot L)$,$L$ 为层数。32K 上下文 70B 模型需 80GB+ KV cache。

### 5.2 MQA / GQA

**Multi-Query Attention**(MQA):所有 head 共享 K, V(只 Q 多头)
- KV cache 减少 $h$ 倍
- 性能略降

**Grouped-Query Attention**(GQA):折中,head 分组共享 K, V
- LLaMA 2 70B 用 GQA
- 性能接近 MHA,显存省

### 5.3 FlashAttention

**问题**:标准注意力 $O(T^2)$ 内存,大序列爆显存。

**方法**:分块计算,利用 GPU SRAM 层次,避免中间矩阵物化。

**效果**:2-4 倍加速,显存 $O(T)$,数学等价。

**版本**:FlashAttention v1/v2/v3

### 5.4 投机解码(Speculative Decoding)

用小模型快速生成 draft,大模型并行验证,接受匹配部分。

```
小模型: A B C D(4 步串行,快)
大模型: 并行验证 A B C D,接受前 3 个,重写第 4 个
```

加速 2-3 倍,数学上等价大模型推理。

### 5.5 其他优化

- **量化**:INT8 / INT4 / FP8
- **蒸馏**:大模型蒸馏小模型
- **PagedAttention**:vLLM 的分页 KV 管理,显存碎片
- **Continuous Batching**:动态拼接不同长度请求

---

## 六、PyTorch 实现

### 6.1 从零实现多头注意力

```python
import torch
import torch.nn as nn
import torch.nn.functional as F
import math

class MultiHeadAttention(nn.Module):
    def __init__(self, d_model, num_heads, dropout=0.1):
        super().__init__()
        assert d_model % num_heads == 0
        self.d_model = d_model
        self.num_heads = num_heads
        self.d_k = d_model // num_heads
        
        self.W_q = nn.Linear(d_model, d_model)
        self.W_k = nn.Linear(d_model, d_model)
        self.W_v = nn.Linear(d_model, d_model)
        self.W_o = nn.Linear(d_model, d_model)
        self.dropout = nn.Dropout(dropout)
    
    def forward(self, q, k, v, mask=None):
        """
        q, k, v: (batch, seq_len, d_model)
        mask: (batch, 1, seq_len, seq_len) 或 broadcast
        """
        B, T_q, _ = q.shape
        T_k = k.shape[1]
        
        # 线性投影 + 分头
        Q = self.W_q(q).view(B, T_q, self.num_heads, self.d_k).transpose(1, 2)  # (B, H, T_q, d_k)
        K = self.W_k(k).view(B, T_k, self.num_heads, self.d_k).transpose(1, 2)
        V = self.W_v(v).view(B, T_k, self.num_heads, self.d_k).transpose(1, 2)
        
        # 注意力分数
        scores = Q @ K.transpose(-2, -1) / math.sqrt(self.d_k)  # (B, H, T_q, T_k)
        
        if mask is not None:
            scores = scores.masked_fill(mask == 0, float('-inf'))
        
        attn = F.softmax(scores, dim=-1)
        attn = self.dropout(attn)
        
        # 加权求和
        out = attn @ V  # (B, H, T_q, d_k)
        
        # 合并头
        out = out.transpose(1, 2).contiguous().view(B, T_q, self.d_model)
        
        return self.W_o(out)

# 测试
mha = MultiHeadAttention(d_model=512, num_heads=8)
x = torch.randn(2, 10, 512)
out = mha(x, x, x)
print(out.shape)  # (2, 10, 512)
```

### 6.2 实现 Transformer 块

```python
class PositionalEncoding(nn.Module):
    def __init__(self, d_model, max_len=5000):
        super().__init__()
        pe = torch.zeros(max_len, d_model)
        position = torch.arange(0, max_len).unsqueeze(1).float()
        div_term = torch.exp(torch.arange(0, d_model, 2).float() * 
                             (-math.log(10000.0) / d_model))
        pe[:, 0::2] = torch.sin(position * div_term)
        pe[:, 1::2] = torch.cos(position * div_term)
        self.register_buffer('pe', pe.unsqueeze(0))  # (1, max_len, d_model)
    
    def forward(self, x):
        # x: (batch, seq_len, d_model)
        return x + self.pe[:, :x.size(1)]


class FeedForward(nn.Module):
    def __init__(self, d_model, d_ff=2048, dropout=0.1):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(d_model, d_ff),
            nn.GELU(),
            nn.Dropout(dropout),
            nn.Linear(d_ff, d_model),
        )
    
    def forward(self, x):
        return self.net(x)


class TransformerBlock(nn.Module):
    """Pre-LN Transformer block"""
    def __init__(self, d_model, num_heads, d_ff, dropout=0.1):
        super().__init__()
        self.ln1 = nn.LayerNorm(d_model)
        self.attn = MultiHeadAttention(d_model, num_heads, dropout)
        self.ln2 = nn.LayerNorm(d_model)
        self.ff = FeedForward(d_model, d_ff, dropout)
        self.dropout = nn.Dropout(dropout)
    
    def forward(self, x, mask=None):
        # Pre-LN: x + Sublayer(LN(x))
        x = x + self.dropout(self.attn(self.ln1(x), self.ln1(x), self.ln1(x), mask))
        x = x + self.dropout(self.ff(self.ln2(x)))
        return x


class TransformerEncoder(nn.Module):
    def __init__(self, vocab_size, d_model=512, num_heads=8, num_layers=6, 
                 d_ff=2048, max_len=512, dropout=0.1):
        super().__init__()
        self.token_emb = nn.Embedding(vocab_size, d_model)
        self.pos_emb = PositionalEncoding(d_model, max_len)
        self.blocks = nn.ModuleList([
            TransformerBlock(d_model, num_heads, d_ff, dropout) 
            for _ in range(num_layers)
        ])
        self.ln_final = nn.LayerNorm(d_model)
    
    def forward(self, x, mask=None):
        x = self.token_emb(x)
        x = self.pos_emb(x)
        for block in self.blocks:
            x = block(x, mask)
        return self.ln_final(x)


class GPT(nn.Module):
    """Decoder-only GPT(简化版)"""
    def __init__(self, vocab_size, d_model=512, num_heads=8, num_layers=6, 
                 d_ff=2048, max_len=512, dropout=0.1):
        super().__init__()
        self.token_emb = nn.Embedding(vocab_size, d_model)
        self.pos_emb = PositionalEncoding(d_model, max_len)
        self.blocks = nn.ModuleList([
            TransformerBlock(d_model, num_heads, d_ff, dropout) 
            for _ in range(num_layers)
        ])
        self.ln_final = nn.LayerNorm(d_model)
        self.lm_head = nn.Linear(d_model, vocab_size, bias=False)
        # weight tying
        self.lm_head.weight = self.token_emb.weight
    
    def forward(self, x):
        B, T = x.shape
        # 因果 mask
        mask = torch.tril(torch.ones(T, T, device=x.device)).unsqueeze(0).unsqueeze(0)
        
        x = self.token_emb(x)
        x = self.pos_emb(x)
        for block in self.blocks:
            x = block(x, mask)
        x = self.ln_final(x)
        logits = self.lm_head(x)  # (B, T, vocab)
        return logits

# 测试
model = GPT(vocab_size=10000)
x = torch.randint(0, 10000, (2, 32))
logits = model(x)
print(logits.shape)  # (2, 32, 10000)
print(f"Params: {sum(p.numel() for p in model.parameters()) / 1e6:.1f}M")
```

### 6.3 KV Cache 推理

```python
class CachedAttention(nn.Module):
    """带 KV cache 的注意力(推理用)"""
    def __init__(self, d_model, num_heads):
        super().__init__()
        self.d_model = d_model
        self.num_heads = num_heads
        self.d_k = d_model // num_heads
        self.W_q = nn.Linear(d_model, d_model)
        self.W_k = nn.Linear(d_model, d_model)
        self.W_v = nn.Linear(d_model, d_model)
        self.W_o = nn.Linear(d_model, d_model)
    
    def forward(self, x, kv_cache=None):
        """
        x: (batch, 1, d_model)  当前 token
        kv_cache: (K, V) 之前的 K,V 缓存
        """
        B, T, _ = x.shape
        Q = self.W_q(x).view(B, T, self.num_heads, self.d_k).transpose(1, 2)
        K_new = self.W_k(x).view(B, T, self.num_heads, self.d_k).transpose(1, 2)
        V_new = self.W_v(x).view(B, T, self.num_heads, self.d_k).transpose(1, 2)
        
        if kv_cache is not None:
            K_prev, V_prev = kv_cache
            K = torch.cat([K_prev, K_new], dim=2)  # 沿 seq 维拼接
            V = torch.cat([V_prev, V_new], dim=2)
        else:
            K, V = K_new, V_new
        
        # 更新 cache
        new_cache = (K, V)
        
        scores = Q @ K.transpose(-2, -1) / math.sqrt(self.d_k)
        attn = F.softmax(scores, dim=-1)
        out = attn @ V
        out = out.transpose(1, 2).contiguous().view(B, T, self.d_model)
        return self.W_o(out), new_cache


# 推理流程
class CachedGPT(nn.Module):
    def __init__(self, vocab_size, d_model=512, num_heads=8, num_layers=6):
        super().__init__()
        self.token_emb = nn.Embedding(vocab_size, d_model)
        self.pos_emb = PositionalEncoding(d_model, 2048)
        self.attns = nn.ModuleList([CachedAttention(d_model, num_heads) for _ in range(num_layers)])
        self.ffs = nn.ModuleList([FeedForward(d_model) for _ in range(num_layers)])
        self.lns = nn.ModuleList([nn.LayerNorm(d_model) for _ in range(num_layers * 2)])
        self.lm_head = nn.Linear(d_model, vocab_size, bias=False)
    
    def forward(self, x, caches=None):
        if caches is None:
            caches = [None] * len(self.attns)
        new_caches = []
        
        x = self.token_emb(x) + self.pos_emb.pe[:, x.size(1)-1:x.size(1)]
        
        for i, (attn, ff, ln1, ln2) in enumerate(zip(self.attns, self.ffs, 
                                                      self.lns[::2], self.lns[1::2])):
            attn_out, new_cache = attn(ln1(x), caches[i])
            x = x + attn_out
            x = x + ff(ln2(x))
            new_caches.append(new_cache)
        
        return self.lm_head(x), new_caches


# 生成
def generate(model, prompt, max_new_tokens=100):
    model.eval()
    with torch.no_grad():
        # 预填充
        x = prompt
        logits, caches = model(x)
        next_token = logits[:, -1].argmax(-1, keepdim=True)
        output = [next_token]
        
        # 逐 token 生成(用 KV cache)
        for _ in range(max_new_tokens - 1):
            logits, caches = model(next_token, caches)
            next_token = logits[:, -1].argmax(-1, keepdim=True)
            output.append(next_token)
        
        return torch.cat(output, dim=1)
```

### 6.4 用 HuggingFace Transformers

```python
from transformers import GPT2LMHeadModel, GPT2Tokenizer, Trainer, TrainingArguments
import torch

# 加载预训练 GPT-2
model = GPT2LMHeadModel.from_pretrained('gpt2')
tokenizer = GPT2Tokenizer.from_pretrained('gpt2')

# 生成
input_ids = tokenizer.encode("Hello, my name is", return_tensors='pt')
output = model.generate(input_ids, max_new_tokens=50, 
                        temperature=0.7, top_p=0.9, do_sample=True)
print(tokenizer.decode(output[0]))

# 微调
def tokenize_function(examples):
    return tokenizer(examples['text'], truncation=True, padding='max_length', max_length=128)

# dataset = ...
training_args = TrainingArguments(
    output_dir='./results',
    num_train_epochs=3,
    per_device_train_batch_size=4,
    save_steps=500,
    learning_rate=5e-5,
)
trainer = Trainer(model=model, args=training_args, train_dataset=dataset)
trainer.train()
```

---

## 七、常见陷阱与调参经验 ⚠️

### 7.1 训练陷阱

| 陷阱 | 表现 | 解决 |
|------|------|------|
| 没用 causal mask | 生成时看未来 | 三角 mask |
| 位置编码错位 | 长度外推差 | 用 RoPE 或 ALiBi |
| warmup 不够 | 训练发散 | 用 cosine + warmup |
| 初始化不当 | 训练不收敛 | 用 GPT-2 风格初始化 |
| Pre-LN vs Post-LN | Post-LN 难训 | 大模型用 Pre-LN |
| 显存爆炸 | 长序列 OOM | FlashAttention + 梯度检查点 |
| 梯度爆炸 | loss NaN | 梯度裁剪 1.0 |
| 数据未分桶 | 训练慢 | 按长度分桶,减少 padding |

### 7.2 调参经验

1. **学习率**:5e-5 ~ 5e-4,warmup 10% + cosine
2. **batch size**:32-512,大 batch + 大学习率
3. **梯度裁剪**:1.0
4. **dropout**:0.1,大模型 0
5. **层数**:6-12(小模型),70+(大模型)
6. **d_model**:512-12288
7. **head 数**:d_model/64
8. **激活**:GELU 或 SwiGLU(LLaMA)

### 7.3 大模型训练技巧

1. **混合精度**:BF16 / FP16,2x 加速
2. **梯度累积**:模拟大 batch
3. **梯度检查点**:用计算换显存
4. **ZeRO / FSDP**:分片参数 / 梯度 / 优化器
5. **数据并行 + 张量并行 + 流水线并行**:3D 并行
6. **MoE**:稀疏激活,7B 激活 70B 参数

---

## 八、与其他方法的关系

### 8.1 Transformer vs CNN vs RNN

| 维度 | RNN | CNN | Transformer |
|------|-----|-----|-------------|
| 并行 | 低 | 高 | 高 |
| 长依赖 | 中 | 低 | 高 |
| 复杂度 | O(T) | O(T) | O(T²) |
| 参数 | 中 | 中 | 多 |
| 数据需求 | 中 | 中 | 极大 |
| 归纳偏置 | 时间 | 局部+平移 | 几乎无 |

### 8.2 注意力的演化

- Bahdanau Attention(2014):RNN + 加性注意力
- Luong Attention(2015):乘性注意力
- Self-Attention(2017):同一序列内
- Multi-Head(2017):多视角
- Sparse Attention(2020):稀疏化省显存
- Linear Attention(2020):核方法,线性复杂度
- FlashAttention(2022):硬件优化
- MQA/GQA(2023):KV cache 优化

### 8.3 大模型缩放定律(Scaling Law)

Kaplan et al. 2020 / Chinchilla 2022:

- 损失 $L(N, D)$ 随参数 $N$ 和数据 $D$ 幂律下降
- **Chinchilla 最优**:数据 token 数 ≈ 20 × 参数数
- 70B 模型需 1.4T tokens 训练
- 计算预算增加 10 倍,参数增加 5.5 倍,数据增加 1.8 倍

---

## 九、面试速答 ⭐

- **缩放点积除 √d**:点积方差 $d_k$,softmax 饱和,缩放到方差 1
- **多头**:多视角,每个头学不同子空间关系
- **自注意力复杂度**:$O(T^2 d)$ 时间,$O(T^2)$ 空间
- **位置编码**:正弦(可外推)/ 可学习(灵活)/ RoPE(旋转,外推好)
- **BERT vs GPT**:BERT 双向 MLM 适合分类,GPT 单向自回归适合生成
- **T5**:Text-to-Text 统一所有任务
- **KV Cache**:缓存历史 K,V,自回归推理从 O(T²) 到 O(T)
- **MQA/GQA**:共享 K,V 减少 cache,性能近 MHA
- **FlashAttention**:分块计算,IO-aware,数学等价 2-4x 加速

---

## 十、综合面试题

1. [基础] 解释 Self-Attention 的 Q/K/V 计算流程。
2. [基础] 为什么注意力要除以 √d_k?
3. [基础] 多头注意力为什么有用?
4. [基础] BERT 和 GPT 的预训练目标有什么区别?
5. [中等] 推导自注意力的复杂度,说明长序列瓶颈。
6. [中等] 解释 RoPE 的原理与外推性优势。
7. [中等] Pre-LN 和 Post-LN 的区别,为什么大模型用 Pre-LN?
8. [中等] KV Cache 如何加速推理?显存占用如何计算?
9. [中等] MQA 和 GQA 解决什么问题?有什么代价?
10. [进阶] 解释 FlashAttention 的核心思想。
11. [进阶] 设计一个 100B 大模型训练方案(并行 / 优化器 / 学习率)。
12. [进阶] 投机解码为什么能加速?有什么条件?
13. [进阶] 解释 Scaling Law 与 Chinchilla 最优。

**答题要点(Q11)**:
1. 数据:1-2T tokens,预处理去重
2. 并行:3D 并行,数据并行 8 路 + 张量并行 8 路 + 流水线并行 4 路
3. 优化器:AdamW,β1=0.9, β2=0.95,wd=0.1
4. 学习率:peak 3e-4,warmup 2000 步,cosine 退火
5. batch:4M tokens,梯度累积
6. 精度:BF16,FlashAttention,梯度检查点
7. 监控:loss / grad norm / activation stats
8. 应急:checkpoint 频繁保存,故障恢复

---

## 十一、参考与延伸

### 经典论文
- Vaswani et al. (2017). *Attention Is All You Need*.
- Devlin et al. (2018). *BERT: Pre-training of Deep Bidirectional Transformers*.
- Radford et al. (2018/2019). *GPT / GPT-2*.
- Brown et al. (2020). *GPT-3: Language Models are Few-Shot Learners*.
- Raffel et al. (2019). *T5: Exploring the Limits of Transfer Learning*.
- Su et al. (2021). *RoFormer: RoPE*.
- Dao et al. (2022). *FlashAttention: Fast and Memory-Efficient Exact Attention*.
- Shazeer (2019). *Fast Transformer Decoding: One Write-Head is Enough*. (MQA)
- Ainslie et al. (2023). *GQA: Training Generalized Multi-Query Transformer Models*.
- Kaplan et al. (2020). *Scaling Laws for Neural Language Models*.
- Hoffmann et al. (2022). *Training Compute-Optimal Large Language Models*. (Chinchilla)
- Touvron et al. (2023). *LLaMA / LLaMA 2*.

### 跨文件链接
- 神经网络基础:[./深度学习-神经网络基础.md](./深度学习-神经网络基础.md)
- CNN:[./深度学习-卷积神经网络.md](./深度学习-卷积神经网络.md)
- RNN:[./深度学习-循环神经网络.md](./深度学习-循环神经网络.md]
- 优化与正则化:[./深度学习-优化与正则化.md](./深度学习-优化与正则化.md)
- 生成模型:[./深度学习-生成模型.md](./深度学习-生成模型.md)

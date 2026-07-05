# 计算 —— MapReduce 与 Spark

> 章号: §16.1
> 层级: 面试 / 原理 / 学术 / 工业
> 标记: ⭐高频 📜论文 🏭工业
> 前置: [[08-1-存储-GFS与HDFS]] [[04-复制]]

---

## 0. 分布式计算的演进

大数据处理框架演进:

1. **MapReduce** (Google 2004):简化分布式计算,但落盘多、慢
2. **Spark** (UC Berkeley 2010):内存计算,迭代快 100x
3. **Flink** (2014):真正的流批一体
4. **Ray / Dask**:分布式 Python 计算

本章分析 MapReduce 和 Spark,流处理详见 [[16-2-计算-Flink与流处理]]。

---

## 1. MapReduce

### 1.1 论文核心

> 📜 Dean & Ghemawat, 2004 OSDI — *MapReduce: Simplified Data Processing on Large Clusters*

思想:把分布式计算抽象为两个函数:

- **Map**:`(k1, v1) → list(k2, v2)`
- **Reduce**:`(k2, list(v2)) → list(k3, v3)`

框架处理:并行调度、容错、数据分布、错误恢复。

### 1.2 经典例子:WordCount

```python
def map(file_chunk):
    for line in file_chunk:
        for word in line.split():
            emit(word, 1)

def reduce(word, counts):
    emit(word, sum(counts))
```

### 1.3 执行流程

```
1. Input Split: 把输入文件切成 M 块(典型 64MB)
2. Map Phase: M 个 worker 并行处理 M 块,输出中间 (k2, v2)
3. Shuffle: 按 k2 分组,把同 key 数据发到同 R 个 reducer
4. Reduce Phase: R 个 worker 并行处理,输出最终结果
5. Output: R 个输出文件
```

### 1.4 容错

- Worker 故障:Master 检测心跳,重新调度该 task
- Master 故障:全任务失败(论文承认,但 Master 很少挂)
- 中间结果落盘:故障时从落盘数据恢复,不重跑 Map

### 1.5 MapReduce 的局限

- **落盘多**:Map → 中间文件 → Reduce → 输出,每次都落盘
- **迭代慢**:机器学习等迭代场景,每次迭代都重新读盘
- **抽象低**:开发者需写 Map/Reduce,不适合复杂逻辑
- **延迟高**:分钟级启动开销

---

## 2. Spark

### 2.1 核心创新:RDD

> 📜 Zaharia et al., 2012 NSDI — *Resilient Distributed Datasets*

**RDD (Resilient Distributed Dataset)**:不可变、分区、可并行计算的数据集。

特征:

- **不可变**:每次转换生成新 RDD
- **分区**:分布在多节点
- **血缘(Lineage)**:记录如何从父 RDD 计算
- **容错**:某分区丢失,按血缘重算
- **可缓存**:用户可指定 cache 到内存

### 2.2 算子

- **转换(Transformation)**:lazy,返回新 RDD
  - `map`, `filter`, `flatMap`, `groupByKey`, `reduceByKey`, `join`
- **行动(Action)**:触发计算,返回结果或写盘
  - `collect`, `count`, `save`, `take`

```python
# Spark WordCount
rdd = sc.textFile("hdfs:///input")
counts = (rdd.flatMap(lambda line: line.split())
            .map(lambda word: (word, 1))
            .reduceByKey(lambda a, b: a + b))
counts.saveAsTextFile("hdfs:///output")
```

### 2.3 执行模型

#### 2.3.1 DAG

Spark 把计算组织为 DAG(有向无环图):

```
textFile → flatMap → map → reduceByKey → save
                                       ↑
                                   shuffle
```

- 窄依赖(map/filter):父子分区一对一,可流水线
- 宽依赖(groupByKey/join):需 shuffle,划分 Stage

#### 2.3.2 Stage 划分

```
Stage 1:        Stage 2:        Stage 3:
textFile        shuffle         save
flatMap
map
reduceByKey
```

- 窄依赖在同一 Stage 内流水线
- 宽依赖划分 Stage 边界

#### 2.3.3 执行

- Driver 解析 DAG,生成 Stage
- 调度 Task 到 Executor
- Task 在 Executor 上执行
- shuffle 通过磁盘或网络交换数据

### 2.4 内存缓存

```python
rdd = sc.textFile("hdfs:///logs")
hot = rdd.filter(lambda l: "ERROR" in l).cache()  # 缓存到内存

# 多次使用,只算一次
errors = hot.count()
samples = hot.take(10)
```

- `cache()` / `persist(MEMORY_ONLY)` / `persist(MEMORY_AND_DISK)`
- 适合迭代算法(机器学习、图计算)

### 2.5 容错

- Task 失败:重试(默认 4 次)
- Executor 失败:重新调度该 Executor 上的 Task
- Stage 失败:重新计算(基于血缘)
- 缓存数据丢失:按血缘重算

### 2.6 Shuffle

Spark Shuffle 是性能关键:

- **Hash Shuffle**(早期):每 reducer 一个文件,文件数 M*R,小文件爆炸
- **Sort Shuffle**(默认):每 mapper 一个文件 + 索引,排序后按 reducer 取
- **Tungsten Sort**:堆外内存 + 优化的排序

```python
# 触发 shuffle 的算子
rdd.groupByKey()       # shuffle
rdd.reduceByKey(...)   # shuffle
rdd.join(other)        # shuffle (除非 broadcast join)
```

### 2.7 Spark SQL

```sql
-- Spark SQL
SELECT user_id, COUNT(*) AS cnt
FROM events
WHERE dt = '2024-01-01'
GROUP BY user_id
ORDER BY cnt DESC
LIMIT 100;
```

底层 Catalyst 优化器:逻辑计划 → 物理计划 → 代码生成。

### 2.8 Spark Structured Streaming

```python
# 实时流处理(微批)
stream = (spark.readStream
    .format("kafka")
    .option("kafka.bootstrap.servers", "kafka:9092")
    .option("subscribe", "events")
    .load())

result = (stream
    .groupBy(window("timestamp", "1 minute"), "event_type")
    .count())

(result.writeStream
    .format("console")
    .outputMode("complete")
    .start()
    .awaitTermination())
```

微批(Micro-batch):每隔 N 秒触发一次批处理,延迟秒级。不如 Flink 真流(毫秒级)。

---

## 3. 工业应用

### 3.1 MapReduce 现状

- 早期 Hadoop MR 已被 Spark 替代
- Google 内部仍用 MapReduce(但已演进为内部版本)
- 主要价值:历史概念,理解分布式计算基础

### 3.2 Spark 应用场景

- ETL:数据清洗、转换
- 机器学习:MLlib
- 图计算:GraphX
- 实时数仓:Structured Streaming
- 数据科学:PySpark / SparkR

### 3.3 Spark 性能优化

#### 3.3.1 数据倾斜

```python
# 倾斜:key "A" 数据量是其他 key 的 100x
# 解决:加盐打散
def salt(key):
    return key + "_" + random.randint(0, 9)

rdd.map(lambda k, v: (salt(k), v))    # 加盐
   .reduceByKey(...)
   .map(lambda k, v: (k.split("_")[0], v))  # 去盐
   .reduceByKey(...)                  # 二次聚合
```

#### 3.3.2 广播 Join

```python
# 小表 broadcast,避免 shuffle
big = spark.read.parquet("big_table")
small = spark.read.parquet("small_table")  # < 10MB

result = big.join(broadcast(small), "key")
```

#### 3.3.3 调参

- `spark.sql.shuffle.partitions`:shuffle 分区数(默认 200,大数据调大)
- `spark.executor.memory`:Executor 内存
- `spark.executor.cores`:Executor 核数(典型 4-5)
- `spark.sql.adaptive.enabled`:自适应执行(Spark 3+)

---

## 4. 面试要点

**Q1: MapReduce 的执行流程?**

> (1) Input Split:输入切 M 块;(2) Map:M 个 worker 并行处理,输出 (k2,v2);(3) Shuffle:按 k2 分组到 R 个 reducer;(4) Reduce:R 个 worker 并行处理;(5) Output:R 个输出文件。框架处理调度、容错、数据分布。

**Q2: MapReduce 的局限?Spark 怎么解决?**

> MR 局限:落盘多、迭代慢、抽象低。Spark 用 RDD:不可变、分区、血缘容错、可缓存。迭代场景 Spark 比 MR 快 100x(数据缓存到内存,不重复读盘)。

**Q3: RDD 的核心特性?**

> (1) 不可变:每次转换生成新 RDD;(2) 分区:分布多节点;(3) 血缘:记录如何从父 RDD 计算;(4) 容错:分区丢失按血缘重算;(5) 可缓存:cache/persist 到内存。

**Q4: Spark 的窄依赖和宽依赖?**

> 窄依赖:父子分区一对一(map/filter),可流水线,不 shuffle。宽依赖:父子分区多对多(groupByKey/join),需 shuffle,划分 Stage 边界。Stage 内流水线执行,Stage 间 shuffle。

**Q5: Spark Shuffle 怎么工作?**

> 早期 Hash Shuffle:每 mapper 每 reducer 一个文件,小文件爆炸。Sort Shuffle(默认):每 mapper 一个数据文件 + 索引,排序后按 reducer 取。Tungsten Sort:堆外内存优化。

**Q6: Spark 怎么处理数据倾斜?**

> (1) 加盐打散:倾斜 key 加随机后缀,二次聚合;(2) Broadcast Join:小表 broadcast 避免 shuffle;(3) 自适应执行(Spark 3 AQE):运行时检测倾斜并拆分;(4) 增大分区数:shuffle.partitions 调大。

**Q7: Spark 和 Flink 的区别?**

> 计算模型:Spark 微批(秒级延迟),Flink 真流(毫秒级)。容错:Spark RDD 血缘重算,Flink Checkpoint(Chandy-Lamport)。流处理语义:Spark At-least-once(默认)/Exactly-once,Flink Exactly-once 原生。批处理:Spark 强,Flink 也可但弱。

**Q8: Spark 的 Catalyst 优化器做什么?**

> 把 SQL/DataFrame 转为执行计划:(1) 逻辑计划分析(解析、绑定);(2) 逻辑优化(谓词下推、列裁剪、常量折叠);(3) 物理计划生成(选 join 策略等);(4) 代码生成(Whole-Stage CodeGen)。让 SQL 接近手写性能。

---

## 5. 论文延伸

| 论文 | 年份 | 关键贡献 |
|------|------|---------|
| Dean & Ghemawat, *MapReduce* | 2004 OSDI | 分布式计算抽象 |
| Zaharia et al., *Spark RDD* | 2012 NSDI | 内存计算 + 血缘容错 |
| Armbrust et al., *Spark SQL* | 2015 | Catalyst 优化器 |
| Apache Spark Docs | — | 工业实践 |

---

## 6. 交叉引用

- [[08-1-存储-GFS与HDFS]]:MapReduce 数据来源
- [[16-2-计算-Flink与流处理]]:对比 Flink
- [[04-复制]]:容错基础
- [[09-2-消息队列-Kafka深度]]:Structured Streaming 数据源

---

## 7. 速查表

```
MapReduce:
  Map (k1,v1) → list(k2,v2)
  Shuffle: by k2
  Reduce (k2, list(v2)) → list(k3,v3)
  局限: 落盘多,迭代慢

Spark:
  RDD: 不可变 + 分区 + 血缘 + 容错 + 可缓存
  算子: 转换 (lazy) + 行动 (触发)
  DAG: Stage 划分 (宽依赖为边界)
  Shuffle: Sort Shuffle (默认)
  
Spark vs MR:
  内存计算,迭代快 100x
  RDD 血缘容错,无需落盘

性能优化:
  数据倾斜: 加盐 / Broadcast Join / AQE
  shuffle.partitions: 200 (默认,大数据调大)
  executor.cores: 4-5 (典型)
```

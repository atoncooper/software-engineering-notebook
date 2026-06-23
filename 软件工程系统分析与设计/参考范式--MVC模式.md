## 参考范式 —— MVC 模式（Model-View-Controller）

> MVC 是表现层最经典的"三人分工"：**数据归 Model、展示归 View、调度归 Controller**。它诞生于 1970 年代桌面 GUI，被 Web 时代改造为"请求-响应"形态，又在前端 SPA 时代演化出 MVP / MVVM / Flux 等变体。理解 MVC 的关键是理解**它解决的是"用户界面复杂度"问题，不是"业务架构"问题**。

> 面试提示：MVC 几乎是面试必问，但考点集中在三个层次：① 三角色职责边界；② 经典 MVC 与 Web MVC 的区别；③ MVC 与分层架构、DDD、MVVM 的关系。能讲清"什么时候不该用 MVC"比会背定义更值钱。

---

### 一、定义

MVC 是一种**表现层**架构模式，将用户界面相关的代码拆分为三个相互协作的部件：

- **Model（模型）**：管理领域数据与业务规则，独立于 UI。
- **View（视图）**：负责把 Model 的数据渲染成用户可见的形式。
- **Controller（控制器）**：接收用户输入，调用 Model 完成业务，选择 View 进行响应。

三者关系可以用一句话概括：**Controller 是调度中枢，Model 是数据真相，View 是展现投影**。

```markdown
                 用户
                  │
          1. 输入 / 操作
                  ▼
           ┌─────────────┐
           │ Controller  │
           └──────┬──────┘
        2. 调用业务  │  4. 返回领域对象
                  ▼
           ┌─────────────┐
           │    Model    │
           │  (数据+规则) │
           └──────┬──────┘
        3. 通知变化  │  5. 查询数据
                  ▼
           ┌─────────────┐
           │     View    │  6. 渲染并回显给用户
           └─────────────┘
```

> 注意：MVC 是**表现层**模式，不要与分层架构（Presentation/Service/Repository）混淆。Controller 属于分层架构中的表现层，Model 在 Web 语境下通常对应领域对象或 DTO，业务逻辑仍应下沉到 Service。

---

### 二、三个角色详解

#### 1. Model（模型）

- 封装应用状态与业务规则。
- **不依赖 View 和 Controller**，可被多种 View 共享。
- 状态变化时通过观察者机制通知 View（经典 MVC）或被 Controller 主动拉取（Web MVC）。

#### 2. View（视图）

- 从 Model 取数据并渲染。
- 不包含业务逻辑，只做展示与格式化。
- 可订阅 Model 的变化以实现自动刷新（桌面 GUI 场景）。

#### 3. Controller（控制器）

- 解析用户输入（HTTP 参数、鼠标点击、键盘事件）。
- 调用 Model 完成业务。
- 选择并组装 View 返回。

---

### 三、经典 MVC vs Web MVC

#### 1. 经典 MVC（Smalltalk 原版，桌面 GUI）

```
       ┌──────────┐  事件   ┌────────────┐
   用户│          │────────▶│ Controller │
       └──────────┘         └─────┬──────┘
                                  │ 更新
                                  ▼
                            ┌──────────┐  通知   ┌──────┐
                            │  Model   │────────▶│ View │
                            └──────────┘         └──┬───┘
                                                    │ 查询
                                                    ▼
                                              重新渲染
```

特点：**View 直接订阅 Model 的变化**，Model 变了 View 自动刷新。典型代表：Java Swing、Qt 信号槽。

#### 2. Web MVC（请求-响应模型，无状态）

```
   Browser ──HTTP Request──▶ Controller ──调用──▶ Service / Model
                                                     │
                                  ◀──返回数据────────┘
                                  │
                                  ▼
                              View (Template)
                                  │
                                  ▼  HTML Response
                              Browser
```

特点：
- HTTP 无状态，View 无法"订阅" Model。
- Controller **主动**把 Model 数据塞给 View 并渲染。
- View 通常退化为模板引擎（Thymeleaf、Jinja2、html/template）。

> 在 Web 语境下，MVC 几乎等价于"Controller + Service + Template"，本质是分层架构在表现层的一种细化。

---

### 四、代码示例

#### 1. Java Spring Boot 版（典型 Web MVC）

目录结构：

```
src/main/java/com/example/order/
├── controller/OrderController.java
├── service/OrderService.java
├── model/Order.java
├── dto/OrderRequest.java
└── repository/OrderRepository.java
src/main/resources/templates/order.html   (View)
```

Model：

```java
@Entity
@Table(name = "orders")
public class Order {
    @Id
    private String id;
    private String userId;
    private BigDecimal amount;
    private String status;
    private LocalDateTime createdAt;

    // 业务规则封装在 Model 内
    public void pay() {
        if (!"CREATED".equals(this.status)) {
            throw new IllegalStateException("order can not be paid: " + status);
        }
        this.status = "PAID";
    }
    // getters / setters 省略
}
```

Controller：

```java
@RestController
@RequestMapping("/orders")
public class OrderController {

    private final OrderService orderService;

    public OrderController(OrderService orderService) {
        this.orderService = orderService;
    }

    @PostMapping
    public Order create(@RequestBody OrderRequest req) {
        return orderService.create(req);
    }

    @GetMapping("/{id}")
    public String detail(@PathVariable String id, Model model) {
        Order order = orderService.findById(id);
        model.addAttribute("order", order);   // Controller 把 Model 塞给 View
        return "order/detail";                // 返回视图名
    }
}
```

View（Thymeleaf 模板 `order/detail.html`）：

```html
<!DOCTYPE html>
<html xmlns:th="http://www.thymeleaf.org">
<body>
    <h1>订单详情</h1>
    <p>订单号：<span th:text="${order.id}"></span></p>
    <p>金额：<span th:text="${order.amount}"></span></p>
    <p>状态：<span th:text="${order.status}"></span></p>
</body>
</html>
```

#### 2. Python Flask 版（轻量 Web MVC）

```python
from flask import Flask, request, render_template, jsonify
from dataclasses import dataclass
from typing import List
import uuid

app = Flask(__name__)

# Model
@dataclass
class Order:
    id: str
    user_id: str
    amount: float
    status: str = "CREATED"

    def pay(self):
        if self.status != "CREATED":
            raise ValueError("order can not be paid")
        self.status = "PAID"


# 仓库（简化为内存存储）
_orders: List[Order] = []


# Controller
@app.route("/orders", methods=["POST"])
def create_order():
    data = request.json
    order = Order(id=str(uuid.uuid4()), user_id=data["user_id"], amount=data["amount"])
    _orders.append(order)
    return jsonify({"id": order.id, "status": order.status})


@app.route("/orders/<order_id>", methods=["GET"])
def order_detail(order_id):
    order = next((o for o in _orders if o.id == order_id), None)
    if order is None:
        return "not found", 404
    return render_template("order_detail.html", order=order)  # View


if __name__ == "__main__":
    app.run(debug=True)
```

View（`templates/order_detail.html`）：

```html
<!DOCTYPE html>
<html>
<body>
    <h1>订单详情</h1>
    <p>订单号：{{ order.id }}</p>
    <p>金额：{{ order.amount }}</p>
    <p>状态：{{ order.status }}</p>
</body>
</html>
```

#### 3. Go 版（net/http + html/template）

```go
package main

import (
    "html/template"
    "net/http"
    "sync"
)

// Model
type Order struct {
    ID     string
    UserID string
    Amount float64
    Status string
}

var (
    orders = sync.Map{}
    tmpl   = template.Must(template.ParseFiles("order_detail.html"))
)

// Controller
func orderDetail(w http.ResponseWriter, r *http.Request) {
    id := r.URL.Query().Get("id")
    val, ok := orders.Load(id)
    if !ok {
        http.NotFound(w, r)
        return
    }
    order := val.(Order)
    // Controller 把 Model 渲染到 View
    tmpl.Execute(w, order)
}

func main() {
    http.HandleFunc("/orders", orderDetail)
    http.ListenAndServe(":8080", nil)
}
```

View（`order_detail.html`）：

```html
<!DOCTYPE html>
<html>
<body>
    <h1>订单详情</h1>
    <p>订单号：{{.ID}}</p>
    <p>金额：{{.Amount}}</p>
    <p>状态：{{.Status}}</p>
</body>
</html>
```

---

### 五、MVC 的价值

| 价值 | 说明 |
|------|------|
| 关注点分离 | 数据、展示、控制各自独立，互不污染 |
| 多视图共享 | 同一 Model 可被表格视图、图表视图、JSON 视图复用 |
| 可测试性 | Model 与 Controller 可脱离 View 单独测试 |
| 团队协作 | 后端写 Controller/Model，前端写 View，并行开发 |
| 演进性 | View 从 HTML 切到 React/Vue 时，Controller/Model 不动 |

---

### 六、常见反模式

#### 1. 胖控制器（Fat Controller）

业务逻辑全堆在 Controller 里，Model 退化为贫血对象。

```java
// 反例：Controller 在算价格、扣库存、发通知
@PostMapping("/orders")
public Order create(@RequestBody OrderRequest req) {
    Order order = new Order();
    order.setAmount(req.getAmount() * 0.9);           // 折扣规则应在 Model/Service
    stockDao.decrease(req.getSkuId(), req.getQty());   // 库存应在 Service
    emailService.send(req.getUserId(), "已下单");       // 通知应在 Service
    return orderRepo.save(order);
}
```

正解：Controller 只做参数解析 + 委托，业务下沉到 Service，规则放进 Model。

#### 2. View 直接访问数据源

模板里写 SQL、调 HTTP，等于绕过 Controller 和 Model，破坏三层边界。

#### 3. Model 与 View 耦合

Model 里塞了 HTML 标签或前端字段名，导致无法复用到其它渠道（如小程序、App）。

#### 4. Controller 操作多个 View 的状态

Controller 变成全局状态中枢，出现"上帝控制器"。应按业务领域拆分多个 Controller。

---

### 七、MVC 的衍生家族

```
                MVC (1979, Smalltalk)
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
       MVP        MVVM        Web MVC
   (Passive View) (WPF/Vue)   (Spring/Flask/Django)
```

#### 1. MVP（Model-View-Presenter）

- View 不再直接订阅 Model，所有交互经 Presenter 中转。
- View 抽象为接口，Presenter 面向接口编程，便于单元测试。
- 典型场景：Android 早期、WinForms。

#### 2. MVVM（Model-View-ViewModel）

- 引入双向数据绑定，View 与 ViewModel 自动同步。
- ViewModel 不操作 View，只暴露状态。
- 典型场景：WPF、Vue、Knockout、现代前端框架。

#### 3. Web MVC

- 因 HTTP 无状态，砍掉了"View 订阅 Model"机制。
- Controller 主动渲染，View 退化为模板。
- 典型：Spring MVC、Django、Flask、Rails。

| 对比项 | MVC | MVP | MVVM |
|--------|-----|-----|------|
| 控制中枢 | Controller | Presenter | ViewModel |
| View 与 Model | 直接通信 | 不通信 | 不通信 |
| 数据绑定 | 无 | 无 | 双向绑定 |
| 测试难度 | 中 | 易 | 易 |
| 典型场景 | 桌面/Web | Android/桌面 | 前端/WPF |

---

### 八、MVC 与分层架构的关系

MVC 不是分层架构的替代品，而是**分层架构表现层内部**的组织方式。

```
┌─────────────────────────────────────┐
│ 表现层  ┌────────────────────────┐  │
│         │ Controller │ View      │  │  ← MVC 作用范围
│         │            │           │  │
│         └─────┬──────┴───────┬───┘  │
│               │              │      │
├───────────────┼──────────────┼──────┤
│ 业务层        ▼              ▼      │
│         Service (业务逻辑)          │
├─────────────────────────────────────┤
│ 数据访问层  Repository               │
├─────────────────────────────────────┤
│ 存储层      MySQL / Redis           │
└─────────────────────────────────────┘
```

- Controller = 表现层调度
- View = 表现层渲染
- Model = 跨层领域对象（DTO/Domain），实际业务在 Service 层
- Service 与 Repository 仍然按分层架构组织

---

### 九、速记要点

| 要点 | 记忆 |
|------|------|
| 三角色 | Model 管数据、View 管展示、Controller 管调度 |
| 依赖方向 | Controller 依赖 Model 和 View；Model 不依赖任何一方 |
| 适用范围 | 表现层模式，不是整个系统架构 |
| Web 变体 | 取消 View 订阅，Controller 主动渲染模板 |
| 衍生 | MVP 中转、MVVM 绑定、Web MVC 主动渲染 |
| 胖控制器 | 反模式，业务必须下沉到 Service / Model |
| 与分层关系 | MVC 是分层架构表现层的细化 |

---

### 十、MVC 的历史演进

> 理解历史能讲清"为什么 MVC 长成现在这样"——面试加分点。

#### 10.1 时间线

| 年份 | 事件 |
|------|------|
| **1979** | Trygve Reenskaug 在 Xerox PARC 为 Smalltalk-80 提出 MVC |
| 1987 | MVC 论文正式发表，成为 GUI 标准范式 |
| **1996** | Mike Potel 提出 **MVP**（Model-View-Presenter），解决 View 直接依赖 Model 难测试 |
| 2004 | Martin Fowler 整理 MVP 为 Passive View / Supervising Controller 两型 |
| **2005** | Microsoft WPF 引入 **MVVM**（Model-View-ViewModel），双向绑定 |
| 2006 | Ruby on Rails 把 Web MVC 带向主流，"约定优于配置" |
| 2010 | Android 推荐 MVP，后转向 MVVM + Jetpack |
| **2014** | Facebook 提出 **Flux**（单向数据流），反思 MVC 在大型 SPA 中失效 |
| 2015 | Redux 把 Flux 简化为"reducer + 单一 store"，前端事实标准 |
| 2018+ | Vue 3 / React Hooks 进一步弱化"组件 = View"边界，状态管理回归函数式 |

#### 10.2 经典 MVC 解决了什么问题

1979 年的 GUI 面临的痛点：

1. **业务代码和界面代码纠缠**——改一个按钮颜色可能碰到业务逻辑。
2. **同一数据要多视图展示**——表格、图表、文本都要看同一份数据。
3. **多视图要联动**——改了数据，所有视图自动刷新。

Reenskaug 的解法：把"数据 + 规则"抽出来当 Model，让 View 订阅 Model 的变化（观察者模式）。这样：

- 改 View 不动 Model（UI 调整无业务风险）。
- 一份数据多视图（Model 是单源真相）。
- 多视图联动（Model 变 → 所有订阅的 View 自动刷新）。

> 这就是经典 MVC 的精髓——**Model 是可观察的，View 是观察者**。

#### 10.3 Web 时代为什么改了 MVC

HTTP 是**无状态、请求-响应**模型，经典 MVC 的"View 订阅 Model 自动刷新"在 Web 下做不到（没有持久连接，浏览器拿完 HTML 就断开）。

Web MVC 的妥协：

1. **Controller 主动渲染**：用户请求 → Controller 调 Model 取数据 → 选模板渲染 → 返回 HTML。
2. **View 退化为模板**：不再"订阅"，只是一次性渲染函数。
3. **Model 退化为 DTO**：连业务规则都下沉到 Service 了（Web 框架的 Model 通常是贫血对象）。

> 这导致 Web MVC 的"Model"和经典 MVC 的"Model"含义不同——Web MVC 的 Model 经常只是数据载体，真正业务在 Service。这是面试高频混淆点。

---

### 十一、Spring MVC 请求处理流程

> Spring MVC 是 Java Web 的事实标准，其请求处理流程是面试高频题。

#### 11.1 核心组件

| 组件 | 角色 |
|------|------|
| **DispatcherServlet** | 前端控制器（Front Controller），所有请求入口 |
| **HandlerMapping** | 根据 URL 找到对应 Handler（Controller 方法） |
| **HandlerAdapter** | 适配不同类型的 Handler 并执行 |
| **ViewResolver** | 把视图名解析为具体 View |
| **View** | 实际渲染（Thymeleaf、JSP、JSON） |
| **HandlerInterceptor** | 拦截器（前置 / 后置 / 完成钩子） |
| **HandlerExceptionResolver** | 异常解析 |

#### 11.2 完整流程

```
   1. HTTP Request
        │
        ▼
   2. DispatcherServlet.doDispatch()
        │
        ▼
   3. HandlerMapping.getHandler()       ← 根据 @RequestMapping 找 Controller 方法
        │                                  返回 HandlerExecutionChain（含拦截器）
        ▼
   4. HandlerInterceptor.preHandle()    ← 前置拦截（认证、限流）
        │
        ▼
   5. HandlerAdapter.handle()           ← 反射调用 Controller 方法
        │
        ▼
   6. Controller 方法执行                 ← 业务调用 Service
        │                                  返回 ModelAndView / ResponseBody
        ▼
   7. HandlerInterceptor.postHandle()   ← 后置拦截
        │
        ▼
   8a. @ResponseBody? ──是──▶ MessageConverter (Jackson) ──▶ JSON 直接写响应
        │
        否
        ▼
   8b. ViewResolver.resolveViewName()   ← 视图名 → View
        │
        ▼
   9. View.render()                     ← 模板渲染
        │
        ▼
   10. HandlerInterceptor.afterCompletion()
        │
        ▼
   11. HTTP Response
```

#### 11.3 关键设计点

- **Front Controller 模式**：`DispatcherServlet` 是唯一入口，统一处理路由、拦截、异常、视图解析。好处是**横切关注点集中**。
- **HandlerMapping 解耦**：URL 到方法的映射可替换（注解、XML、约定）。
- **HandlerAdapter 适配**：支持多种 Controller 写法（注解、接口实现、函数式）。
- **MessageConverter 灵活**：同一 Controller 方法可返回 HTML / JSON / XML，由 Content-Type 决定。

> 这就是为什么 Spring MVC 能从 JSP 时代平滑演进到 RESTful API 时代——核心流程不变，ViewResolver / MessageConverter 可替换。

---

### 十二、前端 MVC 的演化：从 MVC 到 Flux/Redux

> 前端 SPA 兴起后，传统 MVC 在大型应用中暴露出"数据流混乱"问题，催生了 Flux / Redux。

#### 12.1 前端 MVC 的困境

经典 MVC 在浏览器端的问题：

```
   View ──用户操作──▶ Controller ──更新──▶ Model
    ▲                                         │
    └───────────变化通知───────────────────────┘
   
   问题：多个 Model 之间相互依赖时
   Model A 变 → 通知 View → View 触发 Controller → 改 Model B → Model B 变 → ...
   数据流变成网状，无法追踪
```

Facebook 在开发大型应用时遇到"**双向数据流导致状态不可预测**"问题，提出 Flux：

#### 12.2 Flux：单向数据流

```
   Action ──▶ Dispatcher ──▶ Store ──▶ View
                  ▲                       │
                  └─────── Action ────────┘
   
   规则：数据只能单向流动，View 不能直接改 Store，必须发 Action
```

- **Action**：描述"发生了什么"的不可变对象。
- **Dispatcher**：中央总线，把 Action 分发给所有 Store。
- **Store**：持有状态 + 处理 Action，变化后触发事件。
- **View**：订阅 Store，用户操作时发 Action（不改 Store）。

#### 12.3 Redux：Flux 的简化

Redux 把 Flux 的多 Store 合并为**单一 Store**，用 **reducer** 纯函数处理状态：

```
   State (single source of truth)
        │
        ▼
   Action ──▶ Reducer (pure function) ──▶ New State
        ▲                                    │
        └────────── View dispatch ───────────┘
```

三大原则：

1. **单一数据源**：整个应用的状态在一棵树里。
2. **状态只读**：不能直接改，只能发 Action。
3. **纯函数变更**：reducer 是 `(state, action) => newState`，无副作用。

> 这就是 MVC 在前端"反演化"的产物——为了解决双向绑定的不可预测性，Redux 主动放弃了 View ↔ Model 的直接通信，回到"单向数据流"。

#### 12.4 现代前端的回归

React Hooks / Vue 3 Composition API 之后，前端又从"组件 = View"转向"组件 = 函数"，状态管理回归函数式：

- 不再强调 MVC 三角色。
- 状态用 `useState` / `useReducer` 局部管理。
- 全局状态用 Context / Zustand / Pinia。
- 数据流仍是单向（状态 → 视图，动作 → 状态）。

> 趋势：**MVC 在前端没死，但形态变了**——从"三个对象"变成"单向数据流"的思想。

---

### 十三、MVC 与分层架构、DDD、Clean Architecture

> 这是中高级面试的深水区——MVC 和这些架构不是替代关系，而是**不同维度**。

#### 13.1 MVC vs 分层架构

| 维度 | MVC | 分层架构 |
|------|-----|---------|
| 关注点 | 表现层内部组织 | 整个系统的层次划分 |
| 作用范围 | 表现层 | 全系统 |
| 角色数 | 3（M/V/C） | 通常 4（Presentation/Business/DAO/DB） |
| 关系 | MVC 是分层架构表现层的细化 | 分层架构包含 MVC |

```
   分层架构：
   ┌─────────────────────────┐
   │ Presentation (Controller + View = MVC)│
   ├─────────────────────────┤
   │ Business (Service)      │
   ├─────────────────────────┤
   │ Data Access (Repository)│
   ├─────────────────────────┤
   │ Database                │
   └─────────────────────────┘
   
   MVC 是 Presentation 层内部的"三人分工"。
```

#### 13.2 MVC vs DDD

DDD（领域驱动设计）关注**业务层**的建模，与 MVC 关注的层不同：

| 维度 | MVC | DDD |
|------|-----|-----|
| 关注层 | 表现层 | 业务层 |
| 核心概念 | Model / View / Controller | Entity / Value Object / Aggregate / Domain Service / Repository |
| Model 含义 | 通常是贫血 DTO | 充血的领域对象（含行为） |

> 一个系统可以同时用 MVC（表现层）+ DDD（业务层）。这时 MVC 的 "Model" 实际是 DDD 的 DTO / View Model，**不是 DDD 的 Entity**——这是常见混淆。

```
   ┌──────────────────────────────┐
   │ Controller / View (MVC)      │  ← 表现层
   ├──────────────────────────────┤
   │ Application Service          │  ← 用例编排
   ├──────────────────────────────┤
   │ Domain Model (DDD)           │  ← 业务核心
   │  Entity / VO / Aggregate     │
   │  Domain Service              │
   ├──────────────────────────────┤
   │ Repository (DAO)             │
   └──────────────────────────────┘
   
   MVC 的 "Model" 在 DDD 架构里是 DTO（出参）/ Command（入参），
   真正的"业务模型"是 Domain 层的 Entity。
```

#### 13.3 MVC vs Clean Architecture

Uncle Bob 的 Clean Architecture 把依赖方向倒置：**内层不依赖外层**。

```
   ┌──────────────────────────────┐
   │ Frameworks (Web, DB, MVC)    │  ← 外层，依赖内层
   │  ┌────────────────────────┐  │
   │  │ Interface Adapters     │  │  ← Controller、Presenter、Gateway
   │  │  ┌──────────────────┐  │  │
   │  │  │ Use Cases         │  │  │  ← 应用业务规则
   │  │  │  ┌─────────────┐  │  │  │
   │  │  │  │ Entities     │  │  │  │  ← 企业业务规则（最内层）
   │  │  │  └─────────────┘  │  │  │
   │  │  └──────────────────┘  │  │
   │  └────────────────────────┘  │
   └──────────────────────────────┘
```

- Clean Architecture 里 **Controller 在 Interface Adapters 层**，负责把外部请求转成 Use Case 的输入。
- **View 也是 Adapter 层**，负责把 Use Case 输出转成展示格式。
- **Model 在 Entities 层**，是最受保护的业务核心。

> 区别：MVC 的 Controller 可以直接调 Service；Clean Architecture 的 Controller 必须通过 Use Case 边界（Input Port / Output Port）。MVC 是"扁平"分工，Clean Architecture 是"洋葱"分层。

---

### 十四、Active Record vs Data Mapper

> 这是 MVC "Model" 的两种实现风格，Rails vs Java 生态的核心分歧。

#### 14.1 Active Record（活动记录）

**对象本身既承载数据，又负责持久化**。

```ruby
# Rails ActiveRecord
class Order < ApplicationRecord
  def total_with_tax
    amount * 1.1
  end
end

order = Order.find(123)        # 类方法查询
order.amount = 100
order.save                     # 实例方法保存
```

特点：
- Model 对象 = 数据 + 持久化方法 + 业务规则。
- 简单直观，CRUD 零配置。
- **违反 SRP**：对象既管数据又管存储。

#### 14.2 Data Mapper（数据映射器）

**对象只承载数据和业务规则，持久化由独立的 Mapper/Repository 负责**。

```java
// JPA Entity（只管数据 + 业务规则）
@Entity
public class Order {
    private BigDecimal amount;
    public BigDecimal totalWithTax() { return amount.multiply(new BigDecimal("1.1")); }
}

// Repository（管持久化）
public interface OrderRepository {
    Order findById(Long id);
    void save(Order order);
}
```

特点：
- Model 与持久化解耦。
- 可测试性好（不依赖 DB 测 Model）。
- 代码量多，配置重。

#### 14.3 对比

| 维度 | Active Record | Data Mapper |
|------|--------------|-------------|
| 数据与持久化 | 耦合 | 分离 |
| 简单程度 | 高 | 低 |
| 灵活性 | 低 | 高 |
| 测试 | 难（依赖 DB） | 易（可 mock） |
| 典型 | Rails、Laravel Eloquent | JPA + Repository、MyBatis |

> 选型：**小型项目用 Active Record**（开发快），**中大型项目用 Data Mapper**（可维护）。

---

### 十五、Fat Model vs Thin Controller 之争

> MVC 圈的永恒争论：业务逻辑放 Controller 还是 Model？

#### 15.1 两种极端

**Fat Controller（胖控制器）**：业务全在 Controller，Model 是贫血。

```java
// 反例
@PostMapping("/orders")
public Order create(@RequestBody OrderRequest req) {
    // 折扣、库存、通知、保存全在 Controller
    Order order = new Order();
    order.setAmount(req.getAmount() * discountService.calc(req.getUserId()));
    stockService.decrease(req.getSkuId());
    order = orderRepo.save(order);
    notifyService.send(order);
    return order;
}
```

**Fat Model（胖模型）**：业务全在 Model，Controller 只转发。

```java
// Rails 风格
@PostMapping("/orders")
public Order create(@RequestBody OrderRequest req) {
    return orderService.create(req);   // Controller 一行
}

// Model 里堆业务
class Order {
    public void pay(Payment payment) {
        validateStatus();
        applyPayment(payment);
        updateInventory();
        sendNotification();
    }
}
```

#### 15.2 现代共识：分层 Service

两者都走极端。现代 Java 生态的解法是引入 **Service 层**：

```
   Controller：HTTP 协议适配（参数解析、响应封装），无业务
   Service：业务编排（事务、调用多个领域对象）
   Model/Entity：领域规则（自身数据校验、状态流转）
   Repository：持久化
```

| 层 | 职责 | 不该做什么 |
|----|------|-----------|
| Controller | 协议适配 | 不写业务、不直接访问 DAO |
| Service | 业务编排 | 不处理 HTTP、不写 SQL |
| Model/Entity | 领域规则 | 不依赖框架、不调外部服务 |
| Repository | 数据访问 | 不含业务 |

> 一句话：**Controller 瘦、Service 中、Model 充血**。

---

### 十六、MVC 的测试策略

> 可测试性是 MVC 的核心价值之一，面试常问"怎么测 Controller"。

#### 16.1 各层测试

| 层 | 测什么 | 工具 |
|----|--------|------|
| Model | 领域规则、状态流转 | JUnit（纯单元） |
| Service | 业务编排（mock 依赖） | JUnit + Mockito |
| Controller | 路由、参数解析、响应 | MockMvc / WebMvcTest |
| View | 模板渲染 | HtmlUnit / Selenium |
| 端到端 | 完整流程 | RestAssured / Selenium |

#### 16.2 Controller 测试

```java
@WebMvcTest(OrderController.class)
class OrderControllerTest {
    @Autowired MockMvc mvc;
    @MockBean OrderService service;

    @Test
    void should_return_order_when_exists() throws Exception {
        when(service.findById("1")).thenReturn(new Order("1", "PAID"));

        mvc.perform(get("/orders/1"))
           .andExpect(status().isOk())
           .andExpect(jsonPath("$.status").value("PAID"));
    }
}
```

> `@WebMvcTest` 只加载 Controller 层，Service 用 `@MockBean` 替换。这是分层测试的标准做法。

#### 16.3 测试金字塔

```
           ┌─────────┐
           │   E2E   │  少（贵、慢）
           ├─────────┤
           │ 集成测试 │  中
           ├─────────┤
           │ 单元测试 │  多（便宜、快）
           └─────────┘
```

- Model / Service：单元测试为主，mock 依赖。
- Controller：切片测试（MockMvc），不启动整个容器。
- 整体流程：少量 E2E 覆盖关键路径。

---

### 十七、MVC 的争议与局限

#### 17.1 "Model 到底是什么"

最大的争议：Model 是**领域对象**、**DTO**、**ViewModel** 还是**数据表映射**？

- 经典 MVC：Model = 领域对象（含行为）。
- Web MVC：Model = DTO / View Model（数据载体）。
- Rails：Model = 数据表映射 + 业务（Active Record）。
- Spring MVC：`Model` 是个 Map，存视图数据。

> 不同框架的 "Model" 含义不同，这是 MVC 概念混乱的根源。面试讲清"我说的 Model 是哪种"很加分。

#### 17.2 MVC 在大型应用的失效

- **Massive View Controller**：iOS 早期常见，Controller 几千行。
- **Model 蔓延**：业务全堆 Model，Model 变成上帝对象。
- **多渠道适配难**：同一业务要支持 Web / App / 小程序 / OpenAPI，Controller 膨胀。

解法：引入 Service / Use Case / Application Service 层，Controller 退化为协议适配。

#### 17.3 前后端分离后的 MVC

前后端分离后，后端 MVC 的 View 消失了（只返回 JSON），变成：

```
   前端：MVVM（Vue / React）
   后端：Controller + Service + Repository（无 View）
   API：RESTful / GraphQL 契约
```

> 严格说，纯 API 后端不再是完整 MVC——只剩 MC，View 在前端。这是为什么现代后端框架文档不再强调 MVC，而强调 RESTful。

#### 17.4 MVC 的真正适用场景

- **服务端渲染（SSR）**：Django、Rails、Spring MVC + Thymeleaf。
- **桌面 GUI**：Qt、Swift Cocoa。
- **移动端**：Android（早期 MVP，现在 MVVM）。
- **不适合**：纯 API 后端（用分层架构就够）、大型 SPA（用 Flux/Redux）。

---

### 十八、综合面试题

#### Q1：MVC 三层各自的职责边界？

> Model 管数据 + 业务规则，独立于 UI；View 管展示，不写业务；Controller 管调度，解析输入 + 委托 Service + 选择 View。**Controller 不应直接访问 DAO，Model 不应依赖 View**。

#### Q2：经典 MVC 和 Web MVC 区别？

> 经典 MVC（桌面 GUI）View 订阅 Model 变化自动刷新；Web MVC 因为 HTTP 无状态做不到订阅，改为 Controller 主动把 Model 数据塞给 View 渲染。Web 下 View 退化为模板，Model 退化为 DTO。

#### Q3：为什么 Spring MVC 用 DispatcherServlet？

> Front Controller 模式——统一入口集中处理路由、拦截、异常、视图解析，避免每个 Controller 重复这些横切逻辑。可扩展性强（HandlerMapping、ViewResolver 都可替换）。

#### Q4：胖控制器怎么治？

> 三步：① Controller 只做协议适配（参数解析、响应封装）；② 业务编排下沉到 Service；③ 领域规则放进 Model/Entity。判断标准：Controller 里不应该出现 `if (业务条件)`，应该直接 `service.do(req)`。

#### Q5：MVC 和分层架构什么关系？

> MVC 是分层架构**表现层内部**的组织方式。Controller 和 View 属于表现层，Model 跨层（DTO/Domain），真正的业务在 Service 层。完整系统是"分层架构 + 表现层用 MVC 组织"。

#### Q6：MVC 和 DDD 冲突吗？

> 不冲突，关注层不同。MVC 管表现层，DDD 管业务层。一个系统可同时用：Controller/View 用 MVC，Domain 层用 DDD（Entity/VO/Aggregate/Repository）。注意：**MVC 的 Model 不是 DDD 的 Entity**——前者通常是 DTO，后者是充血领域对象。

#### Q7：Active Record 和 Data Mapper 怎么选？

> Active Record（Rails）简单快，Model 对象自带持久化方法，适合小项目；Data Mapper（JPA + Repository）解耦好，可测试性强，适合中大型项目。Active Record 违反 SRP，但开发效率高；Data Mapper 代码多但维护性好。

#### Q8：前后端分离后 MVC 还有意义吗？

> 后端只剩 Controller + Service + Repository（无 View），严格说不是完整 MVC。但前端用 MVVM（Vue/React），仍是 MVC 思想的演化。**MVC 的核心价值——关注点分离——在任何架构中都适用**，只是角色形态变了。

#### Q9：为什么前端从 MVC 演化到 Redux？

> 经典 MVC 在大型 SPA 中"双向数据流"导致状态不可追踪——Model 变 → View → Controller → 另一个 Model → 网状依赖。Redux 用"单向数据流 + 单一 store + 纯函数 reducer"解决，状态变化可预测、可回放。

#### Q10：MVC 的 Model 测试和 Controller 测试有什么区别？

> Model 测领域规则（纯单元，不需要容器）；Controller 用 `@WebMvcTest` 切片测试，mock 掉 Service，验证路由、参数解析、响应格式。Controller 测试不应碰数据库——那是 Service / Repository 集成测试的事。

---

### 十九、参考与延伸

- **《Patterns of Enterprise Application Architecture》**（Martin Fowler）：Active Record / Data Mapper / Page Controller 等模式权威定义。
- **《Domain-Driven Design》**（Eric Evans）：MVC 的 Model 与 DDD 的 Entity 区别。
- **《Clean Architecture》**（Robert C. Martin）：MVC 在洋葱架构中的位置。
- **Trygve Reenskaug 原始论文**：[Models-Views-Controllers (1979)](https://heim.ifi.uio.no/~trygver/themes/mvc/mvc-index.html)。
- **Spring MVC 官方文档**：请求处理流程、HandlerMapping、ViewResolver。

> 配套阅读：
> - [参考范式-分层模式.md](./参考范式-分层模式.md)：MVC 是分层架构表现层的细化
> - [软件设计原则.md](./软件设计原则.md)：SRP 治胖控制器、DIP 治 Controller 直接依赖 DAO
> - [设计模式-行为模式.md](./设计模式-行为模式.md)：观察者模式是经典 MVC 的核心、策略模式是 Controller 选择 View 的基础


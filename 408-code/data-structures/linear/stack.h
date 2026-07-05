/**
 * @file stack.h
 * @topic 数据结构 - 栈 (顺序栈/链栈/最小栈)
 *
 * @考点 408 大纲:数据结构 > 栈和队列 > 栈
 *   - 后进先出 LIFO
 *   - 顺序栈 / 链栈 / 共享栈
 *   - 应用:括号匹配 / 表达式求值 / 递归 / 回溯 / DFS
 *   - 卡特兰数:n 个元素入栈的出栈序列数 C(n,2n)/(n+1)
 *
 * @业务 工业应用
 *   - 函数调用栈 (CPU ESP/RSP 寄存器)
 *   - JVM 虚拟机栈 (栈帧)
 *   - 浏览器后退 / 编辑器撤销
 *   - 表达式解析 (编译器 AST 构造)
 *   - Python list.append/pop 是栈操作
 *
 * @陷阱 408 高频
 *   - 入栈出栈序列合法性:给定 push 序列,判断 pop 序列是否可能
 *   - 共享栈:两栈共享一片空间,栈底分别在两端,栈顶相向生长
 *   - 最小栈:用辅助栈 O(1) 取最小
 *   - 递归深度过深 → 栈溢出 (StackOverflow)
 */
#ifndef CS408_DS_LINEAR_STACK_H
#define CS408_DS_LINEAR_STACK_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>

namespace cs408::ds {

// ===== 顺序栈 (动态扩容) =====
template <typename T>
class ArrayStack {
public:
    void push(const T& v) { data_.push_back(v); }
    void pop() { if (data_.empty()) throw std::out_of_range("pop empty"); data_.pop_back(); }
    const T& top() const { if (data_.empty()) throw std::out_of_range("top empty"); return data_.back(); }
    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }
private:
    std::vector<T> data_;
};

// ===== 最小栈 (O(1) 取最小) =====
template <typename T>
class MinStack {
public:
    void push(const T& v) {
        data_.push_back(v);
        // 仅当新值 <= 当前最小才入辅助栈 (保证辅助栈顶始终是当前最小)
        if (min_.empty() || v <= min_.back()) min_.push_back(v);
    }
    void pop() {
        if (data_.empty()) throw std::out_of_range("pop empty");
        if (data_.back() == min_.back()) min_.pop_back();
        data_.pop_back();
    }
    const T& top() const { return data_.back(); }
    const T& min() const { return min_.back(); }
    size_t size() const { return data_.size(); }
private:
    std::vector<T> data_;
    std::vector<T> min_;
};

// ===== 应用 1:括号匹配 =====
inline bool bracket_match(const std::string& s) {
    ArrayStack<char> st;
    for (char c : s) {
        if (c == '(' || c == '[' || c == '{') {
            st.push(c);
        } else if (c == ')' || c == ']' || c == '}') {
            if (st.empty()) return false;
            char t = st.top(); st.pop();
            if ((c == ')' && t != '(') ||
                (c == ']' && t != '[') ||
                (c == '}' && t != '{')) return false;
        }
    }
    return st.empty();
}

// ===== 应用 2:中缀表达式求值 (双栈法) =====
inline int eval_infix(const std::string& expr) {
    ArrayStack<int>  nums;
    ArrayStack<char> ops;
    auto precedence = [](char op) {
        if (op == '+' || op == '-') return 1;
        if (op == '*' || op == '/') return 2;
        return 0;
    };
    auto apply = [](int a, int b, char op) {
        switch (op) {
            case '+': return a + b;
            case '-': return a - b;
            case '*': return a * b;
            case '/': return b ? a / b : 0;
        }
        return 0;
    };
    for (size_t i = 0; i < expr.size(); ++i) {
        char c = expr[i];
        if (std::isspace(c)) continue;
        if (std::isdigit(c)) {
            int n = 0;
            while (i < expr.size() && std::isdigit(expr[i])) {
                n = n * 10 + (expr[i] - '0'); ++i;
            }
            --i;
            nums.push(n);
        } else if (c == '(') {
            ops.push(c);
        } else if (c == ')') {
            while (!ops.empty() && ops.top() != '(') {
                int b = nums.top(); nums.pop();
                int a = nums.top(); nums.pop();
                nums.push(apply(a, b, ops.top())); ops.pop();
            }
            if (!ops.empty()) ops.pop();
        } else {
            while (!ops.empty() && ops.top() != '(' &&
                   precedence(ops.top()) >= precedence(c)) {
                int b = nums.top(); nums.pop();
                int a = nums.top(); nums.pop();
                nums.push(apply(a, b, ops.top())); ops.pop();
            }
            ops.push(c);
        }
    }
    while (!ops.empty()) {
        int b = nums.top(); nums.pop();
        int a = nums.top(); nums.pop();
        nums.push(apply(a, b, ops.top())); ops.pop();
    }
    return nums.top();
}

// ===== 应用 3:出栈序列合法性 =====
// 给定 push 序列 1..n 与 pop 序列,判断是否合法
inline bool is_valid_pop_sequence(const std::vector<int>& pop_seq) {
    ArrayStack<int> st;
    int n = static_cast<int>(pop_seq.size());
    int push_val = 1;
    for (int target : pop_seq) {
        while (push_val <= n && (st.empty() || st.top() != target)) {
            st.push(push_val++);
        }
        if (st.empty() || st.top() != target) return false;
        st.pop();
    }
    return true;
}

// ===== 演示 =====
void stack_demo() {
    section("最小栈 O(1) 取最小");
    MinStack<int> ms;
    for (int x : {5, 3, 7, 2, 4}) {
        ms.push(x);
        std::cout << "push " << x << " -> min=" << ms.min() << "\n";
    }
    ms.pop(); std::cout << "pop 4 -> min=" << ms.min() << "\n";
    ms.pop(); std::cout << "pop 2 -> min=" << ms.min() << "\n";

    section("括号匹配");
    std::cout << "\"([{}])\" : " << bracket_match("([{}])") << "\n";
    std::cout << "\"([)]\"   : " << bracket_match("([)]") << "\n";

    section("中缀表达式求值 (双栈)");
    std::cout << "3 + 4 * 2 = " << eval_infix("3 + 4 * 2") << "\n";
    std::cout << "(3 + 4) * 2 = " << eval_infix("(3 + 4) * 2") << "\n";
    std::cout << "10 - 2 * 3 + 8 / 2 = " << eval_infix("10 - 2 * 3 + 8 / 2") << "\n";

    section("出栈序列合法性 (push 1..4)");
    std::cout << "pop = [2,4,3,1] : " << is_valid_pop_sequence({2,4,3,1}) << "\n";
    std::cout << "pop = [4,3,2,1] : " << is_valid_pop_sequence({4,3,2,1}) << "\n";
    std::cout << "pop = [4,1,2,3] : " << is_valid_pop_sequence({4,1,2,3}) << "\n";
}

bool stack_test() {
    CS408_EXPECT(bracket_match("([{}])"));
    CS408_EXPECT(!bracket_match("([)]"));
    CS408_EXPECT_EQ(eval_infix("3 + 4 * 2"), 11);
    CS408_EXPECT_EQ(eval_infix("(3 + 4) * 2"), 14);
    CS408_EXPECT(is_valid_pop_sequence({2, 4, 3, 1}));
    CS408_EXPECT(!is_valid_pop_sequence({4, 1, 2, 3}));

    MinStack<int> ms;
    ms.push(5); CS408_EXPECT_EQ(ms.min(), 5);
    ms.push(3); CS408_EXPECT_EQ(ms.min(), 3);
    ms.push(7); CS408_EXPECT_EQ(ms.min(), 3);
    ms.push(2); CS408_EXPECT_EQ(ms.min(), 2);
    ms.pop();   CS408_EXPECT_EQ(ms.min(), 3);
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "linear.stack", stack,
    "LIFO;顺序栈/链栈/共享栈;括号匹配;中缀求值双栈;卡特兰数",
    "函数调用栈 (CPU RSP);JVM 栈帧;浏览器后退;编译器 AST;Python list.pop",
    "出栈序列合法性判定;最小栈辅助栈;递归过深栈溢出;共享栈两端相向生长",
    stack_demo, stack_test
);

} // namespace cs408::ds
#endif // CS408_DS_LINEAR_STACK_H

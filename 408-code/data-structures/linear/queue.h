/**
 * @file queue.h
 * @topic 数据结构 - 队列 (循环队列/双端队列)
 *
 * @考点 408 大纲:数据结构 > 栈和队列 > 队列
 *   - 先进先出 FIFO
 *   - 循环队列:判空 (front==rear) vs 判满 ((rear+1)%N==front) — 牺牲一个单元
 *   - 队列长度:(rear - front + N) % N
 *   - 双端队列 deque:两端均可入出
 *
 * @业务 工业应用
 *   - 操作系统就绪队列 (进程调度)
 *   - 消息队列 (Kafka/RabbitMQ/Redis List)
 *   - BFS 广度优先搜索
 *   - 环形缓冲区 (Linux kfifo,音视频流)
 *   - lock-free 队列 (无锁编程)
 *
 * @陷阱 408 高频
 *   - 循环队列牺牲一个单元区分空/满
 *   - 入队 rear=(rear+1)%N;出队 front=(front+1)%N
 *   - 队列长度公式:(rear-front+N)%N
 *   - 双端队列 vs 双向链表:deque 随机访问 O(1)
 */
#ifndef CS408_DS_LINEAR_QUEUE_H
#define CS408_DS_LINEAR_QUEUE_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <deque>
#include <iostream>
#include <stdexcept>

namespace cs408::ds {

// ===== 循环队列 (顺序存储,牺牲一个单元) =====
template <typename T, size_t N>
class CircularQueue {
public:
    CircularQueue() : front_(0), rear_(0) {}

    bool enqueue(const T& v) {
        if (full()) return false;
        data_[rear_] = v;
        rear_ = (rear_ + 1) % N;
        return true;
    }

    bool dequeue(T& out) {
        if (empty()) return false;
        out = data_[front_];
        front_ = (front_ + 1) % N;
        return true;
    }

    bool empty() const { return front_ == rear_; }
    bool full()  const { return (rear_ + 1) % N == front_; }
    size_t size() const { return (rear_ - front_ + N) % N; }
    size_t capacity() const { return N - 1; }  // 牺牲一格

private:
    T     data_[N];
    size_t front_;
    size_t rear_;
};

// ===== 双端队列 (基于 std::deque 包装) =====
template <typename T>
class Deque {
public:
    void push_back (const T& v) { dq_.push_back(v); }
    void push_front(const T& v) { dq_.push_front(v); }
    void pop_back () { if (dq_.empty()) throw std::out_of_range("pop empty"); dq_.pop_back(); }
    void pop_front() { if (dq_.empty()) throw std::out_of_range("pop empty"); dq_.pop_front(); }
    const T& front() const { return dq_.front(); }
    const T& back()  const { return dq_.back(); }
    size_t size() const { return dq_.size(); }
    bool empty() const { return dq_.empty(); }
    const T& operator[](size_t i) const { return dq_[i]; }
private:
    std::deque<T> dq_;
};

// ===== 演示 =====
void queue_demo() {
    section("循环队列 (容量 5,牺牲 1 格,实际容量 4)");
    CircularQueue<int, 5> q;
    for (int x : {1, 2, 3, 4}) {
        bool ok = q.enqueue(x);
        std::cout << "enqueue " << x << " : " << (ok ? "ok" : "full") << "  size=" << q.size() << "\n";
    }
    std::cout << "enqueue 5 (应满): " << q.enqueue(5) << "\n";
    int v;
    while (q.dequeue(v)) std::cout << "dequeue " << v << "\n";

    section("双端队列");
    Deque<int> d;
    d.push_back(2);  d.push_back(3);
    d.push_front(1); d.push_front(0);
    std::cout << "deque: ";
    for (size_t i = 0; i < d.size(); ++i) std::cout << d[i] << " ";
    std::cout << "\nfront=" << d.front() << " back=" << d.back() << "\n";
    d.pop_back(); d.pop_front();
    std::cout << "pop_back + pop_front 后: front=" << d.front() << " back=" << d.back() << "\n";
}

bool queue_test() {
    CircularQueue<int, 5> q;
    CS408_EXPECT(q.empty());
    CS408_EXPECT(q.enqueue(1));
    CS408_EXPECT(q.enqueue(2));
    CS408_EXPECT(q.enqueue(3));
    CS408_EXPECT(q.enqueue(4));
    CS408_EXPECT(!q.enqueue(5));  // 满
    CS408_EXPECT_EQ(q.size(), 4u);
    int v;
    CS408_EXPECT(q.dequeue(v));
    CS408_EXPECT_EQ(v, 1);
    CS408_EXPECT(q.enqueue(5));   // 现在可入

    Deque<int> d;
    d.push_back(1); d.push_front(0); d.push_back(2);
    CS408_EXPECT_EQ(d.front(), 0);
    CS408_EXPECT_EQ(d.back(), 2);
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "linear.queue", queue,
    "FIFO;循环队列牺牲一格区分空满;队列长度 (rear-front+N)%N;双端队列",
    "OS 就绪队列;Kafka/RabbitMQ;BFS;Linux kfifo 环形缓冲;lock-free 队列",
    "循环队列判空 front==rear,判满 (rear+1)%N==front;deque 随机访问 O(1)",
    queue_demo, queue_test
);

} // namespace cs408::ds
#endif // CS408_DS_LINEAR_QUEUE_H

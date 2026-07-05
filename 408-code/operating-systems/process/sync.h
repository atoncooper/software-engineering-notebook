/**
 * @file sync.h
 * @topic 操作系统 - 同步与互斥 (信号量模拟 + 五大经典模型)
 *
 * @考点 408 大纲:操作系统 > 进程同步
 *   - 信号量 P/V 原语 (P 阻塞,V 唤醒)
 *   - 互斥信号量初值 1;资源信号量初值 N;同步信号量初值 0
 *   - 三大法则:先同步后互斥;V 顺序无关;互斥=1 资源=N 同步=0
 *   - 五大模型:
 *     1) 生产者-消费者 (mutex + empty + full)
 *     2) 读者-写者 (读者优先 / 写者优先 / 公平)
 *     3) 哲学家进餐 (限制人数 / 奇偶 / 同时拿)
 *     4) 理发师睡觉 (customers + barbers + mutex)
 *     5) 吸烟者问题 (轮转调度)
 *
 * @业务 工业应用
 *   - Java synchronized / ReentrantLock (互斥)
 *   - Go channel (同步信号量)
 *   - Python threading.Semaphore
 *   - POSIX sem_wait/sem_post
 *   - 数据库锁 (悲观/乐观锁)
 *
 * @陷阱 408 高频
 *   - 先 P(同步) 后 P(互斥),否则死锁
 *   - V 操作顺序无关,但建议先 V(互斥) 后 V(同步) 提高并发
 *   - 读者优先会饿死写者;需加 w 信号量改公平
 *   - 哲学家 5 人同时抢 5 筷死锁,限 4 人则不死锁
 *   - 理发师问题 barbers 初值 0 (无顾客在等)
 */
#ifndef CS408_OS_PROCESS_SYNC_H
#define CS408_OS_PROCESS_SYNC_H

#include "common/types.h"
#include "common/utils.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <iostream>
#include <memory>

namespace cs408::os {

// ===== 信号量 (用 mutex + condvar 模拟) =====
class Semaphore {
public:
    explicit Semaphore(int count = 0) : count_(count) {}
    void P() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this]{ return count_ > 0; });
        --count_;
    }
    void V() {
        std::unique_lock<std::mutex> lk(m_);
        ++count_;
        cv_.notify_one();
    }
    int value() const {
        std::unique_lock<std::mutex> lk(m_);
        return count_;
    }
private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    int count_;
};

// ===== 1. 生产者-消费者 =====
class ProducerConsumer {
public:
    explicit ProducerConsumer(int capacity)
        : mutex_(1), empty_(capacity), full_(0), capacity_(capacity) {}

    void produce(int item) {
        empty_.P();
        mutex_.P();
        buffer_.push(item);
        std::cout << "  生产者 " << std::this_thread::get_id()
                  << " 放入 " << item << " (size=" << buffer_.size() << ")\n";
        mutex_.V();
        full_.V();
    }

    int consume() {
        full_.P();
        mutex_.P();
        int item = buffer_.front(); buffer_.pop();
        std::cout << "  消费者 " << std::this_thread::get_id()
                  << " 取出 " << item << " (size=" << buffer_.size() << ")\n";
        mutex_.V();
        empty_.V();
        return item;
    }

private:
    Semaphore mutex_, empty_, full_;
    std::queue<int> buffer_;
    int capacity_;
};

// ===== 2. 读者-写者 (读写公平) =====
class ReaderWriterFair {
public:
    ReaderWriterFair() : rw_(1), mutex_(1), w_(1), readcount_(0) {}

    void read(const std::function<void()>& action) {
        w_.P();
        mutex_.P();
        if (++readcount_ == 1) rw_.P();
        mutex_.V();
        w_.V();
        action();  // 多个读者可同时读
        mutex_.P();
        if (--readcount_ == 0) rw_.V();
        mutex_.V();
    }

    void write(const std::function<void()>& action) {
        w_.P();
        rw_.P();
        action();  // 写者独占
        rw_.V();
        w_.V();
    }

private:
    Semaphore rw_, mutex_, w_;
    int readcount_;
};

// ===== 3. 哲学家进餐 (限制人数法) =====
class Philosophers {
public:
    explicit Philosophers(int n)
        : room_(n - 1) {
        chopsticks_.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) chopsticks_.emplace_back(std::make_unique<Semaphore>(1));
    }

    void dine(int i, const std::function<void(int)>& eat) {
        room_.P();
        chopsticks_[i]->P();
        chopsticks_[(i + 1) % chopsticks_.size()]->P();
        eat(i);
        chopsticks_[i]->V();
        chopsticks_[(i + 1) % chopsticks_.size()]->V();
        room_.V();
    }

private:
    Semaphore room_;
    std::vector<std::unique_ptr<Semaphore>> chopsticks_;
};

// ===== 演示 =====
void sync_demo() {
    section("生产者-消费者 (容量 3,2 生产者 2 消费者)");
    ProducerConsumer pc(3);
    auto producer = [&pc](int start) {
        for (int i = start; i < start + 3; ++i) pc.produce(i);
    };
    auto consumer = [&pc]() {
        for (int i = 0; i < 3; ++i) pc.consume();
    };
    std::thread p1(producer, 100), p2(producer, 200);
    std::thread c1(consumer), c2(consumer);
    p1.join(); p2.join(); c1.join(); c2.join();
    std::cout << "  生产消费完成\n";

    section("读写公平 (3 读者 1 写者)");
    ReaderWriterFair rw;
    auto reader = [&rw](int id) {
        rw.read([id]{ std::cout << "  读者 " << id << " 读取\n"; });
    };
    auto writer = [&rw](int id) {
        rw.write([id]{ std::cout << "  写者 " << id << " 写入\n"; });
    };
    std::thread rs[3], ws[2];
    for (int i = 0; i < 3; ++i) rs[i] = std::thread(reader, i);
    for (int i = 0; i < 2; ++i) ws[i] = std::thread(writer, i);
    for (int i = 0; i < 3; ++i) rs[i].join();
    for (int i = 0; i < 2; ++i) ws[i].join();

    section("哲学家进餐 (5 人,限 4 人抢筷)");
    Philosophers ph(5);
    auto eat = [](int i) {
        std::cout << "  哲学家 " << i << " 进餐\n";
    };
    std::vector<std::thread> phs;
    for (int i = 0; i < 5; ++i)
        phs.emplace_back([&ph, i, eat]{ ph.dine(i, eat); });
    for (auto& t : phs) t.join();
    std::cout << "  所有哲学家进餐完毕 (无死锁)\n";
}

bool sync_test() {
    section("信号量基础测试");
    Semaphore s(2);
    CS408_EXPECT_EQ(s.value(), 2);
    s.P(); CS408_EXPECT_EQ(s.value(), 1);
    s.P(); CS408_EXPECT_EQ(s.value(), 0);
    s.V(); CS408_EXPECT_EQ(s.value(), 1);

    section("生产者-消费者串行测试");
    ProducerConsumer pc(2);
    pc.produce(10); pc.produce(20);
    CS408_EXPECT_EQ(pc.consume(), 10);
    CS408_EXPECT_EQ(pc.consume(), 20);
    return true;
}

CS408_REGISTER_MODULE(
    "operating-systems", "process.sync", sync,
    "P/V 原语;互斥=1 资源=N 同步=0;五大模型:生产消费/读写者/哲学家/理发师/吸烟者;先同步后互斥",
    "Java synchronized;Go channel;Python Semaphore;POSIX sem_wait/post;数据库锁",
    "先 P 同步后 P 互斥否则死锁;V 顺序无关;读者优先饿死写者加 w;5 哲学家同时抢 5 筷死锁限 4 人",
    sync_demo, sync_test
);

} // namespace cs408::os
#endif // CS408_OS_PROCESS_SYNC_H

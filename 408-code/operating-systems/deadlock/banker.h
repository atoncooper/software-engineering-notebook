/**
 * @file banker.h
 * @topic 操作系统 - 死锁 - 银行家算法
 *
 * @考点 408 大纲:操作系统 > 死锁 > 死锁避免
 *   - 数据结构:Available / Max / Allocation / Need (= Max - Allocation)
 *   - 安全性算法:找 Need ≤ Available 的进程,假设完成释放资源,重复
 *   - 请求处理:Request ≤ Need;Request ≤ Available;试探分配后跑安全性算法
 *   - 安全序列:存在则安全,不存在则拒绝请求
 *   - 死锁四条件:互斥/占有等待/不剥夺/循环等待
 *
 * @业务 工业应用
 *   - 数据库事务锁管理 (类似避免策略)
 *   - Kubernetes 资源调度 (Pod 资源请求类似 Need)
 *   - 银行授信额度管理
 *   - 分布式系统资源预留
 *
 * @陷阱 408 高频
 *   - Need = Max - Allocation (常考)
 *   - 安全 ≠ 不死锁;不安全 ≠ 必死锁 (只是可能)
 *   - 安全序列不唯一,只要存在一个即可
 *   - 试探分配后必须还原 (回滚)
 *   - 银行家算法是死锁避免,不是预防/检测
 */
#ifndef CS408_OS_DEADLOCK_BANKER_H
#define CS408_OS_DEADLOCK_BANKER_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>

namespace cs408::os {

class Banker {
public:
    Banker(int num_proc, int num_res)
        : n_(num_proc), m_(num_res),
          available_(num_res, 0),
          max_(num_proc, std::vector<int>(num_res, 0)),
          allocation_(num_proc, std::vector<int>(num_res, 0)),
          need_(num_proc, std::vector<int>(num_res, 0)) {}

    void set_available(const std::vector<int>& a) { available_ = a; }
    void set_max(int p, const std::vector<int>& m) { max_[p] = m; recompute_need(); }
    void set_allocation(int p, const std::vector<int>& a) {
        allocation_[p] = a; recompute_need();
    }

    // 安全性算法 - 返回安全序列 (空表示不安全)
    std::vector<int> safety_check() const {
        std::vector<int> work = available_;
        std::vector<bool> finish(n_, false);
        std::vector<int> seq;
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < n_; ++i) {
                if (finish[i]) continue;
                bool ok = true;
                for (int j = 0; j < m_; ++j)
                    if (need_[i][j] > work[j]) { ok = false; break; }
                if (ok) {
                    for (int j = 0; j < m_; ++j) work[j] += allocation_[i][j];
                    finish[i] = true;
                    seq.push_back(i);
                    changed = true;
                }
            }
        }
        if (static_cast<int>(seq.size()) < n_) return {};  // 不安全
        return seq;
    }

    // 处理资源请求 - 返回是否安全 (安全则提交,不安全则回滚)
    bool request(int p, const std::vector<int>& req) {
        // 1. Request <= Need
        for (int j = 0; j < m_; ++j) {
            if (req[j] > need_[p][j]) {
                std::cout << "  错误:Request > Need\n";
                return false;
            }
            if (req[j] > available_[j]) {
                std::cout << "  阻塞:Request > Available,进程 P" << p << " 必须等待\n";
                return false;
            }
        }
        // 2. 试探分配
        for (int j = 0; j < m_; ++j) {
            available_[j]   -= req[j];
            allocation_[p][j] += req[j];
            need_[p][j]     -= req[j];
        }
        // 3. 安全性检查
        auto seq = safety_check();
        if (!seq.empty()) {
            std::cout << "  安全!安全序列: ";
            for (int x : seq) std::cout << "P" << x << " ";
            std::cout << "\n";
            return true;  // 提交
        }
        // 4. 不安全 -> 回滚
        for (int j = 0; j < m_; ++j) {
            available_[j]   += req[j];
            allocation_[p][j] -= req[j];
            need_[p][j]     += req[j];
        }
        std::cout << "  不安全,回滚,P" << p << " 必须等待\n";
        return false;
    }

    void print_state() const {
        std::cout << "PID  Max      Allocation  Need      Available\n";
        for (int i = 0; i < n_; ++i) {
            std::cout << "P" << i << "   ";
            print_row(max_[i]); std::cout << "  ";
            print_row(allocation_[i]); std::cout << "  ";
            print_row(need_[i]); std::cout << "  ";
            if (i == 0) { print_row(available_); }
            std::cout << "\n";
        }
    }

private:
    int n_, m_;
    std::vector<int> available_;
    std::vector<std::vector<int>> max_, allocation_, need_;

    void recompute_need() {
        for (int i = 0; i < n_; ++i)
            for (int j = 0; j < m_; ++j)
                need_[i][j] = max_[i][j] - allocation_[i][j];
    }

    void print_row(const std::vector<int>& v) const {
        for (int x : v) std::cout << x;
    }
};

// ===== 演示 =====
void banker_demo() {
    section("银行家算法 - 经典 5 进程 3 资源实例");
    Banker b(5, 3);
    b.set_available({3, 3, 2});
    b.set_max(0, {7,5,3}); b.set_allocation(0, {0,1,0});
    b.set_max(1, {3,2,2}); b.set_allocation(1, {2,0,0});
    b.set_max(2, {9,0,2}); b.set_allocation(2, {3,0,2});
    b.set_max(3, {2,2,2}); b.set_allocation(3, {2,1,1});
    b.set_max(4, {4,3,3}); b.set_allocation(4, {0,0,2});
    b.print_state();

    auto seq = b.safety_check();
    std::cout << "\n初始安全序列: ";
    for (int x : seq) std::cout << "P" << x << " ";
    std::cout << "\n";

    section("P1 请求 (1,0,2)");
    b.request(1, {1, 0, 2});

    section("P4 请求 (3,3,0) - 应被拒绝");
    b.request(4, {3, 3, 0});

    section("P0 请求 (0,2,0)");
    b.request(0, {0, 2, 0});
}

bool banker_test() {
    Banker b(5, 3);
    b.set_available({10, 5, 7});
    b.set_max(0, {7,5,3}); b.set_allocation(0, {0,1,0});
    b.set_max(1, {3,2,2}); b.set_allocation(1, {2,0,0});
    b.set_max(2, {9,0,2}); b.set_allocation(2, {3,0,2});
    b.set_max(3, {2,2,2}); b.set_allocation(3, {2,1,1});
    b.set_max(4, {4,3,3}); b.set_allocation(4, {0,0,2});

    auto seq = b.safety_check();
    CS408_EXPECT(!seq.empty());
    CS408_EXPECT_EQ(static_cast<int>(seq.size()), 5);

    // Need 验证
    CS408_EXPECT_EQ(b.safety_check().size(), 5u);
    return true;
}

CS408_REGISTER_MODULE(
    "operating-systems", "deadlock.banker", banker,
    "Available/Max/Allocation/Need=Max-Allocation;安全性算法找 Need<=Available;安全序列不唯一;试探分配+回滚",
    "DB 事务锁管理;K8s Pod 资源请求;银行授信;分布式资源预留",
    "Need=Max-Allocation;安全≠不死锁,不安全≠必死锁;试探分配后必须回滚;银行家是避免不是预防",
    banker_demo, banker_test
);

} // namespace cs408::os
#endif // CS408_OS_DEADLOCK_BANKER_H

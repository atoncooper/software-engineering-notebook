/**
 * @file heap.h
 * @topic 数据结构 - 树 - 堆 (大/小顶堆 + 堆排序 + TopK)
 *
 * @考点 408 大纲:数据结构 > 树 > 二叉堆
 *   - 完全二叉树 + 堆序性 (父 >= 子 或 父 <= 子)
 *   - 数组存储:父 i,左 2i+1,右 2i+2 (0-based)
 *   - 上浮 sift_up:插入末尾后向上调整 O(log n)
 *   - 下沉 sift_down:删除堆顶后向下调整 O(log n)
 *   - 建堆:从最后非叶节点 (n/2-1) 开始下沉,O(n)
 *   - 堆排序:建堆 O(n) + n 次取顶 O(n log n) = O(n log n)
 *
 * @业务 工业应用
 *   - 优先队列 (Dijkstra / A* / 任务调度)
 *   - TopK 问题 (海量数据求前 K)
 *   - 定时器 (Linux timerfd / Go time wheel 用最小堆)
 *   - 中位数流 (双堆:大顶堆+小顶堆)
 *   - std::priority_queue / Python heapq
 *
 * @陷阱 408 高频
 *   - 建堆时间 O(n),不是 O(n log n) (严格证明用级数)
 *   - 堆排序不稳定 (跨距离交换)
 *   - 优先队列 vs 堆:堆是存储,优先队列是接口
 *   - 堆不一定是完全二叉树?二叉堆是,但二项堆/斐波那契堆不是
 */
#ifndef CS408_DS_TREE_HEAP_H
#define CS408_DS_TREE_HEAP_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace cs408::ds {

template <typename T, typename Compare = std::less<T>>
class BinaryHeap {
public:
    BinaryHeap(Compare cmp = Compare()) : cmp_(cmp) {}

    void push(const T& v) {
        data_.push_back(v);
        sift_up(data_.size() - 1);
    }

    const T& top() const {
        if (data_.empty()) throw std::out_of_range("heap empty");
        return data_[0];
    }

    void pop() {
        if (data_.empty()) throw std::out_of_range("heap empty");
        data_[0] = data_.back();
        data_.pop_back();
        if (!data_.empty()) sift_down(0);
    }

    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    // 从数组建堆 O(n)
    void build_from(std::vector<T> arr) {
        data_ = std::move(arr);
        if (data_.empty()) return;
        for (int i = static_cast<int>(data_.size() / 2) - 1; i >= 0; --i) {
            sift_down(i);
        }
    }

    const std::vector<T>& raw() const { return data_; }

private:
    std::vector<T> data_;
    Compare cmp_;

    void sift_up(size_t i) {
        while (i > 0) {
            size_t p = (i - 1) / 2;
            if (cmp_(data_[p], data_[i])) {
                std::swap(data_[p], data_[i]);
                i = p;
            } else break;
        }
    }

    void sift_down(size_t i) {
        size_t n = data_.size();
        while (true) {
            size_t l = 2 * i + 1, r = 2 * i + 2, best = i;
            if (l < n && cmp_(data_[best], data_[l])) best = l;
            if (r < n && cmp_(data_[best], data_[r])) best = r;
            if (best == i) break;
            std::swap(data_[i], data_[best]);
            i = best;
        }
    }
};

// 堆排序 (原地)
template <typename T>
void heap_sort(std::vector<T>& a) {
    if (a.size() < 2) return;
    int n = static_cast<int>(a.size());
    // 建大顶堆
    auto sift_down = [&](int i, int sz) {
        while (true) {
            int l = 2 * i + 1, r = 2 * i + 2, best = i;
            if (l < sz && a[l] > a[best]) best = l;
            if (r < sz && a[r] > a[best]) best = r;
            if (best == i) break;
            std::swap(a[i], a[best]);
            i = best;
        }
    };
    for (int i = n / 2 - 1; i >= 0; --i) sift_down(i, n);
    for (int i = n - 1; i > 0; --i) {
        std::swap(a[0], a[i]);
        sift_down(0, i);
    }
}

// TopK:用小顶堆维护前 K 大
template <typename T>
std::vector<T> topk_largest(const std::vector<T>& arr, size_t k) {
    BinaryHeap<T, std::greater<T>> min_heap;  // 小顶堆
    for (const auto& x : arr) {
        if (min_heap.size() < k) min_heap.push(x);
        else if (x > min_heap.top()) { min_heap.pop(); min_heap.push(x); }
    }
    std::vector<T> r;
    while (!min_heap.empty()) { r.push_back(min_heap.top()); min_heap.pop(); }
    std::reverse(r.begin(), r.end());
    return r;
}

// ===== 演示 =====
void heap_demo() {
    section("大顶堆 push + pop");
    BinaryHeap<int> h;  // 默认 less -> 大顶堆
    for (int x : {3, 1, 5, 2, 8, 4, 7}) h.push(x);
    std::cout << "堆顶 (max): " << h.top() << "\n";
    std::cout << "依次出堆 (应降序): ";
    while (!h.empty()) { std::cout << h.top() << " "; h.pop(); }
    std::cout << "\n";

    section("建堆 O(n) — 从数组直接构造");
    BinaryHeap<int> h2;
    h2.build_from({3, 1, 5, 2, 8, 4, 7});
    std::cout << "堆顶: " << h2.top() << "\n";

    section("堆排序");
    std::vector<int> a = {9, 4, 7, 2, 8, 1, 5, 6, 3};
    heap_sort(a);
    std::cout << "排序后: "; print_vec(a);

    section("TopK (前 3 大)");
    auto top3 = topk_largest(std::vector<int>{9, 4, 7, 2, 8, 1, 5, 6, 3}, 3);
    std::cout << "前 3 大: "; print_vec(top3);
}

bool heap_test() {
    BinaryHeap<int> h;
    for (int x : {3, 1, 5, 2, 8}) h.push(x);
    CS408_EXPECT_EQ(h.top(), 8);
    h.pop(); CS408_EXPECT_EQ(h.top(), 5);

    std::vector<int> a = {5, 2, 8, 1, 9, 3, 7, 4, 6};
    heap_sort(a);
    CS408_EXPECT_EQ(a, (std::vector<int>{1,2,3,4,5,6,7,8,9}));

    auto top3 = topk_largest(std::vector<int>{5, 2, 8, 1, 9, 3, 7, 4, 6}, 3);
    CS408_EXPECT_EQ(top3, (std::vector<int>{9, 8, 7}));

    BinaryHeap<int> h2;
    h2.build_from({4, 1, 3, 2, 16, 9, 10, 14, 8, 7});
    CS408_EXPECT_EQ(h2.top(), 16);
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "tree.heap", heap,
    "完全二叉树+堆序性;父 i 子 2i+1/2i+2;上浮下沉 O(logn);建堆 O(n);堆排序不稳定",
    "优先队列 (Dijkstra/A*);TopK;定时器 (Linux timerfd);中位数流双堆;Python heapq",
    "建堆 O(n) 非 O(nlogn);堆排序不稳定;堆 vs 优先队列:存储 vs 接口",
    heap_demo, heap_test
);

} // namespace cs408::ds
#endif // CS408_DS_TREE_HEAP_H

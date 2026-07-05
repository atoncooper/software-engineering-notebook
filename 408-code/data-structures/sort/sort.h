/**
 * @file sort.h
 * @topic 数据结构 - 排序 (9 大排序算法完整实现)
 *
 * @考点 408 大纲:数据结构 > 排序
 *   ┌──────────┬────────────┬────────────┬──────────┬──────┐
 *   │ 算法     │ 平均       │ 最坏       │ 空间     │ 稳定 │
 *   ├──────────┼────────────┼────────────┼──────────┼──────┤
 *   │ 直接插入 │ O(n²)      │ O(n²)      │ O(1)     │ ✓   │
 *   │ 折半插入 │ O(n²)      │ O(n²)      │ O(1)     │ ✓   │
 *   │ 希尔     │ O(n^1.3)   │ O(n²)      │ O(1)     │ ✗   │
 *   │ 冒泡     │ O(n²)      │ O(n²)      │ O(1)     │ ✓   │
 *   │ 快排     │ O(nlogn)   │ O(n²)      │ O(logn)  │ ✗   │
 *   │ 简选     │ O(n²)      │ O(n²)      │ O(1)     │ ✗   │
 *   │ 堆排     │ O(nlogn)   │ O(nlogn)   │ O(1)     │ ✗   │
 *   │ 归并     │ O(nlogn)   │ O(nlogn)   │ O(n)     │ ✓   │
 *   │ 基数     │ O(d(n+r))  │ O(d(n+r))  │ O(r)     │ ✓   │
 *   └──────────┴────────────┴────────────┴──────────┴──────┘
 *
 * @业务 工业应用
 *   - std::sort IntroSort (快排+堆排+插排混合,递归深时切堆排)
 *   - 数据库 ExternalSort (大数据归并排序)
 *   - Python TimSort (归并+插入,利用已排序段)
 *   - V8 引擎 Array.sort 用 TimSort
 *
 * @陷阱 408 高频
 *   - 快排最坏 O(n²):基本有序 + 首枢轴;随机化/三数取中避免
 *   - 快排第一趟后:枢轴必在最终位置
 *   - 堆排建堆 O(n),不是 O(nlogn)
 *   - 归并排序空间 O(n) (需要辅助数组)
 *   - 希尔排序不稳定 (跨距离交换)
 *   - 基数排序需要 O(r) 额外空间,适合位数 d 小的整数
 *   - 折半插入:查找 O(logn) 但移动 O(n),总仍 O(n²)
 */
#ifndef CS408_DS_SORT_SORT_H
#define CS408_DS_SORT_SORT_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <algorithm>
#include <iostream>

namespace cs408::ds {

// ===== 1. 直接插入 =====
template <typename T>
void insertion_sort(std::vector<T>& a) {
    for (size_t i = 1; i < a.size(); ++i) {
        T key = a[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && a[j] > key) { a[j+1] = a[j]; --j; }
        a[j+1] = key;
    }
}

// ===== 2. 折半插入 (查找用二分,移动仍 O(n)) =====
template <typename T>
void binary_insertion_sort(std::vector<T>& a) {
    for (size_t i = 1; i < a.size(); ++i) {
        T key = a[i];
        int lo = 0, hi = static_cast<int>(i);
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (a[mid] > key) hi = mid; else lo = mid + 1;
        }
        for (int j = i; j > lo; --j) a[j] = a[j-1];
        a[lo] = key;
    }
}

// ===== 3. 希尔排序 (分组直接插入,gap 递减) =====
template <typename T>
void shell_sort(std::vector<T>& a) {
    int n = static_cast<int>(a.size());
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; ++i) {
            T key = a[i];
            int j = i - gap;
            while (j >= 0 && a[j] > key) { a[j+gap] = a[j]; j -= gap; }
            a[j+gap] = key;
        }
    }
}

// ===== 4. 冒泡排序 =====
template <typename T>
void bubble_sort(std::vector<T>& a) {
    int n = static_cast<int>(a.size());
    for (int i = 0; i < n - 1; ++i) {
        bool swapped = false;
        for (int j = 0; j < n - 1 - i; ++j) {
            if (a[j] > a[j+1]) { std::swap(a[j], a[j+1]); swapped = true; }
        }
        if (!swapped) break;  // 一趟无交换 -> 已有序
    }
}

// ===== 5. 快速排序 (三数取中选枢轴) =====
template <typename T>
void quick_sort(std::vector<T>& a) {
    std::function<void(int,int)> rec = [&](int lo, int hi) {
        if (lo >= hi) return;
        // 三数取中
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < a[lo]) std::swap(a[lo], a[mid]);
        if (a[hi]  < a[lo]) std::swap(a[lo], a[hi]);
        if (a[mid] < a[hi]) std::swap(a[mid], a[hi]);
        T pivot = a[hi];
        int i = lo;
        for (int j = lo; j < hi; ++j) {
            if (a[j] < pivot) { std::swap(a[i], a[j]); ++i; }
        }
        std::swap(a[i], a[hi]);
        rec(lo, i - 1);
        rec(i + 1, hi);
    };
    rec(0, static_cast<int>(a.size()) - 1);
}

// ===== 6. 简单选择排序 =====
template <typename T>
void selection_sort(std::vector<T>& a) {
    int n = static_cast<int>(a.size());
    for (int i = 0; i < n - 1; ++i) {
        int min_idx = i;
        for (int j = i + 1; j < n; ++j) if (a[j] < a[min_idx]) min_idx = j;
        std::swap(a[i], a[min_idx]);
    }
}

// ===== 7. 堆排序 (复用 heap.h 的实现,避免重复定义) =====
//   template <typename T> void heap_sort(std::vector<T>& a);
//   见 data-structures/tree/heap.h

// ===== 8. 归并排序 =====
template <typename T>
void merge_sort(std::vector<T>& a) {
    std::vector<T> tmp(a.size());
    std::function<void(int,int)> rec = [&](int lo, int hi) {
        if (lo >= hi) return;
        int mid = (lo + hi) / 2;
        rec(lo, mid); rec(mid + 1, hi);
        int i = lo, j = mid + 1, k = lo;
        while (i <= mid && j <= hi) {
            if (a[i] <= a[j]) tmp[k++] = a[i++];
            else              tmp[k++] = a[j++];
        }
        while (i <= mid) tmp[k++] = a[i++];
        while (j <= hi)  tmp[k++] = a[j++];
        for (int x = lo; x <= hi; ++x) a[x] = tmp[x];
    };
    rec(0, static_cast<int>(a.size()) - 1);
}

// ===== 9. 基数排序 (LSD,低位优先,十进制) =====
void radix_sort(std::vector<int>& a) {
    if (a.empty()) return;
    int maxv = *std::max_element(a.begin(), a.end());
    for (int exp = 1; maxv / exp > 0; exp *= 10) {
        std::vector<int> bucket[10];
        for (int x : a) bucket[(x / exp) % 10].push_back(x);
        a.clear();
        for (int b = 0; b < 10; ++b)
            for (int x : bucket[b]) a.push_back(x);
    }
}

// ===== 计数排序 (非比较,适合小范围整数) =====
void counting_sort(std::vector<int>& a, int max_val) {
    std::vector<int> count(max_val + 1, 0);
    for (int x : a) ++count[x];
    a.clear();
    for (int v = 0; v <= max_val; ++v)
        for (int c = 0; c < count[v]; ++c) a.push_back(v);
}

// ===== 演示 =====
void sort_demo() {
    std::vector<int> a = {9, 4, 7, 2, 8, 1, 5, 6, 3};
    auto test_all = [&](const std::string& name, auto fn) {
        std::vector<int> v = a;
        fn(v);
        std::cout << name << ": "; print_vec(v);
    };
    section("9 大排序算法对比 (输入: 9,4,7,2,8,1,5,6,3)");
    test_all("插入    ", [](std::vector<int>& v){ insertion_sort(v); });
    test_all("折半插入", [](std::vector<int>& v){ binary_insertion_sort(v); });
    test_all("希尔    ", [](std::vector<int>& v){ shell_sort(v); });
    test_all("冒泡    ", [](std::vector<int>& v){ bubble_sort(v); });
    test_all("快排    ", [](std::vector<int>& v){ quick_sort(v); });
    test_all("简单选择", [](std::vector<int>& v){ selection_sort(v); });
    test_all("堆排    ", [](std::vector<int>& v){ heap_sort(v); });
    test_all("归并    ", [](std::vector<int>& v){ merge_sort(v); });
    test_all("基数    ", [](std::vector<int>& v){ radix_sort(v); });

    section("稳定性验证 (相同 key 不同卫星数据)");
    struct Item { int key; char tag; };
    std::vector<Item> items = {{3,'a'},{1,'b'},{3,'c'},{2,'d'},{1,'e'}};
    // 简单冒泡 (稳定)
    int n = items.size();
    for (int i = 0; i < n-1; ++i)
        for (int j = 0; j < n-1-i; ++j)
            if (items[j].key > items[j+1].key) std::swap(items[j], items[j+1]);
    std::cout << "稳定排序后: ";
    for (auto& it : items) std::cout << it.key << it.tag << " ";
    std::cout << "\n(应保持 1b 在 1e 前, 3a 在 3c 前)\n";
}

bool sort_test() {
    std::vector<int> expect = {1,2,3,4,5,6,7,8,9};
    auto check = [&](auto fn) -> bool {
        std::vector<int> v = {9,4,7,2,8,1,5,6,3};
        fn(v);
        return v == expect;
    };
    CS408_EXPECT(check([](std::vector<int>& v){ insertion_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ binary_insertion_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ shell_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ bubble_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ quick_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ selection_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ heap_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ merge_sort(v); }));
    CS408_EXPECT(check([](std::vector<int>& v){ radix_sort(v); }));
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "sort", sort,
    "9 大排序:插入/折半/希尔/冒泡/快排/选择/堆排/归并/基数;稳定性与复杂度表",
    "std::sort IntroSort;数据库 ExternalSort;Python TimSort;V8 TimSort",
    "快排最坏 O(n^2) 基本有序+首枢轴;快排第一趟后枢轴在最终位;堆排建堆 O(n);归并空间 O(n);希尔不稳定;折半插入仍 O(n^2)",
    sort_demo, sort_test
);

} // namespace cs408::ds
#endif // CS408_DS_SORT_SORT_H

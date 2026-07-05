/**
 * @file linked_list.h
 * @topic 数据结构 - 线性表 - 链表 (单/双/循环)
 *
 * @考点 408 大纲:数据结构 > 线性表 > 链式存储
 *   - 单链表 / 双链表 / 循环链表 / 静态链表
 *   - 头插法 (逆序) vs 尾插法 (顺序)
 *   - 时间复杂度:查找 O(n),插入/删除 O(1) (已知节点位置)
 *   - 真题陷阱:删除节点需前驱指针;双链表解决单链表回溯问题
 *
 * @业务 工业应用
 *   - Linux kernel list_head 侵入式双循环链表 (零开销)
 *   - LRU Cache 实现底层 (哈希 + 双链表)
 *   - 内存分配器 free list (glibc malloc fastbin 单链表)
 *   - Redis ziplist/listpack (压缩列表)
 *
 * @陷阱 408 高频
 *   - 头结点作用:简化边界 (空表/首节点插入无需特判)
 *   - 双链表空间换时间:多一个指针,删除 O(1)
 *   - 循环链表判空:head->next == head
 *   - 静态链表用数组模拟,适合无指针语言
 */
#ifndef CS408_DS_LINEAR_LINKED_LIST_H
#define CS408_DS_LINEAR_LINKED_LIST_H

#include "common/types.h"
#include "common/utils.h"
#include <memory>
#include <vector>
#include <iostream>

namespace cs408::ds {

// ===== 单链表 =====
template <typename T>
struct SinglyNode {
    T data;
    SinglyNode* next;
    explicit SinglyNode(const T& v) : data(v), next(nullptr) {}
};

template <typename T>
class LinkedList {
public:
    LinkedList() : head_(nullptr), size_(0) {}
    ~LinkedList() { clear(); }

    // 头插法 — 链表逆序 (常用于反序构造)
    void push_front(const T& v) {
        SinglyNode<T>* n = new SinglyNode<T>(v);
        n->next = head_;
        head_ = n;
        ++size_;
    }

    // 尾插法 — 链表正序
    void push_back(const T& v) {
        SinglyNode<T>* n = new SinglyNode<T>(v);
        if (!head_) { head_ = n; ++size_; return; }
        SinglyNode<T>* p = head_;
        while (p->next) p = p->next;
        p->next = n;
        ++size_;
    }

    // 删除首个等于 v 的节点 (需前驱指针)
    bool remove(const T& v) {
        SinglyNode<T>* prev = nullptr;
        SinglyNode<T>* cur  = head_;
        while (cur) {
            if (cur->data == v) {
                if (prev) prev->next = cur->next;
                else      head_ = cur->next;
                delete cur;
                --size_;
                return true;
            }
            prev = cur;
            cur  = cur->next;
        }
        return false;
    }

    // 反转 — 三指针法 (经典考点)
    void reverse() {
        SinglyNode<T>* prev = nullptr;
        SinglyNode<T>* cur  = head_;
        while (cur) {
            SinglyNode<T>* nxt = cur->next;
            cur->next = prev;
            prev = cur;
            cur  = nxt;
        }
        head_ = prev;
    }

    // 查找 — O(n)
    const SinglyNode<T>* find(const T& v) const {
        for (SinglyNode<T>* p = head_; p; p = p->next)
            if (p->data == v) return p;
        return nullptr;
    }

    size_t size() const { return size_; }
    bool empty() const { return head_ == nullptr; }

    void clear() {
        SinglyNode<T>* p = head_;
        while (p) {
            SinglyNode<T>* nxt = p->next;
            delete p;
            p = nxt;
        }
        head_ = nullptr;
        size_ = 0;
    }

    std::vector<T> to_vector() const {
        std::vector<T> v; v.reserve(size_);
        for (SinglyNode<T>* p = head_; p; p = p->next) v.push_back(p->data);
        return v;
    }

    void print(const std::string& label = "") const {
        if (!label.empty()) std::cout << label << ": ";
        std::cout << "head";
        for (SinglyNode<T>* p = head_; p; p = p->next)
            std::cout << " -> " << p->data;
        std::cout << " -> null\n";
    }

private:
    SinglyNode<T>* head_;
    size_t size_;
};

// ===== 双链表 (双向循环) =====
template <typename T>
struct DoublyNode {
    T data;
    DoublyNode* prev;
    DoublyNode* next;
    explicit DoublyNode(const T& v) : data(v), prev(nullptr), next(nullptr) {}
};

template <typename T>
class DoublyLinkedList {
public:
    DoublyLinkedList() {
        sentinel_ = new DoublyNode<T>(T{});
        sentinel_->next = sentinel_;
        sentinel_->prev = sentinel_;
        size_ = 0;
    }
    ~DoublyLinkedList() { clear(); delete sentinel_; }

    void push_back(const T& v) { insert_before(sentinel_, v); }
    void push_front(const T& v) { insert_after(sentinel_, v); }

    void insert_after(DoublyNode<T>* pos, const T& v) {
        DoublyNode<T>* n = new DoublyNode<T>(v);
        n->prev = pos;
        n->next = pos->next;
        pos->next->prev = n;
        pos->next = n;
        ++size_;
    }

    void insert_before(DoublyNode<T>* pos, const T& v) {
        insert_after(pos->prev, v);
    }

    // O(1) 删除 (双链表优势)
    void erase(DoublyNode<T>* n) {
        n->prev->next = n->next;
        n->next->prev = n->prev;
        delete n;
        --size_;
    }

    DoublyNode<T>* head() const { return sentinel_->next; }
    DoublyNode<T>* tail() const { return sentinel_->prev; }
    size_t size() const { return size_; }
    bool empty() const { return sentinel_->next == sentinel_; }

    void clear() {
        DoublyNode<T>* p = sentinel_->next;
        while (p != sentinel_) {
            DoublyNode<T>* nxt = p->next;
            delete p;
            p = nxt;
        }
        sentinel_->next = sentinel_;
        sentinel_->prev = sentinel_;
        size_ = 0;
    }

private:
    DoublyNode<T>* sentinel_;  // 哨兵节点 (头结点)
    size_t size_;
};

// ===== 演示与测试 =====
void linked_list_demo() {
    section("单链表 - 头插法 (逆序构造)");
    LinkedList<int> L1;
    for (int x : {1, 2, 3, 4, 5}) L1.push_front(x);
    L1.print("头插 [1,2,3,4,5]");

    section("单链表 - 尾插法 (顺序构造)");
    LinkedList<int> L2;
    for (int x : {1, 2, 3, 4, 5}) L2.push_back(x);
    L2.print("尾插 [1,2,3,4,5]");

    section("单链表 - 删除 + 反转");
    L2.remove(3);
    L2.print("删除 3");
    L2.reverse();
    L2.print("反转");

    section("双链表 - 哨兵节点 (双向循环)");
    DoublyLinkedList<int> D;
    for (int x : {10, 20, 30, 40}) D.push_back(x);
    auto* p = D.head();
    std::cout << "正向遍历: ";
    for (size_t i = 0; i < D.size(); ++i, p = p->next) std::cout << p->data << " ";
    std::cout << "\n反向遍历: ";
    p = D.tail();
    for (size_t i = 0; i < D.size(); ++i, p = p->prev) std::cout << p->data << " ";
    std::cout << "\n";
}

bool linked_list_test() {
    LinkedList<int> L;
    for (int x : {1, 2, 3}) L.push_back(x);
    CS408_EXPECT_EQ(L.size(), 3u);
    L.reverse();
    CS408_EXPECT_EQ(L.to_vector(), (std::vector<int>{3, 2, 1}));
    L.remove(2);
    CS408_EXPECT_EQ(L.to_vector(), (std::vector<int>{3, 1}));
    CS408_EXPECT(L.find(3) != nullptr);
    CS408_EXPECT(L.find(99) == nullptr);
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "linear.linked_list", linked_list,
    "单/双/循环链表;头插法逆序;反转三指针;双链表 O(1) 删除",
    "Linux list_head 侵入式双循环链表;LRU Cache;glibc free list;Redis listpack",
    "头结点简化边界;循环链表判空 head->next==head;静态链表数组模拟",
    linked_list_demo, linked_list_test
);

} // namespace cs408::ds
#endif // CS408_DS_LINEAR_LINKED_LIST_H

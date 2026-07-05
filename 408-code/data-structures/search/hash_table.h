/**
 * @file hash_table.h
 * @topic 数据结构 - 查找 - 哈希表 (链地址法 + 开放寻址)
 *
 * @考点 408 大纲:数据结构 > 查找 > 散列表
 *   - 哈希函数:除留余数法 H(key) = key % p (p 为不大于表长的最大质数)
 *   - 冲突解决:
 *     1) 开放寻址 (线性探测/二次探测/双重散列)
 *     2) 链地址法 (拉链法)
 *   - 装填因子 α = n/m:n 元素,m 槽位;α 越大冲突越多
 *   - 查找成功 ASL vs 查找失败 ASL (常考计算)
 *   - 线性探测易聚集 (clustering);二次探测避免主聚集
 *
 * @业务 工业应用
 *   - std::unordered_map / HashMap (链地址法 + 红黑树退化)
 *   - Redis dict (渐进式 rehash)
 *   - 数据库 Hash Join / Hash Index
 *   - 布隆过滤器 (哈希的位图扩展)
 *   - 一致性哈希 (分布式存储负载均衡)
 *
 * @陷阱 408 高频
 *   - 除留余数 p 必须是质数 (减少冲突)
 *   - 线性探测查找失败 ASL = 探测到空槽为止的次数 / 表长
 *   - 链地址法 ASL 与装填因子 α 有关,均匀分布 α/1 (成功)
 *   - 删除开放寻址需标记 "墓碑" (tombstone),不能直接删否则断链
 *   - 哈希表平均 O(1),最坏 O(n) (全部冲突)
 */
#ifndef CS408_DS_SEARCH_HASH_TABLE_H
#define CS408_DS_SEARCH_HASH_TABLE_H

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <list>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace cs408::ds {

// ===== 链地址法 (拉链法) =====
template <typename K, typename V>
class HashTableChain {
public:
    explicit HashTableChain(size_t m = 11) : table_(m), size_(0) {}

    void insert(const K& key, const V& val) {
        size_t h = hash(key);
        for (auto& p : table_[h]) {
            if (p.first == key) { p.second = val; return; }
        }
        table_[h].push_back({key, val});
        ++size_;
    }

    std::optional<V> find(const K& key) const {
        size_t h = hash(key);
        for (const auto& p : table_[h]) {
            if (p.first == key) return p.second;
        }
        return std::nullopt;
    }

    bool erase(const K& key) {
        size_t h = hash(key);
        for (auto it = table_[h].begin(); it != table_[h].end(); ++it) {
            if (it->first == key) { table_[h].erase(it); --size_; return true; }
        }
        return false;
    }

    size_t size() const { return size_; }
    double load_factor() const { return static_cast<double>(size_) / table_.size(); }

    // ASL 查找成功:每个元素的探测次数之和 / 元素数
    double asl_success() const {
        if (size_ == 0) return 0;
        size_t total = 0;
        for (const auto& bucket : table_) {
            size_t depth = 0;
            for (const auto& p : bucket) {
                ++depth;
                total += depth;
            }
        }
        return static_cast<double>(total) / size_;
    }

    // ASL 查找失败:每个槽位探测到空为止的次数 / 表长
    double asl_failure() const {
        size_t total = 0;
        for (const auto& bucket : table_) {
            total += bucket.size() + 1;
        }
        return static_cast<double>(total) / table_.size();
    }

private:
    std::vector<std::list<std::pair<K,V>>> table_;
    size_t size_;

    size_t hash(const K& key) const {
        return static_cast<size_t>(key) % table_.size();
    }
};

// ===== 开放寻址 - 线性探测 =====
template <typename K, typename V>
class HashTableOpen {
public:
    enum class SlotState { EMPTY, OCCUPIED, TOMBSTONE };

    explicit HashTableOpen(size_t m = 13) : table_(m), state_(m, SlotState::EMPTY), size_(0) {}

    bool insert(const K& key, const V& val) {
        if (size_ * 2 >= table_.size()) return false;  // 装填因子 > 0.5 拒绝
        size_t h0 = hash(key);
        for (size_t i = 0; i < table_.size(); ++i) {
            size_t idx = (h0 + i) % table_.size();
            if (state_[idx] != SlotState::OCCUPIED) {
                table_[idx] = {key, val};
                state_[idx] = SlotState::OCCUPIED;
                ++size_;
                return true;
            }
            if (state_[idx] == SlotState::OCCUPIED && table_[idx].first == key) {
                table_[idx].second = val;  // 已存在,更新
                return true;
            }
        }
        return false;
    }

    std::optional<V> find(const K& key) const {
        size_t h0 = hash(key);
        for (size_t i = 0; i < table_.size(); ++i) {
            size_t idx = (h0 + i) % table_.size();
            if (state_[idx] == SlotState::EMPTY) return std::nullopt;
            if (state_[idx] == SlotState::OCCUPIED && table_[idx].first == key)
                return table_[idx].second;
        }
        return std::nullopt;
    }

    bool erase(const K& key) {
        size_t h0 = hash(key);
        for (size_t i = 0; i < table_.size(); ++i) {
            size_t idx = (h0 + i) % table_.size();
            if (state_[idx] == SlotState::EMPTY) return false;
            if (state_[idx] == SlotState::OCCUPIED && table_[idx].first == key) {
                state_[idx] = SlotState::TOMBSTONE;  // 墓碑标记
                --size_;
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::pair<K,V>> table_;
    std::vector<SlotState> state_;
    size_t size_;
    size_t hash(const K& key) const { return static_cast<size_t>(key) % table_.size(); }
};

// ===== 演示 =====
void hash_table_demo() {
    section("链地址法哈希表 (m=11)");
    HashTableChain<int, std::string> h(11);
    int keys[] = {12, 23, 34, 45, 56, 67, 78};
    for (int k : keys) h.insert(k, "V" + std::to_string(k));
    std::cout << "插入 7 个元素后:\n";
    std::cout << "  size = " << h.size() << "\n";
    std::cout << "  装填因子 α = " << h.load_factor() << "\n";
    std::cout << "  ASL(成功) = " << h.asl_success() << "\n";
    std::cout << "  ASL(失败) = " << h.asl_failure() << "\n";

    std::cout << "查找 34: " << h.find(34).value_or("NOT FOUND") << "\n";
    std::cout << "查找 99: " << h.find(99).value_or("NOT FOUND") << "\n";

    section("开放寻址 - 线性探测 (m=13)");
    HashTableOpen<int, std::string> ho(13);
    for (int k : {12, 25, 38, 13}) ho.insert(k, "V" + std::to_string(k));
    std::cout << "插入 12,25,38,13 (与 12,25,38 冲突,线性探测)\n";
    std::cout << "查找 25: " << ho.find(25).value_or("NOT FOUND") << "\n";
    std::cout << "查找 38: " << ho.find(38).value_or("NOT FOUND") << "\n";
    ho.erase(25);
    std::cout << "删除 25 后查找 25: " << ho.find(25).value_or("NOT FOUND") << "\n";
    std::cout << "查找 38 (墓碑不阻塞): " << ho.find(38).value_or("NOT FOUND") << "\n";
}

bool hash_table_test() {
    HashTableChain<int, int> h(7);
    h.insert(1, 100); h.insert(8, 200); h.insert(15, 300);  // 全部冲突到槽 1
    CS408_EXPECT_EQ(h.find(1).value(), 100);
    CS408_EXPECT_EQ(h.find(8).value(), 200);
    CS408_EXPECT_EQ(h.find(15).value(), 300);
    CS408_EXPECT(!h.find(99).has_value());
    h.erase(8);
    CS408_EXPECT(!h.find(8).has_value());
    CS408_EXPECT(h.find(15).has_value());

    HashTableOpen<int, int> ho(13);
    ho.insert(1, 100); ho.insert(14, 200); ho.insert(27, 300);
    CS408_EXPECT_EQ(ho.find(1).value(), 100);
    CS408_EXPECT_EQ(ho.find(14).value(), 200);
    CS408_EXPECT_EQ(ho.find(27).value(), 300);
    ho.erase(14);
    CS408_EXPECT(!ho.find(14).has_value());
    CS408_EXPECT(ho.find(27).has_value());  // 墓碑不阻塞
    return true;
}

CS408_REGISTER_MODULE(
    "data-structures", "search.hash_table", hash_table,
    "除留余数法;链地址 vs 开放寻址(线性/二次/双散列);ASL 成功/失败;装填因子 α",
    "std::unordered_map;Redis dict 渐进 rehash;Hash Join;Bloom Filter;一致性哈希",
    "除留余数 p 取质数;线性探测查找失败 ASL 算到空槽;开放寻址删除用墓碑;链地址 ASL≈α",
    hash_table_demo, hash_table_test
);

} // namespace cs408::ds
#endif // CS408_DS_SEARCH_HASH_TABLE_H

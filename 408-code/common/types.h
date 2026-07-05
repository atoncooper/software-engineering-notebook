/**
 * @file types.h
 * @brief 通用类型别名与基础工具
 *
 * @考点 408 中常出现的类型:地址 (uint32_t 模拟 32 位)、字节 (uint8_t)、
 *       字 (uint16_t)、双字 (uint32_t)、机器字 (uint64_t)
 * @业务 真实系统中 typedef 是抽象层,隔离底层变化
 * @陷阱 408 真题常用 32 位地址 + 4KB 页,注意位数对齐
 */
#ifndef CS408_COMMON_TYPES_H
#define CS408_COMMON_TYPES_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <ostream>

namespace cs408 {

// 408 标准类型别名 (与计组/网络对齐)
using Byte  = uint8_t;
using Word  = uint16_t;
using DWord = uint32_t;
using QWord = uint64_t;

using Addr  = uint32_t;   // 32 位虚拟地址
using Frame = uint32_t;   // 物理页框号
using Page  = uint32_t;   // 虚拟页号

// 进程/线程 ID
using Pid = uint32_t;

// 时间戳 (毫秒)
using Time = uint64_t;

// 通用二元组 (链表节点、图边)
template <typename A, typename B>
struct Pair {
    A first;
    B second;
};

// 图边
struct Edge {
    int from;
    int to;
    int weight;
    Edge(int f, int t, int w = 1) : from(f), to(t), weight(w) {}
};

// 模块元信息 (CLI 浏览用)
struct ModuleInfo {
    std::string category;     // data-structures / os / co / networks
    std::string topic;        // 排序 / 调度 / Cache
    std::string name;         // quick_sort / fcfs / direct_mapped
    std::string exam_focus;   // 考点
    std::string business;     // 业务
    std::string traps;        // 陷阱

    // 演示与测试函数指针
    std::function<void()> demo;
    std::function<bool()> test;
};

// 模块注册中心 (所有模块自注册到这里,CLI 遍历)
class Registry {
public:
    static Registry& instance() {
        static Registry r;
        return r;
    }
    void add(ModuleInfo m) { modules_.push_back(std::move(m)); }
    const std::vector<ModuleInfo>& all() const { return modules_; }
private:
    std::vector<ModuleInfo> modules_;
};

// 自动注册辅助类 (模块 .cpp 顶部声明一个静态实例即可注册)
struct AutoRegister {
    AutoRegister(ModuleInfo m) { Registry::instance().add(std::move(m)); }
};

#define CS408_REGISTER_MODULE(category, topic, name, exam, biz, trap, demoFn, testFn) \
    static cs408::AutoRegister _reg_##name { \
        cs408::ModuleInfo{category, topic, #name, exam, biz, trap, demoFn, testFn} \
    };

} // namespace cs408

#endif // CS408_COMMON_TYPES_H

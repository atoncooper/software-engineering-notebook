/**
 * @file 408-cli.cpp
 * @brief CLI 浏览器:列出 / 查看 / 演示 / 测试 任意模块
 *
 * 用法:
 *   408-cli list                          # 列出所有已注册模块
 *   408-cli show <category.topic.name>    # 查看模块元信息 (考点/业务/陷阱)
 *   408-cli demo <category.topic.name>    # 运行模块演示
 *   408-cli test [name]                   # 跑测试 (无参=全部)
 *
 * @考点 CLI 本身不考;只是把 4 科模块统一编排,便于复习时快速浏览
 * @业务 工业代码常用 CLI 子命令模式 (git/kubectl)
 * @陷阱 模块通过静态注册自登记,主程序只 include 头文件即可
 */
#include "common/types.h"
#include "common/test_framework.h"
#include "common/utils.h"

#include <iostream>
#include <string>
#include <vector>

// 显式 include 各旗舰模块头,触发静态注册
// (数据结构)
#include "data-structures/linear/linked_list.h"
#include "data-structures/linear/stack.h"
#include "data-structures/linear/queue.h"
#include "data-structures/tree/bst.h"
#include "data-structures/tree/heap.h"
#include "data-structures/graph/graph.h"
#include "data-structures/sort/sort.h"
#include "data-structures/search/hash_table.h"
// (操作系统)
#include "operating-systems/process/scheduler.h"
#include "operating-systems/process/sync.h"
#include "operating-systems/deadlock/banker.h"
#include "operating-systems/memory/page_replacement.h"
#include "operating-systems/io/disk_scheduler.h"
// (计组)
#include "computer-organization/data/ieee754.h"
#include "computer-organization/data/crc.h"
#include "computer-organization/data/hamming.h"
#include "computer-organization/data/performance.h"
#include "computer-organization/memory/cache.h"
#include "computer-organization/memory/memory_hierarchy.h"
#include "computer-organization/instruction/isa.h"
#include "computer-organization/cpu/pipeline.h"
#include "computer-organization/cpu/branch_predictor.h"
#include "computer-organization/cpu/cpu_datapath.h"
#include "computer-organization/cpu/assembly_sim.h"
#include "computer-organization/bus/bus.h"
#include "computer-organization/io/io_modes.h"
// (网络)
#include "computer-networks/physical/nyquist_shannon.h"
#include "computer-networks/data-link/sliding_window.h"
#include "computer-networks/data-link/csma_cd.h"
#include "computer-networks/network/subnet.h"
#include "computer-networks/transport/tcp_state.h"
#include "computer-networks/transport/tcp_congestion.h"

using namespace cs408;

static void print_help() {
    std::cout <<
        "408-cli — 408 知识点 CLI 浏览器\n"
        "用法:\n"
        "  408-cli list                       列出所有已注册模块\n"
        "  408-cli show  <cat.topic.name>     查看模块元信息\n"
        "  408-cli demo  <cat.topic.name>     运行模块演示\n"
        "  408-cli test  [name]               运行测试 (无参=全部)\n"
        "  408-cli help                       显示本帮助\n";
}

static void list_modules() {
    const auto& ms = Registry::instance().all();
    std::cout << "已注册 " << ms.size() << " 个模块:\n\n";
    std::string last_cat;
    for (const auto& m : ms) {
        if (m.category != last_cat) {
            std::cout << "\n[" << m.category << "]\n";
            last_cat = m.category;
        }
        std::cout << "  " << m.category << "." << m.topic << "." << m.name
                  << "  — " << m.exam_focus.substr(0, 60) << "...\n";
    }
}

static const ModuleInfo* find_module(const std::string& key) {
    const auto& ms = Registry::instance().all();
    for (const auto& m : ms) {
        std::string full = m.category + "." + m.topic + "." + m.name;
        if (full == key) return &m;
    }
    return nullptr;
}

static void show_module(const std::string& key) {
    const ModuleInfo* m = find_module(key);
    if (!m) { std::cerr << "未找到模块: " << key << "\n"; return; }
    std::cout <<
        "模块: " << m->category << "." << m->topic << "." << m->name << "\n"
        "---- 考点 ----\n" << m->exam_focus << "\n\n"
        "---- 业务 ----\n" << m->business << "\n\n"
        "---- 陷阱 ----\n" << m->traps << "\n";
}

static void run_demo(const std::string& key) {
    const ModuleInfo* m = find_module(key);
    if (!m) { std::cerr << "未找到模块: " << key << "\n"; return; }
    if (!m->demo) { std::cerr << "模块无 demo\n"; return; }
    section(m->category + "." + m->topic + "." + m->name + " DEMO");
    m->demo();
}

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 0; }

    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        print_help();
    } else if (cmd == "list") {
        list_modules();
    } else if (cmd == "show" && argc >= 3) {
        show_module(argv[2]);
    } else if (cmd == "demo" && argc >= 3) {
        run_demo(argv[2]);
    } else if (cmd == "test") {
        // 遍历所有已注册模块,运行其 test 函数
        const auto& ms = Registry::instance().all();
        int passed = 0, total = 0;
        for (const auto& m : ms) {
            if (!m.test) continue;
            ++total;
            bool ok = false;
            try { ok = m.test(); }
            catch (const std::exception& e) {
                std::cerr << "[EXCEPT] " << m.name << ": " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[EXCEPT] " << m.name << ": unknown\n";
            }
            if (ok) { ++passed; std::cout << "[PASS] " << m.category << "." << m.topic << "." << m.name << "\n"; }
            else    { std::cout << "[FAIL] " << m.category << "." << m.topic << "." << m.name << "\n"; }
        }
        std::cout << "\n=== " << passed << "/" << total << " modules passed ===\n";
        return (passed == total) ? 0 : 1;
    } else {
        print_help();
        return 1;
    }
    return 0;
}

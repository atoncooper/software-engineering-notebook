/**
 * @file cpu_datapath.h
 * @topic 计组 - 中央处理器 (寄存器组/数据通路/硬布线 vs 微程序/指令周期)
 *
 * @考点 408 大纲:计算机组成原理 > 中央处理器
 *   - 寄存器组:
 *     · 通用: GPR (R0~R31)
 *     · 专用: PC (程序计数器), IR (指令寄存器), MAR (主存地址), MDR (主存数据)
 *             PSW (程序状态字, 含 CF/ZF/SF/OF), ALU, 暂存器
 *   - 指令周期:
 *     · 取指 (IF): PC → MAR → 主存 → MDR → IR; PC+1
 *     · 间址 (IND): 形式地址 → MAR → 主存 → MDR (得有效地址)
 *     · 执行 (EX): 按操作码执行
 *     · 中断 (INT): 保存现场, 跳到中断服务程序
 *   - 数据通路:CPU 内部数据流动的路径
 *     · 单总线: 一组总线, 同时只能一个传输, 简单但慢
 *     · 双总线: 两路并行
 *     · 三总线: ALU 两输入一输出, 最快
 *   - 控制方式:
 *     · 硬布线 (组合逻辑): 速度快, 修改难, RISC 用
 *     · 微程序 (存储逻辑): 速度慢, 修改易, CISC 用
 *       - 微指令 = 控制存储器中的一位串
 *       - 微操作 = 一条机器指令分解为多条微指令
 *       - 取指微程序公用
 *
 * @业务 工业应用
 *   - Intel x86 微码 (微程序, 可刷新, 修复 bug)
 *   - Apple Silicon (硬布线, 高频 4GHz+)
 *   - RISC-V Rocket/BOOM (硬布线)
 *   - AMD Zen (微操作缓存 μop cache)
 *   - GPU SIMT 数据通路
 *
 * @陷阱 408 高频
 *   - PC 自动加 1 (或 +指令长度), PC 存下一条指令地址
 *   - MAR/MDR 是主存接口, 所有访存必经
 *   - 取指周期必访存 1 次, 间址周期再访存 1 次
 *   - 单总线冲突: 同一周期不能两部件同时发数据
 *   - 微程序: 1 机器指令 = 一段微程序 = 多条微指令
 *   - 微指令地址形成: 顺序 + 转移
 *   - PSW 各位: ZF=零, SF=负, CF=进位, OF=溢出
 */
#ifndef CS408_CO_CPU_CPU_DATAPATH_H
#define CS408_CO_CPU_CPU_DATAPATH_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace cs408::co {

// CPU 寄存器组
class CPURegisters {
public:
    CPURegisters() {
        for (int i = 0; i < 32; ++i) gpr_[i] = 0;
        pc_ = 0; ir_ = 0; mar_ = 0; mdr_ = 0;
        psw_ = 0;
        alu_temp_ = 0;
    }

    uint32_t& gpr(int i) { return gpr_[i]; }
    uint32_t& pc() { return pc_; }
    uint32_t& ir() { return ir_; }
    uint32_t& mar() { return mar_; }
    uint32_t& mdr() { return mdr_; }
    uint32_t& psw() { return psw_; }
    uint32_t& alu_temp() { return alu_temp_; }

    // PSW 标志位
    bool zf() const { return (psw_ >> 6) & 1; }
    bool sf() const { return (psw_ >> 7) & 1; }
    bool cf() const { return (psw_ >> 0) & 1; }
    bool of() const { return (psw_ >> 11) & 1; }
    void set_flags(bool z, bool s, bool c, bool o) {
        if (z) psw_ |= (1u << 6); else psw_ &= ~(1u << 6);
        if (s) psw_ |= (1u << 7); else psw_ &= ~(1u << 7);
        if (c) psw_ |= (1u << 0); else psw_ &= ~(1u << 0);
        if (o) psw_ |= (1u << 11); else psw_ &= ~(1u << 11);
    }

    void print() const {
        std::cout << "PC=0x" << std::hex << pc_ << "  IR=0x" << ir_
                  << "  MAR=0x" << mar_ << "  MDR=0x" << mdr_
                  << "  PSW=0x" << psw_ << std::dec << "\n";
        std::cout << "  ZF=" << zf() << " SF=" << sf()
                  << " CF=" << cf() << " OF=" << of() << "\n";
        std::cout << "  GPR: ";
        for (int i = 0; i < 8; ++i) std::cout << "R" << i << "=" << gpr_[i] << " ";
        std::cout << "...\n";
    }

private:
    uint32_t gpr_[32];
    uint32_t pc_, ir_, mar_, mdr_, psw_, alu_temp_;
};

// 简化主存 (字节编址)
class Memory {
public:
    explicit Memory(int size = 256) : data_(size, 0) {}
    uint32_t load(uint32_t addr) {
        if (addr + 3 >= data_.size()) return 0;
        return (data_[addr] | (data_[addr+1] << 8) |
                (data_[addr+2] << 16) | (data_[addr+3] << 24));
    }
    void store(uint32_t addr, uint32_t val) {
        if (addr + 3 >= data_.size()) return;
        data_[addr]   = val & 0xFF;
        data_[addr+1] = (val >> 8) & 0xFF;
        data_[addr+2] = (val >> 16) & 0xFF;
        data_[addr+3] = (val >> 24) & 0xFF;
    }
    uint8_t& at(uint32_t addr) { return data_[addr]; }
private:
    std::vector<uint8_t> data_;
};

// 指令周期各阶段
enum class CyclePhase { FETCH, INDIRECT, EXECUTE, INTERRUPT };

inline const char* phase_str(CyclePhase p) {
    switch (p) {
        case CyclePhase::FETCH:     return "取指";
        case CyclePhase::INDIRECT:  return "间址";
        case CyclePhase::EXECUTE:   return "执行";
        case CyclePhase::INTERRUPT: return "中断";
    }
    return "?";
}

// 取指周期的微操作 (经典 5 步)
void demo_fetch_cycle(CPURegisters& cpu, Memory& mem) {
    std::cout << "取指周期 (5 个微操作):\n";
    std::cout << "  1) PC → MAR          (送地址)\n";
    cpu.mar() = cpu.pc();
    std::cout << "  2) MAR → 主存 → MDR  (读主存, 1 访存)\n";
    cpu.mdr() = mem.load(cpu.mar());
    std::cout << "  3) MDR → IR          (送指令寄存器)\n";
    cpu.ir() = cpu.mdr();
    std::cout << "  4) PC + 1 → PC       (PC 自增)\n";
    cpu.pc() += 4;
    std::cout << "  5) OP(IR) → CU       (操作码送控制器译码)\n";
    std::cout << "  → 共 1 次访存 (取指令)\n";
}

// 间址周期的微操作
void demo_indirect_cycle(CPURegisters& cpu, Memory& mem) {
    std::cout << "间址周期 (寻址方式为间接时执行):\n";
    std::cout << "  1) Ad(IR) → MAR      (形式地址送 MAR)\n";
    cpu.mar() = cpu.ir() & 0xFFFF;  // 假设低 16 位是地址
    std::cout << "  2) MAR → 主存 → MDR  (读主存, 1 访存)\n";
    cpu.mdr() = mem.load(cpu.mar());
    std::cout << "  3) MDR → Ad(IR)      (有效地址替换形式地址)\n";
    std::cout << "  → 共 1 次访存 (取有效地址)\n";
}

// 数据通路: 单总线 vs 双总线 vs 三总线
void print_bus_structures() {
    std::cout << "单总线结构:\n";
    std::cout << "  所有部件挂一组总线 → 同时刻只能一对部件传输\n";
    std::cout << "  优点: 简单   缺点: 数据冲突, 慢\n";
    std::cout << "  例: ADD R1, R2, R3 需 3 步:\n";
    std::cout << "    R2 → 总线 → ALU 暂存器\n";
    std::cout << "    R3 → 总线 → ALU\n";
    std::cout << "    ALU → 总线 → R1\n\n";

    std::cout << "双总线结构:\n";
    std::cout << "  ALU 两输入各接一组总线, 输出经暂存器\n";
    std::cout << "  例: ADD R1, R2, R3 需 2 步:\n";
    std::cout << "    R2→总线1, R3→总线2 → ALU → 暂存器\n";
    std::cout << "    暂存器 → 总线1 → R1\n\n";

    std::cout << "三总线结构:\n";
    std::cout << "  ALU 两输入 + 一输出各一组总线\n";
    std::cout << "  例: ADD R1, R2, R3 需 1 步:\n";
    std::cout << "    R2→总线1, R3→总线2 → ALU → 总线3 → R1\n";
}

// 硬布线 vs 微程序
void print_hardwired_vs_microprogram() {
    std::cout << "              硬布线控制          微程序控制\n";
    std::cout << "原理          组合逻辑电路          存储逻辑 (控存)\n";
    std::cout << "速度          快 (1 级门延迟)       慢 (访控存)\n";
    std::cout << "修改          难 (改电路)           易 (改控存内容)\n";
    std::cout << "指令复杂度    适合简单指令          适合复杂指令\n";
    std::cout << "应用          RISC                  CISC\n";
    std::cout << "微码修复      无                    有 (Intel CPU 微码更新)\n";
}

void cpu_datapath_demo() {
    section("CPU 寄存器组");
    std::cout << "通用: GPR (R0~R31)\n";
    std::cout << "专用:\n";
    std::cout << "  PC  程序计数器 (存下条指令地址)\n";
    std::cout << "  IR  指令寄存器 (存当前指令)\n";
    std::cout << "  MAR 主存地址寄存器\n";
    std::cout << "  MDR 主存数据寄存器\n";
    std::cout << "  PSW 程序状态字 (CF/ZF/SF/OF)\n";
    std::cout << "  ALU 暂存器\n\n";

    CPURegisters cpu;
    Memory mem(256);
    cpu.pc() = 0x10;
    mem.store(0x10, 0x12345678);
    cpu.print();

    section("指令周期 - 取指周期");
    demo_fetch_cycle(cpu, mem);
    cpu.print();

    section("指令周期 - 间址周期");
    demo_indirect_cycle(cpu, mem);

    section("指令周期 - 4 阶段流程");
    std::cout << "  取指 → 间址 (可选) → 执行 → 中断 (可选) → 取指...\n";
    std::cout << "  每条指令至少 1 次访存 (取指)\n";
    std::cout << "  间址指令多 1 次访存\n";
    std::cout << "  中断周期: 保存 PC/PSW → 取中断向量 → 跳转到 ISR\n";

    section("数据通路结构");
    print_bus_structures();

    section("硬布线 vs 微程序控制");
    print_hardwired_vs_microprogram();

    section("微程序控制原理");
    std::cout << "  1 机器指令 = 1 微程序 = 多条微指令\n";
    std::cout << "  微指令 = 控制信号的一组二进制位\n";
    std::cout << "  微指令地址由 μPC (微程序计数器) 维护\n";
    std::cout << "  取指微程序对所有指令公用\n";
    std::cout << "  → CISC 复杂指令通过组合微指令实现\n";

    section("PSW 标志位 (条件转移依赖)");
    cpu.gpr(0) = 0;
    cpu.set_flags(true, false, false, false);  // ZF=1
    std::cout << "  R0=0 → ZF=1, JZ (零转移) 将跳转\n";
    cpu.print();
}

bool cpu_datapath_test() {
    CPURegisters cpu;
    Memory mem(256);
    // 取指: PC=0x10, mem[0x10]=0xABCD
    cpu.pc() = 0x10;
    mem.store(0x10, 0xABCD);
    CS408_EXPECT_EQ(cpu.pc(), 0x10u);

    // 模拟取指
    cpu.mar() = cpu.pc();
    cpu.mdr() = mem.load(cpu.mar());
    cpu.ir() = cpu.mdr();
    cpu.pc() += 4;
    CS408_EXPECT_EQ(cpu.ir(), 0xABCDu);
    CS408_EXPECT_EQ(cpu.pc(), 0x14u);

    // PSW 标志位
    cpu.set_flags(true, false, false, false);
    CS408_EXPECT(cpu.zf());
    CS408_EXPECT(!cpu.sf());
    cpu.set_flags(false, true, true, true);
    CS408_EXPECT(!cpu.zf());
    CS408_EXPECT(cpu.sf());
    CS408_EXPECT(cpu.cf());
    CS408_EXPECT(cpu.of());

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "cpu.cpu_datapath", cpu_datapath,
    "PC/IR/MAR/MDR/PSW;指令周期:取指/间址/执行/中断;单/双/三总线;硬布线 vs 微程序",
    "Intel x86 微码;Apple Silicon 硬布线;RISC-V Rocket;AMD μop cache;GPU SIMT",
    "PC 自动+1存下条指令;MAR/MDR 必经主存;取指1访存间址多1;单总线不能同时传;1机器指令=1微程序=多微指令",
    cpu_datapath_demo, cpu_datapath_test
);

} // namespace cs408::co
#endif // CS408_CO_CPU_CPU_DATAPATH_H

/**
 * @file isa.h
 * @topic 计组 - 指令系统 (扩展操作码 + 8 种寻址方式 + CISC vs RISC)
 *
 * @考点 408 大纲:计算机组成原理 > 指令系统
 *   - 指令格式: 操作码 OP + 地址码 A
 *     按地址数: 0/1/2/3 地址指令
 *   - 扩展操作码: 操作码字段向地址码字段扩展
 *     · 4 位 OP, 16 条三地址指令 (0000~1111 中 0000-1011 用 12 个?)
 *     · 留几个编码作为扩展标志, 转为二地址/一地址/零地址
 *   - 8 种寻址方式:
 *     1) 立即寻址: 操作数在指令中 (最快, 不访存)
 *     2) 直接寻址: A = 有效地址 EA (访存 1 次)
 *     3) 间接寻址: A 指向 EA (访存 2+ 次)
 *     4) 寄存器寻址: EA = Ri (不访存, 快)
 *     5) 寄存器间接: EA = (Ri) (访存 1 次)
 *     6) 基址寻址: EA = (BR) + A (基址寄存器 + 偏移, 程序浮动)
 *     7) 变址寻址: EA = (IX) + A (变址寄存器 + 偏移, 数组循环)
 *     8) 相对寻址: EA = (PC) + A (PC + 偏移, 转移指令)
 *   - CISC vs RISC
 *
 * @业务 工业应用
 *   - x86-64 (CISC, 扩展操作码 EVEX/VEX 前缀)
 *   - ARMv9 (RISC, 固定 32 位编码)
 *   - RISC-V (模块化 I/M/A/F/D/C/V 扩展)
 *   - MIPS (经典 RISC 教学架构)
 *   - Apple Silicon (ARM64 + 大量 NEON/AMX 扩展)
 *
 * @陷阱 408 高频
 *   - 扩展操作码: 留一个编码作扩展标志, 不能全用完
 *   - 寻址方式访存次数必考: 立即 0, 寄存器 0, 直接 1, 间接 2+
 *   - 基址 vs 变址: 基址寄存器内容由 OS 改 (段浮动), 变址由用户改 (数组遍历)
 *   - 相对寻址: 转移目标 = PC + offset, offset 通常用补码 (可正可负)
 *   - 三地址指令最多: 2^n - 1 (留 1 个扩展标志)
 *   - RISC 特征: 固定编码, Load/Store 结构, 大量寄存器, 硬布线控制
 */
#ifndef CS408_CO_INSTRUCTION_ISA_H
#define CS408_CO_INSTRUCTION_ISA_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>

namespace cs408::co {

// 8 种寻址方式
enum class AddressingMode {
    IMMEDIATE,    // 立即
    DIRECT,       // 直接
    INDIRECT,     // 间接
    REGISTER,     // 寄存器
    REGISTER_INDIRECT, // 寄存器间接
    BASED,        // 基址
    INDEXED,      // 变址
    RELATIVE      // 相对
};

inline const char* mode_str(AddressingMode m) {
    switch (m) {
        case AddressingMode::IMMEDIATE:         return "立即";
        case AddressingMode::DIRECT:            return "直接";
        case AddressingMode::INDIRECT:          return "间接";
        case AddressingMode::REGISTER:          return "寄存器";
        case AddressingMode::REGISTER_INDIRECT: return "寄存器间接";
        case AddressingMode::BASED:             return "基址";
        case AddressingMode::INDEXED:           return "变址";
        case AddressingMode::RELATIVE:          return "相对";
    }
    return "?";
}

// 访存次数 (取指令本身 1 次另算)
inline int memory_accesses(AddressingMode m) {
    switch (m) {
        case AddressingMode::IMMEDIATE:         return 0;  // 操作数在指令
        case AddressingMode::REGISTER:          return 0;  // 在寄存器
        case AddressingMode::DIRECT:            return 1;  // 访存取操作数
        case AddressingMode::REGISTER_INDIRECT: return 1;  // 访存
        case AddressingMode::BASED:             return 1;  // 访存 (BR+A)
        case AddressingMode::INDEXED:           return 1;  // 访存 (IX+A)
        case AddressingMode::RELATIVE:          return 1;  // 访存 (PC+A)
        case AddressingMode::INDIRECT:          return 2;  // 先取 EA, 再取操作数
    }
    return 0;
}

// 寻址方式描述
struct ModeInfo {
    AddressingMode mode;
    std::string formula;   // 有效地址公式
    std::string example;   // 汇编示例
    int access_count;      // 访存次数 (除取指外)
    std::string use_case;  // 用途
};

inline std::vector<ModeInfo> all_modes() {
    return {
        {AddressingMode::IMMEDIATE,         "操作数 = A",          "MOV R1, #5",     0, "常数赋值"},
        {AddressingMode::DIRECT,            "EA = A",              "MOV R1, [1000]", 1, "访问固定地址"},
        {AddressingMode::INDIRECT,          "EA = (A)",            "MOV R1, [[1000]]",2,"指针的指针"},
        {AddressingMode::REGISTER,          "EA = Ri, 操作数=(Ri)","MOV R1, R2",     0, "寄存器间传输"},
        {AddressingMode::REGISTER_INDIRECT, "EA = (Ri)",           "MOV R1, [R2]",   1, "指针访问"},
        {AddressingMode::BASED,             "EA = (BR) + A",       "MOV R1, [BR+10]",1, "程序浮动 (OS)"},
        {AddressingMode::INDEXED,           "EA = (IX) + A",       "MOV R1, [IX+10]",1, "数组循环 (用户)"},
        {AddressingMode::RELATIVE,          "EA = (PC) + A",       "JMP +20",        1, "转移指令 (位置无关)"},
    };
}

// 扩展操作码设计
// 16 位指令, 4 位 OP, 4 位 A1, 4 位 A2, 4 位 A3
// 设计: 15 条三地址 + 15 条二地址 + 15 条一地址 + 16 条零地址
struct ExtOpcodeLayout {
    int total_bits;
    int op_bits;
    int addr_bits;  // 单个地址字段位数

    // 三地址: OP 4 位, 留 1111 作扩展 → 15 条
    int three_addr_count() const {
        return (1 << op_bits) - 1;  // 留 1 个扩展标志
    }
    // 二地址: 1111 + OP 4 位, 留 1111 作扩展 → 15 条
    int two_addr_count() const {
        return (1 << op_bits) - 1;
    }
    // 一地址: 1111 1111 + OP 4 位, 留 1111 → 15 条
    int one_addr_count() const {
        return (1 << op_bits) - 1;
    }
    // 零地址: 1111 1111 1111 + OP 4 位, 无需扩展 → 16 条
    int zero_addr_count() const {
        return (1 << op_bits);
    }
    int total() const {
        return three_addr_count() + two_addr_count() + one_addr_count() + zero_addr_count();
    }
};

// CISC vs RISC 对比
void print_cisc_vs_risc() {
    std::cout << "            CISC                 RISC\n";
    std::cout << "指令系统    复杂, 数量多 (300+)   简单, 数量少 (<100)\n";
    std::cout << "指令长度    可变 (1~15 字节)      固定 (通常 4 字节)\n";
    std::cout << "寻址方式    多 (10+ 种)            少 (3~5 种)\n";
    std::cout << "访存指令    任意指令可访存         只有 Load/Store\n";
    std::cout << "寄存器数    少 (8~16)              多 (32+)\n";
    std::cout << "控制方式    微程序                硬布线\n";
    std::cout << "执行时间    不固定                 通常 1 CPI\n";
    std::cout << "代表        x86, x86-64           ARM, RISC-V, MIPS\n";
}

void isa_demo() {
    section("指令格式");
    std::cout << "操作码 OP + 地址码 A1, A2, A3\n";
    std::cout << "  三地址: OP A1 A2 A3   (A1 ← A2 op A3)\n";
    std::cout << "  二地址: OP A1 A2      (A1 ← A1 op A2)\n";
    std::cout << "  一地址: OP A1         (ACC ← ACC op A1)\n";
    std::cout << "  零地址: OP             (堆栈: 弹两个运算后压回)\n";

    section("扩展操作码设计 (16 位指令, 4 位 OP + 3×4 位地址)");
    ExtOpcodeLayout layout{16, 4, 4};
    std::cout << "  三地址指令: " << layout.three_addr_count() << " 条 (4 位 OP 留 1111 扩展)\n";
    std::cout << "  二地址指令: " << layout.two_addr_count() << " 条 (1111 + 4 位 OP, 留 1111)\n";
    std::cout << "  一地址指令: " << layout.one_addr_count() << " 条 (1111 1111 + 4 位 OP, 留 1111)\n";
    std::cout << "  零地址指令: " << layout.zero_addr_count() << " 条 (1111 1111 1111 + 4 位 OP)\n";
    std::cout << "  总数: " << layout.total() << " 条\n";
    std::cout << "  → 扩展操作码让短 OP 字段支持大量指令\n";

    section("8 种寻址方式");
    std::cout << "方式        公式               示例           访存  用途\n";
    for (const auto& m : all_modes()) {
        std::cout << mode_str(m.mode) << "\t"
                  << m.formula << "\t"
                  << m.example << "\t"
                  << m.access_count << "\t"
                  << m.use_case << "\n";
    }

    section("寻址方式访存次数对比");
    std::cout << "立即寻址 (0 次): 操作数在指令中, 取指即得\n";
    std::cout << "寄存器寻址 (0 次): 操作数在寄存器\n";
    std::cout << "直接寻址 (1 次): EA = A, 访存取操作数\n";
    std::cout << "间接寻址 (2 次): 先访存取 EA, 再访存取操作数\n";
    std::cout << "  → 一次间接最常用, 多次间接少见\n";

    section("基址 vs 变址 (易混淆!)");
    std::cout << "基址寻址: EA = (BR) + A\n";
    std::cout << "  · BR 基址寄存器由 OS 管理 (程序浮动)\n";
    std::cout << "  · A 是位移量 (短, 指令字段)\n";
    std::cout << "  · 解决多道程序设计中的地址重定位\n\n";
    std::cout << "变址寻址: EA = (IX) + A\n";
    std::cout << "  · IX 变址寄存器由用户/编译器管理\n";
    std::cout << "  · A 是基地址 (短数组首地址)\n";
    std::cout << "  · 用于数组循环 (IX 自增遍历)\n";

    section("相对寻址");
    std::cout << "EA = (PC) + A\n";
    std::cout << "  · A 是相对位移 (补码, 可正可负)\n";
    std::cout << "  · 用于转移指令 JMP +20\n";
    std::cout << "  · 位置无关代码 (PIC) 的基础\n";

    section("CISC vs RISC");
    print_cisc_vs_risc();

    section("RISC-V 模块化扩展 (现代 ISA 设计趋势)");
    std::cout << "  I: 基本整数 (47 条指令)\n";
    std::cout << "  M: 乘除法\n";
    std::cout << "  A: 原子操作\n";
    std::cout << "  F: 单精度浮点\n";
    std::cout << "  D: 双精度浮点\n";
    std::cout << "  C: 压缩指令 (16 位)\n";
    std::cout << "  V: 向量 (SIMD)\n";
    std::cout << "  → 只实现需要的扩展, 灵活度高于 x86/ARM\n";
}

bool isa_test() {
    // 访存次数
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::IMMEDIATE), 0);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::REGISTER), 0);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::DIRECT), 1);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::REGISTER_INDIRECT), 1);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::BASED), 1);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::INDEXED), 1);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::RELATIVE), 1);
    CS408_EXPECT_EQ(memory_accesses(AddressingMode::INDIRECT), 2);

    // 扩展操作码
    ExtOpcodeLayout layout{16, 4, 4};
    CS408_EXPECT_EQ(layout.three_addr_count(), 15);
    CS408_EXPECT_EQ(layout.two_addr_count(), 15);
    CS408_EXPECT_EQ(layout.one_addr_count(), 15);
    CS408_EXPECT_EQ(layout.zero_addr_count(), 16);
    CS408_EXPECT_EQ(layout.total(), 61);  // 15+15+15+16

    // 寻址方式总数
    CS408_EXPECT_EQ(static_cast<int>(all_modes().size()), 8);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "instruction.isa", isa,
    "OP+地址码;扩展操作码留1标志;8种寻址 (立即0次/直接1次/间接2次);基址OS/变址用户;CISC vs RISC",
    "x86-64 EVEX/VEX;ARMv9 固定编码;RISC-V 模块化;MIPS 教学架构;Apple AMX",
    "扩展操作码留1编码;8种寻址访存次数;基址OS改/变址用户改;相对PC+offset位置无关;RISC Load/Store",
    isa_demo, isa_test
);

} // namespace cs408::co
#endif // CS408_CO_INSTRUCTION_ISA_H

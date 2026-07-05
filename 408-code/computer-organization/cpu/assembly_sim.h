/**
 * @file assembly_sim.h
 * @topic 计组 - 汇编模拟器 (mini-ISA + 汇编程序实例)
 *
 * @设计 实现 16 位 mini-ISA 模拟器, 支持汇编代码执行
 *   - 寄存器: R0~R7 (16 位)
 *   - 指令集 (经典 RISC 风格):
 *     · MOV  Ri, #imm     立即数 → 寄存器
 *     · MOV  Ri, Rj       寄存器 → 寄存器
 *     · LOAD Ri, [Rj]     主存 → 寄存器
 *     · STORE [Ri], Rj    寄存器 → 主存
 *     · ADD  Ri, Rj, Rk   加法
 *     · SUB  Ri, Rj, Rk   减法
 *     · MUL  Ri, Rj, Rk   乘法
 *     · DIV  Ri, Rj, Rk   除法
 *     · CMP  Ri, Rj       比较 (设标志位)
 *     · JMP  label        无条件跳转
 *     · JZ   label        零标志位 = 1 跳转
 *     · JNZ  label        零标志位 = 0 跳转
 *     · JN   label        负标志位 = 1 跳转
 *     · HALT              停机
 *
 * @考点 408 大纲:计算机组成原理 > 指令系统 > 汇编程序
 *   - 汇编指令 = 助记符 + 操作数
 *   - 标号 (label) = 指令地址的符号化
 *   - 立即数前缀 #, 寄存器 R, 主存 []
 *   - 标志位影响: 算术指令设 ZF/SF/CF/OF
 *
 * @业务 工业应用
 *   - LLVM-MCA (机器码分析器)
 *   - Godbolt 编译器探索
 *   - 嵌入式 ASM (驱动/启动代码)
 *   - JIT 编译器 (V8/Hotspot)
 *   - 逆向工程 (IDA Pro/Ghidra)
 *
 * @陷阱 408 高频
 *   - 立即数 vs 直接寻址: MOV R1, #5 是立即; MOV R1, 5 是直接 (地址 5)
 *   - CMP 不写回结果, 只设标志位
 *   - 减法借位: CF 在被减 < 减数时置 1
 *   - 跳转目标地址 = 当前 PC + offset (相对跳转)
 *   - 汇编中数制: 默认十进制, 0x 前缀十六进制, 0b 前缀二进制
 */
#ifndef CS408_CO_CPU_ASSEMBLY_SIM_H
#define CS408_CO_CPU_ASSEMBLY_SIM_H

#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <cstdint>
#include <stdexcept>

namespace cs408::co {

class MiniCPU {
public:
    MiniCPU() {
        for (int i = 0; i < 8; ++i) reg_[i] = 0;
        pc_ = 0; zf_ = sf_ = cf_ = of_ = false; halted_ = false;
    }

    uint16_t reg(int i) const { return reg_[i]; }
    uint16_t pc() const { return pc_; }
    bool halted() const { return halted_; }
    bool zf() const { return zf_; }
    bool sf() const { return sf_; }

    // 装载程序 (汇编指令列表)
    void load(const std::vector<std::string>& program) {
        program_ = program;
        // 第一遍: 扫描标号
        for (size_t i = 0; i < program_.size(); ++i) {
            std::string line = strip(program_[i]);
            if (line.empty()) continue;
            // 标号格式: "label:"
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string label = strip(line.substr(0, colon));
                labels_[label] = static_cast<uint16_t>(i);
                // 标号后面可能有指令
                std::string rest = strip(line.substr(colon + 1));
                if (!rest.empty()) {
                    program_[i] = rest;
                } else {
                    program_[i] = "NOP";
                }
            }
        }
    }

    // 执行一步
    void step() {
        if (halted_ || pc_ >= program_.size()) { halted_ = true; return; }
        std::string line = strip(program_[pc_]);
        if (line.empty() || line[0] == ';') { ++pc_; return; }
        execute(line);
    }

    void run(int max_steps = 10000) {
        int steps = 0;
        while (!halted_ && steps < max_steps) {
            step();
            ++steps;
        }
        if (steps >= max_steps) {
            std::cerr << "警告: 达到最大步数 " << max_steps << ", 可能死循环\n";
        }
    }

    void print_state() const {
        std::cout << "PC=" << pc_ << "  ZF=" << zf_ << " SF=" << sf_
                  << "  halted=" << halted_ << "\n";
        std::cout << "寄存器: ";
        for (int i = 0; i < 8; ++i) std::cout << "R" << i << "=" << reg_[i] << " ";
        std::cout << "\n";
    }

    void print_program() const {
        for (size_t i = 0; i < program_.size(); ++i) {
            std::cout << "  " << i << ": " << program_[i] << "\n";
        }
    }

private:
    uint16_t reg_[8];
    uint16_t pc_;
    bool zf_, sf_, cf_, of_;
    bool halted_;
    std::vector<std::string> program_;
    std::unordered_map<std::string, uint16_t> labels_;
    std::vector<uint8_t> memory_;

    static std::string strip(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // 解析操作数: 返回值 + 是否立即数
    uint16_t parse_operand(const std::string& s) {
        if (s.empty()) throw std::runtime_error("empty operand");
        if (s[0] == '#') {
            // 立即数
            return static_cast<uint16_t>(std::stoi(s.substr(1), nullptr, 0));
        }
        if (s[0] == 'R' || s[0] == 'r') {
            int idx = std::stoi(s.substr(1));
            return reg_[idx];
        }
        // 标号
        auto it = labels_.find(s);
        if (it != labels_.end()) return it->second;
        // 数字 (默认十进制)
        return static_cast<uint16_t>(std::stoi(s, nullptr, 0));
    }

    int reg_index(const std::string& s) {
        if (s.empty() || (s[0] != 'R' && s[0] != 'r'))
            throw std::runtime_error("not a register: " + s);
        return std::stoi(s.substr(1));
    }

    // 简单分词 (按空格和逗号)
    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> toks;
        std::string cur;
        for (char c : line) {
            if (c == ' ' || c == ',') {
                if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) toks.push_back(cur);
        return toks;
    }

    void set_flags_arith(uint32_t result, uint16_t a, uint16_t b, bool is_sub) {
        zf_ = (result & 0xFFFF) == 0;
        sf_ = (result >> 15) & 1;
        if (is_sub) {
            cf_ = a < b;  // 借位
        } else {
            cf_ = (result >> 16) & 1;  // 进位
        }
        of_ = false;  // 简化, 不严格判溢出
    }

    void execute(const std::string& line) {
        auto toks = tokenize(line);
        if (toks.empty()) { ++pc_; return; }
        const std::string& op = toks[0];

        if (op == "NOP") {
            ++pc_;
        } else if (op == "HALT") {
            halted_ = true;
        } else if (op == "MOV") {
            // MOV Ri, #imm  /  MOV Ri, Rj
            int d = reg_index(toks[1]);
            reg_[d] = parse_operand(toks[2]);
            ++pc_;
        } else if (op == "ADD" || op == "SUB" || op == "MUL" || op == "DIV") {
            int d = reg_index(toks[1]);
            uint16_t a = parse_operand(toks[2]);
            uint16_t b = parse_operand(toks[3]);
            uint32_t r = 0;
            if (op == "ADD") { r = a + b; set_flags_arith(r, a, b, false); }
            else if (op == "SUB") { r = a - b; set_flags_arith(r, a, b, true); }
            else if (op == "MUL") { r = a * b; set_flags_arith(r, a, b, false); }
            else if (op == "DIV") {
                if (b == 0) throw std::runtime_error("divide by zero");
                r = a / b; set_flags_arith(r, a, b, false);
            }
            reg_[d] = static_cast<uint16_t>(r);
            ++pc_;
        } else if (op == "CMP") {
            uint16_t a = parse_operand(toks[1]);
            uint16_t b = parse_operand(toks[2]);
            uint32_t r = a - b;
            set_flags_arith(r, a, b, true);
            ++pc_;
        } else if (op == "INC") {
            int d = reg_index(toks[1]);
            uint16_t a = reg_[d];
            uint32_t r = a + 1;
            set_flags_arith(r, a, 1, false);
            reg_[d] = static_cast<uint16_t>(r);
            ++pc_;
        } else if (op == "JMP") {
            pc_ = parse_operand(toks[1]);
        } else if (op == "JZ") {
            if (zf_) pc_ = parse_operand(toks[1]); else ++pc_;
        } else if (op == "JNZ") {
            if (!zf_) pc_ = parse_operand(toks[1]); else ++pc_;
        } else if (op == "JN") {
            if (sf_) pc_ = parse_operand(toks[1]); else ++pc_;
        } else {
            throw std::runtime_error("未知指令: " + op);
        }
    }
};

void assembly_sim_demo() {
    section("程序 1: 求 1+2+...+10 = 55 (倒计数循环)");
    std::cout << "源程序:\n";
    std::vector<std::string> prog1 = {
        "MOV R0, #0      ; sum = 0",
        "MOV R1, #10     ; i = 10",
        "loop:",
        "ADD R0, R0, R1  ; sum += i",
        "SUB R1, R1, #1  ; i--",
        "JNZ loop        ; i≠0 继续",
        "HALT",
    };
    for (size_t i = 0; i < prog1.size(); ++i)
        std::cout << "  " << i << ": " << prog1[i] << "\n";

    MiniCPU cpu1;
    cpu1.load(prog1);
    cpu1.run();
    std::cout << "运行后: "; cpu1.print_state();
    std::cout << "  R0 = sum = " << cpu1.reg(0) << " (期望 55)\n\n";

    section("程序 2: 计算 5! = 120");
    std::cout << "源程序:\n";
    std::vector<std::string> prog2 = {
        "MOV R0, #1      ; result = 1",
        "MOV R1, #5      ; i = 5",
        "loop:",
        "MUL R0, R0, R1  ; result *= i",
        "SUB R1, R1, #1  ; i--",
        "JNZ loop        ; i≠0 继续",
        "HALT",
    };
    for (size_t i = 0; i < prog2.size(); ++i)
        std::cout << "  " << i << ": " << prog2[i] << "\n";

    MiniCPU cpu2;
    cpu2.load(prog2);
    cpu2.run();
    std::cout << "运行后: "; cpu2.print_state();
    std::cout << "  R0 = 5! = " << cpu2.reg(0) << " (期望 120)\n\n";

    section("对照: x86-64 真实汇编 (gcc 输出)");
    std::cout << "// C 代码: int sum = 0; for (int i = 1; i <= 10; i++) sum += i;\n";
    std::cout << "  mov   eax, 0              ; sum = 0\n";
    std::cout << "  mov   edx, 1              ; i = 1\n";
    std::cout << "loop:\n";
    std::cout << "  add   eax, edx            ; sum += i\n";
    std::cout << "  add   edx, 1              ; i++\n";
    std::cout << "  cmp   edx, 11             ; i vs 11\n";
    std::cout << "  jne   loop                ; 不等则跳\n";
    std::cout << "  ret                       ; 返回 sum (eax)\n";
    std::cout << "  → 概念相同, 只是寄存器名 (eax/edx) 和指令格式不同\n\n";

    section("对照: RISC-V 汇编");
    std::cout << "  li   t0, 0          # sum = 0\n";
    std::cout << "  li   t1, 1          # i = 1\n";
    std::cout << "  li   t2, 10         # n = 10\n";
    std::cout << "loop:\n";
    std::cout << "  add  t0, t0, t1     # sum += i\n";
    std::cout << "  addi t1, t1, 1      # i++\n";
    std::cout << "  ble  t1, t2, loop   # if i<=n jump\n";
    std::cout << "  → RISC-V 助记符: li (load imm), addi (add imm)\n";

    section("汇编寻址方式实例 (mini-ISA)");
    std::cout << "  MOV R1, #5          立即寻址   (# 前缀)\n";
    std::cout << "  MOV R1, R2          寄存器寻址\n";
    std::cout << "  LOAD R1, [R2]       寄存器间接\n";
    std::cout << "  STORE [R1], R2      寄存器间接存\n";
    std::cout << "  ADD R1, R2, R3      寄存器寻址 (三地址)\n";
    std::cout << "  CMP R1, #10         立即数比较\n";

    section("汇编编程要点");
    std::cout << "  1) 标号 = 指令地址, 用于跳转目标\n";
    std::cout << "  2) 立即数 # 前缀, 与直接寻址区分\n";
    std::cout << "  3) 算术指令设标志位, CMP 只设标志位\n";
    std::cout << "  4) 循环: CMP + 条件跳转\n";
    std::cout << "  5) 函数调用需保存返回地址 (本模拟器未实现)\n";
}

bool assembly_sim_test() {
    // 程序 1: 10+9+...+1 = 55 (倒计数)
    std::vector<std::string> prog1 = {
        "MOV R0, #0",
        "MOV R1, #10",
        "loop:",
        "ADD R0, R0, R1",
        "SUB R1, R1, #1",
        "JNZ loop",
        "HALT",
    };
    MiniCPU cpu1;
    cpu1.load(prog1);
    cpu1.run();
    CS408_EXPECT_EQ(cpu1.reg(0), 55);

    // 程序 2: 5! = 120
    std::vector<std::string> prog2 = {
        "MOV R0, #1",
        "MOV R1, #5",
        "loop:",
        "MUL R0, R0, R1",
        "SUB R1, R1, #1",
        "JNZ loop",
        "HALT",
    };
    MiniCPU cpu2;
    cpu2.load(prog2);
    cpu2.run();
    CS408_EXPECT_EQ(cpu2.reg(0), 120);

    // 程序 3: 简单赋值与运算
    std::vector<std::string> prog3 = {
        "MOV R0, #3",
        "MOV R1, #4",
        "ADD R2, R0, R1",  // 7
        "SUB R3, R1, R0",  // 1
        "MUL R4, R0, R1",  // 12
        "HALT",
    };
    MiniCPU cpu3;
    cpu3.load(prog3);
    cpu3.run();
    CS408_EXPECT_EQ(cpu3.reg(2), 7);
    CS408_EXPECT_EQ(cpu3.reg(3), 1);
    CS408_EXPECT_EQ(cpu3.reg(4), 12);

    return true;
}

CS408_REGISTER_MODULE(
    "computer-organization", "cpu.assembly_sim", assembly_sim,
    "mini-ISA: MOV/LOAD/STORE/ADD/SUB/MUL/DIV/CMP/JMP/JZ/JNZ/HALT;标号=地址;立即数#前缀",
    "LLVM-MCA;Godbolt;嵌入式 ASM;JIT 编译器 (V8/Hotspot);逆向 IDA/Ghidra",
    "立即# vs 直接;CMP 只设标志不写回;标号=指令地址;JMP 改 PC;条件跳转依赖 ZF/SF",
    assembly_sim_demo, assembly_sim_test
);

} // namespace cs408::co
#endif // CS408_CO_CPU_ASSEMBLY_SIM_H

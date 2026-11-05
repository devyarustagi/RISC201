#include "debugger/disasm.hpp"

#include <array>
#include <cstdio>

#include "common/micro_types.hpp"

namespace {

// Operand layout of each macro instruction (same groups as the assembler).
enum class Format {
    None,        // nop, ret, retz, ebreak, ecall
    RdRs1Rs2,    // add rd, rs1, rs2
    RdRs1,       // not rd, rs1
    RdRs2,       // mov rd, rs2
    Rs1Rs2,      // cmp rs1, rs2
    RdRs1Imm,    // addi rd, rs1, imm   /  ld rd, rs1, imm
    RdImm,       // movi rd, imm
    Rs1Imm,      // cmpi rs1, imm
    Offset,      // b / beq / bgt / call
    Window       // movz rd, rs2 (register window)
};

struct OpInfo {
    const char* name;
    Format format;
};

constexpr std::array<OpInfo, 45> kOps = {{
    {"nop", Format::None},       {"add", Format::RdRs1Rs2},   {"sub", Format::RdRs1Rs2},
    {"mul", Format::RdRs1Rs2},   {"div", Format::RdRs1Rs2},   {"mod", Format::RdRs1Rs2},
    {"and", Format::RdRs1Rs2},   {"or", Format::RdRs1Rs2},    {"xor", Format::RdRs1Rs2},
    {"lsl", Format::RdRs1Rs2},   {"lsr", Format::RdRs1Rs2},   {"asr", Format::RdRs1Rs2},
    {"not", Format::RdRs1},      {"mov", Format::RdRs2},      {"cmp", Format::Rs1Rs2},
    {"addi", Format::RdRs1Imm},  {"subi", Format::RdRs1Imm},  {"muli", Format::RdRs1Imm},
    {"divi", Format::RdRs1Imm},  {"modi", Format::RdRs1Imm},  {"andi", Format::RdRs1Imm},
    {"ori", Format::RdRs1Imm},   {"xori", Format::RdRs1Imm},  {"lsli", Format::RdRs1Imm},
    {"lsri", Format::RdRs1Imm},  {"asri", Format::RdRs1Imm},  {"movi", Format::RdImm},
    {"cmpi", Format::Rs1Imm},    {"ld", Format::RdRs1Imm},    {"st", Format::RdRs1Imm},
    {"fadd", Format::RdRs1Rs2},  {"fsub", Format::RdRs1Rs2},  {"fmul", Format::RdRs1Rs2},
    {"fdiv", Format::RdRs1Rs2},  {"fcmp", Format::Rs1Rs2},    {"b", Format::Offset},
    {"beq", Format::Offset},     {"bgt", Format::Offset},     {"call", Format::Offset},
    {"ret", Format::None},       {"movz", Format::Window},    {"retz", Format::None},
    {"ebreak", Format::None},    {"ecall", Format::None},      {"xtime", Format::RdRs1},
}};

std::string reg(uint32_t index) {
    return index == GPR_SP ? "sp" : "r" + std::to_string(index);
}

std::string window(uint32_t index) {
    switch (index) {
        case WIN_R0: return "r0";
        case WIN_SP: return "sp";
        case WIN_OLDPC: return "oldPC";
        case WIN_OLDSP: return "oldSP";
        case WIN_OLDFLAGS: return "oldFlags";
        case WIN_EBASE: return "eBase";
        case WIN_ECODE: return "eCode";
        default: return "w" + std::to_string(index);
    }
}

// imm18 = [17:16] modifier | [15:0] value; printed like the assembler writes it.
std::string immediate(uint32_t instruction) {
    const uint32_t modifier = (instruction >> 16) & 0x3u;
    const uint32_t value = instruction & 0xFFFFu;
    if (modifier == 0x1) {
        return "u" + std::to_string(value);
    }
    if (modifier == 0x2) {
        return "h" + std::to_string(value);
    }
    return std::to_string(static_cast<int16_t>(value));
}

std::string hex(uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%04x", value);
    return buffer;
}

}  // namespace

std::string MnemonicOf(uint8_t opcode) {
    return opcode < kOps.size() ? kOps[opcode].name : "???";
}

std::string ExceptionName(uint8_t cause) {
    switch (cause) {
        case EXC_NONE: return "none";
        case EXC_PBRK: return "pbrk";
        case EXC_SCALL: return "scall";
        case EXC_DIV0: return "div0";
        case EXC_RASOVF: return "rasovf";
        case EXC_WRONGINST: return "wronginst";
        case EXC_LOWPRIV: return "lowpriv";
        case EXC_INTERRUPT: return "interrupt";
        default: return "exc" + std::to_string(cause);
    }
}

std::string Disassemble(uint32_t instruction, uint32_t pc) {
    const uint8_t opcode = static_cast<uint8_t>((instruction >> 26) & 0x3F);
    if (opcode >= kOps.size()) {
        return ".word " + hex(instruction) + "   (invalid opcode " + std::to_string(opcode) + ")";
    }
    const uint32_t rd = (instruction >> 22) & 0xF;
    const uint32_t rs1 = (instruction >> 18) & 0xF;
    const uint32_t rs2 = (instruction >> 14) & 0xF;
    const std::string name = kOps[opcode].name;

    switch (kOps[opcode].format) {
        case Format::None:     return name;
        case Format::RdRs1Rs2: return name + " " + reg(rd) + ", " + reg(rs1) + ", " + reg(rs2);
        case Format::RdRs1:    return name + " " + reg(rd) + ", " + reg(rs1);
        case Format::RdRs2:    return name + " " + reg(rd) + ", " + reg(rs2);
        case Format::Rs1Rs2:   return name + " " + reg(rs1) + ", " + reg(rs2);
        case Format::RdRs1Imm:
            return name + " " + reg(rd) + ", " + reg(rs1) + ", " + immediate(instruction);
        case Format::RdImm:    return name + " " + reg(rd) + ", " + immediate(instruction);
        case Format::Rs1Imm:   return name + " " + reg(rs1) + ", " + immediate(instruction);
        case Format::Window:   return name + " " + window(rd) + ", " + window(rs2);
        case Format::Offset: {
            const uint32_t signBit = 1u << 24;
            const int32_t offset =
                static_cast<int32_t>(((instruction & 0x1FFFFFFu) ^ signBit) - signBit);
            const uint32_t target = pc + static_cast<uint32_t>(offset * 4);
            const bool predictTaken = ((instruction >> 25) & 1u) != 0;
            return name + " " + hex(target) + (predictTaken ? "  (T)" : "");
        }
    }
    return name;
}

// ALU demo: runs readable examples through the ALU, or one operation from the
// command line.
//
//   make alu-demo                       # preset examples for every operation
//   ./build/alu_demo mul 7 -3           # one integer operation
//   ./build/alu_demo fadd 1.5 2.25      # one FP operation
//   ./build/alu_demo itof 100           # itof / ftoi / not take one operand
//
// Integer operands accept decimal or 0x hex; FP operands are decimal floats.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "components/alu.hpp"

namespace {

enum class Kind { Int, Float };

struct OpInfo {
    const char* name;
    AluOp op;
    Kind in;        // operand type
    Kind out;       // result type
    bool unary;     // uses only A
    const char* symbol;
    const char* unit;
};

const OpInfo kOps[] = {
    {"add", AluOp::ADD, Kind::Int, Kind::Int, false, "+", "Kogge-Stone adder"},
    {"sub", AluOp::SUB, Kind::Int, Kind::Int, false, "-", "Kogge-Stone adder (A + ~B + 1)"},
    {"cmp", AluOp::CMP, Kind::Int, Kind::Int, false, "cmp", "Kogge-Stone subtract -> flags"},
    {"mul", AluOp::MUL, Kind::Int, Kind::Int, false, "*", "Radix-4 Booth + Dadda tree"},
    {"div", AluOp::DIV, Kind::Int, Kind::Int, false, "/", "Non-restoring divider (quotient)"},
    {"mod", AluOp::MOD, Kind::Int, Kind::Int, false, "mod", "Non-restoring divider (isMod: remainder)"},
    {"and", AluOp::AND, Kind::Int, Kind::Int, false, "&", "Logic unit"},
    {"or", AluOp::OR, Kind::Int, Kind::Int, false, "|", "Logic unit"},
    {"xor", AluOp::XOR, Kind::Int, Kind::Int, false, "^", "Logic unit"},
    {"not", AluOp::NOT, Kind::Int, Kind::Int, true, "~", "Logic unit"},
    {"lsl", AluOp::LSL, Kind::Int, Kind::Int, false, "<<", "Barrel shifter (bit-reversed)"},
    {"lsr", AluOp::LSR, Kind::Int, Kind::Int, false, ">>", "Barrel shifter (fill 0)"},
    {"asr", AluOp::ASR, Kind::Int, Kind::Int, false, ">>>", "Barrel shifter (fill sign)"},
    {"fadd", AluOp::FADD, Kind::Float, Kind::Float, false, "+", "FPU: align, add, normalise, round"},
    {"fsub", AluOp::FSUB, Kind::Float, Kind::Float, false, "-", "FPU: flip sign of B, then fadd"},
    {"fmul", AluOp::FMUL, Kind::Float, Kind::Float, false, "*", "FPU: exponent add + Booth/Dadda"},
    {"fdiv", AluOp::FDIV, Kind::Float, Kind::Float, false, "/", "FPU: exponent sub + non-restoring"},
    {"fcmp", AluOp::FCMP, Kind::Float, Kind::Float, false, "fcmp", "FPU: ordered key -> flags"},
    {"itof", AluOp::ITOF, Kind::Int, Kind::Float, true, "", "FPU: leading zeros + shift + round"},
    {"ftoi", AluOp::FTOI, Kind::Float, Kind::Int, true, "", "FPU: shift + truncate"},
};

float bitsToFloat(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t floatToBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int32_t asSigned(uint32_t x) {
    int32_t value;
    std::memcpy(&value, &x, sizeof(value));
    return value;
}

std::string show(uint32_t word, Kind kind) {
    char text[64];
    if (kind == Kind::Float) {
        std::snprintf(text, sizeof(text), "%g (0x%08X)", static_cast<double>(bitsToFloat(word)), word);
    } else {
        std::snprintf(text, sizeof(text), "%d (0x%08X)", asSigned(word), word);
    }
    return text;
}

uint32_t parse(const char* text, Kind kind) {
    if (kind == Kind::Float) return floatToBits(std::strtof(text, nullptr));
    return static_cast<uint32_t>(std::strtoll(text, nullptr, 0));
}

void run(Alu& alu, const OpInfo& info, uint32_t a, uint32_t b) {
    const uint32_t result = alu.Execute(info.op, a, b);

    std::printf("%-5s ", info.name);
    if (info.unary) {
        if (info.symbol[0] != '\0') std::printf("%s ", info.symbol);
        std::printf("%s", show(a, info.in).c_str());
    } else {
        std::printf("%s %s %s", show(a, info.in).c_str(), info.symbol, show(b, info.in).c_str());
    }

    if (info.op == AluOp::CMP || info.op == AluOp::FCMP) {
        std::printf("  ->  E=%d GT=%d LT=%d", alu.FlagE(), alu.FlagGt(), alu.FlagLt());
    } else {
        std::printf("  =  %s", show(result, info.out).c_str());
    }
    if (alu.DivideByZero()) std::printf("  [divide by zero]");
    std::printf("\n      unit: %s, %d cycle%s\n\n", info.unit, alu.LastLatency(),
                alu.LastLatency() == 1 ? "" : "s");
}

const OpInfo* find(const char* name) {
    for (const OpInfo& info : kOps) {
        if (std::strcmp(info.name, name) == 0) return &info;
    }
    return nullptr;
}

void runPreset(Alu& alu, const char* name, const char* a, const char* b) {
    const OpInfo* info = find(name);
    run(alu, *info, parse(a, info->in), parse(b, info->in));
}

void usage() {
    std::printf("usage: alu_demo                 run the preset examples\n"
                "       alu_demo <op> <a> [b]    run one operation\n"
                "ops:");
    for (const OpInfo& info : kOps) std::printf(" %s", info.name);
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    Alu alu;

    if (argc >= 2) {
        const OpInfo* info = find(argv[1]);
        if (info == nullptr || argc < (info->unary ? 3 : 4)) {
            usage();
            return 1;
        }
        const uint32_t a = parse(argv[2], info->in);
        const uint32_t b = info->unary ? 0 : parse(argv[3], info->in);
        run(alu, *info, a, b);
        return 0;
    }

    std::printf("=== RISC201 ALU demo ===\n\n-- Integer --\n\n");
    runPreset(alu, "add", "25", "17");
    runPreset(alu, "add", "0x7FFFFFFF", "1");     // overflow wraps
    runPreset(alu, "sub", "10", "25");
    runPreset(alu, "cmp", "-5", "3");
    runPreset(alu, "mul", "7", "-3");
    runPreset(alu, "mul", "12345", "6789");
    runPreset(alu, "div", "-17", "5");
    runPreset(alu, "mod", "-17", "5");
    runPreset(alu, "div", "9", "0");               // divide by zero
    runPreset(alu, "and", "0xF0F0", "0xFF00");
    runPreset(alu, "or", "0xF0F0", "0x0F00");
    runPreset(alu, "xor", "0xF0F0", "0xFF00");
    runPreset(alu, "not", "0", "0");
    runPreset(alu, "lsl", "1", "5");
    runPreset(alu, "lsr", "-16", "2");
    runPreset(alu, "asr", "-16", "2");

    std::printf("-- Floating point (IEEE-754 single) --\n\n");
    runPreset(alu, "fadd", "1.5", "2.25");
    runPreset(alu, "fadd", "0.1", "0.2");
    runPreset(alu, "fsub", "1", "3.5");
    runPreset(alu, "fmul", "-2.5", "4");
    runPreset(alu, "fdiv", "1", "3");
    runPreset(alu, "fdiv", "1", "0");              // -> infinity
    runPreset(alu, "fdiv", "0", "0");              // -> NaN
    runPreset(alu, "fcmp", "2.5", "-1");
    runPreset(alu, "itof", "100", "0");
    runPreset(alu, "itof", "-7", "0");
    runPreset(alu, "ftoi", "3.99", "0");           // truncates
    runPreset(alu, "ftoi", "-3.99", "0");
    return 0;
}

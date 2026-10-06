// ALU self-test: every integer operation against native C++ arithmetic.
// Build and run with:  make alu-test

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "components/alu.hpp"
#include "components/alu_units.hpp"

namespace {

int32_t toSigned(uint32_t x) {
    int32_t value;
    std::memcpy(&value, &x, sizeof(value));
    return value;
}

uint32_t toUnsigned(int32_t x) {
    uint32_t value;
    std::memcpy(&value, &x, sizeof(value));
    return value;
}

// Native result for each operation (the "golden" reference).
uint32_t reference(AluOp op, uint32_t a, uint32_t b) {
    const int32_t sa = toSigned(a);
    const int32_t sb = toSigned(b);
    switch (op) {
        case AluOp::ADD:  return a + b;
        case AluOp::SUB:
        case AluOp::CMP:  return a - b;
        case AluOp::MUL:  return a * b;
        case AluOp::DIV:
            if (b == 0) return 0;
            if (sa == INT32_MIN && sb == -1) return a;  // wraps
            return toUnsigned(sa / sb);
        case AluOp::AND:  return a & b;
        case AluOp::OR:   return a | b;
        case AluOp::NOT:  return ~a;
        case AluOp::PASS: return a;
        case AluOp::LSL:  return a << (b & 31u);
        case AluOp::LSR:  return a >> (b & 31u);
        case AluOp::ASR:  return toUnsigned(sa >> (b & 31u));
        default:          return 0;
    }
    return 0;
}

bool flagsMatch(const Alu& alu, uint32_t a, uint32_t b) {
    return alu.FlagE() == (a == b) && alu.FlagGt() == (toSigned(a) > toSigned(b)) &&
           alu.FlagLt() == (toSigned(a) < toSigned(b));
}

struct OpCase {
    AluOp op;
    const char* name;
};

const OpCase kOps[] = {
    {AluOp::ADD, "add"},   {AluOp::SUB, "sub"},   {AluOp::CMP, "cmp"},
    {AluOp::MUL, "mul"},   {AluOp::DIV, "div"},   {AluOp::AND, "and"},
    {AluOp::OR, "or"},     {AluOp::NOT, "not"},   {AluOp::PASS, "pass"},
    {AluOp::LSL, "lsl"},   {AluOp::LSR, "lsr"},   {AluOp::ASR, "asr"},
};

}  // namespace

int main() {
    Alu alu;
    std::mt19937 rng(201);
    std::uniform_int_distribution<uint32_t> anyWord;

    const std::vector<uint32_t> intEdges = {0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                                            12345, 0xDEADBEEF};
    const int trials = 200000;

    bool allPassed = true;
    std::printf("%-5s %8s %7s %8s\n", "op", "tests", "failed", "latency");

    for (const OpCase& c : kOps) {
        int tests = 0;
        int failed = 0;
        const auto check = [&](uint32_t a, uint32_t b) {
            const uint32_t got = alu.Execute(c.op, a, b);
            ++tests;
            bool ok = got == reference(c.op, a, b);
            if (c.op == AluOp::CMP) ok = ok && flagsMatch(alu, a, b);
            if (c.op == AluOp::DIV) ok = ok && alu.DivideByZero() == (b == 0);
            if (!ok && failed++ < 3) {
                std::printf("  FAIL %s(0x%08X, 0x%08X) -> 0x%08X\n", c.name, a, b, got);
            }
        };

        for (uint32_t a : intEdges) {
            for (uint32_t b : intEdges) check(a, b);
        }
        for (int i = 0; i < trials; ++i) check(anyWord(rng), anyWord(rng));

        alu.Execute(c.op, 7, 3);
        allPassed = allPassed && failed == 0;
        std::printf("%-5s %8d %7d %8d\n", c.name, tests, failed, alu.LastLatency());
    }

    // Wallace must give the same products as Dadda.
    int wallaceFailures = 0;
    for (int i = 0; i < trials; ++i) {
        const uint32_t a = anyWord(rng);
        const uint32_t b = anyWord(rng);
        if (AluUnits::BoothMultiply(a, b, AluUnits::ReductionTree::Wallace) != a * b) {
            ++wallaceFailures;
        }
    }
    allPassed = allPassed && wallaceFailures == 0;
    std::printf("\nmul with Wallace tree: %d failures\n", wallaceFailures);

    const AluUnits::TreeCost dadda = AluUnits::CountTreeHardware(AluUnits::ReductionTree::Dadda);
    const AluUnits::TreeCost wallace = AluUnits::CountTreeHardware(AluUnits::ReductionTree::Wallace);
    std::printf("\nReduction tree   levels   full adders   half adders\n");
    std::printf("  Dadda          %6d %13d %13d\n", dadda.levels, dadda.fullAdders, dadda.halfAdders);
    std::printf("  Wallace        %6d %13d %13d\n", wallace.levels, wallace.fullAdders, wallace.halfAdders);

    std::printf("\n%s\n", allPassed ? "PASS: all ALU operations match" : "FAIL: see above");
    return allPassed ? 0 : 1;
}

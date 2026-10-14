// ALU self-test: every operation against native C++ arithmetic.
// Integer ops are compared bit for bit with C++ integer maths; FP ops with
// the host's IEEE-754 float maths (round to nearest even).
// Build and run with:  make alu-test

#include <cmath>
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

float toFloat(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t toBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool isFloatOp(AluOp op) {
    return op == AluOp::FADD || op == AluOp::FSUB || op == AluOp::FMUL || op == AluOp::FDIV ||
           op == AluOp::FCMP || op == AluOp::ITOF || op == AluOp::FTOI;
}

// ftoi reference: truncate toward zero, saturate, NaN -> 0.
uint32_t floatToIntReference(uint32_t bits) {
    const float f = toFloat(bits);
    if (std::isnan(f)) return 0;
    if (f >= 2147483648.0f) return 0x7FFFFFFFu;
    if (f < -2147483648.0f) return 0x80000000u;
    return toUnsigned(static_cast<int32_t>(f));
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
        case AluOp::MOD:
            if (b == 0) return 0;
            if (sa == INT32_MIN && sb == -1) return 0;
            return toUnsigned(sa % sb);
        case AluOp::AND:  return a & b;
        case AluOp::OR:   return a | b;
        case AluOp::XOR:  return a ^ b;
        case AluOp::NOT:  return ~a;
        case AluOp::PASS: return b;
        case AluOp::LSL:  return a << (b & 31u);
        case AluOp::LSR:  return a >> (b & 31u);
        case AluOp::ASR:  return toUnsigned(sa >> (b & 31u));
        case AluOp::FADD: return toBits(toFloat(a) + toFloat(b));
        case AluOp::FSUB: return toBits(toFloat(a) - toFloat(b));
        case AluOp::FMUL: return toBits(toFloat(a) * toFloat(b));
        case AluOp::FDIV: return toBits(toFloat(a) / toFloat(b));
        case AluOp::FCMP: return 0;
        case AluOp::ITOF: return toBits(static_cast<float>(sa));
        case AluOp::FTOI: return floatToIntReference(a);
    }
    return 0;
}

// Any NaN matches any NaN (the payload is not specified).
bool sameResult(AluOp op, uint32_t got, uint32_t want) {
    if (got == want) return true;
    const bool fpResult = op == AluOp::FADD || op == AluOp::FSUB || op == AluOp::FMUL ||
                          op == AluOp::FDIV || op == AluOp::ITOF;
    return fpResult && std::isnan(toFloat(got)) && std::isnan(toFloat(want));
}

bool flagsMatch(const Alu& alu, AluOp op, uint32_t a, uint32_t b) {
    bool e, gt, lt;
    if (op == AluOp::CMP) {
        e = a == b;
        gt = toSigned(a) > toSigned(b);
        lt = toSigned(a) < toSigned(b);
    } else {
        e = toFloat(a) == toFloat(b);
        gt = toFloat(a) > toFloat(b);
        lt = toFloat(a) < toFloat(b);
    }
    return alu.FlagE() == e && alu.FlagGt() == gt && alu.FlagLt() == lt;
}

struct OpCase {
    AluOp op;
    const char* name;
};

const OpCase kOps[] = {
    {AluOp::ADD, "add"},   {AluOp::SUB, "sub"},   {AluOp::CMP, "cmp"},
    {AluOp::MUL, "mul"},   {AluOp::DIV, "div"},   {AluOp::MOD, "mod"},
    {AluOp::AND, "and"},   {AluOp::OR, "or"},     {AluOp::XOR, "xor"},
    {AluOp::NOT, "not"},   {AluOp::PASS, "pass"}, {AluOp::LSL, "lsl"},
    {AluOp::LSR, "lsr"},
    {AluOp::ASR, "asr"},   {AluOp::FADD, "fadd"}, {AluOp::FSUB, "fsub"},
    {AluOp::FMUL, "fmul"}, {AluOp::FDIV, "fdiv"}, {AluOp::FCMP, "fcmp"},
    {AluOp::ITOF, "itof"}, {AluOp::FTOI, "ftoi"},
};

}  // namespace

int main() {
    Alu alu;
    std::mt19937 rng(201);
    std::uniform_int_distribution<uint32_t> anyWord;
    std::uniform_real_distribution<float> everydayFloat(-1e6f, 1e6f);
    std::uniform_int_distribution<uint32_t> tinyBits(0, 0x01000000);  // zero, subnormals, smallest normals

    const std::vector<uint32_t> intEdges = {0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                                            12345, 0xDEADBEEF};
    // ±0, ±1, ±∞, NaN, smallest/largest subnormal, smallest/largest normal, 2^31, −2^31, 1/3.
    const std::vector<uint32_t> floatEdges = {0x00000000, 0x80000000, 0x3F800000, 0xBF800000,
                                              0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001,
                                              0x007FFFFF, 0x00800000, 0x7F7FFFFF, 0x4F000000,
                                              0xCF000000, 0x3EAAAAAB};
    const int trials = 200000;

    bool allPassed = true;
    std::printf("%-5s %8s %7s %8s\n", "op", "tests", "failed", "latency");

    for (const OpCase& c : kOps) {
        int tests = 0;
        int failed = 0;
        const auto check = [&](uint32_t a, uint32_t b) {
            const uint32_t got = alu.Execute(c.op, a, b);
            ++tests;
            bool ok = sameResult(c.op, got, reference(c.op, a, b));
            if (c.op == AluOp::CMP || c.op == AluOp::FCMP) ok = ok && flagsMatch(alu, c.op, a, b);
            if (c.op == AluOp::DIV || c.op == AluOp::MOD) ok = ok && alu.DivideByZero() == (b == 0);
            if (!ok && failed++ < 3) {
                std::printf("  FAIL %s(0x%08X, 0x%08X) -> 0x%08X, expected 0x%08X\n", c.name, a, b,
                            got, reference(c.op, a, b));
            }
        };

        const bool fp = isFloatOp(c.op);
        const std::vector<uint32_t>& edges = fp ? floatEdges : intEdges;
        for (uint32_t a : edges) {
            for (uint32_t b : edges) check(a, b);
        }
        for (int i = 0; i < trials; ++i) check(anyWord(rng), anyWord(rng));  // raw bit patterns
        if (fp) {
            for (int i = 0; i < trials; ++i) {  // everyday values
                const uint32_t a = c.op == AluOp::ITOF ? toUnsigned(static_cast<int32_t>(everydayFloat(rng)))
                                                       : toBits(everydayFloat(rng));
                check(a, toBits(everydayFloat(rng)));
            }
            for (int i = 0; i < trials; ++i) {  // subnormals and gradual underflow
                check(tinyBits(rng) | (anyWord(rng) & 0x80000000u), tinyBits(rng) | ((i & 1) ? 0x3F000000u : 0u));
            }
        }

        alu.Execute(c.op, 7, 3);
        allPassed = allPassed && failed == 0;
        std::printf("%-5s %8d %7d %8d\n", c.name, tests, failed, alu.LastLatency());
    }

    const AluUnits::TreeCost dadda = AluUnits::CountDaddaHardware();
    std::printf("\nDadda tree: %d levels, %d full adders, %d half adders\n", dadda.levels,
                dadda.fullAdders, dadda.halfAdders);

    std::printf("\n%s\n", allPassed ? "PASS: all ALU operations match" : "FAIL: see above");
    return allPassed ? 0 : 1;
}

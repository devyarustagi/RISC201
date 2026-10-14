#include "components/alu.hpp"

#include "components/alu_units.hpp"

namespace {

// EX-stage latency of each operation in the hardware design.
int latencyOf(AluOp op) {
    switch (op) {
        case AluOp::MUL:  return 2;
        case AluOp::DIV:
        case AluOp::MOD:  return 32;
        case AluOp::FADD:
        case AluOp::FSUB:
        case AluOp::FMUL: return 2;
        case AluOp::FDIV: return 26;
        default:          return 1;
    }
}

}  // namespace

void Alu::setFlags(const AluUnits::CompareResult& compare) {
    flagE_ = compare.equal;
    flagGt_ = compare.greater;
    flagLt_ = compare.less;
}

uint32_t Alu::Execute(AluOp op, uint32_t a, uint32_t b) {
    using namespace AluUnits;
    divideByZero_ = false;
    lastLatency_ = latencyOf(op);

    switch (op) {
        // Adder / subtractor (Kogge-Stone).
        case AluOp::ADD: return AddSub(a, b, false).sum;
        case AluOp::SUB: return AddSub(a, b, true).sum;
        case AluOp::CMP:
            setFlags(CompareSigned(a, b));
            return AddSub(a, b, true).sum;

        // Multiplier (radix-4 Booth + reduction tree).
        case AluOp::MUL: return BoothMultiply(a, b);

        // Divider (non-restoring): isMod ? R : Q.
        case AluOp::DIV:
        case AluOp::MOD: {
            const DivideResult result = NonRestoringDivide(a, b);
            divideByZero_ = result.divideByZero;
            if (divideByZero_) lastLatency_ = 1;
            return op == AluOp::MOD ? result.remainder : result.quotient;
        }

        // Logic unit + pass-through.
        case AluOp::AND:  return a & b;
        case AluOp::OR:   return a | b;
        case AluOp::XOR:  return a ^ b;
        case AluOp::NOT:  return ~a;  // not rd, rs1
        case AluOp::PASS: return b;   // operand move: passes the B operand (op2 / macro immediate)

        // Barrel shifter.
        case AluOp::LSL: return BarrelShift(a, b, ShiftKind::Lsl);
        case AluOp::LSR: return BarrelShift(a, b, ShiftKind::Lsr);
        case AluOp::ASR: return BarrelShift(a, b, ShiftKind::Asr);

        // Floating-point unit (IEEE-754 single precision).
        case AluOp::FADD: return FloatAdd(a, b, false);
        case AluOp::FSUB: return FloatAdd(a, b, true);
        case AluOp::FMUL: return FloatMultiply(a, b);
        case AluOp::FDIV: return FloatDivide(a, b);
        case AluOp::FCMP:
            setFlags(CompareFloat(a, b));  // flags WE = isFcmp
            return 0;
        case AluOp::ITOF: return IntToFloat(a);
        case AluOp::FTOI: return FloatToInt(a);
    }
    return 0;
}

void Alu::Reset() {
    flagE_ = false;
    flagGt_ = false;
    flagLt_ = false;
    divideByZero_ = false;
    lastLatency_ = 1;
}

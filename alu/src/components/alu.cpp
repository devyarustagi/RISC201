#include "components/alu.hpp"

#include "components/alu_units.hpp"

namespace {

// EX-stage latency of each operation in the hardware design.
int latencyOf(AluOp op) {
    switch (op) {
        case AluOp::MUL:  return 2;
        case AluOp::DIV:  return 32;
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
        case AluOp::MUL: return BoothMultiply(a, b, tree_);

        // Divider (non-restoring); the ISA has no mod, so only the quotient is used.
        case AluOp::DIV: {
            const DivideResult result = NonRestoringDivide(a, b);
            divideByZero_ = result.divideByZero;
            if (divideByZero_) lastLatency_ = 1;
            return result.quotient;
        }

        // Logic unit + pass-through.
        case AluOp::AND:  return a & b;
        case AluOp::OR:   return a | b;
        case AluOp::NOT:  return ~a;  // not rd, rs1
        case AluOp::PASS: return a;   // mov rd, imm (A = macro immediate)

        // Barrel shifter.
        case AluOp::LSL: return BarrelShift(a, b, ShiftKind::Lsl);
        case AluOp::LSR: return BarrelShift(a, b, ShiftKind::Lsr);
        case AluOp::ASR: return BarrelShift(a, b, ShiftKind::Asr);

        // Floating point is not part of this ALU.
        case AluOp::FADD:
        case AluOp::FSUB:
        case AluOp::FMUL:
        case AluOp::FDIV:
        case AluOp::FCMP:
            return 0;
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

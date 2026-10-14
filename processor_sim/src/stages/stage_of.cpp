#include "stages/stage_of.hpp"

#include <cstdint>

#include "components/control_unit.hpp"
#include "components/reg_file.hpp"
#include "components/special_regs.hpp"

namespace {

// Sign-extends the low `bits` bits of `value` to a 32-bit signed value.
int32_t signExtend(uint32_t value, int bits) {
    const uint32_t mask = (1u << bits) - 1u;
    const uint32_t signBit = 1u << (bits - 1);
    return static_cast<int32_t>(((value & mask) ^ signBit) - signBit);
}

// Reads one entry of the privileged register window (movz source).
uint32_t readWindow(uint8_t index, const RegFile& gpr, const SpecialRegs& special) {
    switch (index) {
        case WIN_R0:  return 0;
        case WIN_SP:  return gpr.Read(GPR_SP);
        case WIN_OLDPC: return special.OldPC();
        case WIN_OLDSP: return special.OldSP();
        case WIN_OLDFLAGS:
            return (special.OldFlagE() ? 0x1u : 0u) |
                   (special.OldFlagGt() ? 0x2u : 0u) |
                   (special.OldFlagLt() ? 0x4u : 0u);
        case WIN_EBASE: return special.EBase();
        case WIN_ECODE: return special.ECode();
        default: return 0;
    }
}

// Has the decoded opcode raised a synchronous exception in the OF stage?
uint8_t ofExceptionCause(uint8_t opcode, const SpecialRegs& special) {
    if (opcode > MacroOp::ECALL) {
        return EXC_WRONGINST;
    }
    if ((opcode == MacroOp::MOVZ || opcode == MacroOp::RETZ) && special.Cpl() == 1) {
        return EXC_LOWPRIV;
    }
    if (opcode == MacroOp::EBREAK) {
        return EXC_PBRK;
    }
    if (opcode == MacroOp::ECALL) {
        return EXC_SCALL;
    }
    return EXC_NONE;
}

}  // namespace

void StageOF::Reset() {
    ras_.Reset();
}

void StageOF::Latch() {
    ras_.Latch();
}

OfResult StageOF::Run(const PipelineRegister<IfOfPacket>& ifOf,
                      PipelineRegister<OfExPacket>& ofEx,
                      const RegFile& gpr, ControlUnit& control,
                      const SpecialRegs& special, bool branchLock) {
    OfResult result;
    const IfOfPacket& in = ifOf.out;
    ofEx.in = OfExPacket{};

    if (!in.valid) {
        ras_.Run(false, false, branchLock, 0);   // bubble -> no push/pop
        result.rasOut = ras_.Out();
        return result;   // no-op control
    }

    OfExPacket& out = ofEx.in;
    out.valid = true;
    out.pc = in.pc;
    out.instruction = in.instruction;
    out.predictTaken = in.predictTaken;
    // Propagate an async mark set in IF; a synchronous mark may override it below.
    out.isException = in.isException;
    out.cause = in.cause;

    const uint32_t inst = in.instruction;
    const uint8_t opcode = static_cast<uint8_t>((inst >> 26) & 0x3F);
    const uint8_t rd  = static_cast<uint8_t>((inst >> 22) & 0xF);
    const uint8_t rs1 = static_cast<uint8_t>((inst >> 18) & 0xF);
    const uint8_t rs2 = static_cast<uint8_t>((inst >> 14) & 0xF);

    // I-format instructions (addi..st) carry imm18 = [17:16] modifier |
    // [15:0] value. Branch targets are not computed here: the IF stage predicts
    // the target and the EX stage recovers it on a misprediction.
    const bool immediateFormat = (opcode >= MacroOp::ADDI && opcode <= MacroOp::ST);

    out.imm = 0;
    if (immediateFormat) {
        const uint32_t raw = inst & 0x3FFFFu;
        const uint32_t modifier = (raw >> 16) & 0x3u;
        const uint32_t value16 = raw & 0xFFFFu;
        switch (modifier) {
            case 0x1: out.imm = static_cast<int32_t>(value16); break;        // u: unsigned 16-bit
            case 0x2: out.imm = static_cast<int32_t>(value16 << 16); break;  // h: high half
            default:  out.imm = signExtend(value16, 16); break;              // signed 16-bit
        }
    }

    // The control word decides the operand-source muxes: st puts rd (its data
    // register) into op2.
    if (!control.Busy()) {
        control.Begin(opcode);
    }
    out.control = control.Step();

    // Privileged move: read the window source (the destination is routed in MARW).
    if (out.control.isMovz) {
        out.control.movzDst = rd;
        out.op2 = readWindow(rs2, gpr, special);
    }

    // RAS: push ID_PC + 4 on a call, pop on a ret, both gated by the branch lock.
    ras_.Run(out.control.isCall, out.control.isRet, branchLock, in.pc + 4);
    result.rasOut = ras_.Out();

    // Exception Unit marking (OF stage). An asynchronous mark set in IF is
    // superseded only by a synchronous exception on this instruction.
    uint8_t cause = out.isException ? out.cause : EXC_NONE;
    if (!out.isException) {
        cause = ofExceptionCause(opcode, special);
        if (cause == EXC_NONE && out.control.isCall && ras_.OverflowPending()) {
            cause = EXC_RASOVF;
        }
    }
    out.isException = cause != EXC_NONE;
    out.cause = cause;

    result.control = out.control;
    result.opcode = opcode;
    result.rs1 = rs1;
    result.rs2 = rs2;
    result.rd = rd;
    result.isRet = out.control.isRet;
    result.isRetz = out.control.isRetz && cause == EXC_NONE;

    out.op1 = gpr.Read(rs1);
    if (!out.control.isMovz) {
        out.op2 = out.control.isSt ? gpr.Read(rd) : gpr.Read(rs2);
    }
    out.rd = rd;
    if (out.control.isMovz) {
        // Forwarding sees only the GPR half of the window (r0 / sp).
        out.rs1 = 0;
        out.rs2 = (rs2 == WIN_SP) ? GPR_SP : 0;
    } else {
        out.rs1 = rs1;
        out.rs2 = rs2;
    }
    return result;
}

#include "stages/stage_ibu.hpp"

#include "components/control_unit.hpp"
#include "components/special_regs.hpp"

namespace {

int32_t signExtend(uint32_t value, int bits) {
    const uint32_t mask = (1u << bits) - 1u;
    const uint32_t signBit = 1u << (bits - 1);
    return static_cast<int32_t>(((value & mask) ^ signBit) - signBit);
}

// Synchronous exceptions detected while decoding (as in exceptions.md).
uint8_t decodeExceptionCause(uint8_t opcode, const SpecialRegs& special) {
    if (opcode > MacroOp::XTIME) {
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

IbuResult StageIBU::Run(const PipelineRegister<IfIbuPacket>& ifIbu,
                        PipelineRegister<IbuRfPacket>& ibuRf,
                        ControlUnit& control, const SpecialRegs& special) {
    IbuResult result;
    const IfIbuPacket& in = ifIbu.out;
    ibuRf.in = IbuRfPacket{};
    result.rasOut = ras_.Out();
    if (!in.valid) {
        return result;
    }

    IbuRfPacket& out = ibuRf.in;
    static_cast<PacketBase&>(out) = in;
    out.valid = true;

    const uint32_t inst = in.instruction;
    const uint8_t opcode = static_cast<uint8_t>((inst >> 26) & 0x3F);
    out.rd = static_cast<uint8_t>((inst >> 22) & 0xF);
    out.rs1 = static_cast<uint8_t>((inst >> 18) & 0xF);
    out.rs2 = static_cast<uint8_t>((inst >> 14) & 0xF);

    // Immediate Unit: I-format instructions carry [17:16] modifier | [15:0] value.
    if (opcode >= MacroOp::ADDI && opcode <= MacroOp::ST) {
        const uint32_t raw = inst & 0x3FFFFu;
        const uint32_t value16 = raw & 0xFFFFu;
        switch ((raw >> 16) & 0x3u) {
            case 0x1: out.imm = static_cast<int32_t>(value16); break;        // unsigned
            case 0x2: out.imm = static_cast<int32_t>(value16 << 16); break;  // high half
            default:  out.imm = signExtend(value16, 16); break;              // signed
        }
    }

    // Control Unit (microcode).
    if (!control.Busy()) {
        control.Begin(opcode);
    }
    out.control = control.Step();
    if (out.control.isMovz) {
        out.control.movzDst = out.rd;
        out.windowSrc = out.rs2;
    }

    // RAS. Updated speculatively; the packet keeps the state before and after
    // so a flush can put it back. An instruction already marked in IF
    // (interrupt) never commits and is locked out.
    out.rasBefore = ras_;
    ras_.Run(out.control.isCall, out.control.isRet, in.isException, in.pc + 4);
    const bool overflow = ras_.OverflowPending();
    ras_.Latch();
    out.rasAfter = ras_;

    // Exception marking. An interrupt mark from IF is kept.
    if (!out.isException) {
        uint8_t cause = decodeExceptionCause(opcode, special);
        if (cause == EXC_NONE && out.control.isCall && overflow) {
            cause = EXC_RASOVF;
        }
        out.isException = cause != EXC_NONE;
        out.cause = cause;
    }

    result.valid = true;
    result.control = out.control;
    result.isRet = out.control.isRet && !out.isException;
    result.isRetz = out.control.isRetz && !out.isException;
    return result;
}

#include "stages/stage_ex.hpp"

#include "components/alu.hpp"
#include "components/scratch_file.hpp"

namespace {

// Sign-extends the 25-bit branch offset field [24:0].
int32_t signExtend25(uint32_t value) {
    const uint32_t signBit = 1u << 24;
    return static_cast<int32_t>(((value & 0x1FFFFFFu) ^ signBit) - signBit);
}

// Forwarding Unit: picks a more recent value for a register operand. The
// instruction in MA (EX/MARW register) takes priority over the one in WB.
uint32_t forwardValue(uint32_t current, uint8_t reg,
                      const ExMemPacket& ma, const WbResult& wb) {
    if (reg == 0) {
        return current;
    }
    if (ma.valid) {
        uint8_t dest = 0;
        if (ma.control.isWb && !ma.control.isLd) {
            dest = ma.rd;
        } else if (ma.control.isMovz && ma.control.movzDst == WIN_SP) {
            dest = GPR_SP;   // movz writing sp
        }
        if (dest != 0 && dest == reg) {
            return ma.aluResult;
        }
    }
    if (wb.wb && wb.dest == reg) {
        return wb.value;
    }
    return current;
}

uint32_t selectOperandA(uint8_t select, uint32_t op1, const OfExPacket& in,
                        const ScratchFile& scratch) {
    switch (select) {
        case A_CONST:   return static_cast<uint32_t>(in.control.microImm);
        case A_SCRATCH: return scratch.Read(in.control.src1ScratchAddr);
        case A_OP1:
        default:        return op1;
    }
}

uint32_t selectOperandB(uint8_t select, uint32_t op2, const OfExPacket& in,
                        const ScratchFile& scratch) {
    switch (select) {
        case B_IMM:     return static_cast<uint32_t>(in.imm);
        case B_CONST:   return static_cast<uint32_t>(in.control.microImm);
        case B_SCRATCH: return scratch.Read(in.control.src2ScratchAddr);
        case B_OP2:
        default:        return op2;
    }
}

}  // namespace

ExResult StageEX::Run(const PipelineRegister<OfExPacket>& ofEx,
                      PipelineRegister<ExMemPacket>& exMem,
                      const WbResult& wb, ScratchFile& scratch, Alu& alu) {
    ExResult result;
    const OfExPacket& in = ofEx.out;
    exMem.in = ExMemPacket{};

    if (!in.valid) {
        return result;
    }

    ExMemPacket& out = exMem.in;
    out.valid = true;
    out.pc = in.pc;
    out.instruction = in.instruction;
    out.rd = in.rd;
    out.control = in.control;

    // Operand forwarding feeds the register-operand inputs of the AluSrc muxes.
    const uint32_t op1 = (in.control.aluSrcA == A_OP1)
                             ? forwardValue(in.op1, in.rs1, exMem.out, wb)
                             : in.op1;
    uint32_t op2 = in.op2;
    if (in.control.isSt) {
        op2 = forwardValue(in.op2, in.rd, exMem.out, wb);      // store data register
    } else if (in.control.aluSrcB == B_OP2) {
        op2 = forwardValue(in.op2, in.rs2, exMem.out, wb);
    }

    const uint32_t a = selectOperandA(in.control.aluSrcA, op1, in, scratch);
    const uint32_t b = selectOperandB(in.control.aluSrcB, op2, in, scratch);
    out.aluResult = alu.Execute(in.control.aluOp, a, b);
    out.op2 = op2;   // store data continues to MARW

    // Condition codes travel with the packet so the Exception Unit can save them.
    out.flagE = alu.FlagE();
    out.flagGt = alu.FlagGt();
    out.flagLt = alu.FlagLt();

    // Exception Unit marking (EX stage): divide by zero. A mark already set in
    // OF keeps priority (this instruction is older than anything marked later).
    out.isException = in.isException;
    out.cause = in.cause;
    if (!out.isException && alu.DivideByZero()) {
        out.isException = true;
        out.cause = EXC_DIV0;
    }

    // The Scratch File is written in the EX stage.
    if (in.control.isScratchWb) {
        scratch.Write(in.control.scrWriteAddr, out.aluResult);
    }

    // Correct PC recovery unit: the corrected branch target is computed here,
    // and only matters when the prediction was wrong.
    const bool conditional = in.control.isBeq || in.control.isBgt;
    const bool actualTaken = in.control.isUbranch ||
                             (in.control.isBeq && alu.FlagE()) ||
                             (in.control.isBgt && alu.FlagGt());
    const uint32_t target =
        in.pc + static_cast<uint32_t>(signExtend25(in.instruction) * 4);

    result.mispredict = actualTaken != in.predictTaken;
    result.correctPC = actualTaken ? target : (in.pc + 4);
    result.halt = actualTaken && (in.control.isUbranch || conditional) && target == in.pc;
    out.mispredict = result.mispredict;
    return result;
}

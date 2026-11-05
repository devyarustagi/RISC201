#include "stages/stage_if.hpp"

#include "components/memory.hpp"

namespace {

// Sign-extends the low 25 bits of `value` (the branch offset field).
int32_t signExtend25(uint32_t value) {
    const uint32_t signBit = 1u << 24;
    return static_cast<int32_t>(((value & 0x1FFFFFFu) ^ signBit) - signBit);
}

}  // namespace

void StageIF::Reset() {
    pc_ = 0;
}

void StageIF::Run(PipelineRegister<IfIbuPacket>& ifIbu, const Memory& memory,
                  const IfControl& ctl) {
    // Main PC mux, highest priority first.
    if (ctl.exception) {
        pc_ = ctl.exceptionPC;
        ifIbu.in = IfIbuPacket{};   // flush IF/IBU
        return;
    }

    if (ctl.mispredict) {
        pc_ = ctl.correctPC;
        ifIbu.in = IfIbuPacket{};   // flush IF/IBU
        return;
    }

    if (ctl.isRetz) {
        pc_ = ctl.retzPC;
        ifIbu.in = IfIbuPacket{};   // one-stage flush
        return;
    }

    if (ctl.isRet) {
        pc_ = ctl.rasOut;
        ifIbu.in = IfIbuPacket{};   // one-stage flush
        return;
    }

    if (ctl.stall) {
        // Freeze: keep the instruction already sitting in the IBU stage.
        ifIbu.in = ifIbu.out;
        return;
    }

    const uint32_t instruction = memory.FetchInstruction(pc_);
    const uint8_t opcode = static_cast<uint8_t>((instruction >> 26) & 0x3F);
    const bool isBranchOpcode = opcode >= MacroOp::B && opcode <= MacroOp::CALL;
    const bool predictTaken = isBranchOpcode && ((instruction >> 25) & 0x1u) != 0;

    ifIbu.in.valid = true;
    ifIbu.in.pc = pc_;
    ifIbu.in.instruction = instruction;
    ifIbu.in.predictTaken = predictTaken;
    // Asynchronous exception: mark the instruction being fetched.
    ifIbu.in.isException = ctl.interrupt;
    ifIbu.in.cause = ctl.interrupt ? ctl.interruptCause : 0;

    // IF branch target sub-mux: NextPC = PC + (offset25 << 2), else PC + 4.
    if (predictTaken) {
        pc_ = pc_ + static_cast<uint32_t>(signExtend25(instruction) * 4);
    } else {
        pc_ += 4;
    }
}

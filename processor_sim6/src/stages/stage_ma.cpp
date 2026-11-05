#include "stages/stage_ma.hpp"

#include "components/memory.hpp"

void StageMA::Run(const PipelineRegister<ExMaPacket>& exMa,
                  PipelineRegister<MaRwPacket>& maRw, Memory& memory) {
    const ExMaPacket& in = exMa.out;
    maRw.in = MaRwPacket{};
    if (!in.valid) {
        return;
    }

    MaRwPacket& out = maRw.in;
    static_cast<PacketBase&>(out) = in;
    out.halt = in.halt;
    out.aluResult = in.aluResult;
    out.op2 = in.op2;
    out.rd = in.rd;
    out.flagE = in.flagE;
    out.flagGt = in.flagGt;
    out.flagLt = in.flagLt;
    out.control = in.control;
    out.rasBefore = in.rasBefore;

    if (in.isException) {
        return;   // drains to RW without touching memory
    }
    if (in.control.isLd) {
        out.ldResult = memory.ReadData(in.aluResult);
    }
    if (in.control.isSt) {
        memory.WriteData(in.aluResult, in.op2);
        out.stored = true;
    }
    if (in.mispredict) {
        memory.WriteInstruction(in.pc, in.instruction ^ (1u << 25));
    }
}

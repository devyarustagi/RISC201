#include "stages/stage_marw.hpp"

#include <cstdint>

#include "components/memory.hpp"
#include "components/reg_file.hpp"
#include "components/special_regs.hpp"

namespace {

// Writes one entry of the privileged register window (movz destination).
void writeWindow(uint8_t index, uint32_t value, RegFile& gpr, SpecialRegs& special) {
    switch (index) {
        case WIN_R0:  break;   // r0 is hardwired to zero: write discarded
        case WIN_SP:  gpr.Write(GPR_SP, value); break;
        case WIN_OLDPC: special.SetOldPC(value); break;
        case WIN_OLDSP: special.SetOldSP(value); break;
        case WIN_OLDFLAGS:
            special.SetOldFlags((value & 0x1u) != 0, (value & 0x2u) != 0, (value & 0x4u) != 0);
            break;
        case WIN_EBASE: special.SetEBase(value); break;
        case WIN_ECODE: special.SetECode(static_cast<uint8_t>(value)); break;
        default: break;
    }
}

}  // namespace

MarwResult StageMARW::Run(const PipelineRegister<ExMemPacket>& exMem, RegFile& gpr,
                          Memory& memory, SpecialRegs& special) {
    MarwResult result;
    const ExMemPacket& in = exMem.out;
    if (!in.valid) {
        return result;
    }

    // A marked instruction drains through MA-RW without committing. The
    // Exception Unit extracts the captured condition codes from the packet.
    if (in.isException) {
        result.exception = true;
        result.pc = in.pc;
        result.cause = in.cause;
        result.flagE = in.flagE;
        result.flagGt = in.flagGt;
        result.flagLt = in.flagLt;
        return result;
    }

    uint32_t loadResult = 0;
    if (in.control.isLd) {
        loadResult = memory.ReadData(in.aluResult);
    }
    if (in.control.isSt) {
        memory.WriteData(in.aluResult, in.op2);   // mdr = op2
    }

    // 2-to-1 writeback mux, driven solely by isLd: LdResult or AluResult.
    const uint32_t writeValue = in.control.isLd ? loadResult : in.aluResult;

    if (in.control.isMovz) {
        // Privileged move: the destination is a register-window entry.
        writeWindow(in.control.movzDst, in.aluResult, gpr, special);
        if (in.control.movzDst == WIN_SP) {
            // Publish the sp write so younger instructions can forward it.
            result.wb.value = in.aluResult;
            result.wb.dest = GPR_SP;
            result.wb.wb = true;
        }
    } else if (in.control.isWb) {
        const uint8_t dest = in.rd;
        gpr.Write(dest, writeValue);
        result.wb.value = writeValue;
        result.wb.dest = dest;
        result.wb.wb = dest != 0;   // r0 is hardwired, never a forwarding source
    }

    // In-memory predictor inversion: flip the branch's prediction bit.
    if (in.mispredict) {
        memory.WriteInstruction(in.pc, in.instruction ^ (1u << 25));
    }

    return result;
}

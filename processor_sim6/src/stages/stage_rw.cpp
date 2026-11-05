#include "stages/stage_rw.hpp"

#include "components/reg_file.hpp"
#include "components/special_regs.hpp"

namespace {

// Writes one entry of the privileged register window (movz destination).
void writeWindow(uint8_t index, uint32_t value, RegFile& gpr, SpecialRegs& special) {
    switch (index) {
        case WIN_R0:  break;
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

RwResult StageRW::Run(const PipelineRegister<MaRwPacket>& maRw, RegFile& gpr,
                      SpecialRegs& special) {
    RwResult result;
    const MaRwPacket& in = maRw.out;
    if (!in.valid) {
        return result;
    }

    if (in.isException) {
        result.exception = true;
        result.pc = in.pc;
        result.cause = in.cause;
        result.flagE = in.flagE;
        result.flagGt = in.flagGt;
        result.flagLt = in.flagLt;
        return result;
    }

    result.halt = in.halt;
    const uint32_t value = in.control.isLd ? in.ldResult : in.aluResult;
    if (in.control.isMovz) {
        writeWindow(in.control.movzDst, in.aluResult, gpr, special);
        if (in.control.movzDst == WIN_SP) {
            result.wb = WbResult{in.aluResult, GPR_SP, true};
        }
    } else if (in.control.isWb) {
        gpr.Write(in.rd, value);
        result.wb = WbResult{value, in.rd, in.rd != 0};
    }
    return result;
}

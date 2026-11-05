#include "stages/stage_rf.hpp"

#include "components/reg_file.hpp"
#include "components/special_regs.hpp"

namespace {

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

}  // namespace

void StageRF::Run(const PipelineRegister<IbuRfPacket>& ibuRf,
                  PipelineRegister<RfExPacket>& rfEx,
                  const RegFile& gpr, const SpecialRegs& special) {
    const IbuRfPacket& in = ibuRf.out;
    rfEx.in = RfExPacket{};
    if (!in.valid) {
        return;
    }

    RfExPacket& out = rfEx.in;
    static_cast<PacketBase&>(out) = in;
    out.imm = in.imm;
    out.rd = in.rd;
    out.control = in.control;
    out.rasBefore = in.rasBefore;
    out.rasAfter = in.rasAfter;

    out.op1 = gpr.Read(in.rs1);
    if (in.control.isMovz) {
        out.op2 = readWindow(in.windowSrc, gpr, special);
        // Forwarding sees only the GPR half of the window (r0 / sp).
        out.rs1 = 0;
        out.rs2 = (in.windowSrc == WIN_SP) ? GPR_SP : 0;
    } else {
        out.op2 = in.control.isSt ? gpr.Read(in.rd) : gpr.Read(in.rs2);
        out.rs1 = in.rs1;
        out.rs2 = in.rs2;
    }
}

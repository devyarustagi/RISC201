#ifndef PROCESSOR_SIM6_STAGE_RW_HPP
#define PROCESSOR_SIM6_STAGE_RW_HPP

#include <cstdint>

#include "common/pipeline_registers.hpp"

class RegFile;
class SpecialRegs;

// The value RW writes back this cycle, published for the Forwarding Unit.
struct WbResult {
    uint32_t value = 0;
    uint8_t dest = 0;
    bool wb = false;   // a non-zero GPR was written
};

// What RW reports to the Exception Unit / CPU.
struct RwResult {
    WbResult wb;
    bool halt = false;        // the halting branch (branch to itself) committed
    bool exception = false;   // a marked instruction reached RW
    uint32_t pc = 0;          // -> oldPC
    uint8_t cause = 0;        // -> eCode
    bool flagE = false;       // -> oldFlags
    bool flagGt = false;
    bool flagLt = false;
};

/**
 * @brief Register Writeback stage (last stage of 6StagePipeline.svg).
 *
 * The writeback mux picks LdResult when isLd, else AluResult, and writes GPR[rd]
 * (or a register-window entry for `movz`). This is the commit point: a marked
 * instruction is reported here instead of committing, and the CPU takes the
 * exception.
 */
class StageRW {
public:
    RwResult Run(const PipelineRegister<MaRwPacket>& maRw, RegFile& gpr,
                 SpecialRegs& special);
};

#endif

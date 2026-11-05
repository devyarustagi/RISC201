#ifndef PROCESSOR_SIM_STAGE_MARW_HPP
#define PROCESSOR_SIM_STAGE_MARW_HPP

#include <cstdint>

#include "common/pipeline_registers.hpp"

class Memory;
class RegFile;
class SpecialRegs;

// The writeback value the memory stage commits, published so the EX stage's
// forwarding unit can forward it into a younger instruction.
struct WbResult
{
    uint32_t value = 0;
    uint8_t dest = 0;
    bool wb = false; // true when a non-zero GPR was written this cycle
};

// What the memory stage reports back to the Exception Unit / CPU.
struct MarwResult
{
    WbResult wb;
    bool exception = false; // a marked instruction reached MA-RW
    uint32_t pc = 0;        // faulting instruction's PC (-> oldPC)
    uint8_t cause = 0;      // ExcCode (-> eCode)
    bool flagE = false;     // captured condition codes (-> oldFlags)
    bool flagGt = false;
    bool flagLt = false;
};

/**
 * Memory and Register Writeback stage.
 *
 * Performs the data-memory access (mar = ALU result, mdr = op2) and the 2-to-1
 * writeback mux (LdResult when isLd, otherwise AluResult), commits the result to
 * the GPR selected by rd (or to a special register for a privileged `movz`), and
 * runs the in-memory predictor inversion unit.
 *
 * If the packet is marked, the instruction is drained but its `isWb` / `isLd` /
 * `isSt` side effects are suppressed and the exception context is reported to
 * the CPU.
 */
class StageMARW
{
public:
    /**
     * Runs one cycle of the memory / writeback stage.
     *
     * exMem    the EX/MARW register (reads `out`)
     * gpr      the register file (destination of a GPR writeback)
     * memory   the data / instruction memory
     * special  the special registers (movz destination window)
     */
    MarwResult Run(const PipelineRegister<ExMemPacket> &exMem, RegFile &gpr,
                   Memory &memory, SpecialRegs &special);
};

#endif

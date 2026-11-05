#ifndef PROCESSOR_SIM_STAGE_EX_HPP
#define PROCESSOR_SIM_STAGE_EX_HPP

#include <cstdint>

#include "common/pipeline_registers.hpp"
#include "stages/stage_rw.hpp"

class Alu;
class ScratchFile;

// Wires produced by the execute stage and consumed by the CPU / fetch stage.
struct ExResult {
    bool mispredict = false;    // ActualTaken XOR PredictTaken
    uint32_t correctPC = 0;     // branch target or PC + 4
    bool halt = false;          // branch to self (conventional halt idiom)
};

/**
 * @brief Execute stage.
 *
 * Selects the ALU operands through the AluSrcA / AluSrcB multiplexers, applying
 * operand forwarding from the MA and RW stages first, runs the ALU, writes the
 * Scratch File, resolves the branch (`ActualTaken`) against the prediction bit,
 * and generates the recovery PC.
 */
class StageEX {
public:
    /**
     * @brief Runs one cycle of the execute stage.
     *
     * @param rfEx     the RF/EX register (reads `out`)
     * @param exMa    the EX/MA register (the MA packet, forwarding source)
     * @param wb       the writeback value produced by MA this cycle
     * @param scratch  the scratch register file (written here)
     * @param alu      the ALU (updates flags)
     * @return the misprediction / recovery wires driven this cycle
     */
    ExResult Run(const PipelineRegister<RfExPacket>& rfEx,
                 PipelineRegister<ExMaPacket>& exMa,
                 const WbResult& wb, ScratchFile& scratch, Alu& alu);
};

#endif

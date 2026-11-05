#ifndef PROCESSOR_SIM6_STAGE_MA_HPP
#define PROCESSOR_SIM6_STAGE_MA_HPP

#include "common/pipeline_registers.hpp"

class Memory;

/**
 * @brief Memory Access stage (fifth stage of 6StagePipeline.svg).
 *
 * The Memory Unit loads (mar = AluResult) or stores (mdr = op2) data memory, and
 * the predictor inversion writes `Inst ^ (1 << 25)` back to instruction memory
 * for a mispredicted branch. A marked instruction passes through with no
 * memory side effects; its exception is taken in RW.
 */
class StageMA {
public:
    void Run(const PipelineRegister<ExMaPacket>& exMa,
             PipelineRegister<MaRwPacket>& maRw, Memory& memory);
};

#endif

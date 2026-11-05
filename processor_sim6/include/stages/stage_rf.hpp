#ifndef PROCESSOR_SIM6_STAGE_RF_HPP
#define PROCESSOR_SIM6_STAGE_RF_HPP

#include "common/pipeline_registers.hpp"

class RegFile;
class SpecialRegs;

/**
 * @brief Register Fetch stage (third stage of 6StagePipeline.svg).
 *
 * Reads the Register File: op1 = GPR[rs1]; op2 = GPR[rd] for a store (the isSt
 * mux in front of the second read port), GPR[rs2] otherwise. A privileged
 * `movz` reads its register-window source (oldPC, eBase, ...) instead.
 *
 * The RW stage writes the Register File earlier in the same cycle, so an
 * instruction three stages ahead is already visible here; younger results are
 * forwarded in EX.
 */
class StageRF {
public:
    void Run(const PipelineRegister<IbuRfPacket>& ibuRf,
             PipelineRegister<RfExPacket>& rfEx,
             const RegFile& gpr, const SpecialRegs& special);
};

#endif

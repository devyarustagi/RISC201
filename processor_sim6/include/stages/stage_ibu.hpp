#ifndef PROCESSOR_SIM6_STAGE_IBU_HPP
#define PROCESSOR_SIM6_STAGE_IBU_HPP

#include <cstdint>

#include "common/micro_types.hpp"
#include "common/pipeline_registers.hpp"
#include "components/ras.hpp"

class ControlUnit;
class SpecialRegs;

// Wires the IBU stage drives into the IF main-PC mux.
struct IbuResult {
    bool valid = false;
    ControlSignals control;
    bool isRet = false;   // PC <- RAS Out
    bool isRetz = false;  // PC <- oldPC (privileged return)
    uint32_t rasOut = 0;  // RAS top before this instruction's pop
};

/**
 * @brief Instruction / Branch Unit stage (second stage of 6StagePipeline.svg).
 *
 * Decodes the macroinstruction (rd, rs1, rs2, the immediate through the
 * Immediate Unit), drives the microcoded Control Unit and the RAS (call pushes
 * PC + 4, ret pops), and is the Exception Unit's first synchronous marking
 * point: wrongInst, lowPriv, pbrk, scall and a call that overflows the RAS.
 *
 * The register file is not read here; that is the RF stage.
 */
class StageIBU {
private:
    Ras ras_;

public:
    void Reset() { ras_.Reset(); }

    /**
     * @param ifIbu    IF/IBU register (reads `out`)
     * @param ibuRf    IBU/RF register (writes `in`)
     * @param control  microcoded control unit
     * @param special  special registers (CPL for lowPriv)
     */
    IbuResult Run(const PipelineRegister<IfIbuPacket>& ifIbu,
                  PipelineRegister<IbuRfPacket>& ibuRf,
                  ControlUnit& control, const SpecialRegs& special);

    /** @brief Restores the RAS after a flush (Branch Lock Unit / exception). */
    void RestoreRas(const Ras& saved) { ras_ = saved; }

    const Ras& RasUnit() const { return ras_; }
};

#endif

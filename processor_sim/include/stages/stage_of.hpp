#ifndef PROCESSOR_SIM_STAGE_OF_HPP
#define PROCESSOR_SIM_STAGE_OF_HPP

#include <cstdint>

#include "common/micro_types.hpp"
#include "common/pipeline_registers.hpp"
#include "components/ras.hpp"

class RegFile;
class ControlUnit;
class SpecialRegs;

// Structure of OF/EX Intermediate register.
struct OfResult
{
    ControlSignals control;
    uint8_t opcode = 0; // decoded macro opcode (0 = nop)
    uint8_t rs1 = 0;    // source registers, kept for the load-use interlock
    uint8_t rs2 = 0;
    uint8_t rd = 0;      // destination (also the store's data register)
    bool isRet = false;  // drives the IF main-PC mux (RAS Out)
    bool isRetz = false; // privileged retz -> PC <- oldPC
    uint32_t rasOut = 0; // current RAS top
};

/**
 * Operand Fetch stage.
 *
 * Contains the Immediate and Branch Unit (decodes the macroinstruction into
 * rs1/rs2/rd, the sign-extended immediate and the branch target), hosts the
 * microcoded Control Unit and the RAS, reads the source registers and asks
 * the Control Unit for the control signals of the current microinstruction.
 *
 * It is also the Exception Unit's first marking point: the decoded opcode is
 * checked for `wrongInst`, `lowPriv`, `pbrk`, `scall`, and a `call` that
 * overflows the RAS. Calls push and rets pop the RAS; both enables are gated by
 * the Branch Lock Unit (`branchLock`).
 */
class StageOF
{
private:
    Ras ras_;

public:
    /** Resets the RAS. */
    void Reset();

    /**
     * Runs one cycle of the OF stage.
     *
     * ifOf        the IF/OF register (reads `out`)
     * ofEx        the OF/EX register (writes `in`)
     * gpr         the register file
     * control     the microcoded control unit
     * special     the special registers (CPL, window sources)
     * branchLock  Branch Lock Unit flush (a misprediction in EX); gates call/ret
     * outputs:    The control word, source registers, ret flags and RAS output
     */
    OfResult Run(const PipelineRegister<IfOfPacket> &ifOf,
                 PipelineRegister<OfExPacket> &ofEx,
                 const RegFile &gpr, ControlUnit &control,
                 const SpecialRegs &special, bool branchLock);

    // RAS helper functions
    void Latch();
    const Ras &RasUnit() const { return ras_; } // for testing
};

#endif

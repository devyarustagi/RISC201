#ifndef PROCESSOR_SIM_STAGE_IF_HPP
#define PROCESSOR_SIM_STAGE_IF_HPP

#include <cstdint>

#include "common/pipeline_registers.hpp"

class Memory;

// Everything that steers the program counter and the IF/OF flush this cycle.
// The main-PC mux priority is exception > mispredict > retz > isRet > stall.
struct IfControl {
    bool stall = false;
    bool mispredict = false;
    uint32_t correctPC = 0;      // EX recovery target
    bool isRet = false;          // ret from ID/OF
    uint32_t rasOut = 0;         // RAS top
    bool isRetz = false;         // retz from ID/OF
    uint32_t retzPC = 0;         // oldPC
    bool exception = false;      // a marked instruction committed: load eBase
    uint32_t exceptionPC = 0;    // eBase
    bool interrupt = false;      // asynchronous interrupt: mark the fetched instruction
    uint8_t interruptCause = 0;  // ExcCode for the interrupt
};

/**
 * @brief Instruction Fetch stage.
 *
 * Owns the program counter, the speculative branch-target sub-mux and the main
 * PC multiplexer. The PC source priority is:
 *   1. `eBase` on an exception commit;
 *   2. `CorrectPC` from the EX recovery unit, selected by `mispredict`;
 *   3. `oldPC` from retz;
 *   4. `RAS Out` from the ID/OF RAS unit, selected by `isRet`;
 *   5. `NextPC` (predict-taken target or PC + 4) otherwise.
 *
 * Flushes: an exception / mispredict clears IF/OF; `isRet` and `retz` perform a
 * one-stage flush. A stall freezes the PC and the IF/OF register. An
 * asynchronous interrupt marks the instruction fetched this cycle.
 */
class StageIF {
private:
    uint32_t pc_ = 0;

public:
    /** @brief Resets the program counter to 0. */
    void Reset();

    /**
     * @brief Runs one cycle of the fetch stage.
     *
     * @param ifOf     the IF/OF pipeline register (writes its `in`)
     * @param memory   instruction memory
     * @param ctl      the PC-source / flush / interrupt control wires
     */
    void Run(PipelineRegister<IfOfPacket>& ifOf, const Memory& memory,
             const IfControl& ctl);

    uint32_t Pc() const { return pc_; }
};

#endif

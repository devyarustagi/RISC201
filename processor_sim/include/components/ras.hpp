#ifndef PROCESSOR_SIM_RAS_HPP
#define PROCESSOR_SIM_RAS_HPP

#include <array>
#include <cstdint>

#include "common/micro_types.hpp"

/**
 * Return-Address Stack (RAS) unit of the OF stage.
 *
 * A simple LIFO hardware array with a top-of-stack (TOS) pointer. A `call`
 * pushes the return address (PC + 4); a `ret` pops it. Both operations are
 * gated by the Branch Lock Unit, so speculative wrong-path `call`/`ret`
 * instructions cannot disturb the stack.
 * Like the other state-holding elements the stack is edge-triggered
 */
class Ras
{
private:
    std::array<uint32_t, RAS_DEPTH> stack_{};
    int tos_ = 0; // number of entries; top entry is stack_[tos_ - 1]
    int tosNext_ = 0;
    uint32_t pushValue_ = 0;
    bool pushPending_ = false;
    bool overflowPending_ = false;

public:
    Ras();
    uint32_t Out() const { return tos_ > 0 ? stack_[tos_ - 1] : 0; }
    void Run(bool isCall, bool isRet, bool branchLock, uint32_t pushValue);
    void Latch();
    void Reset();
    int Tos() const { return tos_; }
    int Depth() const { return RAS_DEPTH; }
    bool OverflowPending() const { return overflowPending_; }
};

/**
 * Run function:
 * Evaluates the TOS update for this cycle.
 *
 *  isCall      push PC + 4 (from pushValue)
 *  isRet       pop
 *  branchLock  when set, both enables are forced to 0 (misprediction)
 *  pushValue   the return address pushed by a call (PC + 4)
 */
#endif

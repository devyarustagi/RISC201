#ifndef PROCESSOR_SIM_ALU_HPP
#define PROCESSOR_SIM_ALU_HPP

#include <cstdint>

#include "common/micro_types.hpp"

namespace AluUnits {
struct CompareResult;
}  // namespace AluUnits

/**
 * @brief Arithmetic Logic Unit.
 *
 * Executes one microinstruction's operation per cycle and latches the
 * comparison flags (E / GT / LT) used by the conditional branches. Operand
 * selection happens in the EX stage through the AluSrcA / AluSrcB multiplexers.
 *
 * Every operation runs through an algorithm-level model of its hardware unit
 * (see components/alu_units.hpp): Kogge-Stone adder, radix-4 Booth multiplier
 * with a Dadda tree, non-restoring divider (div and mod), barrel
 * shifter, and an IEEE-754 single-precision FPU that reuses those units.
 */
class Alu {
private:
    bool flagE_ = false;
    bool flagGt_ = false;
    bool flagLt_ = false;

    bool divideByZero_ = false;   // last operation was div by zero
    int lastLatency_ = 1;         // EX cycles the last operation takes in hardware

    void setFlags(const AluUnits::CompareResult& compare);

public:
    /**
     * @brief Executes @p op on @p a and @p b and returns the 32-bit result.
     *
     * CMP and FCMP update the flags (CMP returns a − b, FCMP returns 0).
     * DIV returns the quotient and MOD the remainder; with a zero divisor both
     * return 0 and set DivideByZero(). ITOF and FTOI use only @p a.
     *
     * @param op  ALU operation from the control word
     * @param a   A operand (after the AluSrcA mux)
     * @param b   B operand (after the AluSrcB mux)
     * @return the 32-bit result (IEEE-754 bits for FP operations)
     */
    uint32_t Execute(AluOp op, uint32_t a, uint32_t b);

    bool FlagE() const { return flagE_; }
    bool FlagGt() const { return flagGt_; }
    bool FlagLt() const { return flagLt_; }

    /** @brief True if the last Execute() was a div or mod by zero. */
    bool DivideByZero() const { return divideByZero_; }

    /**
     * @brief EX-stage cycles the last operation takes in the hardware design
     *        (mul 2, div/mod 32, fadd/fsub/fmul 2, fdiv 26, others 1).
     *
     * Informational: the pipeline does not stall on it yet. A hazard unit can
     * hold EX for LastLatency() − 1 cycles (the aluBusy signal).
     */
    int LastLatency() const { return lastLatency_; }

    /** @brief Clears all flags and status. */
    void Reset();
};

#endif

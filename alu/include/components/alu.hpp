#ifndef PROCESSOR_SIM_ALU_HPP
#define PROCESSOR_SIM_ALU_HPP

#include <cstdint>

#include "common/micro_types.hpp"

namespace AluUnits {
struct CompareResult;
enum class ReductionTree : uint8_t;
}  // namespace AluUnits

/**
 * @brief Arithmetic Logic Unit.
 *
 * Executes one microinstruction's operation per cycle and latches the
 * comparison flags (E / GT / LT) used by the conditional branches. The ALU is
 * a pure integer computation; operand selection happens in the EX stage
 * through the AluSrcA / AluSrcB multiplexers.
 *
 * Every operation runs through an algorithm-level model of its hardware unit
 * (see components/alu_units.hpp): Kogge-Stone adder, radix-4 Booth multiplier
 * with a Dadda (or Wallace) tree, non-restoring divider and barrel shifter.
 * Floating-point operations are not handled by this ALU: they return 0 and
 * leave the flags unchanged.
 */
class Alu {
private:
    bool flagE_ = false;
    bool flagGt_ = false;
    bool flagLt_ = false;

    bool divideByZero_ = false;   // last operation was div by zero
    int lastLatency_ = 1;         // EX cycles the last operation takes in hardware
    AluUnits::ReductionTree tree_{};  // Dadda by default

    void setFlags(const AluUnits::CompareResult& compare);

public:
    /**
     * @brief Executes @p op on @p a and @p b and returns the 32-bit result.
     *
     * CMP updates the flags and returns a − b. FP operations return 0.
     * DIV by zero returns 0 and sets DivideByZero().
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

    /** @brief True if the last Execute() was an integer division by zero. */
    bool DivideByZero() const { return divideByZero_; }

    /**
     * @brief EX-stage cycles the last operation takes in the hardware design
     *        (mul 2, div 32, others 1).
     *
     * Informational: the pipeline does not stall on it yet. A hazard unit can
     * hold EX for LastLatency() − 1 cycles (the aluBusy signal).
     */
    int LastLatency() const { return lastLatency_; }

    /** @brief Selects the multiplier's reduction tree (Dadda by default). */
    void SetReductionTree(AluUnits::ReductionTree tree) { tree_ = tree; }

    /** @brief Clears all flags and status. */
    void Reset();
};

#endif

#ifndef PROCESSOR_SIM_ALU_UNITS_HPP
#define PROCESSOR_SIM_ALU_UNITS_HPP

#include <cstdint>

/**
 * @file alu_units.hpp
 * @brief Algorithm-level models of the ALU's functional units.
 *
 * Each function computes its result the way the hardware block does, so the
 * simulator exercises the same algorithms as the design diagrams:
 *
 * | Unit               | Algorithm                                   |
 * | :----------------- | :------------------------------------------ |
 * | Adder / subtractor | Kogge-Stone parallel-prefix adder           |
 * | Multiplier         | Radix-4 Booth recoding + Dadda/Wallace tree |
 * | Divider            | Non-restoring division                      |
 * | Shifter            | Logarithmic barrel shifter                  |
 *
 * All operands and results are raw 32-bit words.
 */
namespace AluUnits {

/** @brief Output of the Kogge-Stone adder. */
struct AddResult {
    uint32_t sum = 0;
    bool carryOut = false;   ///< c(n): carry out of bit 31
    bool overflow = false;   ///< V = c(n) XOR c(n-1): signed overflow
};

/** @brief Comparison outcome, as written to the flags register. */
struct CompareResult {
    bool equal = false;
    bool greater = false;
    bool less = false;
};

/** @brief Output of the divider. */
struct DivideResult {
    uint32_t quotient = 0;
    uint32_t remainder = 0;
    bool divideByZero = false;
};

/** @brief Carry-save reduction tree used by the multiplier. */
enum class ReductionTree : uint8_t { Dadda = 0, Wallace = 1 };

/** @brief Shift performed by the barrel shifter. */
enum class ShiftKind : uint8_t { Lsl, Lsr, Asr };

/** @brief Hardware cost of a reduction tree, counted per bit column. */
struct TreeCost {
    int levels = 0;
    int fullAdders = 0;
    int halfAdders = 0;
};

// ---------------------------------------------------------------------------
// Integer units
// ---------------------------------------------------------------------------

/**
 * @brief Kogge-Stone parallel-prefix adder.
 *
 * Computes generate/propagate per bit, folds the carry-in into bit 0, then
 * combines groups over spans 1, 2, 4, 8, 16 so every carry is known after five
 * levels.
 *
 * @param a        first operand
 * @param b        second operand
 * @param carryIn  carry into bit 0
 * @return the sum, carry out and signed-overflow flag
 */
AddResult KoggeStoneAdd(uint32_t a, uint32_t b, bool carryIn);

/**
 * @brief Adder/subtractor: `a + b`, or `a + ~b + 1` when @p subtract is set.
 *
 * @param a         first operand
 * @param b         second operand
 * @param subtract  true for subtraction (isSub | isCmp)
 * @return the Kogge-Stone result
 */
AddResult AddSub(uint32_t a, uint32_t b, bool subtract);

/**
 * @brief Signed integer comparison from the subtractor's output.
 *
 * E = NOR of all difference bits, GT = ¬E · ¬(N ⊕ V), LT = ¬E · ¬GT.
 *
 * @param a  first operand
 * @param b  second operand
 * @return equal / greater / less, treating both operands as signed
 */
CompareResult CompareSigned(uint32_t a, uint32_t b);

/**
 * @brief Radix-4 Booth multiplier with a carry-save reduction tree.
 *
 * Recodes @p b into 16 digits in {-2..+2} (with b(-1) = 0), builds the partial
 * products 0 / ±A / ±2A shifted by 2j, reduces them to two rows with the chosen
 * tree and adds those with the Kogge-Stone adder. Only the low 32 bits are
 * produced, so overflow wraps (the ISA has no overflow flag).
 *
 * @param a     multiplicand
 * @param b     multiplier
 * @param tree  Dadda or Wallace reduction
 * @return the low 32 bits of a × b
 */
uint32_t BoothMultiply(uint32_t a, uint32_t b, ReductionTree tree);

/**
 * @brief Counts the full and half adders a reduction tree needs for the
 *        32-column Booth product (used for the Dadda vs Wallace evaluation).
 *
 * @param tree  Dadda or Wallace reduction
 * @return levels, full adders and half adders
 */
TreeCost CountTreeHardware(ReductionTree tree);

/**
 * @brief Signed non-restoring divider.
 *
 * Works on |a| and |b| for 32 steps (shift {R, Q}; R ≥ 0 → R − D, else
 * R + D; quotient bit = ¬sign(R)), corrects the remainder once, then applies
 * the signs. INT_MIN / −1 wraps to INT_MIN.
 *
 * @param a  dividend
 * @param b  divisor
 * @return quotient, remainder and the divide-by-zero flag (results 0 when set)
 */
DivideResult NonRestoringDivide(uint32_t a, uint32_t b);

/**
 * @brief Logarithmic barrel shifter.
 *
 * Five layers of 2:1 muxes shift right by 1, 2, 4, 8, 16 (one bit of the
 * shift amount each); vacated bits get `isAsr · a31`. A left shift reverses
 * the bits before and after the same right shift.
 *
 * @param a       value to shift
 * @param amount  shift amount (only bits [4:0] are used)
 * @param kind    lsl, lsr or asr
 * @return the shifted value
 */
uint32_t BarrelShift(uint32_t a, uint32_t amount, ShiftKind kind);

}  // namespace AluUnits

#endif

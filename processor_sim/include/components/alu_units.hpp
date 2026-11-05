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
 * | Multiplier         | Radix-4 Booth recoding + Dadda tree         |
 * | Divider            | Non-restoring division (div and mod)        |
 * | Shifter            | Logarithmic barrel shifter                  |
 * | FPU                | IEEE-754 single precision, reusing the four |
 * |                    | integer units for the significand work      |
 *
 * All operands and results are raw 32-bit words; floating-point values are
 * their IEEE-754 bit patterns.
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

/** @brief Shift performed by the barrel shifter. */
enum class ShiftKind : uint8_t { Lsl, Lsr, Asr };

/** @brief Hardware cost of the Dadda tree, counted per bit column. */
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
 * @brief Radix-4 Booth multiplier with a Dadda tree.
 *
 * Recodes @p b into 16 digits in {-2..+2} (with b(-1) = 0), builds the partial
 * products 0 / ±A / ±2A shifted by 2j, reduces them to two rows with the Dadda
 * tree (16 → 13 → 9 → 6 → 4 → 3 → 2) and adds those with the Kogge-Stone adder. Only the low 32 bits are
 * produced, so overflow wraps (the ISA has no overflow flag).
 *
 * @param a     multiplicand
 * @param b     multiplier
 * @return the low 32 bits of a × b
 */
uint32_t BoothMultiply(uint32_t a, uint32_t b);

/**
 * @brief Full 64-bit signed product from the same Booth + Dadda datapath.
 *
 * BoothMultiply() keeps only the low 32 bits of this. The FPU uses it for the
 * 24 × 24-bit significand product (48 bits).
 *
 * @param a     multiplicand (signed 32-bit)
 * @param b     multiplier (signed 32-bit)
 * @return the exact 64-bit product a × b (two's complement)
 */
uint64_t BoothMultiplyWide(uint32_t a, uint32_t b);

/**
 * @brief Counts the levels, full adders and half adders the Dadda tree needs
 *        for the 32-column Booth product.
 *
 * @return levels, full adders and half adders
 */
TreeCost CountDaddaHardware();

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

// ---------------------------------------------------------------------------
// Floating-point unit (IEEE-754 single precision, round to nearest even)
// ---------------------------------------------------------------------------
// Every FP operation follows the FPU diagrams: unpack -> unit -> normalise ->
// round -> pack, with a separate special-case block (NaN, infinity, zero)
// whose result the output mux picks instead (isSpecial).

/**
 * @brief FP add / subtract.
 *
 * Flips the sign of B for fsub, swaps so X is the larger magnitude, aligns Y
 * by the exponent difference (lost bits OR-ed into sticky), adds or subtracts
 * the significands on the Kogge-Stone adder, then normalises, rounds and packs.
 *
 * @param a         first operand (IEEE-754 bits)
 * @param b         second operand (IEEE-754 bits)
 * @param subtract  true for fsub
 * @return the IEEE-754 result (∞ − ∞ → NaN, overflow → ±∞, subnormals kept)
 */
uint32_t FloatAdd(uint32_t a, uint32_t b, bool subtract);

/**
 * @brief FP multiply.
 *
 * Sign = sA ⊕ sB, exponent = eA + eB − 127 (Kogge-Stone), significand =
 * 24 × 24-bit product on the Booth multiplier (Dadda tree), then normalise (product ≥ 2 →
 * shift right 1), round and pack.
 *
 * @param a     first operand (IEEE-754 bits)
 * @param b     second operand (IEEE-754 bits)
 * @return the IEEE-754 product (0 × ∞ → NaN)
 */
uint32_t FloatMultiply(uint32_t a, uint32_t b);

/**
 * @brief FP divide.
 *
 * Sign = sA ⊕ sB, exponent = eA − eB + 127 (Kogge-Stone), significand =
 * non-restoring division producing 26 quotient bits plus a sticky bit
 * (remainder ≠ 0), then normalise (quotient < 1 → shift left 1), round, pack.
 *
 * @param a  dividend (IEEE-754 bits)
 * @param b  divisor (IEEE-754 bits)
 * @return the IEEE-754 quotient (x / 0 → ±∞, 0 / 0 and ∞ / ∞ → NaN)
 */
uint32_t FloatDivide(uint32_t a, uint32_t b);

/**
 * @brief fcmp: maps each operand to an ordered integer key and compares the
 *        keys on the Kogge-Stone adder. +0 equals −0; any NaN is unordered
 *        (all three flags false).
 *
 * @param a  first operand (IEEE-754 bits)
 * @param b  second operand (IEEE-754 bits)
 * @return equal / greater / less
 */
CompareResult CompareFloat(uint32_t a, uint32_t b);

/**
 * @brief itof: sign + magnitude, leading-zero count, normalise with the barrel
 *        shifter, exponent = 158 − lz, round to 24 bits and pack.
 *
 * @param a  signed 32-bit integer
 * @return the nearest IEEE-754 float (0 → +0)
 */
uint32_t IntToFloat(uint32_t a);

/**
 * @brief ftoi: unpack, shift the 24-bit significand by s = exponent − 150 on
 *        the barrel shifter (truncating the fraction), saturate, then negate
 *        if negative. Rounds toward zero, like C.
 *
 * @param a  IEEE-754 bits
 * @return the 32-bit integer (|x| ≥ 2³¹ → INT_MAX / INT_MIN, NaN → 0)
 */
uint32_t FloatToInt(uint32_t a);

}  // namespace AluUnits

#endif

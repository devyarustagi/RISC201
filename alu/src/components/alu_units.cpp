#include "components/alu_units.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace AluUnits {

namespace {

// ===========================================================================
// Small bit helpers
// ===========================================================================

constexpr uint32_t kSignBit = 0x80000000u;

uint32_t negate(uint32_t x) { return ~x + 1u; }  // two's complement

uint32_t reverseBits(uint32_t x) {
    uint32_t reversed = 0;
    for (unsigned i = 0; i < 32; ++i) {
        reversed |= ((x >> i) & 1u) << (31u - i);
    }
    return reversed;
}

// ===========================================================================
// Multiplier helpers: Booth table and carry-save reduction
// ===========================================================================

// Booth radix-4 table: window (b(2j+1), b(2j), b(2j-1)) -> digit in {-2..+2}.
constexpr std::array<int, 8> kBoothDigit{{0, +1, +1, +2, -2, -1, -1, 0}};

struct CarrySave {
    uint32_t sum;
    uint32_t carry;
};

// 3:2 compressor applied to whole rows.
CarrySave carrySaveAdd(uint32_t x, uint32_t y, uint32_t z) {
    return {x ^ y ^ z, ((x & y) | (x & z) | (y & z)) << 1};
}

// One reduction level: `count` CSAs on the first 3·count rows, rest pass through.
std::vector<uint32_t> reduceLevel(const std::vector<uint32_t>& rows, std::size_t count) {
    std::vector<uint32_t> next;
    next.reserve(rows.size());
    std::size_t i = 0;
    for (std::size_t k = 0; k < count; ++k, i += 3) {
        const CarrySave c = carrySaveAdd(rows[i], rows[i + 1], rows[i + 2]);
        next.push_back(c.sum);
        next.push_back(c.carry);
    }
    for (; i < rows.size(); ++i) {
        next.push_back(rows[i]);
    }
    return next;
}

// Largest Dadda height (2, 3, 4, 6, 9, 13, 19, ...) strictly below `rows`.
std::size_t nextDaddaHeight(std::size_t rows) {
    std::size_t height = 2;
    std::size_t target = 2;
    while (height < rows) {
        target = height;
        height = height * 3 / 2;
    }
    return target;
}

// Wallace compresses every full group of three; Dadda only reaches the next height.
std::vector<uint32_t> reduceToTwo(std::vector<uint32_t> rows, ReductionTree tree) {
    while (rows.size() > 2) {
        const std::size_t count = (tree == ReductionTree::Wallace)
                                      ? rows.size() / 3
                                      : rows.size() - nextDaddaHeight(rows.size());
        rows = reduceLevel(rows, count);
    }
    return rows;
}

}  // namespace

// ===========================================================================
// Adder / subtractor
// ===========================================================================

AddResult KoggeStoneAdd(uint32_t a, uint32_t b, bool carryIn) {
    const uint32_t cin = carryIn ? 1u : 0u;
    const uint32_t propagate = a ^ b;

    uint32_t groupG = (a & b) | (propagate & cin);  // fold cin into bit 0
    uint32_t groupP = propagate;
    for (unsigned span = 1; span < 32; span <<= 1) {  // spans 1, 2, 4, 8, 16
        groupG = groupG | (groupP & (groupG << span));  // (G,P)∘(G',P') = (G+P·G', P·P')
        groupP = groupP & (groupP << span);
    }
    // Bit i of groupG is now G(i:0), the carry out of bit i.

    const uint32_t carries = (groupG << 1) | cin;  // c(i) = G(i-1:0), c0 = cin
    const bool carryOut = ((groupG >> 31) & 1u) != 0;
    const bool carryInto31 = ((groupG >> 30) & 1u) != 0;

    AddResult result;
    result.sum = propagate ^ carries;
    result.carryOut = carryOut;
    result.overflow = carryOut != carryInto31;
    return result;
}

AddResult AddSub(uint32_t a, uint32_t b, bool subtract) {
    return KoggeStoneAdd(a, subtract ? ~b : b, subtract);
}

CompareResult CompareSigned(uint32_t a, uint32_t b) {
    const AddResult diff = AddSub(a, b, true);
    const bool negative = (diff.sum & kSignBit) != 0;

    CompareResult result;
    result.equal = diff.sum == 0;                                // NOR of all bits
    result.greater = !result.equal && (negative == diff.overflow);  // ¬E · ¬(N ⊕ V)
    result.less = !result.equal && !result.greater;
    return result;
}

// ===========================================================================
// Multiplier
// ===========================================================================

uint32_t BoothMultiply(uint32_t a, uint32_t b, ReductionTree tree) {
    const uint64_t window = uint64_t{b} << 1;  // append b(-1) = 0
    std::vector<uint32_t> rows;
    rows.reserve(17);
    uint32_t negCorrection = 0;  // the "+1" of every −A / −2A row

    for (unsigned j = 0; j < 16; ++j) {
        const int digit = kBoothDigit[static_cast<std::size_t>((window >> (2 * j)) & 7u)];
        if (digit == 0) {
            rows.push_back(0);
            continue;
        }
        const uint32_t magnitude = (digit == 2 || digit == -2) ? (a << 1) : a;  // A or 2A
        const uint32_t selected = digit < 0 ? ~magnitude : magnitude;
        rows.push_back(selected << (2 * j));
        if (digit < 0) {
            negCorrection |= 1u << (2 * j);
        }
    }
    rows.push_back(negCorrection);

    const std::vector<uint32_t> two = reduceToTwo(rows, tree);  // cycle 1
    return KoggeStoneAdd(two[0], two[1], false).sum;            // cycle 2
}

TreeCost CountTreeHardware(ReductionTree tree) {
    // Dot height of each result column: Booth row j covers columns 2j..31,
    // plus one negation-correction bit at column 2j.
    std::array<int, 32> height{};
    for (int c = 0; c < 32; ++c) {
        height[static_cast<std::size_t>(c)] = c / 2 + 1 + (c % 2 == 0 ? 1 : 0);
    }
    const auto maxHeight = [&height] { return *std::max_element(height.begin(), height.end()); };

    TreeCost cost;
    while (maxHeight() > 2) {
        std::array<int, 32> next{};
        int carryIn = 0;

        if (tree == ReductionTree::Dadda) {
            const int target = static_cast<int>(nextDaddaHeight(static_cast<std::size_t>(maxHeight())));
            for (std::size_t c = 0; c < 32; ++c) {
                int bits = height[c] + carryIn;
                int carryOut = 0;
                while (bits > target) {
                    if (bits - target >= 2) {
                        bits -= 2;
                        ++cost.fullAdders;
                    } else {
                        bits -= 1;
                        ++cost.halfAdders;
                    }
                    ++carryOut;
                }
                next[c] = bits;
                carryIn = carryOut;  // carries out of column 31 are dropped
            }
        } else {
            for (std::size_t c = 0; c < 32; ++c) {
                const int full = height[c] / 3;
                const int half = (height[c] % 3 == 2) ? 1 : 0;
                const int left = height[c] - 3 * full - 2 * half;
                next[c] = full + half + left + carryIn;
                carryIn = full + half;
                cost.fullAdders += full;
                cost.halfAdders += half;
            }
        }
        height = next;
        ++cost.levels;
    }
    return cost;
}

// ===========================================================================
// Divider
// ===========================================================================

DivideResult NonRestoringDivide(uint32_t a, uint32_t b) {
    DivideResult result;
    if (b == 0) {
        result.divideByZero = true;
        return result;
    }

    const bool signA = (a & kSignBit) != 0;
    const bool signB = (b & kSignBit) != 0;
    const uint32_t absA = signA ? negate(a) : a;
    const uint32_t absB = signB ? negate(b) : b;

    int64_t remainder = 0;  // 33-bit R register
    uint32_t quotient = absA;
    const int64_t divisor = absB;

    for (int step = 0; step < 32; ++step) {
        const bool wasNonNegative = remainder >= 0;
        remainder = remainder * 2 + static_cast<int64_t>(quotient >> 31);  // shift {R, Q} left
        quotient <<= 1;
        remainder = wasNonNegative ? remainder - divisor : remainder + divisor;
        quotient |= remainder >= 0 ? 1u : 0u;  // q = ¬sign(R)
    }
    if (remainder < 0) {
        remainder += divisor;  // final remainder correction
    }

    result.quotient = (signA != signB) ? negate(quotient) : quotient;
    const uint32_t absRemainder = static_cast<uint32_t>(remainder);
    result.remainder = signA ? negate(absRemainder) : absRemainder;
    return result;
}

// ===========================================================================
// Shifter
// ===========================================================================

uint32_t BarrelShift(uint32_t a, uint32_t amount, ShiftKind kind) {
    const uint32_t shift = amount & 0x1Fu;  // B[4:0]
    const bool isLsl = kind == ShiftKind::Lsl;
    const bool fill = kind == ShiftKind::Asr && (a & kSignBit) != 0;  // isAsr · a31

    uint32_t x = isLsl ? reverseBits(a) : a;  // bit-reverse stage (in)
    for (unsigned layer = 0; layer < 5; ++layer) {  // ×1, ×2, ×4, ×8, ×16
        const unsigned k = 1u << layer;
        if ((shift & k) != 0) {
            const uint32_t fillBits = fill ? ~0u << (32u - k) : 0u;
            x = (x >> k) | fillBits;
        }
    }
    return isLsl ? reverseBits(x) : x;  // bit-reverse stage (out)
}

}  // namespace AluUnits

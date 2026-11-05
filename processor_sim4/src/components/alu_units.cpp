#include "components/alu_units.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
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
    uint64_t sum;
    uint64_t carry;
};

// 3:2 compressor applied to whole rows (64 bits wide, so the FPU can use the
// full product; the integer multiply keeps the low 32 bits).
CarrySave carrySaveAdd(uint64_t x, uint64_t y, uint64_t z) {
    return {x ^ y ^ z, ((x & y) | (x & z) | (y & z)) << 1};
}

// One reduction level: `count` CSAs on the first 3·count rows, rest pass through.
std::vector<uint64_t> reduceLevel(const std::vector<uint64_t>& rows, std::size_t count) {
    std::vector<uint64_t> next;
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

// Dadda tree: each level compresses only enough rows to reach the next height.
std::vector<uint64_t> reduceToTwo(std::vector<uint64_t> rows) {
    while (rows.size() > 2) {
        rows = reduceLevel(rows, rows.size() - nextDaddaHeight(rows.size()));
    }
    return rows;
}

// 64-bit add on the 32-bit Kogge-Stone adder: low half, then high half with
// the low half's carry out as carry in.
uint64_t add64(uint64_t a, uint64_t b, bool carryIn) {
    const AddResult low = KoggeStoneAdd(static_cast<uint32_t>(a), static_cast<uint32_t>(b), carryIn);
    const AddResult high = KoggeStoneAdd(static_cast<uint32_t>(a >> 32), static_cast<uint32_t>(b >> 32),
                                         low.carryOut);
    return (uint64_t{high.sum} << 32) | low.sum;
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

uint64_t BoothMultiplyWide(uint32_t a, uint32_t b) {
    const uint64_t window = uint64_t{b} << 1;  // append b(-1) = 0
    // A sign-extended to 64 bits, so ±A / ±2A rows are correct across the full product.
    const uint64_t multiplicand = (a & kSignBit) != 0 ? (0xFFFFFFFF00000000ull | a) : uint64_t{a};

    std::vector<uint64_t> rows;
    rows.reserve(17);
    uint64_t negCorrection = 0;  // the "+1" of every −A / −2A row

    for (unsigned j = 0; j < 16; ++j) {
        const int digit = kBoothDigit[static_cast<std::size_t>((window >> (2 * j)) & 7u)];
        if (digit == 0) {
            rows.push_back(0);
            continue;
        }
        const uint64_t magnitude = (digit == 2 || digit == -2) ? (multiplicand << 1) : multiplicand;  // A or 2A
        const uint64_t selected = digit < 0 ? ~magnitude : magnitude;
        rows.push_back(selected << (2 * j));
        if (digit < 0) {
            negCorrection |= uint64_t{1} << (2 * j);
        }
    }
    rows.push_back(negCorrection);

    const std::vector<uint64_t> two = reduceToTwo(rows);  // cycle 1: Dadda tree
    return add64(two[0], two[1], false);                        // cycle 2: Kogge-Stone final add
}

uint32_t BoothMultiply(uint32_t a, uint32_t b) {
    return static_cast<uint32_t>(BoothMultiplyWide(a, b));  // keep low 32 bits
}

TreeCost CountDaddaHardware() {
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

// ===========================================================================
// Floating-point unit (IEEE-754 single precision)
// ===========================================================================

namespace {

constexpr uint32_t kQuietNaN = 0x7FC00000u;
constexpr uint32_t kInfinity = 0x7F800000u;

enum class Kind { Zero, Finite, Infinity, NaN };

// Unpacked operand: value = sig / 2^63 × 2^(exp − 127), leading 1 of sig at
// bit 63 (subnormals are normalised here, with a smaller exp).
struct Unpacked {
    Kind kind;
    bool sign;
    int exp;
    uint64_t sig;
};

int countLeadingZeros64(uint64_t x) {
    int count = 0;
    for (uint64_t mask = uint64_t{1} << 63; mask != 0 && (x & mask) == 0; mask >>= 1) {
        ++count;
    }
    return count;  // 64 when x == 0
}

int countLeadingZeros32(uint32_t x) {
    return countLeadingZeros64(x) - 32;
}

// Unpack: sign · exponent · significand (1.f), plus zero / ∞ / NaN / subnormal detect.
Unpacked unpack(uint32_t x) {
    const bool sign = (x & kSignBit) != 0;
    const int exp = static_cast<int>((x >> 23) & 0xFFu);
    const uint32_t fraction = x & 0x7FFFFFu;

    if (exp == 0xFF) {
        return {fraction != 0 ? Kind::NaN : Kind::Infinity, sign, 0, 0};
    }
    if (exp == 0 && fraction == 0) {
        return {Kind::Zero, sign, 0, 0};
    }
    if (exp == 0) {  // subnormal: normalise so the leading 1 sits at bit 63
        const uint64_t sig = uint64_t{fraction} << 40;
        const int lz = countLeadingZeros64(sig);
        return {Kind::Finite, sign, 1 - lz, sig << lz};
    }
    return {Kind::Finite, sign, exp, uint64_t{fraction | 0x800000u} << 40};
}

// 24-bit significand 1.f (what the integer multiplier / divider work on).
uint32_t significand24(const Unpacked& u) {
    return static_cast<uint32_t>(u.sig >> 40);
}

// Alignment shift: shift right, OR every bit shifted out into bit 0 (sticky).
uint64_t shiftRightJam(uint64_t x, int n) {
    if (n <= 0) return x;
    if (n >= 64) return x != 0 ? 1u : 0u;
    const uint64_t lost = x & ((uint64_t{1} << n) - 1);
    return (x >> n) | (lost != 0 ? 1u : 0u);
}

uint32_t signedZero(bool sign) { return sign ? kSignBit : 0u; }
uint32_t signedInfinity(bool sign) { return signedZero(sign) | kInfinity; }

// Exponent arithmetic on the Kogge-Stone adder (two's complement in 32 bits).
int expAdd(int x, int y) {
    return static_cast<int32_t>(AddSub(static_cast<uint32_t>(x), static_cast<uint32_t>(y), false).sum);
}
int expSub(int x, int y) {
    return static_cast<int32_t>(AddSub(static_cast<uint32_t>(x), static_cast<uint32_t>(y), true).sum);
}

// Shared "Normalise + round + pack" stage.
// value = sig / 2^63 × 2^(exp − 127); sig need not be normalised yet.
uint32_t roundPack(bool sign, int exp, uint64_t sig) {
    if (sig == 0) return signedZero(sign);

    const int lz = countLeadingZeros64(sig);  // normalise: leading 1 -> bit 63
    sig <<= lz;
    exp -= lz;

    if (exp < 1) {  // below the normal range -> subnormal (underflow)
        sig = shiftRightJam(sig, 1 - exp);
        exp = 1;
    }

    // Round to nearest, ties to even, on the 24 kept bits (guard · round · sticky below).
    uint64_t mantissa = sig >> 40;
    const uint64_t rest = sig & ((uint64_t{1} << 40) - 1);
    const uint64_t half = uint64_t{1} << 39;
    if (rest > half || (rest == half && (mantissa & 1u) != 0)) {
        ++mantissa;
    }
    if (mantissa == (uint64_t{1} << 24)) {  // rounding carried out: +1 to exponent
        mantissa >>= 1;
        ++exp;
    }
    if (exp >= 0xFF) return signedInfinity(sign);  // overflow -> ±∞

    const bool normal = ((mantissa >> 23) & 1u) != 0;
    const uint32_t expField = normal ? static_cast<uint32_t>(exp) : 0u;
    return signedZero(sign) | (expField << 23) | (static_cast<uint32_t>(mantissa) & 0x7FFFFFu);
}

// Significand divider: non-restoring division of two 24-bit significands.
// Produces 1 + steps quotient bits (= floor(x · 2^steps / d)); the sticky bit
// says whether the final remainder is non-zero.
struct FractionQuotient {
    uint64_t quotient;
    bool sticky;
};

FractionQuotient nonRestoringFraction(uint32_t x, uint32_t d, int steps) {
    const int64_t divisor = d;
    int64_t remainder = static_cast<int64_t>(x) - divisor;  // first compare (x < 2d)
    uint64_t quotient = remainder >= 0 ? 1u : 0u;
    for (int step = 0; step < steps; ++step) {
        remainder = remainder >= 0 ? remainder * 2 - divisor : remainder * 2 + divisor;  // R ± D
        quotient = (quotient << 1) | (remainder >= 0 ? 1u : 0u);                       // q = ¬sign(R)
    }
    if (remainder < 0) remainder += divisor;  // correction
    return {quotient, remainder != 0};
}

}  // namespace

uint32_t FloatAdd(uint32_t a, uint32_t b, bool subtract) {
    if (subtract) b ^= kSignBit;  // sign B ⊕ isFsub
    Unpacked x = unpack(a);
    Unpacked y = unpack(b);

    // Special cases (isSpecial = 1).
    if (x.kind == Kind::NaN || y.kind == Kind::NaN) return kQuietNaN;
    if (x.kind == Kind::Infinity) {
        if (y.kind == Kind::Infinity && x.sign != y.sign) return kQuietNaN;  // ∞ − ∞
        return signedInfinity(x.sign);
    }
    if (y.kind == Kind::Infinity) return signedInfinity(y.sign);
    if (x.kind == Kind::Zero && y.kind == Kind::Zero) return signedZero(x.sign && y.sign);
    if (x.kind == Kind::Zero) return b;
    if (y.kind == Kind::Zero) return a;

    // Compare magnitudes & swap: X = larger |operand|, Y = smaller.
    if (y.exp > x.exp || (y.exp == x.exp && y.sig > x.sig)) std::swap(x, y);

    // Alignment shifter: sig Y >> d, lost bits OR-ed into sticky. Both are
    // pre-shifted by one so the sum cannot overflow 64 bits.
    const uint64_t sigX = x.sig >> 1;
    const uint64_t sigY = shiftRightJam(y.sig >> 1, expSub(x.exp, y.exp));

    // Significand add / subtract on the Kogge-Stone adder.
    const uint64_t sum = (x.sign == y.sign) ? add64(sigX, sigY, false)
                                            : add64(sigX, ~sigY, true);  // X − Y
    if (sum == 0) return signedZero(false);  // exact cancellation -> +0
    return roundPack(x.sign, x.exp + 1, sum);
}

uint32_t FloatMultiply(uint32_t a, uint32_t b) {
    const Unpacked x = unpack(a);
    const Unpacked y = unpack(b);
    const bool sign = x.sign != y.sign;  // sA ⊕ sB

    // Special cases.
    if (x.kind == Kind::NaN || y.kind == Kind::NaN) return kQuietNaN;
    if (x.kind == Kind::Infinity || y.kind == Kind::Infinity) {
        if (x.kind == Kind::Zero || y.kind == Kind::Zero) return kQuietNaN;  // 0 × ∞
        return signedInfinity(sign);
    }
    if (x.kind == Kind::Zero || y.kind == Kind::Zero) return signedZero(sign);

    // Exponent adder: eA + eB − 127.
    const int exponent = expSub(expAdd(x.exp, y.exp), 127);
    // Significand multiplier: 24 × 24 on the Booth + tree datapath -> 48-bit product.
    const uint64_t product = BoothMultiplyWide(significand24(x), significand24(y));
    // The product has 46 fraction bits; roundPack expects 63 -> +17.
    return roundPack(sign, exponent + 17, product);
}

uint32_t FloatDivide(uint32_t a, uint32_t b) {
    const Unpacked x = unpack(a);
    const Unpacked y = unpack(b);
    const bool sign = x.sign != y.sign;  // sA ⊕ sB

    // Special cases.
    if (x.kind == Kind::NaN || y.kind == Kind::NaN) return kQuietNaN;
    if (x.kind == Kind::Infinity) {
        return y.kind == Kind::Infinity ? kQuietNaN : signedInfinity(sign);  // ∞/∞, ∞/x
    }
    if (y.kind == Kind::Infinity) return signedZero(sign);  // x/∞
    if (y.kind == Kind::Zero) {
        return x.kind == Kind::Zero ? kQuietNaN : signedInfinity(sign);  // 0/0, x/0
    }
    if (x.kind == Kind::Zero) return signedZero(sign);

    // Exponent subtract: eA − eB + 127.
    const int exponent = expAdd(expSub(x.exp, y.exp), 127);
    // Significand divider: 26 steps -> 26 fraction bits + sticky (remainder ≠ 0).
    const FractionQuotient q = nonRestoringFraction(significand24(x), significand24(y), 26);
    // The quotient has 26 fraction bits; roundPack expects 63 -> +37.
    return roundPack(sign, exponent + 37, q.quotient | (q.sticky ? 1u : 0u));
}

CompareResult CompareFloat(uint32_t a, uint32_t b) {
    CompareResult result;
    if (unpack(a).kind == Kind::NaN || unpack(b).kind == Kind::NaN) {
        return result;  // unordered: E = GT = LT = 0
    }
    // Ordered key: sign-magnitude -> signed integer order (+0 and −0 both map to 0).
    const auto key = [](uint32_t v) {
        const uint32_t magnitude = v & ~kSignBit;
        return (v & kSignBit) != 0 ? negate(magnitude) : magnitude;
    };
    return CompareSigned(key(a), key(b));  // Kogge-Stone cmp
}

uint32_t IntToFloat(uint32_t a) {
    if (a == 0) return 0;  // A = 0 -> +0

    // Sign + magnitude.
    const bool sign = (a & kSignBit) != 0;
    const uint32_t magnitude = sign ? negate(a) : a;

    // Leading-zero counter, then normalise on the barrel shifter (top 1 -> bit 31).
    const int lz = countLeadingZeros32(magnitude);
    const uint32_t shifted = BarrelShift(magnitude, static_cast<uint32_t>(lz), ShiftKind::Lsl);
    int exponent = 158 - lz;

    // Round to nearest even: keep 24 bits, the low 8 decide (sticky from the rest).
    uint32_t mantissa = shifted >> 8;
    const uint32_t rest = shifted & 0xFFu;
    if (rest > 0x80u || (rest == 0x80u && (mantissa & 1u) != 0)) {
        ++mantissa;
    }
    if (mantissa == (1u << 24)) {  // rounding overflow: +1 to exponent
        mantissa >>= 1;
        ++exponent;
    }

    // Pack: sign · exponent · fraction.
    return signedZero(sign) | (static_cast<uint32_t>(exponent) << 23) | (mantissa & 0x7FFFFFu);
}

uint32_t FloatToInt(uint32_t a) {
    // Unpack: sign · exponent · 24-bit significand.
    const bool sign = (a & kSignBit) != 0;
    const int exponent = static_cast<int>((a >> 23) & 0xFFu);
    const uint32_t fraction = a & 0x7FFFFFu;

    if (exponent == 0xFF && fraction != 0) return 0;          // NaN -> 0
    if (exponent == 0) return 0;                              // zero / subnormal: |x| < 1
    const uint32_t significand = fraction | 0x800000u;

    // Shift amount s = exponent − 150 (value = significand × 2^s).
    const int s = exponent - 150;

    // Saturate: |x| ≥ 2^31 (also ±∞) -> INT_MAX / INT_MIN.
    if (s >= 8) return sign ? 0x80000000u : 0x7FFFFFFFu;

    // Barrel shifter: s ≥ 0 -> shift left, s < 0 -> shift right (truncate toward zero).
    uint32_t magnitude = 0;
    if (s >= 0) {
        magnitude = BarrelShift(significand, static_cast<uint32_t>(s), ShiftKind::Lsl);
    } else if (s > -24) {
        magnitude = BarrelShift(significand, static_cast<uint32_t>(-s), ShiftKind::Lsr);
    }

    // Negate if sign.
    return sign ? negate(magnitude) : magnitude;
}

}  // namespace AluUnits

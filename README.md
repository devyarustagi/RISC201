# ALU — RISC201 Processor Simulator

The ALU executes every operation of the EX stage, integer and floating point.
Each operation runs through an **algorithm-level model of its hardware unit**,
so the simulator computes results the same way the design diagrams do — not
with C++'s built-in `+`, `*`, `/` or `float` maths.

The FPU is part of the ALU and **reuses the integer units** (adder, multiplier,
divider, shifter) for its significand and exponent work.

## Files

| File | Purpose |
| :--- | :--- |
| `alu/include/common/micro_types.hpp` | `AluOp` enum (includes `MOD`, `ITOF`, `FTOI`) |
| `alu/include/components/alu.hpp` | `Alu` class: the interface the EX stage uses |
| `alu/src/components/alu.cpp` | Picks the hardware unit for each `AluOp` and latches the flags |
| `alu/include/components/alu_units.hpp` | Declarations and documentation of each hardware unit |
| `alu/src/components/alu_units.cpp` | The algorithms: adder, multiplier, divider, shifter, FPU |
| `alu/tests/alu_test.cpp` | Checks every operation against native C++ arithmetic |

## Interface

The EX stage only needs these calls:

```cpp
uint32_t result = alu.Execute(op, a, b);   // run one operation
alu.FlagE();  alu.FlagGt();  alu.FlagLt(); // flags written by cmp and fcmp
```

FP operands and results are raw IEEE-754 single-precision bit patterns in a
`uint32_t`, so the register file needs no change.

The control unit selects the operation with `aluOp`:

| Instruction | `AluOp` |
| :--- | :--- |
| `mod` | `AluOp::MOD` |
| `itof` | `AluOp::ITOF` |
| `ftoi` | `AluOp::FTOI` |

The ALU side of `mod`, `itof` and `ftoi` is ready. The opcode table and the
control unit decide which instructions map to these values.

Extra read-only status, for later use by the hazard unit and exceptions:

| Method | Meaning |
| :--- | :--- |
| `DivideByZero()` | `true` if the last `div` or `mod` had a zero divisor |
| `LastLatency()` | EX cycles the last operation takes in hardware (see table below) |
| `Reset()` | Clear flags and status |

## Units and algorithms

| Operations | Unit | Algorithm | Latency |
| :--- | :--- | :--- | :---: |
| `add`, `sub`, `cmp` (+ `ld`/`st` address) | Adder / subtractor | Kogge-Stone parallel-prefix adder | 1 |
| `mul` | Multiplier | Radix-4 Booth recoding + Dadda tree | 2 |
| `div`, `mod` | Divider | Non-restoring division (`isMod` selects R or Q) | 32 |
| `lsl`, `lsr`, `asr` | Shifter | Logarithmic barrel shifter | 1 |
| `and`, `or`, `not` | Logic unit | Bitwise gates | 1 |
| `mov` (`PASS`) | Pass-through | Passes operand A (the immediate) | 1 |
| `fadd`, `fsub` | FPU | Align, Kogge-Stone add/subtract, normalise, round | 2 |
| `fmul` | FPU | Exponent add + Booth/Dadda 24×24 significand product | 2 |
| `fdiv` | FPU | Exponent subtract + non-restoring fraction division | 26 |
| `fcmp` | FPU | Sign-magnitude to ordered integer, then signed compare | 1 |
| `itof`, `ftoi` | FPU | Leading-zero count + barrel shift + round | 1 |

### Adder / subtractor — Kogge-Stone

1. Each bit computes **generate** `g = a·b` and **propagate** `p = a ⊕ b`.
2. Carry-in is folded into bit 0.
3. Five prefix levels (spans 1, 2, 4, 8, 16) combine groups:
   `(G, P) ∘ (G', P') = (G + P·G', P·P')`, so every carry is known after five steps.
4. Sum bit = `p ⊕ carry-in of that bit`.

Subtraction is `A + ~B + 1` on the same adder.
`cmp` uses the difference to set the flags:

- `E  = NOR of all difference bits` (A = B)
- `GT = ¬E · ¬(N ⊕ V)` — signed A > B, where N is the sign bit and V the overflow
- `LT = ¬E · ¬GT`

### Multiplier — Radix-4 Booth + Dadda tree

1. **Booth recoding:** B is read 2 bits at a time with a 0 appended below bit 0.
   Each 3-bit window becomes a digit in {−2, −1, 0, +1, +2}, giving **16 partial
   products instead of 32**.
2. **Partial products:** each digit selects 0, ±A or ±2A, shifted left by 2j.
3. **Dadda tree:** carry-save adders reduce the rows to 2 in 6 levels
   (16 → 13 → 9 → 6 → 4 → 3 → 2).
4. **Final add:** the two rows are added with the Kogge-Stone adder
   (two 32-bit adders chained for the 64-bit product).

`mul` keeps only the **low 32 bits**, so overflow wraps (the ISA has no
overflow flag). `fmul` uses the full product of the two 24-bit significands.

### Divider — Non-restoring

1. Work on |A| and |B|, remembering the signs. R = 0, Q = |A|, D = |B|.
2. Repeat 32 times: shift {R, Q} left; if R ≥ 0 then R = R − D, else R = R + D;
   the new quotient bit is 1 if R ≥ 0.
3. **Correction:** if R < 0 at the end, add D once.
4. **Sign fix:** negate Q if A and B had different signs; negate R if A was negative.
5. **Output:** `isMod ? R : Q` — `div` returns the quotient, `mod` the remainder.

A wrong subtraction is never undone — the next step adds instead — so every
step is exactly one add or subtract. `div` and `mod` share the same hardware.

### Shifter — Logarithmic barrel shifter

- Five layers of 2:1 muxes shift right by 1, 2, 4, 8 and 16; each layer is
  switched on by one bit of the shift amount (B[4:0]).
  Example: shift by 5 = `00101` → ×1 and ×4 layers on.
- Vacated bits are filled with `isAsr · a31` (0 for `lsr`, the sign bit for `asr`).
- `lsl` reverses the bits, shifts right, and reverses back — one structure for all three.

### Logic unit and mov

AND, OR and NOT work bit by bit with no carries. `not rd, rs1` inverts A.
`mov rd, imm` passes A (the immediate) through unchanged.

### FPU — IEEE-754 single precision

Every FP operation follows the same path:

1. **Unpack:** split each operand into sign, exponent and significand, and add
   the hidden 1 for normal numbers. Classify each operand as zero, subnormal,
   normal, infinity or NaN.
2. **Special cases:** NaN, infinity and zero inputs go to a special-case block.
   A mux (`isSpecial`) selects its result instead of the computed one.
3. **Compute** in the selected unit (below).
4. **Normalise:** shift the result so the leading 1 is in place and adjust the
   exponent. Results that are too small become subnormal.
5. **Round to nearest, ties to even**, using the guard, round and sticky bits.
   A rounding carry can move the result up one exponent.
6. **Pack** sign, exponent and fraction. Results that are too large become ±∞.

| Operation | Compute step |
| :--- | :--- |
| `fadd`, `fsub` | `fsub` flips the sign of B. The larger operand goes first; the smaller significand shifts right by the exponent difference (shifted-out bits go into sticky). Same signs: add. Different signs: subtract. Both on the Kogge-Stone adder. |
| `fmul` | Sign = sA ⊕ sB. Exponent = eA + eB − 127 on the adder. Significand = 24×24 product on the Booth multiplier with the Dadda tree. |
| `fdiv` | Sign = sA ⊕ sB. Exponent = eA − eB + 127 on the adder. Significand = 26 steps of non-restoring division; a non-zero remainder sets sticky. |
| `fcmp` | Each value maps to an ordered integer (positive: the bits as they are; negative: the magnitude negated). The integer comparator then sets E, GT and LT. |
| `itof` | Take \|A\|, count leading zeros, shift left with the barrel shifter, exponent = 158 − leading zeros, then round. |
| `ftoi` | Shift the significand left or right with the barrel shifter by (exponent − 150), truncate toward zero, then apply the sign. |

## Behaviour notes

| Case | Result |
| :--- | :--- |
| `mul` overflow | Low 32 bits kept (wraps) |
| `div` / `mod` by zero | Returns 0 and sets `DivideByZero()` |
| `INT_MIN / −1` | Quotient wraps to `INT_MIN`, remainder 0 |
| Sign of `mod` result | Same sign as A (like C): `−17 mod 5 = −2` |
| Shift amount ≥ 32 | Only bits [4:0] are used |
| FP rounding | Round to nearest, ties to even (same as C++ `float`) |
| FP subnormals | Supported as inputs and outputs |
| FP overflow | Returns ±∞ |
| `0/0`, `∞ − ∞`, `0 × ∞`, `∞/∞`, any NaN input | Returns NaN |
| `x / 0` (x ≠ 0) | Returns ±∞ (does not set `DivideByZero()`) |
| `fcmp` with a NaN | E, GT and LT are all 0 |
| `fcmp` of +0 and −0 | Equal |
| `ftoi` | Truncates toward zero; out of range saturates to `INT_MAX` / `INT_MIN`; NaN → 0 |
| `itof` | Rounds to nearest even when the integer needs more than 24 bits |

`LastLatency()` is informational for now — the pipeline does not stall on it
yet. A hazard unit can hold EX for `LastLatency() − 1` cycles (the `aluBusy`
signal) for multi-cycle operations.

## Testing

```sh
cd alu
make alu-test
```

Runs every operation on edge cases and random inputs and compares the result
with native C++ arithmetic: C++ integer maths for the integer ops and the
host's IEEE-754 `float` maths for the FP ops. It also checks the `cmp`/`fcmp`
flags and `DivideByZero()`, and prints the hardware cost of the Dadda tree.
FP ops are tested with random bit patterns, everyday values and subnormals.

```text
op       tests  failed  latency
add     200064       0        1
sub     200064       0        1
cmp     200064       0        1
mul     200064       0        2
div     200064       0       32
mod     200064       0       32
and     200064       0        1
or      200064       0        1
not     200064       0        1
pass    200064       0        1
lsl     200064       0        1
lsr     200064       0        1
asr     200064       0        1
fadd    600196       0        2
fsub    600196       0        2
fmul    600196       0        2
fdiv    600196       0       26
fcmp    600196       0        1
itof    600196       0        1
ftoi    600196       0        1

Dadda tree: 6 levels, 210 full adders, 30 half adders

PASS: all ALU operations match
```

The code builds with no warnings under
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.

## Design choices

| Choice | Reason |
| :--- | :--- |
| Kogge-Stone adder | Fastest prefix adder: 5 levels for 32 bits, low fanout |
| Radix-4 Booth | Halves the partial products (32 → 16) and handles signed numbers directly |
| Dadda tree | Reduces 16 rows to 2 in 6 levels with only 210 full + 30 half adders |
| Non-restoring divider | One add or subtract per step; `div` and `mod` share it |
| Barrel shifter | Any shift amount in one cycle; one structure for lsl, lsr and asr |
| FPU reuses integer units | No second adder, multiplier or shifter; less hardware |
| Round to nearest even | IEEE-754 default; matches C++ `float`, so results can be checked exactly |

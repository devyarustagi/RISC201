# ALU — RISC201 Processor Simulator

The ALU executes every integer operation of the EX stage. Each operation runs
through an **algorithm-level model of its hardware unit**, so the simulator
computes results the same way the design diagrams do — not with C++'s built-in
`+`, `*` or `/`.

Floating-point operations are **not** part of this ALU.

## Files

| File | Purpose |
| :--- | :--- |
| `include/components/alu.hpp` | `Alu` class: the interface the EX stage uses |
| `src/components/alu.cpp` | Picks the hardware unit for each `AluOp` and latches the flags |
| `include/components/alu_units.hpp` | Declarations and documentation of each hardware unit |
| `src/components/alu_units.cpp` | The algorithms: adder, multiplier, divider, shifter |
| `tests/alu_test.cpp` | Checks every operation against native C++ arithmetic |

## Interface

`mod` uses `AluOp::MOD`; the control unit sets `aluOp = AluOp::MOD` for the
`mod` instruction.

The EX stage only needs these calls:

```cpp
uint32_t result = alu.Execute(op, a, b);   // run one operation
alu.FlagE();  alu.FlagGt();  alu.FlagLt(); // flags written by cmp
```

Extra read-only status, for later use by the hazard unit and exceptions:

| Method | Meaning |
| :--- | :--- |
| `DivideByZero()` | `true` if the last `div` or `mod` had a zero divisor |
| `LastLatency()` | EX cycles the last operation takes in hardware (see table below) |
| `SetReductionTree(tree)` | Choose `Dadda` (default) or `Wallace` for the multiplier |
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
4. **Final add:** the two rows are added with the Kogge-Stone adder.

Only the **low 32 bits** are produced, so overflow wraps (the ISA has no
overflow flag).

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

## Behaviour notes

| Case | Result |
| :--- | :--- |
| `mul` overflow | Low 32 bits kept (wraps) |
| `div` / `mod` by zero | Returns 0 and sets `DivideByZero()` |
| `INT_MIN / −1` | Quotient wraps to `INT_MIN`, remainder 0 |
| Sign of `mod` result | Same sign as A (like C): `−17 mod 5 = −2` |
| Shift amount ≥ 32 | Only bits [4:0] are used |
| `fadd`, `fsub`, `fmul`, `fdiv`, `fcmp` | Not handled by the ALU: return 0, flags unchanged |

`LastLatency()` is informational for now — the pipeline does not stall on it
yet. A hazard unit can hold EX for `LastLatency() − 1` cycles (the `aluBusy`
signal) when multi-cycle operations are added.

## Design choices

| Choice | Reason |
| :--- | :--- |
| Kogge-Stone adder | Fastest prefix adder: 5 levels for 32 bits, low fanout |
| Radix-4 Booth | Halves the partial products (32 → 16) and handles signed numbers directly |
| Dadda over Wallace | Same 6 levels, but fewer adders (210 + 30 vs 215 + 80) |
| Non-restoring divider | One add or subtract per step; `div` and `mod` share it |
| Barrel shifter | Any shift amount in one cycle; one structure for lsl, lsr and asr |

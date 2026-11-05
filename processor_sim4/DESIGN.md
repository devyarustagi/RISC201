# RISC201 — 4-Stage Pipelined Datapath

**Design document.** This explains *how the processor is built and what happens
in every stage*, matching the block diagram in [`../4StagePipeline.svg`](../4StagePipeline.svg)
and the micro-controller diagram in [`../MicrocontrolUnit.svg`](../MicrocontrolUnit.svg).
It is not a usage guide; for build/run steps see [`processor_sim4.md`](processor_sim4.md).

Names in **bold** are labels used in the SVG. Names in `code font` are the
corresponding C++ entities in `processor_sim4/` (and `alu/`).

---

## 1. The big picture

The machine is a **4-stage, in-order, microcoded pipeline**. Every macroinstruction
is executed by one or more *microinstructions* fetched from a microprogram ROM.

```
            ┌──────────── IF ────────────┐   ┌──────────────── OF ────────────────┐
            │  PC                        │   │  Register File (rd/rs1/rs2)        │
            │  Instruction Memory        │   │  Immediate Unit                    │
 .bin  ───► │  +4 / +offset              │──►│  Control Unit (Microcontrol)       │──┐
            │  Predictor (Inst[25])      │IF/│  RAS (call/ret)                    │  │
            │  PC main mux               │OF │  Data Lock Unit                    │  │
            └────────────┬───────────────┘   └────────────────┬───────────────────┘  │
                         │ NextPC / CorrectPC / RAS Out / oldPC │ OF/EX                │
                         └──────────────◄──────────────────────┘                      │
                                                                                     │
            ┌──────────────── EX ─────────────────┐   ┌────────────── MA-RW ─────────┐
            │  Forwarding Unit                    │   │  Memory Unit (mar/mdr)       │
            │  AluSrcA / AluSrcB muxes             │EX/│  Data Memory                 │
        ───►│  ALU + flags                        │──►│  Writeback mux (isLd)        │
            │  Scratch File (written here)        │MA │  Branch Lock / predictor      │
            │  Correct PC Recovery Unit           │RW │  inversion (Inst ^ bit25)    │
            └─────────────────────────────────────┘   └──────────────────────────────┘
```

The three pipeline registers between the stages are **`IF/OF` (Inst. Packet)**,
**`OF/EX`**, and **`EX/MA-RW`**. Each is edge-triggered: the stages compute into
the register's `in` side during a cycle, and a single rising edge copies `in → out`
(`PipelineRegister::latch()`).

---

## 2. Two levels of instruction

### 2.1 Macroinstruction (32-bit, from the assembler)

```
 31      26 25    22 21    18 17    14 13                              0
┌──────────┬────────┬────────┬────────┬────────────────────────────────┐
│  opcode  │   rd   │  rs1   │  rs2   │            imm / offset         │
└──────────┴────────┴────────┴────────┴────────────────────────────────┘
```

* **R-format** — `rd[25:22]`, `rs1[21:18]`, `rs2[17:14]`.
* **I-format** — `rd[25:22]`, `rs1[21:18]`, `imm18[17:0]` =
  `[17:16] modifier | [15:0] value` (`00` signed, `01` unsigned `u`, `10` high `h`).
* **B-format** — 25-bit signed **word** offset in `[24:0]`; **bit 25 is the
  dynamic branch-prediction bit** stored inside instruction memory.

The opcode map (`MacroOp`, 0x00–0x2C) is shared by the assembler, disassembler,
microassembler and simulator.

### 2.2 Microinstruction (38-bit, from the microassembler)

```
 37      32 31    27 26    22 21    17 16                    1 0
┌──────────┬─────────┬─────────┬─────────┬──────────────────────┬───┐
│  opcode  │  dest   │  src1   │  src2   │     immediate (16)   │ s │
└──────────┴─────────┴─────────┴─────────┴──────────────────────┴───┘
```

* **dest / src1 / src2** are 5-bit register fields. `0b00000` means "the
  macroinstruction's `rd`/`rs1`/`rs2`"; `0b10000 | n` means scratch register `s<n>`.
* **immediate** is a signed 16-bit constant (`mimm`).
* **s (stall)** = 1 while more micro-ops follow; 0 on the last one.

Micro-opcodes are assigned **by parity**: *even = register form* (the ALU's B
input is `op2`), *odd = immediate form* (B input is the macro immediate). The
controller can therefore distinguish the two forms with a single bit test
`opcode & 1`. (The privileged group `mmovz/mretz/mebreak/mecall` is decoded
explicitly.)

### 2.3 Microcoded control — matching `MicrocontrolUnit.svg`

```
        .label table               microprogram ROM
 opcode ──► [ switch ] ──► µmux ──► [ memory ] ──► [ decode ] ──► control signals
                 ▲            ▲                         │
                 │            └──── µpc ◄── (+1) ◄──────┘
                 │                       ▲
                 └──────────── stall ────┘
```

* The **switch** (`ControlUnit::Begin(opcode)`) reads
  `rom_address_table.rom` — one byte per macro opcode 0x00–0x2C — and loads the
  micro-PC (`upc_`) with the start address of that instruction's sequence.
* The **microprogram memory** (`microprograms.rom`) is a continuous **MSB-first
  bit stream of 38-bit words**, unpacked by `ControlUnit::loadMicroprograms`.
* The **decode unit** (`ControlUnit::decode`) translates a microinstruction into
  `ControlSignals`. The `stall` bit becomes `isStall`, which freezes IF while a
  sequence is being walked.
* `upc_` advances while `busy_` is set; on the last micro-op (`stall = 0`) the
  sequencer is idle and `Begin` reloads the start address for the next macro-
  instruction.

The bundled `microcode.masm` currently maps each macroinstruction to a single
micro-op (e.g. `add → madd`), but the mechanism supports multi-micro-op sequences.

---

## 3. Pipeline registers and the clocking model

### 3.1 Edge-triggered registers

```cpp
template <typename T> struct PipelineRegister {
    T out{};            // value consumed by the stage this cycle
    T in{};             // value written by the producing stage this cycle
    void latch() { out = in; }
};
```

A cycle is: all four stages evaluate from the current `out` values, then one
`latch()` copies every `in` to `out`. Nothing the stages write this cycle is
visible to another stage until the next cycle.

### 3.2 Evaluation order inside `Cpu::Step()`

The stages are evaluated **oldest-first** (the order matches data flow, not the
names):

1. **MA-RW** consumes `exMem_.out` → `MarwResult` (writeback value, exception).
2. **EX** consumes `ofEx_.out`, forwarding from MA (`exMem_.out`) and WB
   (`marw.wb`) → `ExResult` (mispredict, correct PC, halt).
3. **OF** consumes `ifOf_.out` → `OfResult` (control word, operands, RAS out).
4. **Data Lock Unit** decides `isStall` (microcode stall OR load-use hazard).
5. **IF** drives the main PC mux and fetches; it may flush the IF/OF register.
6. Branch-lock / exception flush clears younger packets.
7. `latch()` on IF/OF, OF/EX, EX/MA-RW and the RAS.

Because all stages read `out` and write `in`, evaluating them in any order within
the cycle is equivalent; the order above mainly determines which combinational
signals (e.g. the forwarding values) are ready when.

### 3.3 What each packet carries

| Register | Fields |
| :--- | :--- |
| **IF/OF** (`IfOfPacket`) | `valid`, `pc`, `instruction`, `predictTaken`, `isException`, `cause` |
| **OF/EX** (`OfExPacket`) | `valid`, `pc`, `instruction`, `imm`, `op1`, `op2`, `rd`, `rs1`, `rs2`, `predictTaken`, `isException`, `cause`, `flagE/flagGt/flagLt`, `control` |
| **EX/MA-RW** (`ExMemPacket`) | `valid`, `pc`, `instruction`, `mispredict`, `op2`, `aluResult`, `rd`, `isException`, `cause`, `flagE/flagGt/flagLt`, `control` |

The exception fields (`isException`, `cause`, flag snapshot) implement the
precise-exception mechanism described in `../exceptions.md`.

---

## 4. Stage **IF** — Instruction Fetch

Implemented by `StageIF` (`src/stages/stage_if.cpp`).

* Owns the **program counter** and the **PC main mux**.
* Reads the instruction word from **Instruction Memory** (`Memory::FetchInstruction`,
  word-addressed `ram[pc/4]`).
* Computes the *predicted-next* PC:
  * `PredictTaken = isBranchOpcode ∧ Inst[25]`, where `isBranchOpcode` is
    `b/beq/bgt/call` (0x23–0x26).
  * `NextPC = PredictTaken ? PC + (signExtend25(Inst[24:0]) << 2) : PC + 4`.
* Drives the **main PC mux**, highest priority first:

  | Priority | Source | Condition |
  | :--- | :--- | :--- |
  | 1 | `eBase` (`exceptionPC`) | an exception committed in MA-RW |
  | 2 | `CorrectPC` | `Mispredict` from EX |
  | 3 | `oldPC` | `retz` resolved in OF |
  | 4 | `RAS Out` | `ret` resolved in OF |
  | 5 | `NextPC` (predicted target or PC + 4) | normal |

* **Flushes:** exception or mispredict clear the IF/OF register (one fetch
  discarded); `ret`/`retz` perform a one-stage flush. A **stall** freezes the PC
  and keeps the current IF/OF contents (`ifOf.in = ifOf.out`).
* **Asynchronous interrupt:** if the CPU has a pending interrupt, the fetched
  instruction is marked (`isException`, `cause = EXC_INTERRUPT`).

---

## 5. Stage **OF** — Operand Fetch / Decode

Implemented by `StageOF` (`src/stages/stage_of.cpp`).

### 5.1 Immediate Unit and operand fetch

* Decodes the opcode and the register indices from the instruction word.
* **Immediate Unit** decodes `imm18` for the I-format opcodes (`addi … st`):
  modifier `00` → sign-extended 16-bit, `01` (`u`) → zero-extended,
  `10` (`h`) → `value << 16`.
* **Register File** reads `GPR[rs1]` into `op1`. The B operand is
  `isSt ? GPR[rd] : GPR[rs2]` (a store's data register is `rd`).
* For the privileged **`movz`**, both operands are register-window entries:
  the destination index is `rd`, and the source is read by `readWindow(rs2)`
  (`r0`, `sp`, `oldPC`, `oldSP`, `oldFlags`, `eBase`, `eCode`).

### 5.2 Control Unit (Microcontrol)

* When the microsequencer is idle, `Begin(opcode)` points `upc_` at the start of
  the opcode's micro-sequence; then `Step()` returns the `ControlSignals` for
  this cycle (see §2.3).
* `ControlSignals` carries the ALU op, `AluSrcA`/`AluSrcB` select, the writeback
  enable, `isLd`/`isSt`, the scratch-file address, `isStall`, and the
  branch/privileged bits (`isUbranch`, `isBeq`, `isBgt`, `isCall`, `isRet`,
  `isRetz`, `isMovz`/`movzDst`).

### 5.3 RAS — **RAS Register Unit**

`Ras` (`src/components/ras.cpp`), a 16-entry LIFO with a top-of-stack pointer:

* `call` → **push** `PC + 4` (`isCall`).
* `ret` → **pop**; the popped address drives `RAS Out` into the IF main mux
  (`isRet`).
* Both enables are gated by the **Branch Lock Unit** (`branchLock`), so a
  wrong-path `call`/`ret` cannot move the TOS.
* A push onto a full stack raises `rasOverflow` (an exception).

### 5.4 Exception Unit — first marking point

OF checks the decoded opcode and marks the packet:

| Condition | `cause` |
| :--- | :--- |
| unknown opcode (`opcode > ecall`) | `wrongInst` |
| `movz`/`retz` while `CPL == 1` | `lowPriv` |
| `ebreak` | `pbrk` |
| `ecall` | `scall` |
| `call` with a full RAS | `rasOverflow` |

`isRet` / `isRetz` are also exported to IF to drive the PC mux.

---

## 6. Stage **EX** — Execute

Implemented by `StageEX` (`src/stages/stage_ex.cpp`).

### 6.1 **Forwarding Unit**

Before the ALU muxes, register operands are replaced by the most recent value:

* MA (`exMem_.out`) takes priority over WB (`WbResult` from MA-RW).
* Only a **non-load** write in MA is forwardable (a load's MA result is its
  address, not the data); a load's value is forwarded from **WB**.
* `movz` writing `sp` is forwarded as a GPR write.

### 6.2 AluSrcA / AluSrcB muxes

| `AluSrcA` | Source | | `AluSrcB` | Source |
| :---: | :--- | :--- | :---: | :--- |
| `00` | `op1` | | `00` | `op2` |
| `01` | **Constant from ROM** (micro `imm`) | | `01` | macro immediate |
| `10` | scratch-A | | `10` | **Constant from ROM** |
| | | | `11` | scratch-B |

### 6.3 **ALU** and flags

`alu/` is the team's execution unit: `Alu::Execute(AluOp, a, b)` runs one
micro-op through the algorithm-level hardware model (Kogge-Stone adder, Booth +
Dadda multiplier, non-restoring divider, barrel shifter, IEEE-754 FPU). `cmp`
and `fcmp` latch `E/GT/LT`; `div`/`mod` set `DivideByZero()`.

### 6.4 **Scratch File**

The 16 microarchitectural scratch registers are **written in EX** when
`isScratchWb` is set (`scrWriteAddr`, `ScrWriteAdd`). They are read through the
A/B muxes and let the microcode keep temporaries invisible to the macro ISA.

### 6.5 Branch resolution and **Correct PC Recovery Unit**

* `ActualTaken = isUBranch | (isBeq ∧ flags.E) | (isBgt ∧ flags.GT)`.
* `target = PC + (signExtend25(Inst[24:0]) << 2)`.
* `Mispredict = ActualTaken XOR PredictTaken`.
* `CorrectPC = ActualTaken ? target : PC + 4`.
* **Halt idiom:** a taken branch whose `target == PC` (e.g. `b 0`) stops the
  simulation.

EX is also the second **exception marking** point: `div`/`mod` by zero sets
`cause = divByZero`.

---

## 7. Stage **MA-RW** — Memory & Register Writeback

Implemented by `StageMARW` (`src/stages/stage_marw.cpp`).

### 7.1 **Memory Unit** / **Data Memory**

* `mar = aluResult`, `mdr = op2`.
* `isLd` → `LdResult = ReadData(mar)`.
* `isSt` → `WriteData(mar, mdr)`.
* Data memory is byte-addressed but word-granular (`address & ~3`) and sparse.

### 7.2 Writeback mux and Register File

* A 2-to-1 mux driven solely by `isLd`: `isLd ? LdResult : AluResult`.
* If `isWb`, the value is written to `GPR[rd]` and published as `WbResult`
  (`value`, `dest`, `wb`) for the Forwarding Unit. `r0` is hardwired to zero and
  is never a forwarding source.
* `call` writes **no** GPR: the return address lives only in the RAS.

### 7.3 Privileged `movz` destination

If `isMovz`, the ALU result is written through the register window instead
(`writeWindow`): `r0` (discarded), `sp` (→ `r14`, also forwarded), `oldPC`,
`oldSP`, `oldFlags`, `eBase`, `eCode` in `SpecialRegs`.

### 7.4 Predictor inversion — **`1 << 25` / “New Inst.”**

If `mispredict` is set, MA-RW writes back the instruction with its prediction bit
flipped:

```cpp
memory.WriteInstruction(pc, instruction ^ (1u << 25));
```

Instruction memory is writable for exactly this purpose, so a branch learns from
its misprediction in place.

### 7.5 Exception commit

A marked packet drains through MA-RW with `isWb` / `isLd` / `isSt` **suppressed**
(no architectural state is committed). The stage reports the captured
`pc`, `cause` and condition codes to the CPU, which then performs the context
switch (§10).

---

## 8. Control and hazard units

### 8.1 **Data Lock Unit** — load-use interlock

Detects a **load in EX** whose destination is read by the instruction in OF:

```
load.inEX.control.isLd ∧ load.inEX.control.isWb ∧ load.rd ≠ 0
∧ consumer.opcode < B
∧ (load.rd == consumer.rs1 ∨ load.rd == consumer.rs2
   ∨ (consumer.isSt ∧ load.rd == consumer.rd))
```

When true, `isStall` freezes IF (the consumer stays in OF) and injects a bubble
into EX. Next cycle the load's value is available in WB and forwarded.

### 8.2 **Branch Lock Unit**

On `Mispredict` (from EX):

* flush IF/OF (IF) and OF/EX (the younger packet),
* gate the RAS `call`/`ret` enables to 0,
* let the mispredicting instruction continue to MA-RW so it can invert its
  prediction bit.

`ret`/`retz` resolved in OF also perform a one-stage flush of IF/OF.

### 8.3 Forwarding (recap)

MA > WB priority; loads forwarded from WB only; the interlock covers the
remaining load-use window.

### 8.4 PC main mux priority

`exception > mispredict > retz > ret > stall(freeze) > normal`,
as in §4.

---

## 9. Branch prediction

* The predictor is a **1-bit “taken/not-taken” bit stored in `Inst[25]`** of the
  instruction word in instruction memory — no separate table.
* IF predicts and never stalls on branches: it either fetches the predicted
  target (`PC + (offset << 2)`) or falls through to `PC + 4`.
* EX computes `ActualTaken`; `Mispredict = ActualTaken XOR PredictTaken`.
* MA-RW flips `Inst[25]` on a misprediction (`Inst ^ (1 << 25)`).
* A backward loop therefore mispredicts on its first pass, flips the bit to
  “taken”, and runs the remaining iterations without further penalty.

---

## 10. Precise exceptions and privileged instructions

Registers live in `SpecialRegs`: `eBase`, `eCode`, `oldPC`, `oldSP`, `oldFlags`,
and **CPL** (1 = user, 0 = OS). The machine boots in **OS mode** (`CPL = 0`).

**Marking points and causes**

| Source | Stage | `cause` |
| :--- | :--- | :--- |
| `ebreak` | OF | `pbrk` (1) |
| `ecall` | OF | `scall` (2) |
| `div`/`mod` by zero | EX | `divByZero` (3) |
| `call` with full RAS | OF | `rasOverflow` (4) |
| invalid opcode | OF | `wrongInst` (5) |
| `movz`/`retz` with CPL=1 | OF | `lowPriv` (6) |
| asynchronous interrupt | IF | `interrupt` (7) |

Because an older instruction reaches MA-RW first, a synchronous fault naturally
takes priority over any younger one; the commit flush removes the younger ones.

**Precise flow**

1. The marked instruction drains to MA-RW; its writeback / memory side effects
   are suppressed.
2. The CPU captures `PC → oldPC`, `cause → eCode`, condition codes → `oldFlags`,
   and drops CPL to 0.
3. The younger packets are flushed and the PC jumps to `eBase`.
4. The handler runs in OS mode; `retz` reloads `PC ← oldPC` and returns CPL to 1.

**Privileged / exception instructions**

* `movz` — moves values to/from the restricted register window
  (`r0`, `sp`, `oldPC`, `oldSP`, `oldFlags`, `eBase`, `eCode`); the only way to
  touch the special registers.
* `retz` — privileged return from exception (`PC ← oldPC`, `CPL ← 1`).
* `ebreak` / `ecall` — user-mode instructions that raise `pbrk` / `scall`.

`Cpu::RequestInterrupt()` arms an asynchronous interrupt; the next fetched
instruction is marked with `cause = interrupt`.

---

## 11. Worked example — cycle by cycle

Program (word index → address):

| Addr | Instruction |
| :--- | :--- |
| 0x00 | `addi r1, r0, 5` |
| 0x04 | `addi r2, r0, 7` |
| 0x08 | `add r3, r1, r2` |
| 0x0c | `b 0` (halt) |

Trace printed by the simulator (one line per cycle, after the rising edge;
`IF/OF`, `OF/EX`, `EX/MEM` are the three pipeline registers):

```
cyc   1 | next pc=0x4  | IF/OF v 0x3c400005 pred=0 | OF/EX - | EX/MEM - mis=0 exc=0 wb=0 ld=0 st=0
cyc   2 | next pc=0x8  | IF/OF v 0x3c800007 pred=0 | OF/EX v | EX/MEM - mis=0 exc=0 wb=0 ld=0 st=0
cyc   3 | next pc=0xc  | IF/OF v 0x04c48000 pred=0 | OF/EX v | EX/MEM v mis=0 exc=0 wb=1 ld=0 st=0
cyc   4 | next pc=0xc  | IF/OF v 0x8e000000 pred=1 | OF/EX v | EX/MEM v mis=0 exc=0 wb=1 ld=0 st=0
cyc   5 | next pc=0xc  | IF/OF v 0x8e000000 pred=1 | OF/EX v | EX/MEM v mis=0 exc=0 wb=1 ld=0 st=0
cyc   6 | next pc=0xc  | IF/OF v 0x8e000000 pred=1 | OF/EX v | EX/MEM v mis=0 exc=0 wb=0 ld=0 st=0
```

Reading the three registers at the end of each cycle gives the classic pipeline
overlap:

| Cycle | IF calls | OF decodes | EX executes | MA-RW commits |
| :---: | :--- | :--- | :--- | :--- |
| 1 | `addi r1` | — | — | — |
| 2 | `addi r2` | `addi r1` | — | — |
| 3 | `add r3` | `addi r2` | `addi r1` | — |
| 4 | `b 0` (pred=taken) | `add r3` | `addi r2` | `addi r1` → `r1=5` |
| 5 | `b 0` | `b 0` | `add r3` | `addi r2` → `r2=7` |
| 6 | `b 0` | `b 0` | `b 0` | `add r3` → `r3=12`, halt |

`b 0` is predicted taken (bit 25 = 1), so IF keeps the PC at 0x0c; EX confirms
`ActualTaken` and `target == PC`, so the machine halts with `r1=5, r2=7, r3=12`.

---

## 12. Signal glossary

| Signal (SVG / code) | Meaning |
| :--- | :--- |
| `PredictTaken` | IF/OF and OF/EX: branch predicted taken = `isBranch ∧ Inst[25]` |
| `ActualTaken` | EX: branch is really taken from the flags |
| `Mispredict` | `ActualTaken XOR PredictTaken` |
| `CorrectPC` | EX recovery target |
| `NextPC` | predicted target or `PC + 4` |
| `RAS Out` / `TOS` | top of the return-address stack |
| `isCall` / `isRet` / `isRetz` | RAS push / pop / return-from-exception |
| `isStall` | freeze IF (microcode stall or load-use) |
| `AluSrcA` / `AluSrcB` | operand mux selects (see §6.2) |
| `ScrWriteAdd` / `isScratchWB` | Scratch File write index / enable |
| `isWb` / `isLd` / `isSt` | register write / load / store |
| `mar` / `mdr` | memory address / store data |
| `LdResult` | data-memory read value |
| `isUBranch` / `isBeq` / `isBgt` | branch kinds feeding `ActualTaken` |
| `flags.E` / `flags.GT` | condition codes from `cmp` / `fcmp` |
| `isMovz` / `movzDst` | privileged move and its window destination |
| `1 << 25` / “New Inst.” | predictor inversion in MA-RW |

---

## 13. SVG ↔ code cross-reference

| SVG label | Code | File |
| :--- | :--- | :--- |
| IF/OF Pipeline Register (Inst. Packet) | `IfOfPacket` / `PipelineRegister` | `include/common/pipeline_registers.hpp` |
| OF/EX | `OfExPacket` | `include/common/pipeline_registers.hpp` |
| EX/MA-RW | `ExMemPacket` | `include/common/pipeline_registers.hpp` |
| Instruction Memory / PC / main mux | `StageIF`, `Memory::FetchInstruction` | `src/stages/stage_if.cpp`, `src/components/memory.cpp` |
| Immediate Unit | immediate decode in `StageOF::Run` | `src/stages/stage_of.cpp` |
| Register File | `RegFile` | `src/components/reg_file.cpp` |
| Control Unit (Microcontrol) | `ControlUnit` | `src/components/control_unit.cpp` |
| RAS Register Unit | `Ras` | `src/components/ras.cpp` |
| Data Lock Unit | `loadUse` in `Cpu::Step` | `src/core/cpu.cpp` |
| Branch Lock Unit | mispredict flush / RAS gating in `Cpu::Step` | `src/core/cpu.cpp` |
| Forwarding Unit | `forwardValue` in `StageEX` | `src/stages/stage_ex.cpp` |
| ALU | `Alu` | `../alu/src/components/alu.cpp` |
| Scratch File | `ScratchFile` | `src/components/scratch_file.cpp` |
| Correct PC Recovery Unit | `StageEX::Run` (target / correctPC) | `src/stages/stage_ex.cpp` |
| Memory Unit / Data Memory | `StageMARW`, `Memory` | `src/stages/stage_marw.cpp` |
| Writeback mux (`isLd`) | `StageMARW::Run` | `src/stages/stage_marw.cpp` |
| Predictor inversion (`1<<25`) | `StageMARW::Run` | `src/stages/stage_marw.cpp` |
| Special registers / CPL | `SpecialRegs` | `src/components/special_regs.cpp` |

---

## Appendix A — Current implementation deviations

These are places where the tree currently differs from the figures above; they
are listed for accuracy, not as part of the intended design.

1. **Control-unit tap timing.** The figures place the (latched) Control Unit on
   the **input** side of the IF/OF register, so a fetched instruction's control
   word is ready when it reaches OF. The code currently drives the controller
   from `ifOf.out` inside OF and latches the result into OF/EX
   (`stage_of.cpp` → `control.Step()`). This is the pending change.
2. **`AluOp::PASS` operand.** The controller routes the move source to the
   **B** operand (`mov`/`movz` → `op2`, `movi` → immediate), but `alu.cpp`
   returns the **A** operand for `PASS`. As a result `mov`/`movi`/`movz`
   currently write the wrong value.
3. **`xor` / `xori`.** `mxor`/`mxori` and `AluOp::XOR` exist, but `alu.cpp` has
   no `case AluOp::XOR`; it returns 0 (the compiler warns about the unhandled
   enumerator).
4. **Two `micro_types.hpp` copies.** `alu/include/common/micro_types.hpp` is an
   older snapshot (35-opcode map, no `XOR`, different `AluSrcA`); the simulator
   uses `processor_sim4/include/common/micro_types.hpp` (44 opcodes). They should
   be collapsed into one source of truth.
5. **Assembler branch mask.** `assemblerPasses.cpp` masks the offset with
   `0x3FFFFFF` (26 bits) before setting bit 25 for `b`/`call`; for a negative
   offset this sets bit 25 for conditional branches too. It should mask
   `0x1FFFFFF` (25-bit offset) and set bit 25 only for `b`/`call`.

---

## Appendix B — Trace format

`--trace` prints one line per cycle **after the rising edge**, i.e. the register
contents the *next* cycle will consume:

```
cyc N | next pc=0x<hex> | IF/OF <v|-> 0x<inst> pred=<0|1> [!exc]
      | OF/EX <v|-(bubble)> | EX/MEM <v|-> mis=<0|1> exc=<0|1>
      wb=<0|1> ld=<0|1> st=<0|1>
```

`DumpState` then reports cycles, halt status, mispredictions, load-use stalls,
exceptions, `CPL`, `eCode`, `oldPC`, the register files and data memory.

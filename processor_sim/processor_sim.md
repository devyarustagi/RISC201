# processor_sim — 4-Stage Pipelined Processor

A cycle-accurate C++ simulator of the 4-stage pipelined datapath drawn in
[`../4StagePipeline(final).svg`](../4StagePipeline(final).svg)
and laid out in
[`../pipeline/directoryStructure.jpg`](../pipeline/directoryStructure.jpg).

The pipeline is **microcoded**: it consumes the two ROM images produced by the
[`../microassembler`](../microassembler) (`microprograms.rom` and
`rom_address_table.rom`) and runs a binary program produced by the
[`../assembler`](../assembler).

```
  IF                    OF                      EX                    MARW
+-------------+   +----------------+     +-------------------+   +-------------------+
| PC          |   | Reg File       |     | Forwarding Unit   |   | Data memory       |
| Instr. Mem  |-->| Imm / Branch U.|---->| AluSrcA / AluSrcB |-->| writeback mux     |
| Predictor   |   | RAS   + Ctrl   |     | ALU + flags       |   | GPR write (rd)    |
| PC mux      |   | (ID_PC + 4)    |     | Scratch write     |   | predictor write   |
+-------------+   +----------------+     | Correct PC recov. |   +-------------------+
       ^                                  +-------------------+
       +------- CorrectPC | RAS Out | NextPC  (main PC mux) ------+
```

The ALU itself is the team's project under [`../alu`](../alu) (Kogge-Stone
adder, radix-4 Booth multiplier, non-restoring divider, barrel shifter, IEEE-754
FPU); the simulator compiles it directly.

## Stages

| Stage | File | Responsibility |
| :--- | :--- | :--- |
| **IF** | `include/stages/stage_if.hpp` | Program counter, predictive branch-target sub-mux (`PredictTaken = isBranch ∧ Inst[25]`), main PC mux (`CorrectPC` → `RAS Out` → `NextPC`), instruction fetch, mispredict / `ret` flushes. |
| **OF** | `include/stages/stage_of.hpp` | Register read + operand muxes, Immediate Unit (field decode, immediate-modifier decode), **linear RAS** (push on call, pop on ret, branch-lock gated), microcoded Control Unit. No branch target is computed here. |
| **EX** | `include/stages/stage_ex.hpp` | **Forwarding Unit** into the `AluSrcA`/`AluSrcB` muxes, ALU + flags, **Scratch File write**, `ActualTaken` / `Mispredict` / `CorrectPC` (the corrected branch target is computed here). |
| **MARW** | `include/stages/stage_marw.hpp` | Memory access (`mar`/`mdr`), a 2-to-1 GPR writeback mux driven by `isLd`, and the **in-memory predictor inversion** (`Inst ⊕ (1<<25)`). |

Pipeline registers: `IF/OF` (`pc`, `instruction`, `predictTaken`), `OF/EX`
(`pc`, `instruction`, `imm`, `op1`, `op2`, `rd`, `rs1`, `rs2`, `predictTaken`,
control), `EX/MARW` (`pc`, `instruction`, `mispredict`, `op2`, `aluResult`, `rd`,
control).

## Directory layout

```
processor_sim/
├── Makefile
├── main.cpp
├── include/
│   ├── common/{micro_types,pipeline_registers}.hpp
│   ├── components/{control_unit,reg_file,scratch_file,memory,ras}.hpp
│   ├── stages/{stage_if,stage_of,stage_ex,stage_marw}.hpp
│   └── core/cpu.hpp
└── src/…            # one .cpp per header above
```

The ALU (`include/components/alu.hpp`, `alu_units.hpp`, …) lives in `../alu`
and is compiled by the Makefile.

## Build & run

```sh
make            # builds ./processor_sim (compiles ../alu too)
make roms       # builds ../microassembler and generates build/*.rom from its microcode
make alu-test   # builds and runs ../alu/tests/alu_test.cpp
```

Assemble a program with the assembler, then run it:

```sh
../assembler/riscasm program.asm -o program.bin
./processor_sim program.bin build/microprograms.rom build/rom_address_table.rom \
                [--max-cycles N] [--trace]
```

- the program binary is a raw sequence of **32-bit little-endian** words;
- `--trace` prints one line per cycle (PC, stage occupancy, wb/ld/st);
- execution stops on the halt idiom (a taken branch to its own PC, e.g. `b 0`)
  or after `--max-cycles` cycles.

## Microcoded control

`ControlUnit::Load` reads `rom_address_table.rom` (44 bytes, one start address
per macro opcode `0x00–0x2B`) and unpacks `microprograms.rom` (a continuous
**MSB-first** bit stream of 38-bit words) into `Microinstruction`s.

For every macroinstruction entering OF the unit does `Begin(opcode)` (jumping to
`addressTable[opcode]`) and then one `Step()` per cycle. Each microinstruction is
translated into `ControlSignals`, and its `stall` bit becomes `isStall`, which
freezes IF while a multi-micro-op sequence is still running.

Register operands use the tag convention from `microinstructions.md`: a `0`
field means "the macroinstruction's `rd`/`rs1`/`rs2`", while a set MSB means
scratch register `s(n)`.

### Micro opcode parity

Micro opcodes are assigned by parity so the decoder picks the two forms with a
single bit test — `bool immediateForm = (opcode & 1) == 1;`:

| Even (register form) | Odd (immediate form) |
| :--- | :--- |
| `madd` `msub` `mmul` `mdiv` `mmod` `mand` `mor` `mxor` `mlsl` `mlsr` `masr` | `maddi` `msubi` `mmuli` `mdivi` `mmodi` `mandi` `mori` `mxori` `mlsli` `mlsri` `masri` |
| `mcmp` `mnot` `mmov` `mfadd` `mfsub` `mfmul` `mfdiv` `mfcmp` `mret` | `mcmpi` `mmovi` `mld` `mst` `mb` `mbeq` `mbgt` `mcall` |

The privileged / exception micro-ops (`mmovz`=0x28, `mretz`=0x29, `mebreak`=0x2A,
`mecall`=0x2B) are decoded explicitly rather than by the parity rule.

The complete table lives in `microassembler/microassembler.cpp`; the simulator
mirrors it in `include/common/micro_types.hpp` (`namespace MicroOp`).
`mmov` is the register move (`rd = rs2`), `mmovi` the immediate move
(`rd = imm`); both are implemented as an ALU pass-through of the selected B
operand (parity chooses `op2` vs the macro immediate).

### Macroinstruction immediate format

An I-format macroinstruction carries an 18-bit immediate laid out as
`[17:16] modifier | [15:0] value`. The Immediate Unit in the OF stage decodes it:

| Modifier | Written as | Meaning |
| :---: | :--- | :--- |
| `00` | `5`, `-1` | signed 16-bit (sign-extended) |
| `01` | `u10` | unsigned 16-bit (zero-extended) |
| `10` | `h123` | value shifted left by 16 (high half of a 32-bit constant) |

Modifier `11` is rejected by the assembler. Branch-format instructions carry a
25-bit signed **word** offset in bits `[24:0]`; bit `[25]` is the processor's
dynamic branch-prediction bit (the hardware does `PC + (offset << 2)`).

### Mux encodings

| Signal | Values |
| :--- | :--- |
| `AluSrcA` | `00` op1 · `01` constant-from-ROM · `10` scratch-A |
| `AluSrcB` | `00` op2 · `01` macro immediate · `10` constant-from-ROM · `11` scratch-B |
| MARW writeback data | 2-to-1 mux, driven by `isLd`: `LdResult` (isLd) or `AluResult` |

Operand / address multiplexers:

- **OF**: `op1 = GPR[rs1]`, `op2 = isSt ? GPR[rd] : GPR[rs2]`
- **EX**: the register-operand inputs of the `AluSrcA`/`AluSrcB` muxes come from
  the Forwarding Unit (below); `st` puts `GPR[rd]` (its data register) into op2,
  and the corrected branch target (`PC + (offset25 << 2)`) is computed here.
- **MARW**: destination = `rd`; write data = the 2-to-1 mux output

## Hazard handling

- **Data Lock Unit (load-use interlock).** When a load is in EX and the
  instruction in OF reads its `rd`, `isStall` freezes the PC and the IF/OF
  register and injects a one-cycle bubble into EX. It is OR-ed with the
  microcode's `Control_Stall`.
- **Forwarding Unit.** The instruction in MA (`exMem.out`) takes priority over
  the one in WB (`WbResult`); both feed the register-operand inputs of the
  `AluSrcA`/`AluSrcB` muxes. A load's MA result is only its address, so loads
  are forwardable from *WB* only — their consumers are covered by the interlock
  plus the transparent register file.
- **Branch Lock Unit.** A misprediction flushes IF/OF (StageIF) and OF/EX, and
  forces the RAS call/ret enables to 0 so a wrong-path `call`/`ret` cannot move
  the TOS. An `isRet` performs a one-stage flush of IF/OF.

## Branch prediction

- `PredictTaken = isBranchOpcode ∧ Inst[25]`. The prediction bit lives inside
  instruction memory (the assembler initialises it to 0 = predict-not-taken).
- EX evaluates `ActualTaken` and asserts `Mispredict = ActualTaken ⊕
  PredictTaken`; on a mismatch it redirects the PC to `CorrectPC` and MA-RW
  flips the bit (`Inst ⊕ (1<<25)`) back into instruction memory. A backward loop
  therefore mispredicts only on its first iteration.

## Exceptions

The Exception Unit supports precise exceptions. Every packet carries
`isException` / `cause` (4-bit `ExcCode`) and a snapshot of the condition codes.

| Source | Raised in | `cause` |
| :--- | :--- | :--- |
| `ebreak` | OF | `pbrk` (1) |
| `ecall` | OF | `scall` (2) |
| `div`/`mod` by zero | EX (ALU) | `divByZero` (3) |
| `call` with a full RAS | OF | `rasOverflow` (4) |
| invalid opcode | OF | `wrongInst` (5) |
| `movz`/`retz` while CPL = 1 | OF | `lowPriv` (6) |
| hardware interrupt (API) | IF | `interrupt` (7) |

Marking happens in the stage that detects the fault; because the older (later
stage) instruction reaches MA-RW first, a synchronous mark naturally wins over
any younger one, which the commit flush removes.

**Flow.** A marked instruction drains to MA-RW with its `isWb` / `isLd` / `isSt`
side effects suppressed. There the CPU captures `PC → oldPC`, `cause → eCode`
and the packet's condition codes `→ oldFlags`, drops `CPL` to 0, flushes the
three younger instructions, and jumps the PC to `eBase`. The handler runs in OS
mode; `retz` reloads `PC` from `oldPC` and raises `CPL` back to 1.

**Special registers & privilege.** `SpecialRegs` holds `eBase`, `eCode`, `oldPC`,
`oldSP`, `oldFlags` and `CPL` (1 = user, 0 = OS). The CPU boots in OS mode
(`CPL = 0`) so the boot code can set `eBase`; `retz` drops to user mode. The
privileged `movz` reaches these through the restricted register window
(`r0`, `sp` = r14, `oldPC`, `oldSP`, `oldFlags`, `eBase`, `eCode`) — it is the
only way to move values to/from the special registers. `ebreak`/`ecall` are
ordinary (user-mode) instructions; `movz`/`retz` are privileged.

`Cpu::RequestInterrupt()` arms an asynchronous interrupt; the next fetched
instruction is marked with `cause = interrupt`.

## Assumptions & limitations

- The RAS holds `RAS_DEPTH` (16) entries; a push onto a full stack sets
  `OverflowPending()`, which the OF stage turns into an `rasOverflow` exception.
- **Store data** is `op2` — `GPR[rd]` for a store, forwarded in EX like any
  other register operand.
- **`call` writes no register**: the return address (`ID_PC + 4`) lives only in
  the RAS. **`ret`** returns through `RAS Out`; **`retz`** through `oldPC`.
- The **Scratch File** reads feed the ALU A/B muxes; it is **written in EX**.
- Branch targets are `PC + (offset25 << 2)`: predicted in IF, recovered in EX.
  Instruction memory is word-addressed (`ram[pc/4]`); data memory is
  byte-addressed but word-granular and sparse.
- The ALU implements `fadd/fsub/fmul/fdiv/fcmp` (plus the unconsumed
  `itof`/`ftoi`); the microcode dispatches to them.
- `sp` is GPR **r14**; there is no link register (`call`/`ret` use the RAS). The
  exception handler base `eBase` and `eCode` were added to the `movz` window (the
  spec lists only `r0`/`sp`/`oldPC`/`oldSP`/`oldFlags`) so the OS can set and
  read them.

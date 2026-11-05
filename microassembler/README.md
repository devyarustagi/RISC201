# Microassembler

Turns a hand-written **microassembly** source file (`.masm`) into the two ROM
images that drive the microprogrammed control unit of the RISC201 processor:

- **`microprograms.rom`** — the assembled microinstruction sequences, bit-packed
  into one continuous stream.
- **`rom_address_table.rom`** — a lookup table mapping each macroinstruction
  opcode to the start address of its sequence inside `microprograms.rom`. This is
  what the switch unit reads to dispatch a fetched macroinstruction.

The microinstruction set and the macroinstruction → microinstruction mapping are
defined in [`microinstructions.md`](microinstructions.md).

## Building

```sh
make            # builds ./microasm
make test       # builds and assembles the bundled microcode.masm into build/
```

Or compile directly:

```sh
g++ -std=c++17 -Wall -Wextra -O2 -o microasm microassembler.cpp main.cpp
```

## Usage

```sh
./microasm <input.masm> [-o <output-directory>]
```

- Without `-o`, both ROM files are written to the current directory.
- With `-o`, they are written into the given directory (which must exist).

Example:

```sh
./microasm microcode.masm -o build
# build/microprograms.rom
# build/rom_address_table.rom
```

The program prints a diagnostic to `stderr` and exits with a non-zero status on
any malformed input (unknown mnemonic, bad operand, missing label, duplicate
label, oversized microprogram, ...).

## Writing a `.masm` file

A `.masm` file is a flat list of **labels**; each label begins with `.` and is
followed by the microinstruction sequence that implements that macroinstruction.
A ready-to-assemble copy of the spec's mapping table is provided in
[`microcode.masm`](microcode.masm):

```
.nop
    maddi s15, s15, 0
.add
    madd rd, rs1, rs2
```

- Everything from a `#` to the end of the line is a comment.
- Blank lines are ignored.
- Label names are the macroinstruction mnemonics (`add`, `subi`, `ld`, ...),
  written with a leading dot. A trailing `:` is tolerated (`.add:`).
- The label marks the address of the **next** microinstruction. A label with no
  instructions before the next label simply shares that address.

**Every active macro opcode (0x00–0x27, i.e. `nop` through `ret`) must have a
label**, because `rom_address_table.rom` contains one entry per opcode. The full
required set is:

```
nop add sub mul div mod and or xor lsl lsr asr not mov cmp
addi subi muli divi modi andi ori xori lsli lsri asri movi cmpi ld st
fadd fsub fmul fdiv fcmp
b beq bgt call ret
movz retz ebreak ecall
```

### Operands

| Kind | Written as | Encoding |
| :--- | :--- | :--- |
| Macro register | `rd`, `rs1`, `rs2` | `0b00000` — the actual register is supplied by the macroinstruction at run time |
| Scratch register | `s0` … `s15` | `0b10000 \| n` (MSB set = scratch) |
| Macro immediate | `imm` | `0` — placeholder resolved from the macroinstruction at run time |
| Micro immediate | a literal (`5`, `-1`, `0x1F`, `mimm 5`) | signed value truncated to 16 bits |

The `mimm` keyword is optional and only marks that the literal is the
microinstruction's own immediate (`mimm 5` is identical to `5`).

Memory operands are written as `rd, rs1, imm`, e.g. `mld rd, rs1, imm` and
`mst rd, rs1, imm`.

### Microinstructions

Opcodes are assigned **by parity** so the decoder can pick the two forms with a
single bit test (`opcode & 1`): **even = register form** (second ALU operand is a
register), **odd = immediate form** (second operand is an immediate).
Paired mnemonics therefore sit next to each other.

| Opcode | Mnemonic | Operands | Opcode | Mnemonic | Operands |
| :---: | :--- | :--- | :---: | :--- | :--- |
| `0x00` | `madd`  | `rd, rs1, rs2` | `0x01` | `maddi` | `rd, rs1, imm` |
| `0x02` | `msub`  | `rd, rs1, rs2` | `0x03` | `msubi` | `rd, rs1, imm` |
| `0x04` | `mmul`  | `rd, rs1, rs2` | `0x05` | `mmuli` | `rd, rs1, imm` |
| `0x06` | `mdiv`  | `rd, rs1, rs2` | `0x07` | `mdivi` | `rd, rs1, imm` |
| `0x08` | `mmod`  | `rd, rs1, rs2` | `0x09` | `mmodi` | `rd, rs1, imm` |
| `0x0A` | `mand`  | `rd, rs1, rs2` | `0x0B` | `mandi` | `rd, rs1, imm` |
| `0x0C` | `mor`   | `rd, rs1, rs2` | `0x0D` | `mori`  | `rd, rs1, imm` |
| `0x0E` | `mxor`  | `rd, rs1, rs2` | `0x0F` | `mxori` | `rd, rs1, imm` |
| `0x10` | `mlsl`  | `rd, rs1, rs2` | `0x11` | `mlsli` | `rd, rs1, imm` |
| `0x12` | `mlsr`  | `rd, rs1, rs2` | `0x13` | `mlsri` | `rd, rs1, imm` |
| `0x14` | `masr`  | `rd, rs1, rs2` | `0x15` | `masri` | `rd, rs1, imm` |
| `0x16` | `mcmp`  | `rs1, rs2`      | `0x17` | `mcmpi` | `rs1, imm` |
| `0x18` | `mnot`  | `rd, rs1`       | `0x19` | `mmovi` | `rd, imm` |
| `0x1A` | `mfadd` | `rd, rs1, rs2`  | `0x1B` | `mld`   | `rd, rs1, imm` |
| `0x1C` | `mfsub` | `rd, rs1, rs2`  | `0x1D` | `mst`   | `rd, rs1, imm` |
| `0x1E` | `mfmul` | `rd, rs1, rs2`  | `0x1F` | `mb`    | *(none)* |
| `0x20` | `mfdiv` | `rd, rs1, rs2`  | `0x21` | `mbeq`  | *(none)* |
| `0x22` | `mfcmp` | `rs1, rs2`      | `0x23` | `mbgt`  | *(none)* |
| `0x24` | `mret`  | *(none)*        | `0x25` | `mcall` | *(none)* |
| `0x26` | `mmov`  | `rd, rs2`       |        |         |         |
| `0x28` | `mmovz` | `rd, rs2`       | `0x29` | `mretz` | *(none)* |
| `0x2A` | `mebreak` | *(none)*      | `0x2B` | `mecall`| *(none)* |

`mmov` is the register move (`rd = rs2`) and `mmovi` the immediate move
(`rd = imm`); they are distinct microinstructions.

To change an assignment, edit the single table in `microassembler.cpp`.

## Microinstruction encoding (38 bits)

```
bit:  37        32 31     27 26     22 21     17 16                1 0
     +------------+---------+---------+---------+-------------------+---+
     |  opcode 6  | dest 5  | src1 5  | src2 5  |   immediate 16    | s |
     +------------+---------+---------+---------+-------------------+---+
```

- **opcode** — the microinstruction's 6-bit opcode (table above).
- **dest / src1 / src2** — 5-bit register fields: `0b00000` for a macro register
  (`rd`/`rs1`/`rs2`), or `0b10000 | n` for scratch register `s<n>`.
- **immediate** — signed 16-bit immediate, two's complement.
- **s (stall)** — `1` while the instruction-fetch stage must stall; `0` on the
  final microinstruction of a macroinstruction's sequence. The assembler clears
  it automatically on the last microinstruction before the next label (or EOF).

## Output files

### `microprograms.rom`

The 38-bit words are written in program order as one continuous **MSB-first**
bit stream, packed into bytes (bit 7 of each byte is written first). There is no
padding *between* words; if the total bit count is not a multiple of 8, the final
byte is zero-padded on the right. A reader must therefore decode 38 bits at a
time from the stream rather than seek to byte-aligned words.

### `rom_address_table.rom`

Exactly **44 bytes**, one per active macro opcode in order from `0x00` to
`0x2B`:

```
byte 0 -> opcode 0x00 (nop)
byte 1 -> opcode 0x01 (add)
...
byte 43 -> opcode 0x2B (ecall)
```

Each byte is the 8-bit start index of that macroinstruction's sequence within
`microprograms.rom`. Because addresses are one byte, the assembled microprogram
is limited to 256 microinstructions.

## Assumptions

- `mod`/`modi` (0x05/0x13) and `xor`/`xori` (0x08/0x16) were added, so the
  macro opcode map now runs `0x00`-`0x2B`: branch/control opcodes are
  `b`=0x23, `beq`=0x24, `bgt`=0x25, `call`=0x26, `ret`=0x27, then the
  privileged/exception instructions `movz`=0x28, `retz`=0x29, `ebreak`=0x2A,
  `ecall`=0x2B.
- `itof`/`ftoi` are no longer part of the macro ISA; the ALU still implements
  them, but no macro opcode dispatches to them.
- `mcall` and `mret` take no operands in the mapping table; any offset/return
  address is handled by the datapath rather than encoded here.
- Micro opcodes are assigned sequentially from `0x00` in mapping-table order.

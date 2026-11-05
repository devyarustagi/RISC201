#### Encoding of a microinstruction:
`opcode ( 6 bits) | destination register (5 bits) | first source register (5 bits) | second source register (5 bits) |
signed immediate (16 bits) | stall signal (1 bit) |`
Specification of the purposes of the above fields:
- opcode: It stores the opcode of the microinstruction.
- destination register: if the microinstruction uses rd provided in the microinstruction, then this field will be zeroed out, else its MSB will be 1 which signals that the microinstruction uses a scratch register and the rest of the bits refer to the number of the scratch register used as the destination register for the computation.
- first/second source register: same specification as the destination register, except for the fact that these fields are analogous to rs1 and rs2 as in the case of macroinstructions .
- signed immediate: As the name suggests, this is the immediate which may be used by certain microinstructions. Its size has been kept 16 so that in case a microinstruction needs to use a larger immediate such as a 32 bit immediate, it can be further broken down into smaller microinstructions, which with the help of a few bitwise operations can use two 16 bit immediates to populate an entire 32 bit register.
- stall signal: This signal is set to 0 if the IF stage needs to fetch the next instruction. Typically it will be 0 only in the last microinstruction for a particular macroinstruction. For the other microinstructions in the sequence, this field will be set to 1.

- - - 

For the purposes of writing microassembly code, we label the registers given in the macroinstruction as rd, rs1 and rs2. Scratch registers are labelled as s0 to s15.For distinguishing between the immediate in the macroinstruction and the microinstruction, we have 'imm' and 'mimm'.

Unique ideas:
- Instead of having a dedicated microinstruction for nop, we just use maddi, s15, s15, 0.

## Macroinstruction to Microinstruction Mapping

| Macroinstruction | Microinstruction Sequence |
| :--- | :--- |
| `nop` | `maddi s15, s15, 0` |
| `add rd, rs1, rs2` | `madd rd, rs1, rs2` |
| `sub rd, rs1, rs2` | `msub rd, rs1, rs2` |
| `mul rd, rs1, rs2` | `mmul rd, rs1, rs2` |
| `div rd, rs1, rs2` | `mdiv rd, rs1, rs2` |
| `mod rd, rs1, rs2` | `mmod rd, rs1, rs2` |
| `and rd, rs1, rs2` | `mand rd, rs1, rs2` |
| `or rd, rs1, rs2` | `mor rd, rs1, rs2` |
| `xor rd, rs1, rs2` | `mxor rd, rs1, rs2` |
| `lsl rd, rs1, rs2` | `mlsl rd, rs1, rs2` |
| `lsr rd, rs1, rs2` | `mlsr rd, rs1, rs2` |
| `asr rd, rs1, rs2` | `masr rd, rs1, rs2` |
| `not rd, rs1` | `mnot rd, rs1` |
| `mov rd, rs2` | `mmov rd, rs2` |
| `cmp rs1, rs2` | `mcmp rs1, rs2` |
| `addi rd, rs1, imm` | `maddi rd, rs1, imm` |
| `subi rd, rs1, imm` | `msubi rd, rs1, imm` |
| `muli rd, rs1, imm` | `mmuli rd, rs1, imm` |
| `divi rd, rs1, imm` | `mdivi rd, rs1, imm` |
| `modi rd, rs1, imm` | `mmodi rd, rs1, imm` |
| `andi rd, rs1, imm` | `mandi rd, rs1, imm` |
| `ori rd, rs1, imm` | `mori rd, rs1, imm` |
| `xori rd, rs1, imm` | `mxori rd, rs1, imm` |
| `lsli rd, rs1, imm` | `mlsli rd, rs1, imm` |
| `lsri rd, rs1, imm` | `mlsri rd, rs1, imm` |
| `asri rd, rs1, imm` | `masri rd, rs1, imm` |
| `movi rd, imm` | `mmovi rd, imm` |
| `cmpi rs1, imm` | `mcmpi rs1, imm` |
| `ld rd, rs1, imm` | `mld rd, rs1, imm` |
| `st rd, rs1, imm` | `mst rd, rs1, imm` |
| `fadd rd, rs1, rs2` | `mfadd rd, rs1, rs2` |
| `fsub rd, rs1, rs2` | `mfsub rd, rs1, rs2` |
| `fmul rd, rs1, rs2` | `mfmul rd, rs1, rs2` |
| `fdiv rd, rs1, rs2` | `mfdiv rd, rs1, rs2` |
| `fcmp rs1, rs2` | `mfcmp rs1, rs2` |
| `b offset` | `mb` |
| `beq offset` | `mbeq` |
| `bgt offset` | `mbgt` |
| `call offset` | `mcall` |
| `ret` | `mret` |
| `movz rd, rs2` (privileged, register window) | `mmovz rd, rs2` |
| `retz` (privileged) | `mretz` |
| `ebreak` | `mebreak` |
| `ecall` | `mecall` |

--- 
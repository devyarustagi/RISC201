#ifndef PROCESSOR_SIM_MICRO_TYPES_HPP
#define PROCESSOR_SIM_MICRO_TYPES_HPP

#include <cstdint>

// Width of the microinstruction word produced by ../microassembler.
constexpr int MICRO_WORD_BITS = 38;

// Architectural register file sizes.
constexpr int GPR_COUNT = 16;
constexpr int SCRATCH_COUNT = 16;

// Depth of the ID/OF linear return-address stack (RAS).
constexpr int RAS_DEPTH = 16;

// Number of entries in rom_address_table.rom (macro opcodes 0x00 - 0x2B).
constexpr int ADDRESS_TABLE_SIZE = 44;

// Macroinstruction opcodes (project ISA).
namespace MacroOp {
constexpr uint8_t NOP  = 0x00;
constexpr uint8_t ADD  = 0x01;
constexpr uint8_t SUB  = 0x02;
constexpr uint8_t MUL  = 0x03;
constexpr uint8_t DIV  = 0x04;
constexpr uint8_t MOD  = 0x05;
constexpr uint8_t AND  = 0x06;
constexpr uint8_t OR   = 0x07;
constexpr uint8_t XOR  = 0x08;
constexpr uint8_t LSL  = 0x09;
constexpr uint8_t LSR  = 0x0A;
constexpr uint8_t ASR  = 0x0B;
constexpr uint8_t NOT  = 0x0C;
constexpr uint8_t MOV  = 0x0D;
constexpr uint8_t CMP  = 0x0E;
constexpr uint8_t ADDI = 0x0F;
constexpr uint8_t SUBI = 0x10;
constexpr uint8_t MULI = 0x11;
constexpr uint8_t DIVI = 0x12;
constexpr uint8_t MODI = 0x13;
constexpr uint8_t ANDI = 0x14;
constexpr uint8_t ORI  = 0x15;
constexpr uint8_t XORI = 0x16;
constexpr uint8_t LSLI = 0x17;
constexpr uint8_t LSRI = 0x18;
constexpr uint8_t ASRI = 0x19;
constexpr uint8_t MOVI = 0x1A;
constexpr uint8_t CMPI = 0x1B;
constexpr uint8_t LD   = 0x1C;
constexpr uint8_t ST   = 0x1D;
constexpr uint8_t FADD = 0x1E;
constexpr uint8_t FSUB = 0x1F;
constexpr uint8_t FMUL = 0x20;
constexpr uint8_t FDIV = 0x21;
constexpr uint8_t FCMP = 0x22;
constexpr uint8_t B    = 0x23;
constexpr uint8_t BEQ  = 0x24;
constexpr uint8_t BGT  = 0x25;
constexpr uint8_t CALL = 0x26;
constexpr uint8_t RET  = 0x27;

// Privileged / exception-raising instructions (RISC-201 exceptions).
constexpr uint8_t MOVZ   = 0x28;   // privileged move through the register window
constexpr uint8_t RETZ   = 0x29;   // privileged return from exception
constexpr uint8_t EBREAK = 0x2A;   // breakpoint (pbrk)
constexpr uint8_t ECALL  = 0x2B;   // system call (scall)
}  // namespace MacroOp

// Microinstruction opcodes (must match microassembler.cpp).
//
// Parity convention: EVEN opcodes are the register form (second operand = op2),
// ODD opcodes are the immediate form (second operand = imm). The
// Control Unit decodes the two forms with a single test:
// `bool immediateForm = (opcode & 1) == 1;`.
namespace MicroOp {
// Even - register form.
constexpr uint8_t MADD  = 0x00;
constexpr uint8_t MSUB  = 0x02;
constexpr uint8_t MMUL  = 0x04;
constexpr uint8_t MDIV  = 0x06;
constexpr uint8_t MMOD  = 0x08;
constexpr uint8_t MAND  = 0x0A;
constexpr uint8_t MOR   = 0x0C;
constexpr uint8_t MXOR  = 0x0E;
constexpr uint8_t MLSL  = 0x10;
constexpr uint8_t MLSR  = 0x12;
constexpr uint8_t MASR  = 0x14;
constexpr uint8_t MCMP  = 0x16;
constexpr uint8_t MNOT  = 0x18;
constexpr uint8_t MFADD = 0x1A;
constexpr uint8_t MFSUB = 0x1C;
constexpr uint8_t MFMUL = 0x1E;
constexpr uint8_t MFDIV = 0x20;
constexpr uint8_t MFCMP = 0x22;
constexpr uint8_t MRET  = 0x24;
constexpr uint8_t MMOV  = 0x26;   // mov rd, rs2 (register move)
// Odd - immediate form.
constexpr uint8_t MADDI = 0x01;
constexpr uint8_t MSUBI = 0x03;
constexpr uint8_t MMULI = 0x05;
constexpr uint8_t MDIVI = 0x07;
constexpr uint8_t MMODI = 0x09;
constexpr uint8_t MANDI = 0x0B;
constexpr uint8_t MORI  = 0x0D;
constexpr uint8_t MXORI = 0x0F;
constexpr uint8_t MLSLI = 0x11;
constexpr uint8_t MLSRI = 0x13;
constexpr uint8_t MASRI = 0x15;
constexpr uint8_t MCMPI = 0x17;
constexpr uint8_t MMOVI = 0x19;   // movi rd, imm
constexpr uint8_t MLD   = 0x1B;
constexpr uint8_t MST   = 0x1D;
constexpr uint8_t MB    = 0x1F;
constexpr uint8_t MBEQ  = 0x21;
constexpr uint8_t MBGT  = 0x23;
constexpr uint8_t MCALL = 0x25;
constexpr uint8_t MMOVZ = 0x28;   // movz rd, rs2 (windowed, register form)
constexpr uint8_t MRETZ = 0x29;   // retz
constexpr uint8_t MEBREAK = 0x2A; // ebreak
constexpr uint8_t MECALL  = 0x2B; // ecall
}  // namespace MicroOp

// A decoded microinstruction, read out of the microprogram ROM.
struct Microinstruction {
    uint8_t opcode = 0;   // 6-bit micro opcode
    uint8_t dst = 0;      // 5-bit dest: 0 = macro rd, MSB set = scratch s(n)
    uint8_t src1 = 0;     // 5-bit src1: 0 = macro rs1, MSB set = scratch s(n)
    uint8_t src2 = 0;     // 5-bit src2: 0 = macro rs2, MSB set = scratch s(n)
    int16_t imm = 0;      // signed 16-bit micro immediate (mimm)
    bool stall = false;   // 1 = more micro-ops follow (freeze IF)

    // Unpacks a 38-bit ROM word:
    // opcode[37:32] dst[31:27] src1[26:22] src2[21:17] imm[16:1] stall[0].
    static Microinstruction decode(uint64_t word) {
        Microinstruction micro;
        micro.opcode = static_cast<uint8_t>((word >> 32) & 0x3F);
        micro.dst    = static_cast<uint8_t>((word >> 27) & 0x1F);
        micro.src1   = static_cast<uint8_t>((word >> 22) & 0x1F);
        micro.src2   = static_cast<uint8_t>((word >> 17) & 0x1F);
        micro.imm    = static_cast<int16_t>((word >> 1) & 0xFFFF);
        micro.stall  = (word & 0x1) != 0;
        return micro;
    }
};

// ALU operation selected by a microinstruction.
enum class AluOp {
    ADD, SUB, MUL, DIV, AND, OR, LSL, LSR, ASR, NOT, CMP,
    FADD, FSUB, FMUL, FDIV, FCMP, PASS,
    MOD,   // remainder from the divider (isMod) - mod / modi
    XOR,   // bitwise exclusive-or (logic unit) - xor / xori
    ITOF,  // int32 -> float (FPU convert)
    FTOI   // float -> int32 (FPU convert)
};

// ALU A-operand select (op1 / constant-from-ROM / scratch-A).
enum AluSrcA : uint8_t { A_OP1 = 0, A_CONST = 1, A_SCRATCH = 2 };
// ALU B-operand select.
enum AluSrcB : uint8_t { B_OP2 = 0, B_IMM = 1, B_CONST = 2, B_SCRATCH = 3 };

// Exception codes written into the instruction packet's `cause` field.
enum ExcCode : uint8_t {
    EXC_NONE = 0,
    EXC_PBRK = 1,        // ebreak
    EXC_SCALL = 2,       // ecall
    EXC_DIV0 = 3,        // div / mod by zero
    EXC_RASOVF = 4,      // return-address stack overflow
    EXC_WRONGINST = 5,   // invalid opcode
    EXC_LOWPRIV = 6,     // privileged instruction with CPL = 1
    EXC_INTERRUPT = 7    // asynchronous hardware interrupt
};

// Restricted register window used by the privileged `movz`. Indices 0/1 are the
// visible GPRs (r0 and sp = r14); the rest are hardware special registers.
enum WindowReg : uint8_t {
    WIN_R0 = 0,
    WIN_SP = 1,
    WIN_OLDPC = 2,
    WIN_OLDSP = 3,
    WIN_OLDFLAGS = 4,
    WIN_EBASE = 5,
    WIN_ECODE = 6
};
constexpr uint8_t GPR_SP = 14;   // the stack pointer is GPR r14

// Control signals emitted by the Control Unit and carried down the pipeline.
struct ControlSignals {
    AluOp aluOp = AluOp::ADD;
    uint8_t aluSrcA = A_OP1;
    uint8_t aluSrcB = B_OP2;

    bool isWb = false;         // write the destination GPR
    bool isScratchWb = false;  // write the Scratch File instead
    bool isLd = false;
    bool isSt = false;
    bool isCall = false;
    bool isRet = false;
    bool isUbranch = false;    // unconditional branch (b / call)
    bool isBeq = false;
    bool isBgt = false;
    bool isStall = false;

    // Privileged / exception instructions.
    bool isMovz = false;       // windowed move; the destination is a window register
    uint8_t movzDst = 0;       // destination window index (WIN_*)
    bool isRetz = false;       // return from exception (PC <- oldPC, CPL <- 1)

    uint8_t scrWriteAddr = 0;      // Scratch File write index
    uint8_t src1ScratchAddr = 0;   // Scratch File read index for the A operand
    uint8_t src2ScratchAddr = 0;   // Scratch File read index for the B operand
    int32_t microImm = 0;          // constant from ROM (the microinstruction's imm)
};

#endif

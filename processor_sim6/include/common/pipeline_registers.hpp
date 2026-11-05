#ifndef PROCESSOR_SIM6_PIPELINE_REGISTERS_HPP
#define PROCESSOR_SIM6_PIPELINE_REGISTERS_HPP

#include <cstdint>

#include "common/micro_types.hpp"
#include "components/ras.hpp"

// Edge-triggered pipeline register: stages write `in` during a cycle, then
// `latch()` copies it to `out` for the next cycle.
template <typename T>
struct PipelineRegister {
    T out{};
    T in{};

    void latch() { out = in; }
    void reset() { out = T{}; in = T{}; }
};

// The six stages of 6StagePipeline.svg and the five registers between them:
//
//   IF --IF/IBU--> IBU --IBU/RF--> RF --RF/EX--> EX --EX/MA--> MA --MA/RW--> RW
//
//   IF   PC, instruction memory, branch prediction (Inst[25]), main PC mux
//   IBU  Control Unit (microcode), Immediate Unit, RAS; exception marking
//   RF   Register File read, operand muxes (rd for a store), window read for movz
//   EX   Forwarding Unit, AluSrcA/B muxes, ALU, Scratch File, Correct PC recovery
//   MA   Memory Unit (mar / mdr), Data Memory, predictor inversion (Inst ^ 1<<25)
//   RW   writeback mux (LdResult / AluResult) into the Register File
//
// Every packet carries two copies of the RAS: the stack just before and just
// after its own IBU step. The RAS is updated speculatively in IBU, three stages
// before a branch is resolved and five before an exception is taken, so a
// flush restores it from the packet that caused the flush (see Cpu::Step).

// Fields shared by every packet after IF.
struct PacketBase {
    bool valid = false;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    bool predictTaken = false;
    bool isException = false;   // marked by the Exception Unit
    uint8_t cause = 0;          // ExcCode
};

// IF/IBU - instruction packet.
struct IfIbuPacket : PacketBase {};

// IBU/RF - decoded instruction: register numbers, immediate and control word.
struct IbuRfPacket : PacketBase {
    int32_t imm = 0;            // decoded macro immediate (16-bit signed / u / h)
    uint8_t rd = 0;
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    uint8_t windowSrc = 0;      // movz: register-window source index
    ControlSignals control;
    Ras rasBefore;
    Ras rasAfter;
};

// RF/EX - operands read from the register file.
struct RfExPacket : PacketBase {
    int32_t imm = 0;
    uint32_t op1 = 0;           // GPR[rs1]
    uint32_t op2 = 0;           // GPR[rd] for a store, window value for movz, else GPR[rs2]
    uint8_t rd = 0;
    uint8_t rs1 = 0;            // kept for forwarding / interlocks
    uint8_t rs2 = 0;
    ControlSignals control;
    Ras rasBefore;
    Ras rasAfter;
};

// EX/MA - ALU result, store data (op2), branch outcome, flags snapshot.
struct ExMaPacket : PacketBase {
    bool mispredict = false;
    bool halt = false;          // branch to itself: the program ends when it retires
    uint32_t op2 = 0;           // store data (mdr)
    uint32_t aluResult = 0;     // address (mar) for ld / st
    uint8_t rd = 0;
    bool flagE = false;         // condition codes travel with the packet
    bool flagGt = false;
    bool flagLt = false;
    ControlSignals control;
    Ras rasBefore;
};

// MA/RW - load result and ALU result for the writeback mux.
struct MaRwPacket : PacketBase {
    bool halt = false;
    uint32_t ldResult = 0;
    uint32_t aluResult = 0;
    uint32_t op2 = 0;           // store data (for the debugger's retire view)
    bool stored = false;        // the store committed in MA
    uint8_t rd = 0;
    bool flagE = false;
    bool flagGt = false;
    bool flagLt = false;
    ControlSignals control;
    Ras rasBefore;
};

#endif

#ifndef PROCESSOR_SIM_PIPELINE_REGISTERS_HPP
#define PROCESSOR_SIM_PIPELINE_REGISTERS_HPP

#include <cstdint>

#include "common/micro_types.hpp"

// Edge-triggered pipeline register: stages write `in` during a cycle, then
// `latch()` copies it to `out` for the next cycle.
template <typename T>
struct PipelineRegister {
    T out{};
    T in{};

    void latch() { out = in; }
    void reset() { out = T{}; in = T{}; }
};

// IF/OF - instruction packet. The prediction bit is captured here because the
// fetch stage reads it from instruction memory and the execute stage needs it
// to resolve the branch against `ActualTaken`.
struct IfOfPacket {
    bool valid = false;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    bool predictTaken = false;
    bool isException = false;   // marked by the Exception Unit (async interrupt)
    uint8_t cause = 0;          // ExcCode
};

// OF/EX - the operands the EX stage needs, the decoded immediate, the source
// and writeback registers, the prediction bit and the control word. The branch
// target is not computed here; EX recovers it from the instruction word.
struct OfExPacket {
    bool valid = false;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    int32_t imm = 0;              // decoded macro immediate (16-bit signed / u / h)
    uint32_t op1 = 0;             // GPR[rs1]
    uint32_t op2 = 0;             // mux(rd, rs2): GPR[rd] for a store, else GPR[rs2]
    uint8_t rd = 0;               // writeback destination register
    uint8_t rs1 = 0;              // source registers, kept for forwarding / interlocks
    uint8_t rs2 = 0;
    bool predictTaken = false;
    bool isException = false;     // this instruction is marked
    uint8_t cause = 0;            // ExcCode
    bool flagE = false;           // flags snapshot (condition codes travel with the packet)
    bool flagGt = false;
    bool flagLt = false;
    ControlSignals control;
};

// EX/MARW - ALU result, store data (op2), writeback registers and control. The
// instruction word and mispredict flag travel with the branch so the memory
// stage can invert the prediction bit in instruction memory.
struct ExMemPacket {
    bool valid = false;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    bool mispredict = false;
    uint32_t op2 = 0;             // store data (mdr)
    uint32_t aluResult = 0;
    uint8_t rd = 0;
    bool isException = false;
    uint8_t cause = 0;
    bool flagE = false;
    bool flagGt = false;
    bool flagLt = false;
    ControlSignals control;
};

#endif

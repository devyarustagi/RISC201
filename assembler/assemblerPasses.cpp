#include "assembler.hpp"

#include <stdexcept>

void Assembler::firstPass() {
    std::string line;
    while (std::getline(inputFile, line)) {
        lineNumber++;
        if (line.size() > MAX_LINE_LEN) {
            throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                    " is longer than the maximum allowed " +
                                    std::to_string(MAX_LINE_LEN) + " characters.");
        }

        tokenizer(line);
    }
}

void Assembler::secondPass() {
    for (instructionIR currIR : irList) {
        uint32_t instruction = 0;
        instructionDetails details = instructionTable.at(currIR.mnemonic);
        instructionFields fieldFlags = details.fields;
        uint32_t opcode = details.opcode.to_ulong();
        instruction |= (opcode << 26);
        size_t opIdx = 0;
        if (currIR.mnemonic == "movz") {
            // Both operands are register-window entries (r0, sp, oldPC, oldSP,
            // oldFlags, eBase, eCode), packed into the rd and rs2 fields.
            uint32_t rdWin  = parseWindowRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t srcWin = parseWindowRegister(currIR.operands[opIdx++], currIR.lineNumber);

            instruction |= (rdWin  & 0x0F) << 22;
            instruction |= (srcWin & 0x0F) << 14;

        } else if (fieldFlags == (HAS_RD | HAS_RS1 | HAS_RS2)) {
            uint32_t rd  = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t rs1 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t rs2 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);

            instruction |= (rd  & 0x0F) << 22;
            instruction |= (rs1 & 0x0F) << 18;
            instruction |= (rs2 & 0x0F) << 14;

        } else if (fieldFlags == (HAS_RD | HAS_RS1 | HAS_IMM)) {
            uint32_t rd  = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t rs1 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            int32_t imm  = parseImmediateOrSymbol(currIR.operands[opIdx++], currIR.address, currIR.lineNumber);

            instruction |= (rd  & 0x0F) << 22;
            instruction |= (rs1 & 0x0F) << 18;
            instruction |= (imm & 0x3FFFF);

        } else if (fieldFlags == (HAS_RD | HAS_IMM)) {
            uint32_t rd = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            int32_t imm = parseImmediateOrSymbol(currIR.operands[opIdx++], currIR.address, currIR.lineNumber);

            instruction |= (rd  & 0x0F) << 22;
            instruction |= (imm & 0x3FFFF);

        } else if (fieldFlags == (HAS_RS1 | HAS_IMM)) {
            uint32_t rs1 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            int32_t imm  = parseImmediateOrSymbol(currIR.operands[opIdx++], currIR.address, currIR.lineNumber);

            instruction |= (rs1 & 0x0F) << 18;
            instruction |= (imm & 0x3FFFF);

        } else if (fieldFlags == (HAS_RD | HAS_RS1)) {
            uint32_t rd  = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t rs1 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);

            instruction |= (rd  & 0x0F) << 22;
            instruction |= (rs1 & 0x0F) << 18;

        } else if (fieldFlags == (HAS_RD | HAS_RS2)) {
            uint32_t rd  = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t rs2 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);

            instruction |= (rd  & 0x0F) << 22;
            instruction |= (rs2 & 0x0F) << 14;

        } else if (fieldFlags == (HAS_RS1 | HAS_RS2)) {
            uint32_t rs1 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);
            uint32_t rs2 = parseRegister(currIR.operands[opIdx++], currIR.lineNumber);

            instruction |= (rs1 & 0x0F) << 18;
            instruction |= (rs2 & 0x0F) << 14;

        } else if (fieldFlags == HAS_OFFSET) {
            int32_t offset = parseBranchOffset(currIR.operands[opIdx++], currIR.address, currIR.lineNumber);

            // Bits [24:0]: signed word offset. Bit 25 (the dynamic prediction
            // bit maintained by the processor) is left clear.
            instruction |= (offset & 0x1FFFFFF);

        } else if (fieldFlags == 0) {
        }
        outputFile.write(reinterpret_cast<const char*>(&instruction), sizeof(instruction));
    }
}

#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <bitset>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#define MAX_LINE_LEN 256
#define HAS_RD 1
#define HAS_RS1 2
#define HAS_RS2 4
#define HAS_IMM 8
#define HAS_OFFSET 16

using instructionFields = std::bitset<5>;
using instructionName = std::string;
using instructionOpcode = std::bitset<6>;

struct instructionDetails {
    instructionOpcode opcode;
    instructionFields fields;
};

struct DecodedInstruction {
    uint32_t address;
    size_t index;
    std::string mnemonic;
    std::string operands;
    bool isBranch{false};
    int32_t branchOffset{0};
    uint32_t targetAddress{0};
    std::string labelName;
};

class Disassembler {
private:
    std::ifstream inputFile;
    std::ofstream outputFile;
    static const std::unordered_map<instructionName, instructionDetails> instructionTable;

    std::string decodeRegister(uint32_t reg) const;
    std::string decodeImmediate(uint32_t immediateValue) const;
    int32_t decodeSigned18(uint32_t immediateValue) const;
    DecodedInstruction decodeInstruction(uint32_t instruction, uint32_t address, size_t index) const;

public:
    Disassembler(const std::string& inputFilePath, const std::string& outputFilePath);
    void Disassemble();
};

#endif

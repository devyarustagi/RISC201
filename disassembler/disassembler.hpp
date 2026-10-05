#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <fstream>
#include <string>
#include <unordered_map>
#include <bitset>

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

class Disassembler {
private:
    std::ifstream inputFile;
    std::ofstream outputFile;
    static const std::unordered_map<instructionName, instructionDetails> instructionTable;

public:
    Disassembler(const std::string& inputFilePath, const std::string& outputFilePath);
    void Disassemble();
};

#endif

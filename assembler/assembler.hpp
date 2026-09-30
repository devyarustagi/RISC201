#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <bitset>

#define MAX_LINE_LEN 256
#define HAS_RD 1
#define HAS_RS1 2
#define HAS_RS2 4
#define HAS_IMM 8
#define HAS_OFFSET 16

// this bitset is packed as | offset | immediate | rs2 | rs1 | rd | ;
// where each bit is set if the corresponding field is present in the instruction 
using instructionFields = std::bitset<5>;
using instructionName = std::string;
using instructionOpcode = std::bitset<6>;

struct instructionDetails {
    instructionOpcode opcode;
    instructionFields fields;
};


class Assembler {

private:
    int lc{0};
    std::ifstream inputFile;
    std::ofstream outputFile;
    static const std::unordered_map<instructionName, instructionDetails> instructionTable;

private:
    std::vector<std::string> tokenizer(std::string&);
    void firstPass();

public :
    Assembler(const std::string& inputFilePath, const std::string& outputFilePath);
    void Assemble();
    
};

#endif
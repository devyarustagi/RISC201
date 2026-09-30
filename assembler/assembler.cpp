#include "assembler.hpp"
#include <cstdlib>
#include <stdexcept>


const std::unordered_map<instructionName, instructionDetails> Assembler::instructionTable = {

    // Arithmetic / logic :
    {"nop",  {instructionOpcode(0x00), instructionFields(0)}},
    {"add",  {instructionOpcode(0x01), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"sub",  {instructionOpcode(0x02), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"mul",  {instructionOpcode(0x03), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"div",  {instructionOpcode(0x04), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"and",  {instructionOpcode(0x05), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"or",   {instructionOpcode(0x06), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"lsl",  {instructionOpcode(0x07), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"lsr",  {instructionOpcode(0x08), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"asr",  {instructionOpcode(0x09), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"not",  {instructionOpcode(0x0A), instructionFields(HAS_RD | HAS_RS1)}},
    {"cmp",  {instructionOpcode(0x0B), instructionFields(HAS_RS1 | HAS_RS2)}},

    // Immediate / memory :
    {"addi", {instructionOpcode(0x0C), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"subi", {instructionOpcode(0x0D), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"muli", {instructionOpcode(0x0E), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"divi", {instructionOpcode(0x0F), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"andi", {instructionOpcode(0x10), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"ori",  {instructionOpcode(0x11), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"lsli", {instructionOpcode(0x12), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"lsri", {instructionOpcode(0x13), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"asri", {instructionOpcode(0x14), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"mov",  {instructionOpcode(0x15), instructionFields(HAS_RD | HAS_IMM)}},
    {"cmpi", {instructionOpcode(0x16), instructionFields(HAS_RS1 | HAS_IMM)}},
    {"ld",   {instructionOpcode(0x17), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"st",   {instructionOpcode(0x18), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},

    // Floating-point :
    {"fadd", {instructionOpcode(0x19), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fsub", {instructionOpcode(0x1A), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fmul", {instructionOpcode(0x1B), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fdiv", {instructionOpcode(0x1C), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fcmp", {instructionOpcode(0x1D), instructionFields(HAS_RS1 | HAS_RS2)}},

    // Branch / control flow :
    {"b",    {instructionOpcode(0x1E), instructionFields(HAS_OFFSET)}},
    {"beq",  {instructionOpcode(0x1F), instructionFields(HAS_OFFSET)}},
    {"bgt",  {instructionOpcode(0x20), instructionFields(HAS_OFFSET)}},
    {"call", {instructionOpcode(0x21), instructionFields(HAS_OFFSET)}},
    {"ret",  {instructionOpcode(0x22), instructionFields(0)}},

};


Assembler::Assembler(const std::string& inputFilePath, const std::string& outputFilePath) : inputFile(inputFilePath) {
    if (!inputFile.is_open()) {
        throw std::runtime_error("Error: Could not open file '" + inputFilePath +
                                 "' (it may not exist or lack permissions).");
    }
    outputFile = std::ofstream(outputFilePath);
}

// Splits a single source line into tokens. Strips the comment (everything from
// the first '#'), separates on whitespace, carriage returns and commas, and
// verifies that every token except the first and last is followed by a comma.
std::vector<std::string> Assembler::tokenizer(std::string& line) {
    // Strip a comment if present (the caller does not reuse the line).
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos) {
        line.erase(commentPos);
    }
    // Split the remaining text into tokens, remembering for each token whether
    // a comma followed it (needed for the separator check below).
    std::vector<std::string> tokens;
    std::vector<bool> commaAfter;
    std::string current;
    bool inToken = false;

    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            // Whitespace is a plain separator with no comma.
            if (inToken) {
                tokens.push_back(current);
                commaAfter.push_back(false);
                current.clear();
                inToken = false;
            }
        } else if (c == ',') {
            // A comma both separates tokens and flags the previous one.
            if (inToken) {
                tokens.push_back(current);
                commaAfter.push_back(true);
                current.clear();
                inToken = false;
            } else if (!tokens.empty()) {
                // Covers forms like "r1 , r2" where the token was already flushed.
                commaAfter.back() = true;
            }
        } else {
            current.push_back(c);
            inToken = true;
        }
    }
    // Flush the final token; there is never a comma after it.
    if (inToken) {
        tokens.push_back(current);
        commaAfter.push_back(false);
    }
    // Every token except the first (the instruction name) and the last must be
    // followed by at least one comma.
    for (size_t i = 1; i + 1 < tokens.size(); ++i) {
        if (!commaAfter[i]) {
            throw std::runtime_error("Error: line " + std::to_string(lc) +
                                        ": expected ',' after '" + tokens[i] + "'.");
        }
    }
    return tokens;
}

void Assembler::firstPass() {
    std::string line;
    while (std::getline(inputFile, line)) {
        lc++;
        // Enforce the maximum line length on the raw line; abort if exceeded.
        if (line.size() > MAX_LINE_LEN) {
            throw std::runtime_error("Error: line " + std::to_string(lc) +
                                        " is longer than the maximum allowed " +
                                        std::to_string(MAX_LINE_LEN) + " characters.");
        }

        std::vector<std::string> tokens = tokenizer(line);
    }
}

void Assembler::Assemble() {
    return;
}
#include "assembler.hpp"
#include "stringTrim.hpp"

#include <cctype>
#include <stdexcept>

void Assembler::tokenizer(std::string& line) {
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos) {
        line.erase(commentPos);
        if (line.empty()) {
            return;
        }
    }

    while (true) {
        size_t labelPos = line.find(':');
        if (labelPos == std::string::npos) {
            break;
        }

        std::string label = line.substr(0, labelPos);
        trim(label);
        if (label.empty()) {
            throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                    ": empty label before ':'.");
        }

        for (size_t i = 0; i < label.size(); ++i) {
            if (label[i] == ' ' || label[i] == '\t' || label[i] == '\r') {
                throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                        ": label '" + label + "' has spaces before ':'.");
            }
            if (i == 0 && std::isdigit(static_cast<unsigned char>(label[i]))) {
                throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                        ": label '" + label + "' starts with a number.");
            }
        }
        if (symbolTable.count(label) > 0) {
            throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                    ": label '" + label + "' already defined.");
        }
        symbolTable[label] = lc;

        size_t nextPos = labelPos + 1;
        while (nextPos < line.size() &&
               (line[nextPos] == ' ' || line[nextPos] == '\t' || line[nextPos] == '\r')) {
            ++nextPos;
        }
        line.erase(0, nextPos);

        if (line.empty()) {
            return;
        }
    }

    std::vector<std::string> tokens;
    std::vector<bool> commaAfter;
    std::string current;
    bool inToken = false;

    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (inToken) {
                tokens.push_back(current);
                commaAfter.push_back(false);
                current.clear();
                inToken = false;
            }
        } else if (c == ',') {
            if (inToken) {
                tokens.push_back(current);
                commaAfter.push_back(true);
                current.clear();
                inToken = false;
            } else if (!tokens.empty()) {
                if (commaAfter.back()) {
                    throw std::runtime_error("Syntax Error: line " + std::to_string(lineNumber)
                                            + ": got multiple commas.");
                } else commaAfter.back() = true;
            }
        } else {
            current.push_back(c);
            inToken = true;
        }
    }
    if (inToken) {
        tokens.push_back(current);
        commaAfter.push_back(false);
    }
    for (size_t i = 1; i + 1 < tokens.size(); ++i) {
        if (!commaAfter[i]) {
            throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                    ": expected ',' after '" + tokens[i] + "'.");
        }
    }
    if (tokens.empty()) { return; }
    if (commaAfter.back()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                ": got ',' after end of an instruction.");
    }
    instructionName iName = tokens[0];
    if (iName == "PUSH" || iName == "push" ||
        iName == "POP" || iName == "pop") {
        if (tokens.size() != 2) {
            throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                    ": " + iName + " expects exactly one register operand.");
        }

        const std::string reg = tokens[1];
        parseRegister(reg, lineNumber);

        instructionIR firstInstruction;
        firstInstruction.lineNumber = lineNumber;
        firstInstruction.address = lc;
        firstInstruction.mnemonic = (iName == "PUSH" || iName == "push") ? "subi" : "ld";
        firstInstruction.operands = (iName == "PUSH" || iName == "push")
            ? std::vector<std::string>{"r14", "r14", "4"}
            : std::vector<std::string>{reg, "r14", "0"};
        irList.push_back(firstInstruction);
        lc += 4;

        instructionIR secondInstruction;
        secondInstruction.lineNumber = lineNumber;
        secondInstruction.address = lc;
        secondInstruction.mnemonic = (iName == "PUSH" || iName == "push") ? "st" : "addi";
        secondInstruction.operands = (iName == "PUSH" || iName == "push")
            ? std::vector<std::string>{reg, "r14", "0"}
            : std::vector<std::string>{"r14", "r14", "4"};
        irList.push_back(secondInstruction);
        lc += 4;
        return;
    }

    if (instructionTable.count(iName) == 0) {
        throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                                ": instruction '" + tokens[0] + "' is not recognized.");
    }
    instructionFields currFields = instructionTable.at(iName).fields;
    if (tokens.size()-1 != currFields.count()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNumber) +
                            ": expected " + std::to_string(currFields.count()) + " operands.");
    }

    instructionIR currIR;
    currIR.lineNumber = lineNumber;
    currIR.address = lc;
    currIR.mnemonic = iName;
    for (std::size_t i = 1; i < tokens.size(); i++) {
        currIR.operands.push_back(tokens[i]);
    }

    irList.push_back(currIR);
    lc += 4;
    return;
}

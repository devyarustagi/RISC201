#include "assembler.hpp"
#include <stdexcept>

Assembler::Assembler(const std::string& inputFilePath, const std::string& outputFilePath) : inputFile(inputFilePath) {
    if (!inputFile.is_open()) {
        throw std::runtime_error("Error: Could not open file '" + inputFilePath +
                                 "' (it may not exist or lack permissions).");
    }
    outputFile = std::ofstream(outputFilePath);
}

void Assembler::Assemble() {
    inputFile.clear();
    inputFile.seekg(0, std::ios::beg);
    lineNumber = 0;
    lc = 0;
    firstPass();
    secondPass();
}
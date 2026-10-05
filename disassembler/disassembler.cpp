#include "disassembler.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

Disassembler::Disassembler(const std::string& inputFilePath, const std::string& outputFilePath)
    : inputFile(inputFilePath, std::ios::binary) {
    if (!inputFile.is_open()) {
        throw std::runtime_error("Error: Could not open file '" + inputFilePath +
                                 "' (it may not exist or lack permissions).");
    }
    outputFile = std::ofstream(outputFilePath);
    if (!outputFile.is_open()) {
        throw std::runtime_error("Error: Could not open output file '" + outputFilePath + "'.");
    }
}

std::string Disassembler::decodeRegister(uint32_t reg) const {
    return "r" + std::to_string(reg);
}

int32_t Disassembler::decodeSigned18(uint32_t immediateValue) const {
    int32_t value = static_cast<int32_t>(immediateValue & 0x3FFFFu);
    if (value & (1 << 17)) {
        value |= static_cast<int32_t>(0xFFE00000u);
    }
    return value;
}

std::string Disassembler::decodeImmediate(uint32_t immediateValue) const {
    uint32_t modifierBits = (immediateValue >> 16) & 0x03u;
    uint32_t payload = immediateValue & 0xFFFFu;

    if (modifierBits == 0b01u) {
        return "u" + std::to_string(payload);
    }
    if (modifierBits == 0b10u) {
        std::stringstream ss;
        ss << "h" << std::hex << payload;
        return ss.str();
    }

    return std::to_string(decodeSigned18(immediateValue));
}

void Disassembler::Disassemble() {
    return;
}
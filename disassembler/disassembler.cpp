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

DecodedInstruction Disassembler::decodeInstruction(uint32_t instruction, uint32_t address, size_t index) const {
    uint32_t opcodeBits = (instruction >> 26) & 0x3Fu;
    instructionName mnemonic;
    instructionFields fieldFlags(0);

    for (const auto& [name, details] : instructionTable) {
        if (details.opcode.to_ulong() == opcodeBits) {
            mnemonic = name;
            fieldFlags = details.fields;
            break;
        }
    }

    if (mnemonic.empty()) {
        throw std::runtime_error("Error: unknown opcode 0x" + std::to_string(opcodeBits));
    }

    DecodedInstruction decoded;
    decoded.address = address;
    decoded.index = index;
    decoded.mnemonic = mnemonic;

    uint32_t rd = (instruction >> 22) & 0x0Fu;
    uint32_t rs1 = (instruction >> 18) & 0x0Fu;
    uint32_t rs2 = (instruction >> 14) & 0x0Fu;
    uint32_t rawImmediate = instruction & 0x3FFFFu;

    if (fieldFlags == (HAS_RD | HAS_RS1 | HAS_RS2)) {
        decoded.operands = decodeRegister(rd) + ", " + decodeRegister(rs1) + ", " + decodeRegister(rs2);
    } else if (fieldFlags == (HAS_RD | HAS_RS1 | HAS_IMM)) {
        decoded.operands = decodeRegister(rd) + ", " + decodeRegister(rs1) + ", " + decodeImmediate(rawImmediate);
    } else if (fieldFlags == (HAS_RD | HAS_IMM)) {
        decoded.operands = decodeRegister(rd) + ", " + decodeImmediate(rawImmediate);
    } else if (fieldFlags == (HAS_RS1 | HAS_IMM)) {
        decoded.operands = decodeRegister(rs1) + ", " + decodeImmediate(rawImmediate);
    } else if (fieldFlags == (HAS_RD | HAS_RS1)) {
        decoded.operands = decodeRegister(rd) + ", " + decodeRegister(rs1);
    } else if (fieldFlags == (HAS_RD | HAS_RS2)) {
        decoded.operands = decodeRegister(rd) + ", " + decodeRegister(rs2);
    } else if (fieldFlags == (HAS_RS1 | HAS_RS2)) {
        decoded.operands = decodeRegister(rs1) + ", " + decodeRegister(rs2);
    } else if (fieldFlags == HAS_OFFSET) {
        int32_t offset = static_cast<int32_t>(instruction & 0x3FFFFFFu);
        if (offset & (1 << 25)) {
            offset |= 0xFC000000;
        }
        decoded.isBranch = true;
        decoded.branchOffset = offset;
        decoded.targetAddress = static_cast<uint32_t>(address + 4 + offset);
        decoded.operands = std::to_string(offset);
    } else if (fieldFlags == 0) {
        decoded.operands.clear();
    } else {
        throw std::runtime_error("Error: unsupported instruction format for opcode 0x" + std::to_string(opcodeBits));
    }

    return decoded;
}

void Disassembler::Disassemble() {
    std::vector<uint32_t> words;
    uint32_t instruction = 0;
    while (inputFile.read(reinterpret_cast<char*>(&instruction), sizeof(instruction))) {
        words.push_back(instruction);
    }

    if (words.empty()) {
        throw std::runtime_error("Error: input file is empty.");
    }
    if (inputFile.gcount() != 0 && inputFile.gcount() != static_cast<std::streamsize>(sizeof(instruction))) {
        throw std::runtime_error("Error: input file length is not a multiple of 4 bytes.");
    }

    std::vector<DecodedInstruction> decodedInstructions;
    decodedInstructions.reserve(words.size());

    for (size_t i = 0; i < words.size(); ++i) {
        decodedInstructions.push_back(decodeInstruction(words[i], static_cast<uint32_t>(i * 4), i));
    }

    std::unordered_map<uint32_t, std::string> labelsByAddress;
    for (const auto& decoded : decodedInstructions) {
        if (!decoded.isBranch) {
            continue;
        }
        uint32_t target = decoded.targetAddress;
        if (target % 4 != 0) {
            continue;
        }
        size_t targetIndex = target / 4;
        if (targetIndex >= words.size()) {
            continue;
        }
        auto result = labelsByAddress.emplace(target, "L" + std::to_string(targetIndex + 1));
        if (!result.second) {
            continue;
        }
    }

    for (auto& decoded : decodedInstructions) {
        auto labelIt = labelsByAddress.find(decoded.address);
        if (labelIt != labelsByAddress.end()) {
            decoded.labelName = labelIt->second;
        }

        if (!decoded.isBranch) {
            continue;
        }
        uint32_t target = decoded.targetAddress;
        if (target % 4 == 0 && target / 4 < words.size()) {
            auto it = labelsByAddress.find(target);
            if (it != labelsByAddress.end()) {
                decoded.operands = it->second;
            }
        }
    }

    for (const auto& decoded : decodedInstructions) {
        if (!decoded.labelName.empty()) {
            outputFile << decoded.labelName << ": ";
        }
        outputFile << decoded.mnemonic;
        if (!decoded.operands.empty()) {
            outputFile << " " << decoded.operands;
        }
        outputFile << '\n';
    }
    return;
}
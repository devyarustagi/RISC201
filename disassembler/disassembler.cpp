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

std::string Disassembler::decodeWindowRegister(uint32_t reg) const {
    // Restricted register window used by the privileged `movz` (matches the
    // assembler's parseWindowRegister).
    switch (reg) {
        case 0: return "r0";
        case 1: return "sp";
        case 2: return "oldPC";
        case 3: return "oldSP";
        case 4: return "oldFlags";
        case 5: return "eBase";
        case 6: return "eCode";
        default:
            throw std::runtime_error("Error: invalid register-window operand " +
                                     std::to_string(reg) + ".");
    }
}

std::string Disassembler::decodeImmediate(uint32_t immediateValue) const {
    uint32_t modifierBits = (immediateValue >> 16) & 0x03u;
    uint32_t payload = immediateValue & 0xFFFFu;

    if (modifierBits == 0b01u) {
        return "u" + std::to_string(payload);   // unsigned 16-bit
    }
    if (modifierBits == 0b10u) {
        return "h" + std::to_string(payload);   // value << 16 (decimal, like the assembler)
    }

    return std::to_string(static_cast<int16_t>(payload));   // signed 16-bit
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

    if (mnemonic == "movz") {
        // Both operands are register-window entries (r0, sp, oldPC, oldSP,
        // oldFlags, eBase, eCode) packed into the rd and rs2 fields.
        decoded.operands = decodeWindowRegister(rd) + ", " + decodeWindowRegister(rs2);
    } else if (fieldFlags == (HAS_RD | HAS_RS1 | HAS_RS2)) {
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
        // Branch-format: a 25-bit signed WORD offset in [24:0]; bit 25 is the
        // dynamic prediction bit and is ignored here.
        int32_t offset = static_cast<int32_t>(instruction & 0x1FFFFFFu);
        if (offset & (1 << 24)) {
            offset |= static_cast<int32_t>(0xFE000000u);
        }
        decoded.isBranch = true;
        decoded.branchOffset = offset;
        decoded.targetAddress = static_cast<uint32_t>(
            static_cast<int64_t>(address) + static_cast<int64_t>(offset) * 4);
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
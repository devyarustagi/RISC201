#include "assembler.hpp"
#include "stringTrim.hpp"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

uint32_t Assembler::parseRegister(const std::string& regStr, int lineNum) {
    if (regStr == "sp") {
        return 14;   // sp aliases r14
    }

    std::string s = regStr;
    if (!s.empty() && (s[0] == 'r' || s[0] == 'R')) {
        s = s.substr(1);
    }

    try {
        size_t idx = 0;
        int regVal = std::stoi(s, &idx);
        if (idx != s.size() || regVal < 0 || regVal > 15) {
            throw std::out_of_range("");
        }
        return static_cast<uint32_t>(regVal);
    } catch (...) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": invalid register operand '" + regStr + "'. Expected r0-r15.");
    }
}

uint32_t Assembler::parseWindowRegister(const std::string& name, int lineNum) {
    // Restricted register window used by the privileged `movz`.
    if (name == "r0")       return 0;
    if (name == "sp")       return 1;
    if (name == "oldPC")    return 2;
    if (name == "oldSP")    return 3;
    if (name == "oldFlags") return 4;
    if (name == "eBase")    return 5;
    if (name == "eCode")    return 6;

    throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                            ": invalid window register '" + name +
                            "'. Expected r0, sp, oldPC, oldSP, oldFlags, eBase or eCode.");
}

static uint32_t encodeImmediateField(const std::string& immStr, int lineNum) {
    if (immStr.empty()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": empty immediate operand.");
    }

    std::string valueText = immStr;
    uint32_t modifierBits = 0;

    if (!valueText.empty() && (valueText[0] == 'u' || valueText[0] == 'U' ||
                              valueText[0] == 'h' || valueText[0] == 'H')) {
        modifierBits = (valueText[0] == 'u' || valueText[0] == 'U') ? 0b01u : 0b10u;
        valueText = valueText.substr(1);
    }

    if (valueText.empty()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": immediate is missing its numeric value.");
    }

    size_t idx = 0;
    long long value = std::stoll(valueText, &idx, 0);
    if (idx != valueText.size()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": invalid immediate '" + immStr + "'.");
    }

    if (value < 0) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": immediate '" + immStr + "' must be non-negative for u/h forms.");
    }

    if (value > 0xFFFF) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": immediate '" + immStr + "' exceeds 16-bit payload range (0..65535)."
                                " 18-bit immediate format is 2-bit modifier + 16-bit value.");
    }

    return (modifierBits << 16) | static_cast<uint32_t>(value);
}

// I-format immediate: 2 modifier bits | 16-bit value.
//   00 -> signed 16-bit, 01 -> unsigned 16-bit (`u`), 10 -> high half (`h`).
// Modifier 11 is never produced. A bare symbol resolves to its absolute address.
int32_t Assembler::parseImmediateOrSymbol(const std::string& immStr, uint32_t /*currentAddress*/, int lineNum) {
    if (immStr.empty()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": empty immediate operand.");
    }

    // A symbol resolves to its absolute address (e.g. loading a handler address).
    auto symbol = symbolTable.find(immStr);
    if (symbol != symbolTable.end()) {
        const int32_t address = static_cast<int32_t>(symbol->second);
        if (address < -32768 || address > 32767) {
            throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                    ": address of symbol '" + immStr + "' does not fit the "
                                    "signed 16-bit immediate field.");
        }
        return address;
    }

    if (immStr[0] == 'u' || immStr[0] == 'U' || immStr[0] == 'h' || immStr[0] == 'H') {
        return static_cast<int32_t>(encodeImmediateField(immStr, lineNum));
    }

    size_t idx = 0;
    int32_t value = 0;
    try {
        value = std::stoi(immStr, &idx, 0);
    } catch (const std::exception&) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": invalid immediate '" + immStr + "'.");
    }
    if (idx != immStr.size()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": invalid immediate '" + immStr + "'.");
    }
    if (value < -32768 || value > 32767) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": immediate '" + immStr +
                                "' out of signed 16-bit range (-32768..32767); use u/h for the other forms.");
    }
    return value & 0xFFFF;  // modifier 00 | 16-bit two's-complement value
}

// Branch target: a signed 25-bit WORD offset in bits [24:0], relative to the
// branch's own PC (the hardware computes PC + (offset << 2)). Bit 25 is left
// clear for the dynamic branch-prediction bit, which the processor maintains
// in instruction memory. A label resolves to that word distance; a number is
// taken as-is.
int32_t Assembler::parseBranchOffset(const std::string& offStr, uint32_t currentAddress, int lineNum) {
    auto symbol = symbolTable.find(offStr);
    if (symbol != symbolTable.end()) {
        const int32_t deltaBytes = static_cast<int32_t>(symbol->second) -
                                   static_cast<int32_t>(currentAddress);
        return deltaBytes / 4;
    }

    size_t idx = 0;
    int32_t value = 0;
    try {
        value = std::stoi(offStr, &idx, 0);
    } catch (const std::exception&) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": undefined symbol or invalid branch offset '" + offStr + "'.");
    }
    if (idx != offStr.size()) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": undefined symbol or invalid branch offset '" + offStr + "'.");
    }
    if (value < -16777216 || value > 16777215) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": branch offset '" + offStr + "' out of signed 25-bit word range.");
    }
    return value;
}

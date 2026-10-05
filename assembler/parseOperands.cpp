#include "assembler.hpp"
#include "stringTrim.hpp"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

uint32_t Assembler::parseRegister(const std::string& regStr, int lineNum) {
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

int32_t Assembler::parseImmediateOrSymbol(const std::string& immStr, uint32_t currentAddress, int lineNum) {
    if (symbolTable.count(immStr) > 0) {
        uint32_t targetAddr = symbolTable.at(immStr);
        return static_cast<int32_t>(targetAddr - (currentAddress + 4));
    }

    try {
        if (!immStr.empty() && (immStr[0] == 'u' || immStr[0] == 'U' ||
                               immStr[0] == 'h' || immStr[0] == 'H')) {
            uint32_t encoded = encodeImmediateField(immStr, lineNum);
            return static_cast<int32_t>(encoded);
        }

        size_t idx = 0;
        int32_t val = std::stoi(immStr, &idx, 0);
        if (idx != immStr.size()) {
            throw std::invalid_argument("");
        }
        if (val < -131072 || val > 131071) {
            throw std::out_of_range("");
        }
        return val;
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": immediate '" + immStr + "' exceeds 18-bit signed range.");
    } catch (...) {
        throw std::runtime_error("Error: line " + std::to_string(lineNum) +
                                ": undefined symbol or invalid immediate '" + immStr + "'.");
    }
}

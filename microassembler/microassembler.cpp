// RISC201 microassembler.
//
// Translates a microcode source file (.masm) into the two control-store images
// that processor_sim's Control Unit loads:
//
//   microprograms.rom      38-bit microinstructions, packed as one continuous
//                          MSB-first bit stream (padded with 0 to a whole byte)
//   rom_address_table.rom  one byte per macro opcode (0x00 - 0x2B): the start
//                          address of that instruction's microroutine
//
// Microinstruction word (must match Microinstruction::decode in micro_types.hpp):
//   opcode[37:32] dst[31:27] src1[26:22] src2[21:17] imm[16:1] stall[0]
//
// Source format:
//   # comment
//   <macro>:  <micro-op> [dst=<f>] [src1=<f>] [src2=<f>] [imm=<n>]
//             <micro-op> ...          (indented line: next micro-op of the routine)
//
//   <f> is rd / rs1 / rs2 (the macro instruction's own field, encoded 0) or a
//   scratch register s0 - s15 (encoded 0x10 | n). The assembler sets stall = 1
//   on every micro-op of a routine except the last one.
//
// Usage: microassembler <source.masm> [-o <output-dir>] [-l]
//        -l prints a listing of the control store.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kWordBits = 38;
constexpr int kAddressTableSize = 44;   // macro opcodes 0x00 - 0x2B
constexpr int kMaxWords = 256;          // address table entries are one byte

// Macro mnemonic -> opcode (project ISA, same table as micro_types.hpp).
const std::map<std::string, int> kMacroOps = {
    {"nop", 0x00},  {"add", 0x01},  {"sub", 0x02},  {"mul", 0x03},  {"div", 0x04},
    {"mod", 0x05},  {"and", 0x06},  {"or", 0x07},   {"xor", 0x08},  {"lsl", 0x09},
    {"lsr", 0x0A},  {"asr", 0x0B},  {"not", 0x0C},  {"mov", 0x0D},  {"cmp", 0x0E},
    {"addi", 0x0F}, {"subi", 0x10}, {"muli", 0x11}, {"divi", 0x12}, {"modi", 0x13},
    {"andi", 0x14}, {"ori", 0x15},  {"xori", 0x16}, {"lsli", 0x17}, {"lsri", 0x18},
    {"asri", 0x19}, {"movi", 0x1A}, {"cmpi", 0x1B}, {"ld", 0x1C},   {"st", 0x1D},
    {"fadd", 0x1E}, {"fsub", 0x1F}, {"fmul", 0x20}, {"fdiv", 0x21}, {"fcmp", 0x22},
    {"b", 0x23},    {"beq", 0x24},  {"bgt", 0x25},  {"call", 0x26}, {"ret", 0x27},
    {"movz", 0x28}, {"retz", 0x29}, {"ebreak", 0x2A}, {"ecall", 0x2B},
};

// Micro-op mnemonic -> micro opcode (same table as MicroOp in micro_types.hpp).
// Even = register form, odd = immediate form.
const std::map<std::string, int> kMicroOps = {
    {"madd", 0x00},  {"msub", 0x02},  {"mmul", 0x04},  {"mdiv", 0x06},  {"mmod", 0x08},
    {"mand", 0x0A},  {"mor", 0x0C},   {"mxor", 0x0E},  {"mlsl", 0x10},  {"mlsr", 0x12},
    {"masr", 0x14},  {"mcmp", 0x16},  {"mnot", 0x18},  {"mfadd", 0x1A}, {"mfsub", 0x1C},
    {"mfmul", 0x1E}, {"mfdiv", 0x20}, {"mfcmp", 0x22}, {"mret", 0x24},  {"mmov", 0x26},
    {"maddi", 0x01}, {"msubi", 0x03}, {"mmuli", 0x05}, {"mdivi", 0x07}, {"mmodi", 0x09},
    {"mandi", 0x0B}, {"mori", 0x0D},  {"mxori", 0x0F}, {"mlsli", 0x11}, {"mlsri", 0x13},
    {"masri", 0x15}, {"mcmpi", 0x17}, {"mmovi", 0x19}, {"mld", 0x1B},   {"mst", 0x1D},
    {"mb", 0x1F},    {"mbeq", 0x21},  {"mbgt", 0x23},  {"mcall", 0x25}, {"mmovz", 0x28},
    {"mretz", 0x29}, {"mebreak", 0x2A}, {"mecall", 0x2B}, {"mnop", 0x3E},
};

struct MicroWord {
    int opcode = 0;
    int dst = 0;
    int src1 = 0;
    int src2 = 0;
    int imm = 0;
    bool stall = false;
    std::string text;   // source text, for the listing
    int line = 0;
};

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

[[noreturn]] void fail(int line, const std::string& message) {
    throw std::runtime_error("microassembler: line " + std::to_string(line) + ": " + message);
}

// rd / rs1 / rs2 -> 0 (the macro field), sN -> 0x10 | N.
int parseField(const std::string& value, int line) {
    if (value == "rd" || value == "rs1" || value == "rs2") {
        return 0;
    }
    if (value.size() >= 2 && value[0] == 's') {
        const int n = std::stoi(value.substr(1));
        if (n < 0 || n > 15) {
            fail(line, "scratch register out of range: " + value);
        }
        return 0x10 | n;
    }
    fail(line, "bad operand field '" + value + "' (use rd, rs1, rs2 or s0-s15)");
}

MicroWord parseMicroOp(const std::string& text, int line) {
    std::istringstream in(text);
    std::string name;
    in >> name;
    const auto op = kMicroOps.find(name);
    if (op == kMicroOps.end()) {
        fail(line, "unknown micro-op '" + name + "'");
    }
    MicroWord word;
    word.opcode = op->second;
    word.text = text;
    word.line = line;
    std::string operand;
    while (in >> operand) {
        const auto eq = operand.find('=');
        if (eq == std::string::npos) {
            fail(line, "operand must be key=value: '" + operand + "'");
        }
        const std::string key = operand.substr(0, eq);
        const std::string value = operand.substr(eq + 1);
        if (key == "dst") {
            word.dst = parseField(value, line);
        } else if (key == "src1") {
            word.src1 = parseField(value, line);
        } else if (key == "src2") {
            word.src2 = parseField(value, line);
        } else if (key == "imm") {
            word.imm = std::stoi(value, nullptr, 0);
            if (word.imm < -32768 || word.imm > 32767) {
                fail(line, "imm out of 16-bit signed range");
            }
        } else {
            fail(line, "unknown operand key '" + key + "'");
        }
    }
    return word;
}

uint64_t encode(const MicroWord& w) {
    return (static_cast<uint64_t>(w.opcode & 0x3F) << 32) |
           (static_cast<uint64_t>(w.dst & 0x1F) << 27) |
           (static_cast<uint64_t>(w.src1 & 0x1F) << 22) |
           (static_cast<uint64_t>(w.src2 & 0x1F) << 17) |
           (static_cast<uint64_t>(static_cast<uint16_t>(w.imm)) << 1) |
           (w.stall ? 1u : 0u);
}

}  // namespace

int main(int argc, char** argv) {
    std::string sourcePath;
    std::string outDir = ".";
    bool listing = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (arg == "-l") {
            listing = true;
        } else if (sourcePath.empty()) {
            sourcePath = arg;
        } else {
            std::cerr << "usage: microassembler <source.masm> [-o <dir>] [-l]\n";
            return EXIT_FAILURE;
        }
    }
    if (sourcePath.empty()) {
        std::cerr << "usage: microassembler <source.masm> [-o <dir>] [-l]\n";
        return EXIT_FAILURE;
    }

    try {
        std::ifstream source(sourcePath);
        if (!source.is_open()) {
            throw std::runtime_error("microassembler: cannot open '" + sourcePath + "'");
        }

        std::vector<MicroWord> words;
        std::vector<int> startAddress(kAddressTableSize, -1);
        std::vector<std::string> routineOf;   // macro name per word (listing)
        int currentRoutineStart = -1;
        std::string currentMacro;
        std::string rawLine;
        int lineNumber = 0;

        while (std::getline(source, rawLine)) {
            ++lineNumber;
            const auto hash = rawLine.find('#');
            if (hash != std::string::npos) {
                rawLine = rawLine.substr(0, hash);
            }
            std::string text = trim(rawLine);
            if (text.empty()) {
                continue;
            }
            const auto colon = text.find(':');
            if (colon != std::string::npos) {
                currentMacro = trim(text.substr(0, colon));
                const auto macro = kMacroOps.find(currentMacro);
                if (macro == kMacroOps.end()) {
                    fail(lineNumber, "unknown macro instruction '" + currentMacro + "'");
                }
                if (startAddress[macro->second] != -1) {
                    fail(lineNumber, "routine for '" + currentMacro + "' defined twice");
                }
                currentRoutineStart = static_cast<int>(words.size());
                startAddress[macro->second] = currentRoutineStart;
                text = trim(text.substr(colon + 1));
                if (text.empty()) {
                    fail(lineNumber, "routine '" + currentMacro + "' has no micro-op");
                }
            } else if (currentRoutineStart < 0) {
                fail(lineNumber, "micro-op outside any routine");
            } else {
                words.back().stall = true;   // a further micro-op follows
            }
            words.push_back(parseMicroOp(text, lineNumber));
            routineOf.push_back(currentMacro);
        }

        for (const auto& [name, opcode] : kMacroOps) {
            if (startAddress[opcode] < 0) {
                throw std::runtime_error("microassembler: no routine for macro '" + name + "'");
            }
        }
        if (static_cast<int>(words.size()) > kMaxWords) {
            throw std::runtime_error("microassembler: control store exceeds 256 words");
        }

        // Pack the 38-bit words into one MSB-first bit stream.
        std::vector<uint8_t> bytes((words.size() * kWordBits + 7) / 8, 0);
        uint64_t bitIndex = 0;
        for (const MicroWord& w : words) {
            const uint64_t value = encode(w);
            for (int bit = kWordBits - 1; bit >= 0; --bit, ++bitIndex) {
                if ((value >> bit) & 1u) {
                    bytes[bitIndex / 8] |= static_cast<uint8_t>(0x80u >> (bitIndex % 8));
                }
            }
        }

        const std::string romPath = outDir + "/microprograms.rom";
        const std::string tablePath = outDir + "/rom_address_table.rom";
        std::ofstream rom(romPath, std::ios::binary);
        std::ofstream table(tablePath, std::ios::binary);
        if (!rom.is_open() || !table.is_open()) {
            throw std::runtime_error("microassembler: cannot write to '" + outDir + "'");
        }
        rom.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        for (int address : startAddress) {
            table.put(static_cast<char>(address));
        }

        if (listing) {
            std::cout << "addr  word (38-bit)   routine  micro-op\n";
            for (size_t i = 0; i < words.size(); ++i) {
                std::cout << std::setw(4) << i << "  0x" << std::hex << std::setw(10)
                          << std::setfill('0') << encode(words[i]) << std::dec
                          << std::setfill(' ') << "  " << std::left << std::setw(8)
                          << routineOf[i] << std::right << " " << words[i].text
                          << (words[i].stall ? "   ; stall" : "") << '\n';
            }
        }
        std::cout << "microassembler: " << words.size() << " microinstructions ("
                  << bytes.size() << " bytes) -> " << romPath << ", " << tablePath << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

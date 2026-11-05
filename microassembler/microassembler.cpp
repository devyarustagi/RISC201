#include "microassembler.hpp"

#include <stdexcept>

// Microinstruction opcodes are assigned by parity so that the hardware decoder
// can tell the two forms apart with a single bit test (opcode & 1):
//   EVEN opcode -> register form (ALU B input = op2)
//   ODD  opcode -> immediate form (ALU B input = imm)
// Paired mnemonics therefore sit next to each other, register form first:
//   madd (even) / maddi (odd), etc.
const std::unordered_map<std::string, microinstructionDetails> Microassembler::microinstructionTable = {
    // Register form (even).
    {"madd",  {0x00, "d12"}},   // add
    {"msub",  {0x02, "d12"}},   // sub
    {"mmul",  {0x04, "d12"}},   // mul
    {"mdiv",  {0x06, "d12"}},   // div
    {"mmod",  {0x08, "d12"}},   // mod
    {"mand",  {0x0A, "d12"}},   // and
    {"mor",   {0x0C, "d12"}},   // or
    {"mxor",  {0x0E, "d12"}},   // xor
    {"mlsl",  {0x10, "d12"}},   // lsl
    {"mlsr",  {0x12, "d12"}},   // lsr
    {"masr",  {0x14, "d12"}},   // asr
    {"mcmp",  {0x16, "12"}},    // cmp
    {"mnot",  {0x18, "d1"}},    // not
    {"mfadd", {0x1A, "d12"}},   // fadd
    {"mfsub", {0x1C, "d12"}},   // fsub
    {"mfmul", {0x1E, "d12"}},   // fmul
    {"mfdiv", {0x20, "d12"}},   // fdiv
    {"mfcmp", {0x22, "12"}},    // fcmp
    {"mret",  {0x24, ""}},      // ret
    {"mmov",  {0x26, "d2"}},    // mov rd, rs2  (register move)
    // Special group (privileged / exception). These are decoded explicitly by
    // the control unit, so they are exempt from the even = register / odd =
    // immediate parity rule and form one contiguous block after the normal
    // micro-opcodes (0x00 - 0x26), with no parity-based meaning.
    {"mmovz", {0x27, "d2"}},    // movz (privileged, register window)
    {"mretz", {0x28, ""}},      // retz (privileged)
    {"mebreak", {0x29, ""}},    // ebreak
    {"mecall",  {0x2A, ""}},    // ecall

    // Immediate form (odd).
    {"maddi", {0x01, "d1i"}},   // nop / addi
    {"msubi", {0x03, "d1i"}},   // subi
    {"mmuli", {0x05, "d1i"}},   // muli
    {"mdivi", {0x07, "d1i"}},   // divi
    {"mmodi", {0x09, "d1i"}},   // modi
    {"mandi", {0x0B, "d1i"}},   // andi
    {"mori",  {0x0D, "d1i"}},   // ori
    {"mxori", {0x0F, "d1i"}},   // xori
    {"mlsli", {0x11, "d1i"}},   // lsli
    {"mlsri", {0x13, "d1i"}},   // lsri
    {"masri", {0x15, "d1i"}},   // asri
    {"mcmpi", {0x17, "1i"}},    // cmpi
    {"mmovi", {0x19, "di"}},    // movi rd, imm
    {"mld",   {0x1B, "d1i"}},   // ld rd, rs1, imm
    {"mst",   {0x1D, "d1i"}},   // st rd, rs1, imm
    {"mb",    {0x1F, ""}},      // b
    {"mbeq",  {0x21, ""}},      // beq
    {"mbgt",  {0x23, ""}},      // bgt
    {"mcall", {0x25, ""}},      // call
};

// Active macroinstructions, matching the project ISA (44 opcodes 0x00 - 0x2B).
const std::unordered_map<uint8_t, std::string> Microassembler::macroOpcodeTable = {
    {0x00, "nop"},  {0x01, "add"},  {0x02, "sub"},  {0x03, "mul"},
    {0x04, "div"},  {0x05, "mod"},  {0x06, "and"},  {0x07, "or"},
    {0x08, "xor"},  {0x09, "lsl"},  {0x0A, "lsr"},  {0x0B, "asr"},
    {0x0C, "not"},  {0x0D, "mov"},  {0x0E, "cmp"},  {0x0F, "addi"},
    {0x10, "subi"}, {0x11, "muli"}, {0x12, "divi"}, {0x13, "modi"},
    {0x14, "andi"}, {0x15, "ori"},  {0x16, "xori"}, {0x17, "lsli"},
    {0x18, "lsri"}, {0x19, "asri"}, {0x1A, "movi"}, {0x1B, "cmpi"},
    {0x1C, "ld"},   {0x1D, "st"},   {0x1E, "fadd"}, {0x1F, "fsub"},
    {0x20, "fmul"}, {0x21, "fdiv"}, {0x22, "fcmp"}, {0x23, "b"},
    {0x24, "beq"},  {0x25, "bgt"},  {0x26, "call"}, {0x27, "ret"},
    {0x28, "movz"}, {0x29, "retz"}, {0x2A, "ebreak"}, {0x2B, "ecall"},
};

Microassembler::Microassembler(const std::string& inputFilePath) : inputFile(inputFilePath) {
    if (!inputFile.is_open()) {
        throw std::runtime_error("Error: Could not open file '" + inputFilePath +
                                 "' (it may not exist or lack permissions).");
    }
}

std::vector<std::string> Microassembler::tokenizer(std::string& line) const {
    // Strip a comment (everything from the first '#').
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos) {
        line.erase(commentPos);
    }

    // Split the remaining text on whitespace and commas.
    std::vector<std::string> tokens;
    std::string current;
    bool inToken = false;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r' || c == ',') {
            if (inToken) {
                tokens.push_back(current);
                current.clear();
                inToken = false;
            }
        } else {
            current.push_back(c);
            inToken = true;
        }
    }
    if (inToken) {
        tokens.push_back(current);
    }
    return tokens;
}

uint32_t Microassembler::encodeRegister(const std::string& operand) const {
    // rd / rs1 / rs2 are the registers supplied by the macroinstruction; their
    // field is zeroed out and resolved from the macroinstruction at run time.
    if (operand == "rd" || operand == "rs1" || operand == "rs2") {
        return 0;
    }

    // Scratch registers s0 - s15 carry the tag bit in the MSB.
    if (operand.size() >= 2 && operand[0] == 's') {
        long value = 0;
        for (std::size_t i = 1; i < operand.size(); ++i) {
            const char c = operand[i];
            if (c < '0' || c > '9') {
                throw std::runtime_error("Error: line " + std::to_string(lc) +
                                         ": invalid register operand '" + operand + "'.");
            }
            value = value * 10 + (c - '0');
            if (value > 15) {
                throw std::runtime_error("Error: line " + std::to_string(lc) +
                                         ": scratch register '" + operand +
                                         "' out of range (s0 - s15).");
            }
        }
        return SCRATCH_TAG | static_cast<uint32_t>(value);
    }

    throw std::runtime_error("Error: line " + std::to_string(lc) +
                             ": invalid register operand '" + operand +
                             "' (expected rd, rs1, rs2 or s0 - s15).");
}

uint16_t Microassembler::encodeImmediate(const std::string& operand) const {
    // "imm" is the macroinstruction's immediate placeholder, resolved at run
    // time, so it is encoded as zero.
    if (operand == "imm") {
        return 0;
    }

    const auto invalid = [&]() {
        return std::runtime_error("Error: line " + std::to_string(lc) +
                                  ": invalid immediate operand '" + operand + "'.");
    };

    std::size_t i = 0;
    bool negative = false;
    if (i < operand.size() && (operand[i] == '+' || operand[i] == '-')) {
        negative = operand[i] == '-';
        ++i;
    }

    unsigned int base = 10;
    if (i + 1 < operand.size() && operand[i] == '0' &&
        (operand[i + 1] == 'x' || operand[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (i + 1 < operand.size() && operand[i] == '0' &&
               (operand[i + 1] == 'b' || operand[i + 1] == 'B')) {
        base = 2;
        i += 2;
    }
    if (i >= operand.size()) {
        throw invalid();
    }

    unsigned long long magnitude = 0;
    for (; i < operand.size(); ++i) {
        const char c = operand[i];
        unsigned int digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned int>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned int>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned int>(c - 'A' + 10);
        } else {
            throw invalid();
        }
        if (digit >= base) {
            throw invalid();
        }
        magnitude = magnitude * base + digit;
        if (magnitude > 0xFFFFFFFFULL) {
            throw invalid();
        }
    }

    const long long value = negative ? -static_cast<long long>(magnitude)
                                     : static_cast<long long>(magnitude);
    // Truncate to the 16-bit two's-complement field.
    return static_cast<uint16_t>(value & 0xFFFF);
}

uint64_t Microassembler::encodeInstruction(const std::vector<std::string>& tokens) const {
    const std::string& mnemonic = tokens[0];

    auto entry = microinstructionTable.find(mnemonic);
    if (entry == microinstructionTable.end()) {
        throw std::runtime_error("Error: line " + std::to_string(lc) +
                                 ": unknown microinstruction '" + mnemonic + "'.");
    }
    const microinstructionDetails& details = entry->second;
    const std::string pattern = details.pattern;

    // Collect the operands, dropping the "mimm" keyword (which merely marks a
    // literal micro immediate).
    std::vector<std::string> operands;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == "mimm") {
            continue;
        }
        operands.push_back(tokens[i]);
    }

    if (operands.size() != pattern.size()) {
        throw std::runtime_error("Error: line " + std::to_string(lc) + ": '" + mnemonic +
                                 "' expects " + std::to_string(pattern.size()) +
                                 " operand(s), got " + std::to_string(operands.size()) + ".");
    }

    uint32_t dest = 0, src1 = 0, src2 = 0;
    uint16_t immediate = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        switch (pattern[i]) {
            case 'd': dest = encodeRegister(operands[i]); break;
            case '1': src1 = encodeRegister(operands[i]); break;
            case '2': src2 = encodeRegister(operands[i]); break;
            case 'i': immediate = encodeImmediate(operands[i]); break;
        }
    }

    // | opcode [37:32] | dest [31:27] | src1 [26:22] | src2 [21:17] |
    // | immediate [16:1] | stall [0] |
    return (static_cast<uint64_t>(details.opcode) << 32) |
           (static_cast<uint64_t>(dest) << 27) |
           (static_cast<uint64_t>(src1) << 22) |
           (static_cast<uint64_t>(src2) << 17) |
           (static_cast<uint64_t>(immediate) << 1);
}

void Microassembler::writeMicroprogramsRom(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("Error: Could not open output file '" + path + "' for writing.");
    }

    uint8_t byte = 0;
    int bitsFilled = 0;
    for (uint64_t word : microprogram) {
        for (int bit = MICRO_WORD_BITS - 1; bit >= 0; --bit) {
            byte = static_cast<uint8_t>((byte << 1) | ((word >> bit) & 1ULL));
            if (++bitsFilled == 8) {
                output.put(static_cast<char>(byte));
                byte = 0;
                bitsFilled = 0;
            }
        }
    }
    // Flush the trailing partial byte, zero padded on the right.
    if (bitsFilled > 0) {
        byte = static_cast<uint8_t>(byte << (8 - bitsFilled));
        output.put(static_cast<char>(byte));
    }

    if (!output) {
        throw std::runtime_error("Error: Failed while writing '" + path + "'.");
    }
}

void Microassembler::writeAddressTableRom(const std::string& path) const {
    // The table stores 8-bit addresses, so the microprogram must fit in 256 words.
    if (microprogram.size() > 0xFF + 1) {
        throw std::runtime_error("Error: microprogram has " +
                                 std::to_string(microprogram.size()) +
                                 " instructions; the 8-bit address table supports at most 256.");
    }

    // Resolve every active macro opcode to its label before touching the file,
    // so a missing sequence fails cleanly and leaves no partial output.
    std::vector<uint8_t> addresses;
    std::vector<std::string> missing;
    for (uint16_t opcode = 0x00; opcode <= 0x2B; ++opcode) {
        auto macro = macroOpcodeTable.find(static_cast<uint8_t>(opcode));
        if (macro == macroOpcodeTable.end()) {
            continue;
        }
        const std::string& mnemonic = macro->second;

        auto label = labels.find(mnemonic);
        if (label == labels.end()) {
            missing.push_back(mnemonic);
            continue;
        }
        addresses.push_back(static_cast<uint8_t>(label->second));
    }

    if (!missing.empty()) {
        std::string list;
        for (std::size_t i = 0; i < missing.size(); ++i) {
            list += (i == 0 ? "." : ", .") + missing[i];
        }
        throw std::runtime_error("Error: missing microinstruction sequence(s) for macroinstruction(s): " +
                                 list + ".");
    }

    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("Error: Could not open output file '" + path + "' for writing.");
    }

    for (uint8_t address : addresses) {
        output.put(static_cast<char>(address));
    }

    if (!output) {
        throw std::runtime_error("Error: Failed while writing '" + path + "'.");
    }
}

void Microassembler::Assemble(const std::string& microprogramsPath, const std::string& addressTablePath) {
    std::string line;
    bool seenLabel = false;

    while (std::getline(inputFile, line)) {
        ++lc;

        if (line.size() > MAX_LINE_LEN) {
            throw std::runtime_error("Error: line " + std::to_string(lc) +
                                     " is longer than the maximum allowed " +
                                     std::to_string(MAX_LINE_LEN) + " characters.");
        }

        std::vector<std::string> tokens = tokenizer(line);
        if (tokens.empty()) {
            continue;
        }

        // A leading '.' marks a label. It records the address of the next
        // instruction and closes the previous sequence (stall bit cleared).
        if (tokens[0][0] == '.') {
            std::string name = tokens[0].substr(1);
            if (!name.empty() && name.back() == ':') {
                name.pop_back();
            }
            if (name.empty()) {
                throw std::runtime_error("Error: line " + std::to_string(lc) +
                                         ": empty label.");
            }
            if (labels.find(name) != labels.end()) {
                throw std::runtime_error("Error: line " + std::to_string(lc) +
                                         ": duplicate label '." + name + "'.");
            }
            if (!microprogram.empty()) {
                microprogram.back() &= ~1ULL;   // last instruction of the sequence
            }
            labels[name] = static_cast<uint32_t>(microprogram.size());
            seenLabel = true;
            continue;
        }

        if (!seenLabel) {
            throw std::runtime_error("Error: line " + std::to_string(lc) +
                                     ": instruction outside of any .label.");
        }

        // Default stall = 1; the next label (or EOF) clears it for the last
        // instruction of the sequence.
        microprogram.push_back(encodeInstruction(tokens) | 1ULL);
    }

    if (!microprogram.empty()) {
        microprogram.back() &= ~1ULL;
    }

    writeMicroprogramsRom(microprogramsPath);
    writeAddressTableRom(addressTablePath);
}

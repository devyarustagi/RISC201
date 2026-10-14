#include "components/control_unit.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

void ControlUnit::loadAddressTable(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Error: cannot open address table '" + path + "'.");
    }

    for (int i = 0; i < ADDRESS_TABLE_SIZE; ++i) {
        char byte = 0;
        if (!input.get(byte)) {
            throw std::runtime_error("Error: address table '" + path + "' is shorter than " +
                                     std::to_string(ADDRESS_TABLE_SIZE) + " bytes.");
        }
        addressTable_[i] = static_cast<uint8_t>(byte);
    }
}

void ControlUnit::loadMicroprograms(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Error: cannot open microprogram ROM '" + path + "'.");
    }

    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>()};
    if (bytes.empty()) {
        throw std::runtime_error("Error: microprogram ROM '" + path + "' is empty.");
    }

    // The stream is a continuous MSB-first bit stream of 38-bit words.
    const uint64_t totalBits = static_cast<uint64_t>(bytes.size()) * 8;
    const uint64_t wordCount = totalBits / MICRO_WORD_BITS;

    microprogram_.clear();
    microprogram_.reserve(wordCount);
    for (uint64_t w = 0; w < wordCount; ++w) {
        uint64_t word = 0;
        for (int bit = 0; bit < MICRO_WORD_BITS; ++bit) {
            const uint64_t index = w * MICRO_WORD_BITS + bit;
            const uint8_t byte = bytes[index / 8];
            const int shift = 7 - static_cast<int>(index % 8);
            word = (word << 1) | ((byte >> shift) & 0x1);
        }
        microprogram_.push_back(Microinstruction::decode(word));
    }
}

void ControlUnit::Load(const std::string& microprogramsPath,
                       const std::string& addressTablePath) {
    loadAddressTable(addressTablePath);
    loadMicroprograms(microprogramsPath);
    Reset();
}

void ControlUnit::Reset() {
    upc_ = 0;
    busy_ = false;
}

void ControlUnit::Begin(uint8_t macroOpcode) {
    upc_ = (macroOpcode < ADDRESS_TABLE_SIZE) ? addressTable_[macroOpcode] : 0;
    busy_ = false;
}

ControlSignals ControlUnit::decode(const Microinstruction& micro) const {
    ControlSignals control;
    control.isStall = micro.stall;
    control.microImm = micro.imm;

    // Parity shortcut: odd -> immediate form, even -> register form.
    const bool immediateForm = (micro.opcode & 0x1) != 0;

    // A operand: the macro rs1 (op1) or scratch-A, chosen by the tag bit of the
    // micro src1 field.
    control.src1ScratchAddr = static_cast<uint8_t>(micro.src1 & 0x0F);
    control.aluSrcA = ((micro.src1 & 0x10) != 0) ? A_SCRATCH : A_OP1;

    // B operand: op2 (register form), the macro immediate (immediate form) or
    // scratch-B.
    control.src2ScratchAddr = static_cast<uint8_t>(micro.src2 & 0x0F);
    if (immediateForm) {
        control.aluSrcB = B_IMM;
    } else {
        control.aluSrcB = ((micro.src2 & 0x10) != 0) ? B_SCRATCH : B_OP2;
    }

    // Routes a writing micro-op either to the destination GPR or to a scratch
    // register, depending on the tag bit of the micro `dst` field.
    const auto writeDst = [&]() {
        if ((micro.dst & 0x10) != 0) {
            control.isScratchWb = true;
            control.scrWriteAddr = static_cast<uint8_t>(micro.dst & 0x0F);
        } else {
            control.isWb = true;
        }
    };

    switch (micro.opcode) {
        case MicroOp::MADDI: case MicroOp::MADD:
            control.aluOp = AluOp::ADD; writeDst(); break;
        case MicroOp::MSUBI: case MicroOp::MSUB:
            control.aluOp = AluOp::SUB; writeDst(); break;
        case MicroOp::MMULI: case MicroOp::MMUL:
            control.aluOp = AluOp::MUL; writeDst(); break;
        case MicroOp::MDIVI: case MicroOp::MDIV:
            control.aluOp = AluOp::DIV; writeDst(); break;
        case MicroOp::MMODI: case MicroOp::MMOD:
            control.aluOp = AluOp::MOD; writeDst(); break;
        case MicroOp::MANDI: case MicroOp::MAND:
            control.aluOp = AluOp::AND; writeDst(); break;
        case MicroOp::MORI: case MicroOp::MOR:
            control.aluOp = AluOp::OR;  writeDst(); break;
        case MicroOp::MXORI: case MicroOp::MXOR:
            control.aluOp = AluOp::XOR; writeDst(); break;
        case MicroOp::MLSLI: case MicroOp::MLSL:
            control.aluOp = AluOp::LSL; writeDst(); break;
        case MicroOp::MLSRI: case MicroOp::MLSR:
            control.aluOp = AluOp::LSR; writeDst(); break;
        case MicroOp::MASRI: case MicroOp::MASR:
            control.aluOp = AluOp::ASR; writeDst(); break;
        case MicroOp::MNOT:
            control.aluOp = AluOp::NOT; writeDst(); break;
        case MicroOp::MCMPI: case MicroOp::MCMP:
            control.aluOp = AluOp::CMP; break;  // flags only
        case MicroOp::MMOV: case MicroOp::MMOVI:
            // Operand move: the ALU passes the B operand (op2 or the immediate).
            control.aluOp = AluOp::PASS; writeDst(); break;
        case MicroOp::MLD:
            control.aluOp = AluOp::ADD;
            control.isLd = true; writeDst(); break;
        case MicroOp::MST:
            control.aluOp = AluOp::ADD; control.isSt = true; break;
        case MicroOp::MFADD:
            control.aluOp = AluOp::FADD; writeDst(); break;
        case MicroOp::MFSUB:
            control.aluOp = AluOp::FSUB; writeDst(); break;
        case MicroOp::MFMUL:
            control.aluOp = AluOp::FMUL; writeDst(); break;
        case MicroOp::MFDIV:
            control.aluOp = AluOp::FDIV; writeDst(); break;
        case MicroOp::MFCMP:
            control.aluOp = AluOp::FCMP; break;
        case MicroOp::MB:
            control.isUbranch = true; break;
        case MicroOp::MBEQ:
            control.isBeq = true; break;
        case MicroOp::MBGT:
            control.isBgt = true; break;
        case MicroOp::MCALL:
            // The call is handled entirely by the RAS: no GPR writeback.
            control.isCall = true;
            control.isUbranch = true;
            break;
        case MicroOp::MRET:
            control.isRet = true; break;
        case MicroOp::MMOVZ:
            // Privileged move: pass the window source; the MARW stage routes the
            // result to the windowed destination (control.movzDst).
            control.aluOp = AluOp::PASS;
            control.aluSrcB = B_OP2;
            control.isMovz = true;
            break;
        case MicroOp::MRETZ:
            control.isRetz = true; break;
        case MicroOp::MEBREAK:
        case MicroOp::MECALL:
            break;   // no datapath effect; the OF stage marks the exception
        default:
            break;
    }
    return control;
}

ControlSignals ControlUnit::Step() {
    if (upc_ >= microprogram_.size()) {
        busy_ = false;
        return ControlSignals{};
    }

    const Microinstruction& micro = microprogram_[upc_];
    ControlSignals control = decode(micro);

    // stall = 1 means more micro-ops follow, so keep stepping and freeze IF.
    busy_ = micro.stall;
    if (busy_) {
        ++upc_;
    }
    return control;
}

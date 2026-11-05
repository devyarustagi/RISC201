#include "components/control_unit.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {

// Horizontal 64-bit word field positions (must match microassembler6).
constexpr int W_STALL=63, W_ALU=58, W_SRCA=55, W_SRCB=53, W_ISWB=52, W_ISSCR=51,
              W_SCRADDR=47, W_ISLD=46, W_ISST=45, W_ISCALL=44, W_ISRET=43,
              W_ISUB=42, W_ISBEQ=41, W_ISBGT=40, W_ISMOVZ=39, W_MOVZDST=36,
              W_ISRETZ=35, W_DST=30, W_SRC1=25, W_SRC2=20, W_IMM=4;
constexpr int HW_BYTES = 8;

}  // namespace

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
    if (bytes.size() % HW_BYTES != 0) {
        throw std::runtime_error("Error: microprogram ROM length is not a multiple of 8 bytes.");
    }
    microprogram_.clear();
    for (std::size_t i = 0; i < bytes.size(); i += HW_BYTES) {
        uint64_t word = 0;
        for (int b = 0; b < HW_BYTES; ++b) {
            word |= static_cast<uint64_t>(bytes[i + b]) << (8 * b);
        }
        microprogram_.push_back(word);
    }
}

ControlSignals ControlUnit::unpack(uint64_t word) {
    ControlSignals c;
    c.isStall = ((word >> W_STALL) & 1u) != 0;
    c.aluOp = static_cast<AluOp>((word >> W_ALU) & 0x1Fu);
    c.aluSrcA = static_cast<uint8_t>((word >> W_SRCA) & 0x7u);
    c.aluSrcB = static_cast<uint8_t>((word >> W_SRCB) & 0x3u);
    c.isWb = ((word >> W_ISWB) & 1u) != 0;
    c.isScratchWb = ((word >> W_ISSCR) & 1u) != 0;
    c.scrWriteAddr = static_cast<uint8_t>((word >> W_SCRADDR) & 0xFu);
    c.isLd = ((word >> W_ISLD) & 1u) != 0;
    c.isSt = ((word >> W_ISST) & 1u) != 0;
    c.isCall = ((word >> W_ISCALL) & 1u) != 0;
    c.isRet = ((word >> W_ISRET) & 1u) != 0;
    c.isUbranch = ((word >> W_ISUB) & 1u) != 0;
    c.isBeq = ((word >> W_ISBEQ) & 1u) != 0;
    c.isBgt = ((word >> W_ISBGT) & 1u) != 0;
    c.isMovz = ((word >> W_ISMOVZ) & 1u) != 0;
    c.movzDst = static_cast<uint8_t>((word >> W_MOVZDST) & 0x7u);
    c.isRetz = ((word >> W_ISRETZ) & 1u) != 0;
    c.src1ScratchAddr = static_cast<uint8_t>((word >> W_SRC1) & 0xFu);
    c.src2ScratchAddr = static_cast<uint8_t>((word >> W_SRC2) & 0xFu);
    c.microImm = static_cast<int16_t>((word >> W_IMM) & 0xFFFFu);
    return c;
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

ControlSignals ControlUnit::Step() {
    if (upc_ >= microprogram_.size()) {
        busy_ = false;
        return ControlSignals{};
    }
    ControlSignals c = unpack(microprogram_[upc_]);
    busy_ = c.isStall;
    if (busy_) {
        ++upc_;
    }
    return c;
}

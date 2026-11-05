#include "components/memory.hpp"

#include <fstream>
#include <ostream>
#include <stdexcept>

void Memory::LoadProgram(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Error: cannot open program file '" + path + "'.");
    }

    instructionRam_.clear();
    uint8_t bytes[4];
    while (input.read(reinterpret_cast<char*>(bytes), sizeof(bytes))) {
        const uint32_t word = static_cast<uint32_t>(bytes[0]) |
                              (static_cast<uint32_t>(bytes[1]) << 8) |
                              (static_cast<uint32_t>(bytes[2]) << 16) |
                              (static_cast<uint32_t>(bytes[3]) << 24);
        instructionRam_.push_back(word);
    }
}

uint32_t Memory::FetchInstruction(uint32_t pc) const {
    const uint32_t index = pc >> 2;
    if (index >= instructionRam_.size()) {
        return 0;
    }
    return instructionRam_[index];
}

void Memory::WriteInstruction(uint32_t pc, uint32_t instruction) {
    const uint32_t index = pc >> 2;
    if (index < instructionRam_.size()) {
        instructionRam_[index] = instruction;
    }
}

uint32_t Memory::ReadData(uint32_t address) const {
    auto it = dataRam_.find(address & ~0x3u);
    return it == dataRam_.end() ? 0 : it->second;
}

void Memory::WriteData(uint32_t address, uint32_t value) {
    dataRam_[address & ~0x3u] = value;
}

void Memory::DumpData(std::ostream& out) const {
    if (dataRam_.empty()) {
        out << "(data memory empty)\n";
        return;
    }
    for (const auto& [address, value] : dataRam_) {
        out << "mem[" << address << "] = " << value << '\n';
    }
}

void Memory::Reset() {
    instructionRam_.clear();
    dataRam_.clear();
}

void Memory::ResetData() {
    dataRam_.clear();
}

#include "components/scratch_file.hpp"

#include <ostream>

ScratchFile::ScratchFile() {
    Reset();
}

uint32_t ScratchFile::Read(uint8_t index) const {
    return registers_[index & 0xF];
}

void ScratchFile::Write(uint8_t index, uint32_t value) {
    registers_[index & 0xF] = value;
}

void ScratchFile::Reset() {
    registers_.fill(0);
}

void ScratchFile::Dump(std::ostream& out) const {
    for (int i = 0; i < SCRATCH_COUNT; ++i) {
        out << "s" << i << " = " << registers_[i] << '\n';
    }
}

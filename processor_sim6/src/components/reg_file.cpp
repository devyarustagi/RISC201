#include "components/reg_file.hpp"

#include <ostream>

RegFile::RegFile() {
    Reset();
}

uint32_t RegFile::Read(uint8_t index) const {
    if ((index & 0xF) == 0) {
        return 0;
    }
    return registers_[index & 0xF];
}

void RegFile::Write(uint8_t index, uint32_t value) {
    if ((index & 0xF) == 0) {
        return;
    }
    registers_[index & 0xF] = value;
}

void RegFile::Reset() {
    registers_.fill(0);
}

void RegFile::Dump(std::ostream& out) const {
    for (int i = 0; i < GPR_COUNT; ++i) {
        out << "r" << i << " = " << registers_[i] << '\n';
    }
}

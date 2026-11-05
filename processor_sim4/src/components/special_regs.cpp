#include "components/special_regs.hpp"

void SpecialRegs::Reset() {
    eBase_ = 0;
    eCode_ = 0;
    oldPC_ = 0;
    oldSP_ = 0;
    oldFlagE_ = false;
    oldFlagGt_ = false;
    oldFlagLt_ = false;
    cpl_ = 0;   // boot in OS mode; the boot code drops to user mode with retz
}

void SpecialRegs::EnterException(uint32_t pc, uint8_t cause, bool equal, bool greater,
                                 bool less) {
    oldPC_ = pc;
    eCode_ = cause;
    oldFlagE_ = equal;
    oldFlagGt_ = greater;
    oldFlagLt_ = less;
    cpl_ = 0;   // switch to OS mode
}

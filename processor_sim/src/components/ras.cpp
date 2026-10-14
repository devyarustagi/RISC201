#include "components/ras.hpp"

Ras::Ras() {
    Reset();
}

void Ras::Run(bool isCall, bool isRet, bool branchLock, uint32_t pushValue) {
    tosNext_ = tos_;
    pushPending_ = false;
    pushValue_ = pushValue;
    overflowPending_ = false;

    if (branchLock) {
        return;   // Branch Lock Unit overrides the call/ret enables
    }

    if (isCall) {
        if (tos_ < RAS_DEPTH) {
            pushPending_ = true;
            tosNext_ = tos_ + 1;
        } else {
            overflowPending_ = true;   // rasOverflow (handled by the Exception Unit)
        }
    } else if (isRet && tos_ > 0) {
        tosNext_ = tos_ - 1;
    }
}

void Ras::Latch() {
    if (pushPending_) {
        stack_[tosNext_ - 1] = pushValue_;
    }
    tos_ = tosNext_;
}

void Ras::Reset() {
    stack_.fill(0);
    tos_ = 0;
    tosNext_ = 0;
    pushValue_ = 0;
    pushPending_ = false;
    overflowPending_ = false;
}

#include "core/cpu.hpp"

#include <iomanip>
#include <iostream>
#include <ostream>

Cpu::Cpu() {
    Reset();
}

void Cpu::LoadProgram(const std::string& path) {
    memory_.LoadProgram(path);
}

void Cpu::LoadMicrocode(const std::string& microprogramsPath,
                        const std::string& addressTablePath) {
    control_.Load(microprogramsPath, addressTablePath);
}

void Cpu::Reset() {
    gpr_.Reset();
    scratch_.Reset();
    alu_.Reset();
    memory_.ResetData();  // keep the loaded instruction program
    control_.Reset();
    special_.Reset();

    ifOf_.reset();
    ofEx_.reset();
    exMem_.reset();
    stageIf_.Reset();
    stageOf_.Reset();

    halted_ = false;
    cycles_ = 0;
    mispredicts_ = 0;
    dataStalls_ = 0;
    exceptions_ = 0;
    interruptPending_ = false;
}

void Cpu::Step() {
    if (halted_) {
        return;
    }

    // 1. Memory / writeback consumes the oldest packet. A marked instruction
    //    drains here: its commit signals are suppressed and the exception is
    //    reported. Also runs the predictor inversion unit.
    const MarwResult marw = stageMarw_.Run(exMem_, gpr_, memory_, special_);

    // 2. Execute: forward operands, run the ALU, resolve the branch.
    const ExResult ex = stageEx_.Run(ofEx_, exMem_, marw.wb, scratch_, alu_);

    // 3. Operand fetch: decode, drive the microsequencer and the RAS. The branch
    //    lock (a misprediction) gates the RAS call/ret enables.
    const OfResult of = stageOf_.Run(ifOf_, ofEx_, gpr_, control_, special_, ex.mispredict);

    // 4. Data Lock Unit: a load in EX whose rd is read by the instruction in OF.
    const bool loadUse = ofEx_.out.valid && ofEx_.out.control.isLd &&
                         ofEx_.out.control.isWb && ofEx_.out.rd != 0 &&
                         of.opcode < MacroOp::B &&
                         (ofEx_.out.rd == of.rs1 || ofEx_.out.rd == of.rs2 ||
                          (of.control.isSt && ofEx_.out.rd == of.rd));
    const bool isStall = of.control.isStall || loadUse;

    // 5. Fetch: main PC mux (exception > mispredict > retz > ret > stall).
    IfControl ctl;
    if (marw.exception) {
        // Context switch: capture oldPC / eCode / oldFlags and drop to OS mode.
        special_.EnterException(marw.pc, marw.cause, marw.flagE, marw.flagGt, marw.flagLt);
        ctl.exception = true;
        ctl.exceptionPC = special_.EBase();
    } else if (ex.mispredict) {
        ctl.mispredict = true;
        ctl.correctPC = ex.correctPC;
    } else if (of.isRetz) {
        special_.ReturnFromException();
        ctl.isRetz = true;
        ctl.retzPC = special_.OldPC();
    } else {
        ctl.isRet = of.isRet;
        ctl.rasOut = of.rasOut;
        ctl.stall = isStall;
        if (interruptPending_) {
            ctl.interrupt = true;
            ctl.interruptCause = EXC_INTERRUPT;
        }
    }
    stageIf_.Run(ifOf_, memory_, ctl);

    // Branch Lock Unit / exception flush: clear the younger instructions.
    if (marw.exception) {
        exMem_.in = ExMemPacket{};
        ofEx_.in = OfExPacket{};
        ++exceptions_;
    } else if (ex.mispredict) {
        ofEx_.in = OfExPacket{};
        ++mispredicts_;
    } else if (isStall) {
        ofEx_.in = OfExPacket{};   // inject a bubble into EX
        ++dataStalls_;
    }
    if (ctl.interrupt) {
        interruptPending_ = false;
    }

    if (ex.halt) {
        halted_ = true;
    }

    // Rising edge: all state-holding elements latch.
    ifOf_.latch();
    ofEx_.latch();
    exMem_.latch();
    stageOf_.Latch();
    ++cycles_;
}

void Cpu::Run(uint64_t maxCycles, bool traceEnabled) {
    while (!halted_ && cycles_ < maxCycles) {
        Step();
        if (traceEnabled) {
            trace();
        }
    }
}

void Cpu::DumpState(std::ostream& out) const {
    out << "cycles executed : " << cycles_ << '\n';
    out << "halted          : " << (halted_ ? "yes" : "no") << '\n';
    out << "final PC        : " << stageIf_.Pc() << '\n';
    out << "mispredictions  : " << mispredicts_ << '\n';
    out << "load-use stalls : " << dataStalls_ << '\n';
    out << "exceptions      : " << exceptions_ << '\n';
    out << "CPL             : " << static_cast<int>(special_.Cpl()) << '\n';
    out << "eCode           : " << static_cast<int>(special_.ECode()) << '\n';
    out << "oldPC           : " << special_.OldPC() << '\n';

    out << "\n-- general purpose registers --\n";
    gpr_.Dump(out);

    out << "\n-- scratch registers --\n";
    scratch_.Dump(out);

    out << "\n-- data memory --\n";
    memory_.DumpData(out);
}

void Cpu::trace() {
    std::ostream& out = std::cout;
    out << "cyc " << std::setw(3) << cycles_
        << " | next pc=0x" << std::hex << stageIf_.Pc() << std::dec
        << " | IF/OF " << (ifOf_.out.valid ? "v" : "-")
        << " 0x" << std::hex << std::setw(8) << std::setfill('0')
        << ifOf_.out.instruction << std::setfill(' ') << std::dec
        << " pred=" << (ifOf_.out.predictTaken ? 1 : 0)
        << (ifOf_.out.isException ? " !exc" : "")
        << " | OF/EX " << (ofEx_.out.valid ? "v" : "-")
        << " | EX/MEM " << (exMem_.out.valid ? "v" : "-")
        << " mis=" << (exMem_.out.mispredict ? 1 : 0)
        << " exc=" << (exMem_.out.isException ? 1 : 0)
        << " wb=" << (exMem_.out.control.isWb ? 1 : 0)
        << " ld=" << (exMem_.out.control.isLd ? 1 : 0)
        << " st=" << (exMem_.out.control.isSt ? 1 : 0)
        << '\n';
}

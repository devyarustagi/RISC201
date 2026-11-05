#include "core/cpu.hpp"

#include <iomanip>
#include <iostream>
#include <ostream>

namespace {

// Does the instruction in RF read register `reg`? (Data Lock Unit, load-use.)
bool readsRegister(const IbuRfPacket& rf, uint8_t reg) {
    if (!rf.valid || reg == 0) {
        return false;
    }
    const uint8_t opcode = static_cast<uint8_t>(rf.instruction >> 26);
    if (rf.control.isMovz) {
        return rf.windowSrc == WIN_SP && reg == GPR_SP;
    }
    if (opcode >= MacroOp::B) {
        return false;   // branches, call/ret and the special group read no GPR
    }
    return rf.rs1 == reg || rf.rs2 == reg || (rf.control.isSt && rf.rd == reg);
}

}  // namespace

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
    memory_.ResetData();
    control_.Reset();
    special_.Reset();

    ifIbu_.reset();
    ibuRf_.reset();
    rfEx_.reset();
    exMa_.reset();
    maRw_.reset();
    stageIf_.Reset();
    stageIbu_.Reset();

    halted_ = false;
    cycles_ = 0;
    mispredicts_ = 0;
    dataStalls_ = 0;
    exceptions_ = 0;
    retired_ = 0;
    interruptPending_ = false;
    debugStopped_ = false;
    lastRetired_ = RetireInfo{};
    lastStore_ = StoreInfo{};
    haltPc_ = 0;
}

void Cpu::Step() {
    if (halted_) {
        return;
    }
    debugStopped_ = false;
    lastStore_ = StoreInfo{};
    const MaRwPacket retiring = maRw_.out;

    // ---- RW: commit the oldest instruction, or report its exception.
    const RwResult rw = stageRw_.Run(maRw_, gpr_, special_);

    // ---- MA: skipped when an exception is taken (its instruction is younger).
    if (rw.exception) {
        maRw_.in = MaRwPacket{};
    } else {
        stageMa_.Run(exMa_, maRw_, memory_);
        if (maRw_.in.stored) {
            lastStore_ = StoreInfo{true, maRw_.in.pc, maRw_.in.instruction};
        }
    }

    // ---- EX: skipped while an older marked instruction is in MA or RW.
    const bool olderMarked = rw.exception || (exMa_.out.valid && exMa_.out.isException);
    ExResult ex;
    if (olderMarked) {
        exMa_.in = ExMaPacket{};
    } else {
        ex = stageEx_.Run(rfEx_, exMa_, rw.wb, scratch_, alu_);
    }

    // ---- Data Lock Unit.
    // Load-use: a load in EX whose result the instruction in RF needs.
    const RfExPacket& inEx = rfEx_.out;
    const bool loadUse = inEx.valid && inEx.control.isLd && inEx.control.isWb &&
                         readsRegister(ibuRf_.out, inEx.rd);
    // Special registers: retz waits for RF, EX and MA to drain; movz waits for an
    // older movz (they are read in IBU / RF and written in RW).
    bool specialLock = false;
    if (ifIbu_.out.valid) {
        const uint8_t opcode = static_cast<uint8_t>(ifIbu_.out.instruction >> 26);
        if (opcode == MacroOp::RETZ) {
            specialLock = ibuRf_.out.valid || rfEx_.out.valid || exMa_.out.valid;
        } else if (opcode == MacroOp::MOVZ) {
            specialLock = (ibuRf_.out.valid && ibuRf_.out.control.isMovz) ||
                          (rfEx_.out.valid && rfEx_.out.control.isMovz) ||
                          (exMa_.out.valid && exMa_.out.control.isMovz);
        }
    }

    // ---- RF: read the register file (RW has already written it this cycle).
    if (loadUse) {
        rfEx_.in = RfExPacket{};     // bubble into EX
        ibuRf_.in = ibuRf_.out;      // hold the instruction in RF
    } else {
        stageRf_.Run(ibuRf_, rfEx_, gpr_, special_);
    }

    // ---- IBU: decode, Control Unit, RAS, exception marking.
    IbuResult ibu;
    if (loadUse) {
        // Frozen: IBU/RF already holds its instruction, IF/IBU is held below.
    } else if (specialLock) {
        ibuRf_.in = IbuRfPacket{};   // bubble into RF
    } else {
        ibu = stageIbu_.Run(ifIbu_, ibuRf_, control_, special_);
    }
    const bool stall = loadUse || specialLock || (ibu.valid && ibu.control.isStall);

    // ---- IF: main PC mux (exception > mispredict > retz > ret > stall).
    IfControl ctl;
    const bool trapped = rw.exception && debugTrap_ && debugTrap_(rw.pc, rw.cause);
    if (trapped) {
        ctl.exception = true;
        ctl.exceptionPC = rw.pc;
        debugStopped_ = true;
    } else if (rw.exception) {
        special_.EnterException(rw.pc, rw.cause, rw.flagE, rw.flagGt, rw.flagLt);
        ctl.exception = true;
        ctl.exceptionPC = special_.EBase();
    } else if (ex.mispredict) {
        ctl.mispredict = true;
        ctl.correctPC = ex.correctPC;
    } else if (ibu.isRetz) {
        special_.ReturnFromException();
        ctl.isRetz = true;
        ctl.retzPC = special_.OldPC();
    } else if (ibu.isRet) {
        ctl.isRet = true;
        ctl.rasOut = ibu.rasOut;
    } else {
        ctl.stall = stall;
        if (interruptPending_ && !stall) {
            ctl.interrupt = true;
            ctl.interruptCause = EXC_INTERRUPT;
        }
    }
    stageIf_.Run(ifIbu_, memory_, ctl);
    if (ctl.interrupt) {
        interruptPending_ = false;
    }

    // ---- Flushes (Branch Lock Unit / exception) and RAS repair.
    if (rw.exception) {
        exMa_.in = ExMaPacket{};
        rfEx_.in = RfExPacket{};
        ibuRf_.in = IbuRfPacket{};
        stageIbu_.RestoreRas(retiring.rasBefore);
        if (!trapped) {
            ++exceptions_;
        }
    } else if (ex.mispredict) {
        rfEx_.in = RfExPacket{};
        ibuRf_.in = IbuRfPacket{};
        stageIbu_.RestoreRas(rfEx_.out.rasAfter);
        ++mispredicts_;
    } else if (stall && !(ibu.valid && ibu.control.isStall)) {
        ++dataStalls_;
    }

    if (rw.halt) {
        halted_ = true;
        haltPc_ = retiring.pc;
    }

    // ---- What left the pipeline this cycle.
    lastRetired_ = RetireInfo{};
    if (retiring.valid) {
        lastRetired_.valid = true;
        lastRetired_.pc = retiring.pc;
        lastRetired_.instruction = retiring.instruction;
        lastRetired_.exception = rw.exception;
        lastRetired_.cause = rw.exception ? rw.cause : static_cast<uint8_t>(EXC_NONE);
        lastRetired_.trapped = trapped;
        if (!rw.exception) {
            ++retired_;
            lastRetired_.wroteGpr = rw.wb.wb;
            lastRetired_.rd = rw.wb.dest;
            lastRetired_.value = rw.wb.value;
            lastRetired_.stored = retiring.stored;
            lastRetired_.storeAddress = retiring.aluResult & ~0x3u;
            lastRetired_.storeValue = retiring.op2;
        }
    }

    // ---- Rising edge.
    ifIbu_.latch();
    ibuRf_.latch();
    rfEx_.latch();
    exMa_.latch();
    maRw_.latch();
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
    out << "pipeline        : 6-stage (IF IBU RF EX MA RW)\n";
    out << "cycles executed : " << cycles_ << '\n';
    out << "halted          : " << (halted_ ? "yes" : "no") << '\n';
    out << "final PC        : " << stageIf_.Pc() << '\n';
    out << "mispredictions  : " << mispredicts_ << '\n';
    out << "load-use stalls : " << dataStalls_ << '\n';
    out << "instructions    : " << retired_ << '\n';
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
    const auto slot = [](std::ostream& out, const char* name, const PacketBase& p) {
        out << " | " << name << ' ';
        if (p.valid) {
            out << "0x" << std::hex << std::setw(4) << std::setfill('0') << p.pc
                << std::setfill(' ') << std::dec << (p.isException ? "!" : " ");
        } else {
            out << "  -    ";
        }
    };
    out << "cyc " << std::setw(4) << cycles_ << " | IF 0x" << std::hex << std::setw(4)
        << std::setfill('0') << stageIf_.Pc() << std::setfill(' ') << std::dec;
    slot(out, "IBU", ifIbu_.out);
    slot(out, "RF", ibuRf_.out);
    slot(out, "EX", rfEx_.out);
    slot(out, "MA", exMa_.out);
    slot(out, "RW", maRw_.out);
    out << '\n';
}

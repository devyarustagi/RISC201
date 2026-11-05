#include "debugger/debugger.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "core/cpu.hpp"
#include "debugger/disasm.hpp"

namespace {

constexpr uint32_t kEbreakWord = static_cast<uint32_t>(MacroOp::EBREAK) << 26;

std::string hex32(uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return buffer;
}

std::string hexAddr(uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%04x", value);
    return buffer;
}

std::string regName(uint32_t index) {
    return index == GPR_SP ? "sp" : "r" + std::to_string(index);
}

// Parses 42, -7, 0x2a, 0b101.
bool parseNumber(const std::string& text, uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    try {
        size_t used = 0;
        long long parsed = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
            parsed = std::stoll(text.substr(2), &used, 2);
            used += 2;
        } else {
            parsed = std::stoll(text, &used, 0);
        }
        if (used != text.size()) {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

// r0 - r15, or sp (= r14).
bool parseRegister(const std::string& text, uint32_t& index) {
    if (text == "sp") {
        index = GPR_SP;
        return true;
    }
    if (text.size() >= 2 && text[0] == 'r') {
        uint32_t n = 0;
        if (parseNumber(text.substr(1), n) && n < GPR_COUNT) {
            index = n;
            return true;
        }
    }
    return false;
}

bool parseException(const std::string& text, uint8_t& cause) {
    for (uint8_t c = EXC_PBRK; c <= EXC_INTERRUPT; ++c) {
        if (ExceptionName(c) == text) {
            cause = c;
            return true;
        }
    }
    return false;
}

const char* aluName(AluOp op) {
    switch (op) {
        case AluOp::ADD: return "ADD";   case AluOp::SUB: return "SUB";
        case AluOp::MUL: return "MUL";   case AluOp::DIV: return "DIV";
        case AluOp::AND: return "AND";   case AluOp::OR: return "OR";
        case AluOp::LSL: return "LSL";   case AluOp::LSR: return "LSR";
        case AluOp::ASR: return "ASR";   case AluOp::NOT: return "NOT";
        case AluOp::CMP: return "CMP";   case AluOp::FADD: return "FADD";
        case AluOp::FSUB: return "FSUB"; case AluOp::FMUL: return "FMUL";
        case AluOp::FDIV: return "FDIV"; case AluOp::FCMP: return "FCMP";
        case AluOp::PASS: return "PASS"; case AluOp::MOD: return "MOD";
        case AluOp::XOR: return "XOR";   case AluOp::ITOF: return "ITOF";
        case AluOp::FTOI: return "FTOI";
    }
    return "?";
}

// One-line summary of what a control word does (for the pipeline view).
std::string describeControl(const ControlSignals& c, uint8_t rd) {
    std::string text;
    if (c.isLd) {
        text = "load";
    } else if (c.isSt) {
        text = "store";
    } else if (c.isCall) {
        text = "call (push RAS)";
    } else if (c.isRet) {
        text = "ret (pop RAS)";
    } else if (c.isUbranch) {
        text = "branch";
    } else if (c.isBeq) {
        text = "branch if E";
    } else if (c.isBgt) {
        text = "branch if GT";
    } else if (c.isRetz) {
        text = "retz";
    } else {
        text = std::string("ALU ") + aluName(c.aluOp);
    }
    if (c.isMovz) {
        text += " -> window " + std::to_string(c.movzDst);
    } else if (c.isWb) {
        text += " -> " + regName(rd);
    } else if (c.isScratchWb) {
        text += " -> s" + std::to_string(c.scrWriteAddr);
    }
    return text;
}

std::vector<std::string> split(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) {
        words.push_back(word);
    }
    return words;
}

}  // namespace

Debugger::Debugger(Cpu& cpu, std::string programPath, uint64_t maxCycles)
    : cpu_(cpu), programPath_(std::move(programPath)), maxCycles_(maxCycles) {
    resetMachine();
}

// ---------------------------------------------------------------- execution --

void Debugger::resetMachine() {
    cpu_.LoadProgram(programPath_);   // fresh copy: drops predictor flips and patches
    cpu_.Reset();
    stepOverPending_ = false;
    frames_.clear();
    for (Breakpoint& bp : breakpoints_) {
        bp.inserted = false;
        if (bp.enabled && !bp.hardware) {
            insertBreakpoint(bp);
        }
    }
    for (Watchpoint& wp : watchpoints_) {
        wp.lastValue = watchValue(wp);
    }
    // Software breakpoints: intercept the pbrk exception of an inserted ebreak.
    cpu_.SetDebugTrap([this](uint32_t pc, uint8_t cause) {
        if (cause != EXC_PBRK) {
            return false;
        }
        const Breakpoint* bp = softBreakpointAt(pc);
        return bp != nullptr && bp->inserted;
    });
}

void Debugger::insertBreakpoint(Breakpoint& bp) {
    if (bp.hardware || bp.inserted) {
        return;
    }
    if ((bp.address >> 2) >= cpu_.Mem().InstructionRam().size()) {
        return;   // outside the loaded program: nothing to patch
    }
    bp.original = cpu_.Mem().FetchInstruction(bp.address);
    cpu_.Mem().WriteInstruction(bp.address, kEbreakWord);
    bp.inserted = true;
}

void Debugger::removeBreakpoint(Breakpoint& bp) {
    if (!bp.inserted) {
        return;
    }
    cpu_.Mem().WriteInstruction(bp.address, bp.original);
    bp.inserted = false;
}

// The in-memory branch predictor writes `instruction ^ bit25` back at commit.
// If a copy of a branch fetched before its breakpoint was inserted commits
// later, it overwrites the ebreak: take the new word as the original and
// re-insert the breakpoint.
void Debugger::keepBreakpointsIntact() {
    for (Breakpoint& bp : breakpoints_) {
        if (bp.inserted && cpu_.Mem().FetchInstruction(bp.address) != kEbreakWord) {
            bp.original = cpu_.Mem().FetchInstruction(bp.address);
            cpu_.Mem().WriteInstruction(bp.address, kEbreakWord);
        }
    }
}

Debugger::Breakpoint* Debugger::softBreakpointAt(uint32_t address) {
    for (Breakpoint& bp : breakpoints_) {
        if (!bp.hardware && bp.address == address) {
            return &bp;
        }
    }
    return nullptr;
}

uint32_t Debugger::originalInstruction(uint32_t address) const {
    for (const Breakpoint& bp : breakpoints_) {
        if (!bp.hardware && bp.inserted && bp.address == address) {
            return bp.original;
        }
    }
    return cpu_.Mem().FetchInstruction(address);
}

// A committed call opens a frame (sp at that moment); a committed ret closes it.
void Debugger::trackFrames() {
    const RetireInfo& r = cpu_.LastRetired();
    if (!r.valid || r.exception) {
        return;
    }
    const uint8_t opcode = static_cast<uint8_t>(r.instruction >> 26);
    if (opcode == static_cast<uint8_t>(MacroOp::CALL)) {
        frames_.push_back(Frame{r.pc, r.pc + 4, cpu_.Gpr().Read(GPR_SP)});
    } else if (opcode == static_cast<uint8_t>(MacroOp::RET) && !frames_.empty()) {
        frames_.pop_back();
    }
}

uint32_t Debugger::watchValue(const Watchpoint& wp) const {
    return wp.memory ? cpu_.Mem().ReadData(wp.target)
                     : cpu_.Gpr().Read(static_cast<uint8_t>(wp.target));
}

Debugger::StopReason Debugger::advance(uint64_t cycleLimit, uint64_t retireLimit,
                                       std::ostream& out) {
    uint64_t cycles = 0;
    uint64_t retired = 0;
    stopMessage_.clear();

    while (true) {
        if (cpu_.Halted()) {
            stopMessage_ = "Program halted (branch to self at " + hexAddr(cpu_.ExMem().out.pc) +
                           ") after " + std::to_string(cpu_.Cycles()) + " cycles.";
            return StopReason::Halted;
        }
        if (cycleLimit != 0 && cycles >= cycleLimit) {
            return StopReason::CycleCount;
        }
        if (retireLimit != 0 && retired >= retireLimit) {
            return StopReason::InstructionCount;
        }
        if (cycles >= maxCycles_) {
            stopMessage_ = "Stopped: cycle limit (" + std::to_string(maxCycles_) +
                           " cycles per command) reached. Use 'continue' to go on.";
            return StopReason::Limit;
        }

        const bool prevFetchValid = cpu_.IfOf().out.valid;
        const uint32_t prevFetchPc = cpu_.IfOf().out.pc;

        cpu_.Step();
        ++cycles;
        const RetireInfo& r = cpu_.LastRetired();
        if (r.valid) {
            ++retired;
        }
        trackFrames();

        // The breakpoint we resumed from has left the pipeline: put it back.
        if (stepOverPending_ && ((r.valid && r.pc == stepOverAddress_) || cpu_.Halted())) {
            stepOverPending_ = false;
            if (Breakpoint* bp = softBreakpointAt(stepOverAddress_); bp && bp->enabled) {
                insertBreakpoint(*bp);
            }
        }
        keepBreakpointsIntact();

        if (traceEnabled_) {
            printTraceLine(out);
        }

        // 1. Software breakpoint: the ebreak's pbrk was intercepted at MA-RW.
        if (cpu_.DebugStopped()) {
            Breakpoint* bp = softBreakpointAt(r.pc);
            if (bp != nullptr) {
                ++bp->hits;
                removeBreakpoint(*bp);          // show / run the real instruction
                stepOverPending_ = true;
                stepOverAddress_ = r.pc;
                stopMessage_ = "Breakpoint " + std::to_string(bp->id) + " at " +
                               hexAddr(r.pc) + ": " + Disassemble(bp->original, r.pc) +
                               "   (cycle " + std::to_string(cpu_.Cycles()) + ")";
            }
            return StopReason::Breakpoint;
        }

        // 2. A caught exception was taken (context already switched to eBase).
        if (r.valid && r.exception && caught_.count(r.cause) != 0) {
            const SpecialRegs& sp = cpu_.Special();
            stopMessage_ = "Caught exception " + ExceptionName(r.cause) + " at " +
                           hexAddr(r.pc) + ": " +
                           Disassemble(originalInstruction(r.pc), r.pc) +
                           "\n  eCode=" + std::to_string(sp.ECode()) +
                           "  oldPC=" + hexAddr(sp.OldPC()) +
                           "  oldFlags=E:" + std::to_string(sp.OldFlagE()) +
                           " GT:" + std::to_string(sp.OldFlagGt()) +
                           " LT:" + std::to_string(sp.OldFlagLt()) +
                           "  CPL=" + std::to_string(sp.Cpl()) +
                           "  -> handler at eBase=" + hexAddr(sp.EBase());
            return StopReason::Caught;
        }

        // 3. Watchpoints.
        bool watchHit = false;
        for (Watchpoint& wp : watchpoints_) {
            const uint32_t now = watchValue(wp);
            if (now != wp.lastValue) {
                std::ostringstream msg;
                msg << (stopMessage_.empty() ? "" : "\n") << "Watchpoint " << wp.id << ": "
                    << (wp.memory ? "mem[" + hexAddr(wp.target) + "]" : regName(wp.target))
                    << " changed " << wp.lastValue << " -> " << now;
                if (r.valid && !r.exception) {
                    msg << "   (by " << hexAddr(r.pc) << ": "
                        << Disassemble(r.instruction, r.pc) << ", cycle " << cpu_.Cycles()
                        << ")";
                }
                stopMessage_ += msg.str();
                wp.lastValue = now;
                watchHit = true;
            }
        }
        if (watchHit) {
            return StopReason::Watch;
        }

        // 4. Hardware breakpoint: the address was just fetched into IF/OF.
        const IfOfPacket& fetched = cpu_.IfOf().out;
        if (fetched.valid && (!prevFetchValid || prevFetchPc != fetched.pc)) {
            for (Breakpoint& bp : breakpoints_) {
                if (bp.hardware && bp.enabled && bp.address == fetched.pc) {
                    ++bp.hits;
                    stopMessage_ = "Hardware breakpoint " + std::to_string(bp.id) + " at " +
                                   hexAddr(bp.address) + " fetched: " +
                                   Disassemble(fetched.instruction, fetched.pc) +
                                   "   (cycle " + std::to_string(cpu_.Cycles()) + ")";
                    return StopReason::HardBreakpoint;
                }
            }
        }
    }
}

// ------------------------------------------------------------------- output --

std::string Debugger::describeRetire() const {
    const RetireInfo& r = cpu_.LastRetired();
    if (!r.valid) {
        return "(nothing)";
    }
    // A trapped breakpoint retires as the patched ebreak: show the real instruction.
    const uint32_t inst = r.trapped ? originalInstruction(r.pc) : r.instruction;
    std::string text = hexAddr(r.pc) + "  " + Disassemble(inst, r.pc);
    if (r.exception) {
        text += r.trapped ? "   [breakpoint trap]"
                          : "   [EXCEPTION " + ExceptionName(r.cause) + " -> eBase]";
    } else if (r.wroteGpr) {
        text += "   " + regName(r.rd) + " <- " + std::to_string(static_cast<int32_t>(r.value));
    } else if (r.stored) {
        text += "   mem[" + hexAddr(r.storeAddress) + "] <- " +
                std::to_string(static_cast<int32_t>(r.storeValue));
    }
    return text;
}

void Debugger::printTraceLine(std::ostream& out) const {
    const auto stage = [](bool valid, uint32_t pc, uint32_t inst, bool marked) {
        if (!valid) {
            return std::string("-");
        }
        std::string text = hexAddr(pc) + " " + MnemonicOf(static_cast<uint8_t>(inst >> 26));
        if (marked) {
            text += "!";
        }
        return text;
    };
    const auto& ifOf = cpu_.IfOf().out;
    const auto& ofEx = cpu_.OfEx().out;
    const auto& exMa = cpu_.ExMem().out;
    out << "cyc " << std::setw(4) << cpu_.Cycles() << " | fetch " << hexAddr(cpu_.Pc())
        << " | OF " << std::left << std::setw(13)
        << stage(ifOf.valid, ifOf.pc, ifOf.instruction, ifOf.isException)
        << "| EX " << std::setw(13) << stage(ofEx.valid, ofEx.pc, ofEx.instruction, ofEx.isException)
        << "| MA " << std::setw(13) << stage(exMa.valid, exMa.pc, exMa.instruction, exMa.isException)
        << std::right << "| retired " << describeRetire() << '\n';
}

void Debugger::printStop(StopReason reason, std::ostream& out) const {
    if (!stopMessage_.empty()) {
        out << stopMessage_ << '\n';
    }
    if (reason == StopReason::Breakpoint || reason == StopReason::HardBreakpoint ||
        reason == StopReason::Caught || reason == StopReason::Watch) {
        const uint32_t pc = cpu_.Pc();
        out << "Next fetch => " << hexAddr(pc) << ": "
            << Disassemble(originalInstruction(pc), pc) << '\n';
    }
}

void Debugger::printPipeline(std::ostream& out) const {
    const auto& ifOf = cpu_.IfOf().out;
    const auto& ofEx = cpu_.OfEx().out;
    const auto& exMa = cpu_.ExMem().out;
    const auto mark = [](bool marked, uint8_t cause) {
        return marked ? "   ! marked " + ExceptionName(cause) : std::string();
    };

    out << "cycle " << cpu_.Cycles() << "   next fetch " << hexAddr(cpu_.Pc()) << "   CPL "
        << static_cast<int>(cpu_.Special().Cpl()) << "   flags E:" << cpu_.FlagE()
        << " GT:" << cpu_.FlagGt() << " LT:" << cpu_.FlagLt() << "   RAS depth "
        << cpu_.RasUnit().Tos() << (cpu_.Halted() ? "   [HALTED]" : "") << '\n';

    out << "  IF/OF  ";
    if (ifOf.valid) {
        out << hexAddr(ifOf.pc) << "  " << std::left << std::setw(26)
            << Disassemble(ifOf.instruction, ifOf.pc) << std::right << "(waiting for OF)"
            << mark(ifOf.isException, ifOf.cause) << '\n';
    } else {
        out << "(bubble)\n";
    }

    out << "  OF/EX  ";
    if (ofEx.valid) {
        out << hexAddr(ofEx.pc) << "  " << std::left << std::setw(26)
            << Disassemble(ofEx.instruction, ofEx.pc) << std::right
            << describeControl(ofEx.control, ofEx.rd) << "  read op1=" << static_cast<int32_t>(ofEx.op1)
            << " op2=" << static_cast<int32_t>(ofEx.op2) << mark(ofEx.isException, ofEx.cause)
            << '\n';
    } else {
        out << "(bubble)\n";
    }

    out << "  EX/MA  ";
    if (exMa.valid) {
        out << hexAddr(exMa.pc) << "  " << std::left << std::setw(26)
            << Disassemble(exMa.instruction, exMa.pc) << std::right
            << describeControl(exMa.control, exMa.rd)
            << "  result=" << static_cast<int32_t>(exMa.aluResult)
            << (exMa.mispredict ? "  MISPREDICT" : "") << mark(exMa.isException, exMa.cause)
            << '\n';
    } else {
        out << "(bubble)\n";
    }

    out << "  retired  " << describeRetire() << '\n';
}

void Debugger::printRegisters(std::ostream& out) const {
    for (int i = 0; i < GPR_COUNT; ++i) {
        const uint32_t v = cpu_.Gpr().Read(static_cast<uint8_t>(i));
        out << std::left << std::setw(4) << regName(static_cast<uint32_t>(i)) << std::right
            << "= " << hex32(v) << " " << std::setw(11) << static_cast<int32_t>(v)
            << ((i % 2 == 1) ? "\n" : "     ");
    }
    out << "pc (next fetch) = " << hexAddr(cpu_.Pc()) << "   flags E:" << cpu_.FlagE()
        << " GT:" << cpu_.FlagGt() << " LT:" << cpu_.FlagLt() << '\n';
}

void Debugger::printSpecial(std::ostream& out) const {
    const SpecialRegs& sp = cpu_.Special();
    out << "CPL      = " << static_cast<int>(sp.Cpl()) << (sp.Cpl() == 0 ? " (OS mode)" : " (user mode)")
        << '\n'
        << "eBase    = " << hexAddr(sp.EBase()) << '\n'
        << "eCode    = " << static_cast<int>(sp.ECode()) << " (" << ExceptionName(sp.ECode()) << ")\n"
        << "oldPC    = " << hexAddr(sp.OldPC()) << '\n'
        << "oldSP    = " << hex32(sp.OldSP()) << '\n'
        << "oldFlags = E:" << sp.OldFlagE() << " GT:" << sp.OldFlagGt() << " LT:" << sp.OldFlagLt()
        << '\n'
        << "interrupt pending = " << (cpu_.InterruptPending() ? "yes" : "no") << '\n';
}

void Debugger::printDisasm(uint32_t start, int count, std::ostream& out) const {
    const size_t words = cpu_.Mem().InstructionRam().size();
    start &= ~0x3u;
    for (int i = 0; i < count; ++i) {
        const uint32_t address = start + static_cast<uint32_t>(i) * 4;
        if ((address >> 2) >= words) {
            break;
        }
        bool hasBreak = false;
        for (const Breakpoint& bp : breakpoints_) {
            hasBreak = hasBreak || (bp.address == address && bp.enabled);
        }
        const uint32_t inst = originalInstruction(address);
        out << (address == cpu_.Pc() ? "=> " : "   ") << (hasBreak ? "* " : "  ")
            << hexAddr(address) << ":  " << hex32(inst) << "  " << Disassemble(inst, address)
            << '\n';
    }
}

void Debugger::printStats(std::ostream& out) const {
    const uint64_t cycles = cpu_.Cycles();
    const uint64_t retired = cpu_.Retired();
    out << "cycles           " << cycles << '\n'
        << "instructions     " << retired << '\n';
    if (retired != 0) {
        out << "CPI              " << std::fixed << std::setprecision(2)
            << static_cast<double>(cycles) / static_cast<double>(retired)
            << std::defaultfloat << '\n';
    }
    out << "mispredictions   " << cpu_.Mispredicts() << '\n'
        << "stall cycles     " << cpu_.DataStalls() << '\n'
        << "exceptions taken " << cpu_.Exceptions() << '\n'
        << "halted           " << (cpu_.Halted() ? "yes" : "no") << '\n';
}

void Debugger::printHelp(std::ostream& out) const {
    out << "Execution\n"
           "  run                      restart the program from the beginning and run\n"
           "  continue | c             run until a breakpoint / watchpoint / caught exception / halt\n"
           "  step [n] | s [n]         advance n clock cycles (default 1), then show the pipeline\n"
           "  next [n] | n [n]         advance until n instructions leave the pipeline\n"
           "  reset                    reload the program and reset the CPU (breakpoints kept)\n"
           "  interrupt                raise a hardware interrupt (next fetched instruction is marked)\n"
           "Breakpoints and watchpoints\n"
           "  break <addr> | b <addr>  software breakpoint (patches ebreak; stops precisely at MA-RW)\n"
           "  hbreak <addr>            hardware breakpoint (stops when the address is fetched)\n"
           "  watch r<n> | watch mem <addr>   stop when the value changes\n"
           "  catch <exc>... | catch all       stop when an exception is taken\n"
           "                           (pbrk scall div0 rasovf wronginst lowpriv interrupt)\n"
           "  uncatch <exc>... | uncatch all\n"
           "  delete [id] | enable <id> | disable <id>\n"
           "  info break | info watch | info catch\n"
           "Inspection\n"
           "  regs                     general-purpose registers, pc, flags\n"
           "  print r<n> | print mem <addr> | print pc\n"
           "  set r<n> <value> | set mem <addr> <value> | set inst <addr> <word>\n"
           "  x <addr> [n]             n data-memory words from addr\n"
           "  mem                      every data-memory word written so far\n"
           "  disasm [addr] [n]        disassemble instruction memory (=> next fetch, * breakpoint)\n"
           "  pipe                     what each pipeline stage holds\n"
           "  special                  exception registers: CPL eBase eCode oldPC oldSP oldFlags\n"
           "  scratch                  microcode scratch registers s0-s15\n"
           "  stats                    cycles, instructions, CPI, mispredictions, stalls, exceptions\n"
           "  trace on|off             print one line per cycle while running\n"
           "  help | quit\n"
           "Numbers: decimal, 0x hex or 0b binary.\n";
}

// ----------------------------------------------------------------- commands --

bool Debugger::Execute(const std::string& line, std::ostream& out) {
    const std::vector<std::string> args = split(line);
    if (args.empty() || args[0][0] == '#') {
        return true;
    }
    const std::string& cmd = args[0];
    const auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    uint32_t number = 0;
    uint32_t index = 0;

    if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        return false;
    }
    if (cmd == "help" || cmd == "h" || cmd == "?") {
        printHelp(out);
    } else if (cmd == "run" || cmd == "r") {
        resetMachine();
        printStop(advance(0, 0, out), out);
    } else if (cmd == "continue" || cmd == "c") {
        printStop(advance(0, 0, out), out);
    } else if (cmd == "step" || cmd == "s") {
        uint32_t n = 1;
        if (!arg(1).empty() && !parseNumber(arg(1), n)) {
            out << "usage: step [n]\n";
            return true;
        }
        const bool savedTrace = traceEnabled_;
        traceEnabled_ = n > 1;   // one line per cycle for multi-cycle steps
        const StopReason reason = advance(n, 0, out);
        traceEnabled_ = savedTrace;
        printStop(reason, out);
        printPipeline(out);
    } else if (cmd == "next" || cmd == "n") {
        uint32_t n = 1;
        if (!arg(1).empty() && !parseNumber(arg(1), n)) {
            out << "usage: next [n]\n";
            return true;
        }
        const StopReason reason = advance(0, n, out);
        printStop(reason, out);
        if (reason == StopReason::InstructionCount) {
            out << "retired  " << describeRetire() << '\n';
        }
    } else if (cmd == "reset") {
        resetMachine();
        out << "Program reloaded; CPU reset (cycle 0, CPL 0).\n";
    } else if (cmd == "interrupt") {
        cpu_.RequestInterrupt();
        out << "Interrupt requested: the next instruction fetched will be marked.\n";
    } else if (cmd == "break" || cmd == "b" || cmd == "hbreak") {
        if (!parseNumber(arg(1), number)) {
            out << "usage: " << cmd << " <address>\n";
            return true;
        }
        Breakpoint bp;
        bp.id = nextId_++;
        bp.address = number & ~0x3u;
        bp.hardware = (cmd == "hbreak");
        if (!bp.hardware && softBreakpointAt(bp.address) != nullptr) {
            out << "A breakpoint already exists at " << hexAddr(bp.address) << ".\n";
            return true;
        }
        if (!bp.hardware && (bp.address >> 2) >= cpu_.Mem().InstructionRam().size()) {
            out << "Address " << hexAddr(bp.address) << " is outside the program.\n";
            return true;
        }
        breakpoints_.push_back(bp);
        Breakpoint& added = breakpoints_.back();
        if (!added.hardware && !(stepOverPending_ && stepOverAddress_ == added.address)) {
            insertBreakpoint(added);
        }
        out << (added.hardware ? "Hardware breakpoint " : "Breakpoint ") << added.id << " at "
            << hexAddr(added.address) << ": "
            << Disassemble(originalInstruction(added.address), added.address) << '\n';
    } else if (cmd == "watch") {
        Watchpoint wp;
        if (arg(1) == "mem" && parseNumber(arg(2), number)) {
            wp.memory = true;
            wp.target = number & ~0x3u;
        } else if (parseRegister(arg(1), index)) {
            wp.target = index;
        } else {
            out << "usage: watch r<n> | watch mem <addr>\n";
            return true;
        }
        wp.id = nextId_++;
        wp.lastValue = watchValue(wp);
        watchpoints_.push_back(wp);
        out << "Watchpoint " << wp.id << ": "
            << (wp.memory ? "mem[" + hexAddr(wp.target) + "]" : regName(wp.target))
            << " (now " << static_cast<int32_t>(wp.lastValue) << ")\n";
    } else if (cmd == "catch" || cmd == "uncatch") {
        const bool add = (cmd == "catch");
        if (args.size() == 1 && !add) {
            out << "usage: uncatch <pbrk|scall|div0|rasovf|wronginst|lowpriv|interrupt|all>...\n";
            return true;
        }
        for (size_t i = 1; i < args.size(); ++i) {
            uint8_t cause = 0;
            if (args[i] == "all") {
                for (uint8_t c = EXC_PBRK; c <= EXC_INTERRUPT; ++c) {
                    add ? (void)caught_.insert(c) : (void)caught_.erase(c);
                }
            } else if (parseException(args[i], cause)) {
                add ? (void)caught_.insert(cause) : (void)caught_.erase(cause);
            } else {
                out << "unknown exception '" << args[i] << "'\n";
            }
        }
        out << "Catching:";
        for (uint8_t c : caught_) {
            out << ' ' << ExceptionName(c);
        }
        out << (caught_.empty() ? " (none)" : "") << '\n';
    } else if (cmd == "delete" || cmd == "d") {
        if (args.size() == 1) {
            for (Breakpoint& bp : breakpoints_) {
                removeBreakpoint(bp);
            }
            breakpoints_.clear();
            watchpoints_.clear();
            out << "Deleted all breakpoints and watchpoints.\n";
            return true;
        }
        if (!parseNumber(arg(1), number)) {
            out << "usage: delete [id]\n";
            return true;
        }
        const int id = static_cast<int>(number);
        for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
            if (it->id == id) {
                removeBreakpoint(*it);
                breakpoints_.erase(it);
                out << "Deleted breakpoint " << id << ".\n";
                return true;
            }
        }
        for (auto it = watchpoints_.begin(); it != watchpoints_.end(); ++it) {
            if (it->id == id) {
                watchpoints_.erase(it);
                out << "Deleted watchpoint " << id << ".\n";
                return true;
            }
        }
        out << "No breakpoint or watchpoint " << id << ".\n";
    } else if (cmd == "enable" || cmd == "disable") {
        if (!parseNumber(arg(1), number)) {
            out << "usage: " << cmd << " <id>\n";
            return true;
        }
        for (Breakpoint& bp : breakpoints_) {
            if (bp.id == static_cast<int>(number)) {
                bp.enabled = (cmd == "enable");
                if (bp.enabled) {
                    insertBreakpoint(bp);
                } else {
                    removeBreakpoint(bp);
                }
                out << "Breakpoint " << bp.id << (bp.enabled ? " enabled.\n" : " disabled.\n");
                return true;
            }
        }
        out << "No breakpoint " << number << ".\n";
    } else if (cmd == "info") {
        if (arg(1) == "break" || arg(1) == "b") {
            if (breakpoints_.empty()) {
                out << "No breakpoints.\n";
            }
            for (const Breakpoint& bp : breakpoints_) {
                out << std::setw(3) << bp.id << "  " << (bp.hardware ? "hw  " : "sw  ")
                    << (bp.enabled ? "on   " : "off  ") << hexAddr(bp.address) << "  "
                    << std::left << std::setw(24)
                    << Disassemble(originalInstruction(bp.address), bp.address) << std::right
                    << " hits " << bp.hits << '\n';
            }
        } else if (arg(1) == "watch" || arg(1) == "w") {
            if (watchpoints_.empty()) {
                out << "No watchpoints.\n";
            }
            for (const Watchpoint& wp : watchpoints_) {
                out << std::setw(3) << wp.id << "  "
                    << (wp.memory ? "mem[" + hexAddr(wp.target) + "]" : regName(wp.target))
                    << " = " << static_cast<int32_t>(watchValue(wp)) << '\n';
            }
        } else if (arg(1) == "catch") {
            return Execute("catch", out);
        } else {
            out << "usage: info break | info watch | info catch\n";
        }
    } else if (cmd == "regs" || cmd == "registers") {
        printRegisters(out);
    } else if (cmd == "print" || cmd == "p") {
        if (arg(1) == "pc") {
            out << "pc (next fetch) = " << hexAddr(cpu_.Pc()) << '\n';
        } else if (arg(1) == "mem" && parseNumber(arg(2), number)) {
            const uint32_t v = cpu_.Mem().ReadData(number);
            out << "mem[" << hexAddr(number & ~0x3u) << "] = " << hex32(v) << " ("
                << static_cast<int32_t>(v) << ")\n";
        } else if (parseRegister(arg(1), index)) {
            const uint32_t v = cpu_.Gpr().Read(static_cast<uint8_t>(index));
            out << regName(index) << " = " << hex32(v) << " (" << static_cast<int32_t>(v) << ")\n";
        } else {
            out << "usage: print r<n> | print mem <addr> | print pc\n";
        }
    } else if (cmd == "set") {
        uint32_t value = 0;
        if (arg(1) == "inst" && parseNumber(arg(2), number) && parseNumber(arg(3), value)) {
            // Patch instruction memory (e.g. plant an invalid opcode for a demo).
            if ((number >> 2) >= cpu_.Mem().InstructionRam().size()) {
                out << "Address " << hexAddr(number) << " is outside the program.\n";
                return true;
            }
            if (Breakpoint* bp = softBreakpointAt(number & ~0x3u); bp && bp->inserted) {
                bp->original = value;     // keep the breakpoint, change what it guards
            } else {
                cpu_.Mem().WriteInstruction(number, value);
            }
            out << "inst[" << hexAddr(number & ~0x3u) << "] = " << hex32(value) << "  "
                << Disassemble(value, number & ~0x3u) << '\n';
        } else if (arg(1) == "mem" && parseNumber(arg(2), number) && parseNumber(arg(3), value)) {
            cpu_.Mem().WriteData(number, value);
            out << "mem[" << hexAddr(number & ~0x3u) << "] = " << static_cast<int32_t>(value) << '\n';
        } else if (parseRegister(arg(1), index) && parseNumber(arg(2), value)) {
            cpu_.Gpr().Write(static_cast<uint8_t>(index), value);
            out << regName(index) << " = " << static_cast<int32_t>(cpu_.Gpr().Read(static_cast<uint8_t>(index)))
                << (index == 0 ? "   (r0 is hardwired to zero)" : "") << '\n';
        } else {
            out << "usage: set r<n> <value> | set mem <addr> <value> | set inst <addr> <word>\n";
        }
        for (Watchpoint& wp : watchpoints_) {
            wp.lastValue = watchValue(wp);   // a manual write is not a watch hit
        }
    } else if (cmd == "x") {
        uint32_t count = 4;
        if (!parseNumber(arg(1), number) || (!arg(2).empty() && !parseNumber(arg(2), count))) {
            out << "usage: x <addr> [n]\n";
            return true;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t address = (number & ~0x3u) + i * 4;
            const uint32_t v = cpu_.Mem().ReadData(address);
            out << "mem[" << hexAddr(address) << "] = " << hex32(v) << "  "
                << static_cast<int32_t>(v) << '\n';
        }
    } else if (cmd == "mem") {
        cpu_.Mem().DumpData(out);
    } else if (cmd == "disasm" || cmd == "list" || cmd == "l") {
        uint32_t count = 16;
        number = 0;
        if (!arg(1).empty() && !parseNumber(arg(1), number)) {
            out << "usage: disasm [addr] [n]\n";
            return true;
        }
        if (!arg(2).empty() && !parseNumber(arg(2), count)) {
            out << "usage: disasm [addr] [n]\n";
            return true;
        }
        printDisasm(number, static_cast<int>(count), out);
    } else if (cmd == "pipe" || cmd == "pipeline") {
        printPipeline(out);
    } else if (cmd == "special") {
        printSpecial(out);
    } else if (cmd == "scratch") {
        cpu_.Scratch().Dump(out);
    } else if (cmd == "stats") {
        printStats(out);
    } else if (cmd == "trace") {
        if (arg(1) == "on") {
            traceEnabled_ = true;
        } else if (arg(1) == "off") {
            traceEnabled_ = false;
        } else {
            out << "usage: trace on|off\n";
            return true;
        }
        out << "trace " << (traceEnabled_ ? "on" : "off") << '\n';
    } else {
        out << "Unknown command '" << cmd << "'. Type 'help'.\n";
    }
    return true;
}

void Debugger::Repl(std::istream& in, std::ostream& out, bool echoCommands) {
    out << "rdb - RISC201 pipeline debugger. Program: " << programPath_ << " ("
        << cpu_.Mem().InstructionRam().size() << " instructions). Type 'help'.\n";
    std::string line;
    while (true) {
        out << "(rdb) " << std::flush;
        if (!std::getline(in, line)) {
            out << '\n';
            break;
        }
        if (echoCommands) {
            out << line << '\n';
        }
        try {
            if (!Execute(line, out)) {
                break;
            }
        } catch (const std::exception& e) {
            out << "error: " << e.what() << '\n';
        }
    }
}

#ifndef PROCESSOR_SIM_DEBUGGER_HPP
#define PROCESSOR_SIM_DEBUGGER_HPP

#include <cstdint>
#include <iosfwd>
#include <set>
#include <string>
#include <vector>

class Cpu;

/**
 * @brief Interactive command-line debugger for the RISC201 pipeline (rdb).
 *
 * Drives a Cpu one clock cycle at a time and stops on:
 *   - software breakpoints: the instruction is replaced by `ebreak`; its pbrk
 *     exception is intercepted at MA-RW through the Cpu debug trap (precise:
 *     older instructions have committed, younger ones are flushed);
 *   - hardware breakpoints: the instruction at the address is fetched;
 *   - caught exceptions: an exception of a chosen cause is taken (the context
 *     switch to eBase has happened, so the handler can be stepped);
 *   - watchpoints: a register or data-memory word changes value;
 *   - halt (branch to self), or the cycle limit.
 *
 * Commands are read from a stream, so the same code serves the interactive
 * prompt and scripted sessions (`--script`).
 */
class Debugger {
public:
    /** @brief One call frame, recorded when a `call` commits and dropped on `ret`. */
    struct Frame {
        uint32_t callPc = 0;         // address of the call instruction
        uint32_t returnAddress = 0;  // callPc + 4
        uint32_t sp = 0;             // sp when the call committed
    };

    /** @brief Call frames seen since the last reset (innermost last). */
    const std::vector<Frame>& Frames() const { return frames_; }

    Debugger(Cpu& cpu, std::string programPath, uint64_t maxCycles);

    /** @brief Reads and runs commands until `quit` or end of input. */
    void Repl(std::istream& in, std::ostream& out, bool echoCommands);

    /** @brief Runs one command line; returns false when the session should end. */
    bool Execute(const std::string& line, std::ostream& out);

private:
    friend class Tui;   // the full-screen view drives the same engine

    struct Breakpoint {
        int id = 0;
        uint32_t address = 0;
        bool hardware = false;     // hbreak: stop when fetched (no ebreak patch)
        bool enabled = true;
        bool inserted = false;     // ebreak currently written into memory
        uint32_t original = 0;     // the instruction the ebreak replaced
        uint64_t hits = 0;
    };

    struct Watchpoint {
        int id = 0;
        bool memory = false;       // false: register, true: data-memory word
        uint32_t target = 0;       // register index or byte address
        uint32_t lastValue = 0;
    };

    enum class StopReason { None, CycleCount, InstructionCount, Breakpoint, HardBreakpoint,
                            Caught, Watch, Halted, Limit };

    Cpu& cpu_;
    std::string programPath_;
    uint64_t maxCycles_;           // per run / continue command
    std::vector<Breakpoint> breakpoints_;
    std::vector<Watchpoint> watchpoints_;
    std::set<uint8_t> caught_;     // exception causes to stop on
    int nextId_ = 1;
    bool traceEnabled_ = false;
    bool stepOverPending_ = false; // a breakpoint's original instruction is in flight
    uint32_t stepOverAddress_ = 0;
    std::string stopMessage_;
    std::vector<Frame> frames_;

    // Execution.
    StopReason advance(uint64_t cycleLimit, uint64_t retireLimit, std::ostream& out);
    void resetMachine();
    void insertBreakpoint(Breakpoint& bp);
    void removeBreakpoint(Breakpoint& bp);
    void keepBreakpointsIntact();
    Breakpoint* softBreakpointAt(uint32_t address);
    uint32_t originalInstruction(uint32_t address) const;
    uint32_t watchValue(const Watchpoint& wp) const;
    void trackFrames();

    // Output.
    void printStop(StopReason reason, std::ostream& out) const;
    void printPipeline(std::ostream& out) const;
    void printRegisters(std::ostream& out) const;
    void printSpecial(std::ostream& out) const;
    void printDisasm(uint32_t start, int count, std::ostream& out) const;
    void printStats(std::ostream& out) const;
    void printTraceLine(std::ostream& out) const;
    void printHelp(std::ostream& out) const;
    std::string describeRetire() const;
};

#endif

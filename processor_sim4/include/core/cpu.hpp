#ifndef PROCESSOR_SIM_CPU_HPP
#define PROCESSOR_SIM_CPU_HPP

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>

#include "common/pipeline_registers.hpp"
#include "components/alu.hpp"
#include "components/control_unit.hpp"
#include "components/memory.hpp"
#include "components/reg_file.hpp"
#include "components/scratch_file.hpp"
#include "components/special_regs.hpp"
#include "stages/stage_ex.hpp"
#include "stages/stage_if.hpp"
#include "stages/stage_marw.hpp"
#include "stages/stage_of.hpp"

/**
 * @brief Top-level 4-stage pipelined CPU orchestrator.
 *
 * Owns every component, the three pipeline registers and the four stages, and
 * runs one clock cycle per Step(): MA-RW consumes the oldest packet, EX runs
 * (forwarding from MA and WB, resolving the branch), OF decodes and drives the
 * RAS, IF fetches, then all registers latch. It also hosts the Exception Unit:
 * a marked instruction drains to MA-RW, where its commit signals are suppressed,
 * the context is captured into the special registers, the younger instructions
 * are flushed and the PC jumps to `eBase`.
 */
/**
 * @brief What left the pipeline at MA-RW in the last cycle (for the debugger).
 *
 * Either the instruction committed (register / memory / window write), or it
 * was a marked instruction that raised its exception there.
 */
struct RetireInfo {
    bool valid = false;          // something reached MA-RW this cycle
    uint32_t pc = 0;
    uint32_t instruction = 0;
    bool exception = false;      // drained as an exception instead of committing
    uint8_t cause = EXC_NONE;    // ExcCode when `exception`
    bool trapped = false;        // the exception was intercepted by the debug trap
    bool wroteGpr = false;       // committed a GPR write
    uint8_t rd = 0;
    uint32_t value = 0;
    bool stored = false;         // committed a data-memory store
    uint32_t storeAddress = 0;
    uint32_t storeValue = 0;
};

/**
 * @brief Debug-trap hook: called when a marked instruction reaches MA-RW.
 *
 * Returning true intercepts the exception for the debugger: no context switch
 * (special registers and CPL untouched), the younger instructions are flushed
 * as for a normal exception, and the PC is set back to the faulting
 * instruction so execution can resume there. Used for `ebreak` breakpoints.
 */
using DebugTrap = std::function<bool(uint32_t pc, uint8_t cause)>;

class Cpu {
private:
    // Components (control stores, register files, memories).
    RegFile gpr_;
    ScratchFile scratch_;
    Alu alu_;
    Memory memory_;
    ControlUnit control_;
    SpecialRegs special_;

    // Pipeline registers.
    PipelineRegister<IfOfPacket> ifOf_;
    PipelineRegister<OfExPacket> ofEx_;
    PipelineRegister<ExMemPacket> exMem_;

    // Stage logic.
    StageIF stageIf_;
    StageOF stageOf_;
    StageEX stageEx_;
    StageMARW stageMarw_;

    bool halted_ = false;
    uint64_t cycles_ = 0;
    uint64_t mispredicts_ = 0;
    uint64_t dataStalls_ = 0;
    uint64_t exceptions_ = 0;
    uint64_t retired_ = 0;      // instructions committed (excludes ones that raised)
    bool interruptPending_ = false;

    // Debugger support.
    DebugTrap debugTrap_;
    bool debugStopped_ = false;
    RetireInfo lastRetired_;

private:
    void trace();

public:
    Cpu();

    /** @brief Loads a 32-bit little-endian program into instruction memory. */
    void LoadProgram(const std::string& path);

    /** @brief Loads the microcode ROM images produced by ../microassembler. */
    void LoadMicrocode(const std::string& microprogramsPath,
                       const std::string& addressTablePath);

    /** @brief Resets all datapath state (keeps the loaded program). */
    void Reset();

    /** @brief Executes a single clock cycle. */
    void Step();

    /** @brief Runs until halt or @p maxCycles; optionally traces each cycle. */
    void Run(uint64_t maxCycles, bool trace);

    /**
     * @brief Raises an asynchronous interrupt.
     *
     * The next instruction fetched by IF is marked (`EXC_INTERRUPT`) and the
     * exception is taken when it reaches MA-RW.
     */
    void RequestInterrupt() { interruptPending_ = true; }

    /** @brief Prints the final architectural state. */
    void DumpState(std::ostream& out) const;

    bool Halted() const { return halted_; }
    uint64_t Cycles() const { return cycles_; }
    uint64_t Mispredicts() const { return mispredicts_; }
    uint64_t DataStalls() const { return dataStalls_; }
    uint64_t Exceptions() const { return exceptions_; }
    uint64_t Retired() const { return retired_; }
    uint8_t Cpl() const { return special_.Cpl(); }
    uint8_t ECode() const { return special_.ECode(); }
    uint32_t OldPC() const { return special_.OldPC(); }
    uint32_t Pc() const { return stageIf_.Pc(); }

    // ---- Debugger interface (read-only views plus a few controlled writes) ----

    /** @brief Installs (or clears, with an empty function) the debug trap. */
    void SetDebugTrap(DebugTrap trap) { debugTrap_ = std::move(trap); }

    /** @brief True if the last Step() intercepted an exception for the debugger. */
    bool DebugStopped() const { return debugStopped_; }

    /** @brief What reached MA-RW in the last cycle. */
    const RetireInfo& LastRetired() const { return lastRetired_; }

    /** @brief Redirects fetch to @p pc (used to resume after a breakpoint). */
    void SetPc(uint32_t pc) { stageIf_.SetPc(pc); }

    /** @brief Clears the halt latch so execution can continue after a breakpoint edit. */
    void ClearHalt() { halted_ = false; }

    const RegFile& Gpr() const { return gpr_; }
    RegFile& Gpr() { return gpr_; }
    const ScratchFile& Scratch() const { return scratch_; }
    const SpecialRegs& Special() const { return special_; }
    const Memory& Mem() const { return memory_; }
    Memory& Mem() { return memory_; }
    const PipelineRegister<IfOfPacket>& IfOf() const { return ifOf_; }
    const PipelineRegister<OfExPacket>& OfEx() const { return ofEx_; }
    const PipelineRegister<ExMemPacket>& ExMem() const { return exMem_; }
    const ControlUnit& Control() const { return control_; }
    const Ras& RasUnit() const { return stageOf_.RasUnit(); }
    bool FlagE() const { return alu_.FlagE(); }
    bool FlagGt() const { return alu_.FlagGt(); }
    bool FlagLt() const { return alu_.FlagLt(); }
    bool InterruptPending() const { return interruptPending_; }
};

#endif

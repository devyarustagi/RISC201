#ifndef PROCESSOR_SIM6_CPU_HPP
#define PROCESSOR_SIM6_CPU_HPP

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
#include "stages/stage_ibu.hpp"
#include "stages/stage_if.hpp"
#include "stages/stage_ma.hpp"
#include "stages/stage_rf.hpp"
#include "stages/stage_rw.hpp"

/**
 * @brief What left the pipeline at RW in the last cycle (for the debugger).
 */
struct RetireInfo {
    bool valid = false;
    uint32_t pc = 0;
    uint32_t instruction = 0;
    bool exception = false;      // raised its exception instead of committing
    uint8_t cause = EXC_NONE;
    bool trapped = false;        // intercepted by the debug trap
    bool wroteGpr = false;
    uint8_t rd = 0;
    uint32_t value = 0;
    bool stored = false;         // its store committed (in MA, a cycle earlier)
    uint32_t storeAddress = 0;
    uint32_t storeValue = 0;
};

/** @brief A store that committed in MA in the last cycle (for memory watchpoints). */
struct StoreInfo {
    bool valid = false;
    uint32_t pc = 0;
    uint32_t instruction = 0;
};

/**
 * @brief Debug-trap hook: called when a marked instruction reaches RW.
 *
 * Returning true intercepts the exception: no context switch, the younger
 * instructions are flushed, and fetch restarts at the faulting instruction.
 */
using DebugTrap = std::function<bool(uint32_t pc, uint8_t cause)>;

/**
 * @brief The 6-stage pipelined CPU of 6StagePipeline.svg.
 *
 *   IF -> IBU -> RF -> EX -> MA -> RW
 *
 * One Step() is one clock cycle. Stages are evaluated from RW back to IF so each
 * one reads its input register's `out` before the register latches:
 *
 *   RW   commits the oldest instruction or reports its exception;
 *   MA   loads / stores, predictor inversion;
 *   EX   forwarding from MA and RW, ALU, branch resolution;
 *   RF   register read (RW has already written this cycle);
 *   IBU  decode, Control Unit, RAS, exception marking;
 *   IF   fetch and the main PC mux.
 *
 * Hazard units:
 *   Forwarding Unit  ALU results from MA, any result from RW, into EX.
 *   Data Lock Unit   a load in EX whose register is read by the instruction in
 *                    RF stalls IF, IBU and RF for one cycle (bubble into EX).
 *                    retz waits in IBU until RF, EX and MA are empty, and movz
 *                    waits while an older movz is in RF, EX or MA, so the
 *                    special registers it reads are up to date.
 *   Branch Lock Unit a misprediction in EX flushes IF/IBU, IBU/RF and RF/EX
 *                    (three wrong-path instructions) and restores the RAS.
 *
 * Exceptions are marked in IF (interrupt), IBU (wronginst, lowpriv, pbrk, scall,
 * rasovf) or EX (div0) and taken in RW. While a marked instruction is in MA or
 * RW, EX does not run, and while it is in RW, MA does not run: the younger
 * instructions are about to be flushed and must not change flags, the Scratch
 * File or memory.
 */
class Cpu {
private:
    RegFile gpr_;
    ScratchFile scratch_;
    Alu alu_;
    Memory memory_;
    ControlUnit control_;
    SpecialRegs special_;

    PipelineRegister<IfIbuPacket> ifIbu_;
    PipelineRegister<IbuRfPacket> ibuRf_;
    PipelineRegister<RfExPacket> rfEx_;
    PipelineRegister<ExMaPacket> exMa_;
    PipelineRegister<MaRwPacket> maRw_;

    StageIF stageIf_;
    StageIBU stageIbu_;
    StageRF stageRf_;
    StageEX stageEx_;
    StageMA stageMa_;
    StageRW stageRw_;

    bool halted_ = false;
    uint64_t cycles_ = 0;
    uint64_t mispredicts_ = 0;
    uint64_t dataStalls_ = 0;
    uint64_t exceptions_ = 0;
    uint64_t retired_ = 0;
    bool interruptPending_ = false;

    DebugTrap debugTrap_;
    bool debugStopped_ = false;
    RetireInfo lastRetired_;
    StoreInfo lastStore_;
    uint32_t haltPc_ = 0;

    void trace();

public:
    static constexpr int kStages = 6;

    Cpu();

    void LoadProgram(const std::string& path);
    void LoadMicrocode(const std::string& microprogramsPath,
                       const std::string& addressTablePath);
    void Reset();
    void Step();
    void Run(uint64_t maxCycles, bool trace);
    void RequestInterrupt() { interruptPending_ = true; }
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
    uint32_t HaltPc() const { return haltPc_; }

    // ---- Debugger interface ----
    void SetDebugTrap(DebugTrap trap) { debugTrap_ = std::move(trap); }
    bool DebugStopped() const { return debugStopped_; }
    const RetireInfo& LastRetired() const { return lastRetired_; }
    const StoreInfo& LastStore() const { return lastStore_; }
    void SetPc(uint32_t pc) { stageIf_.SetPc(pc); }
    void ClearHalt() { halted_ = false; }

    const RegFile& Gpr() const { return gpr_; }
    RegFile& Gpr() { return gpr_; }
    const ScratchFile& Scratch() const { return scratch_; }
    const SpecialRegs& Special() const { return special_; }
    const Memory& Mem() const { return memory_; }
    Memory& Mem() { return memory_; }
    const PipelineRegister<IfIbuPacket>& IfIbu() const { return ifIbu_; }
    const PipelineRegister<IbuRfPacket>& IbuRf() const { return ibuRf_; }
    const PipelineRegister<RfExPacket>& RfEx() const { return rfEx_; }
    const PipelineRegister<ExMaPacket>& ExMa() const { return exMa_; }
    const PipelineRegister<MaRwPacket>& MaRw() const { return maRw_; }
    const ControlUnit& Control() const { return control_; }
    const Ras& RasUnit() const { return stageIbu_.RasUnit(); }
    bool FlagE() const { return alu_.FlagE(); }
    bool FlagGt() const { return alu_.FlagGt(); }
    bool FlagLt() const { return alu_.FlagLt(); }
    bool InterruptPending() const { return interruptPending_; }
};

#endif

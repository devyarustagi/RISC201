#ifndef PROCESSOR_SIM_CPU_HPP
#define PROCESSOR_SIM_CPU_HPP

#include <cstdint>
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
    bool interruptPending_ = false;

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
    uint8_t Cpl() const { return special_.Cpl(); }
    uint8_t ECode() const { return special_.ECode(); }
    uint32_t OldPC() const { return special_.OldPC(); }
    uint32_t Pc() const { return stageIf_.Pc(); }
};

#endif

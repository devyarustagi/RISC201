#ifndef PROCESSOR_SIM_SPECIAL_REGS_HPP
#define PROCESSOR_SIM_SPECIAL_REGS_HPP

#include <cstdint>

#include "common/micro_types.hpp"

/**
 * @brief Hardware special registers of the Register Unit.
 *
 * Holds the exception context registers and the privilege level:
 *   - `eBase`     : base address of the exception-handler preamble (set by the OS).
 *   - `eCode`     : code of the exception being handled.
 *   - `oldPC`     : return address of the interrupted program.
 *   - `oldSP`     : saved user stack pointer.
 *   - `oldFlags`  : condition codes at the moment of interruption.
 *   - `CPL`       : Current Privilege Level (1 = user, 0 = OS).
 *
 * The privileged `movz` reads/writes these through the restricted register
 * window (see `WindowReg`); the routing itself lives in the OF / MARW stages.
 */
class SpecialRegs {
private:
    uint32_t eBase_ = 0;
    uint8_t eCode_ = 0;
    uint32_t oldPC_ = 0;
    uint32_t oldSP_ = 0;
    bool oldFlagE_ = false;
    bool oldFlagGt_ = false;
    bool oldFlagLt_ = false;
    uint8_t cpl_ = 1;   // 1 = user mode, 0 = OS mode

public:
    /** @brief Resets every special register (CPL = user). */
    void Reset();

    uint8_t Cpl() const { return cpl_; }
    uint32_t EBase() const { return eBase_; }
    uint8_t ECode() const { return eCode_; }
    uint32_t OldPC() const { return oldPC_; }
    uint32_t OldSP() const { return oldSP_; }
    bool OldFlagE() const { return oldFlagE_; }
    bool OldFlagGt() const { return oldFlagGt_; }
    bool OldFlagLt() const { return oldFlagLt_; }

    void SetEBase(uint32_t value) { eBase_ = value; }
    void SetECode(uint8_t value) { eCode_ = value; }
    void SetOldPC(uint32_t value) { oldPC_ = value; }
    void SetOldSP(uint32_t value) { oldSP_ = value; }
    void SetOldFlags(bool equal, bool greater, bool less) {
        oldFlagE_ = equal;
        oldFlagGt_ = greater;
        oldFlagLt_ = less;
    }

    /**
     * @brief Captures context when a marked instruction reaches MA-RW.
     *
     * Saves @p pc / @p cause / the condition codes and drops to OS mode.
     */
    void EnterException(uint32_t pc, uint8_t cause, bool equal, bool greater, bool less);

    /** @brief Signals a return to user mode (retz sets CPL back to 1). */
    void ReturnFromException() { cpl_ = 1; }
};

#endif

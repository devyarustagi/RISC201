#ifndef PROCESSOR_SIM_DISASM_HPP
#define PROCESSOR_SIM_DISASM_HPP

#include <cstdint>
#include <string>

/**
 * @brief Disassembles one RISC201 instruction word for the debugger.
 *
 * Uses the same syntax as the team assembler / disassembler
 * (`add r3, r1, r2`, `ld r1, r2, 4`, `movi r1, h5`, `movz eBase, r1`, ...).
 * Branch-format instructions show the absolute target computed from @p pc
 * (word offset in [24:0]) and a `(T)` marker when the prediction bit is set.
 */
std::string Disassemble(uint32_t instruction, uint32_t pc);

/** @brief Mnemonic of a macro opcode, or "???" for an opcode outside the ISA. */
std::string MnemonicOf(uint8_t opcode);

/** @brief Short name of an exception code (pbrk, scall, div0, ...). */
std::string ExceptionName(uint8_t cause);

#endif

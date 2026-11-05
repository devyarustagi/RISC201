#ifndef MICROASSEMBLER_H
#define MICROASSEMBLER_H

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// Maximum number of characters allowed on a single .masm source line.
#define MAX_LINE_LEN 256

// Width (in bits) of the encoded microinstruction.
#define MICRO_WORD_BITS 38

// Tag bit of a 5-bit register field: 0 selects a macroinstruction register
// (rd / rs1 / rs2), 1 selects a scratch register (s0 - s15).
#define SCRATCH_TAG 0x10

/**
 * @brief One entry of the microinstruction lookup table.
 *
 * `pattern` describes the operand syntax of the instruction. Every character
 * consumes exactly one operand in source order and routes it to a field:
 *   - 'd' -> destination register field
 *   - '1' -> first source register field
 *   - '2' -> second source register field
 *   - 'i' -> signed 16-bit immediate field
 * The empty pattern "" denotes an instruction that takes no operands.
 */
struct microinstructionDetails {
    uint8_t opcode;
    const char* pattern;
};

/**
 * @brief Assembles a hand written microassembly (.masm) file into a
 *        microprogram ROM image and a macro-opcode -> start-address table.
 *
 * The .masm file is a list of labels (e.g. ".add"), each followed by the
 * microinstruction sequence implementing that macroinstruction. Assembling it
 * produces two raw binary files:
 *   - microprograms.rom     : the concatenated 38-bit microinstructions,
 *                             bit-packed MSB first with no per-word padding.
 *   - rom_address_table.rom : one byte per active macro opcode (0x00 - 0x2B)
 *                             holding the start index of its sequence in
 *                             microprograms.rom.
 *
 * One Microassembler instance is bound to exactly one input file; the file is
 * opened (and any failure thrown) by the constructor.
 */
class Microassembler {
private:
    unsigned int lc{0};                                  // 1-based source line counter
    std::ifstream inputFile;                             // the .masm source
    std::vector<uint64_t> microprogram;                  // assembled 38-bit words
    std::unordered_map<std::string, uint32_t> labels;    // label -> start index

    // mnemonic -> {opcode, operand pattern}; the opcode order here *is* the
    // sequential opcode assignment (0x00, 0x01, ...).
    static const std::unordered_map<std::string, microinstructionDetails> microinstructionTable;

    // active macro opcode -> macro mnemonic (drives rom_address_table.rom).
    static const std::unordered_map<uint8_t, std::string> macroOpcodeTable;

private:
    /**
     * @brief Splits a source line into whitespace/comma separated tokens.
     *
     * Strips a comment (everything from the first '#') and splits the rest on
     * whitespace and commas. The first token is normally the mnemonic or a
     * ".label"; the remaining tokens are operands.
     *
     * @param line the raw source line (consumed/mutated in place)
     * @return the extracted tokens
     */
    std::vector<std::string> tokenizer(std::string& line) const;

    /**
     * @brief Encodes a register operand into its 5-bit field value.
     *
     * "rd", "rs1" and "rs2" refer to the register supplied by the enclosing
     * macroinstruction and are encoded as 0. Scratch registers "s0".."s15"
     * are encoded with the MSB set: 0b10000 | n.
     *
     * @param operand the register token
     * @return the 5-bit field value
     * @throws std::runtime_error if the operand is not a valid register
     */
    uint32_t encodeRegister(const std::string& operand) const;

    /**
     * @brief Encodes an immediate operand into its 16-bit field value.
     *
     * The keyword "imm" is a placeholder for the macroinstruction's immediate
     * and encodes as 0 (its real value is supplied at run time). Any numeric
     * literal (decimal, 0x.., 0b.., or negative) becomes the microinstruction's
     * own immediate and is truncated to 16 two's-complement bits.
     *
     * @param operand the immediate token 
     * @return the 16-bit field value
     * @throws std::runtime_error if the operand is not a valid immediate
     */
    uint16_t encodeImmediate(const std::string& operand) const;

    /**
     * @brief Assembles one microinstruction line into its 38-bit encoding.
     *
     * The stall bit is returned as 0; the caller sets it once it knows whether
     * the instruction is the last of its sequence.
     *
     * @param tokens the tokenized source line (mnemonic first)
     * @return the 38-bit encoded microinstruction (stall bit clear)
     * @throws std::runtime_error on unknown mnemonics or malformed operands
     */
    uint64_t encodeInstruction(const std::vector<std::string>& tokens) const;

    /**
     * @brief Writes the bit-packed microprogram image to a raw binary file.
     *
     * The 38-bit words are concatenated in program order, most significant bit
     * first, and emitted as bytes (MSB first within each byte). A trailing
     * partial byte is zero padded to the next byte boundary.
     *
     * @param path destination file path
     * @throws std::runtime_error if the file cannot be opened
     */
    void writeMicroprogramsRom(const std::string& path) const;

    /**
     * @brief Writes the macro-opcode -> start-address table as raw bytes.
     *
     * Emits one byte per active macro opcode, from 0x00 to 0x2B, where the
     * byte holds the start index of that instruction's microprogram sequence.
     * Every active opcode must have a matching ".label" in the source.
     *
     * @param path destination file path
     * @throws std::runtime_error if the file cannot be opened, a required label
     *         is missing, or the microprogram exceeds 256 instructions
     */
    void writeAddressTableRom(const std::string& path) const;

public:
    /**
     * @brief Constructs the assembler and opens the .masm source file.
     *
     * @param inputFilePath path to the .masm file to assemble
     * @throws std::runtime_error if the file cannot be opened
     */
    explicit Microassembler(const std::string& inputFilePath);

    /**
     * @brief Runs the assembly passes and emits both ROM files.
     *
     * Labels are resolved as the source is read; the stall bit of the final
     * instruction of every sequence is cleared (all others stay set).
     *
     * @param microprogramsPath destination path for microprograms.rom
     * @param addressTablePath destination path for rom_address_table.rom
     * @throws std::runtime_error on invalid source or output failure
     */
    void Assemble(const std::string& microprogramsPath, const std::string& addressTablePath);
};

#endif

#ifndef PROCESSOR_SIM_MEMORY_HPP
#define PROCESSOR_SIM_MEMORY_HPP

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

/**
 * Unified instruction and data memory.
 *
 * Instruction memory is word-addressed (index = pc / 4) and loaded from a raw
 * binary file of 32-bit little-endian words. Data memory is byte-addressed but
 * word-granular (the low two address bits are ignored) and sparse, so only
 * touched locations are allocated.
 */
class Memory
{
private:
    std::vector<uint32_t> instructionRam_;
    std::map<uint32_t, uint32_t> dataRam_;

public:
    void LoadProgram(const std::string &path);
    // loads all instructions from provided path

    uint32_t FetchInstruction(uint32_t pc) const;

    void WriteInstruction(uint32_t pc, uint32_t instruction);
    /**
     * Writes instruction back to instruction memory at pc.
     * Used by the branch predictor logic to flip a branch's prediction bit.
     */

    uint32_t ReadData(uint32_t address) const;
    void WriteData(uint32_t address, uint32_t value);
    const std::vector<uint32_t> &InstructionRam() const { return instructionRam_; }
    void DumpData(std::ostream &out) const;
    void Reset();     // clears both memories
    void ResetData(); // clears data memory only
};

#endif

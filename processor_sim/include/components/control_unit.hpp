#ifndef PROCESSOR_SIM_CONTROL_UNIT_HPP
#define PROCESSOR_SIM_CONTROL_UNIT_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "common/micro_types.hpp"

/**Working of Micro Control unit
 *
 * Loads the two ROM files produced by ../microassembler - the macro-opcode to
 * start-address table (rom_address_table.rom) and the microprogram
 * (microprograms.rom) - then, for each macroinstruction that enters OF, walks
 * the matching microinstruction sequence and translates each microinstruction
 * into ControlSignals. The microinstruction's `stall` bit becomes `isStall`,
 * which freezes the fetch stage while a sequence is still running.
 */
class ControlUnit
{
private:
    std::array<uint8_t, ADDRESS_TABLE_SIZE> addressTable_{};
    std::vector<Microinstruction> microprogram_;
    uint32_t upc_ = 0;  // micro-program counter
    bool busy_ = false; // true while a multi-micro-op sequence is in flight

private:
    void loadAddressTable(const std::string &path);
    void loadMicroprograms(const std::string &path);
    ControlSignals decode(const Microinstruction &micro) const;

public:
    /** Loads both ROM files. Throws on I/O or format errors. */
    void Load(const std::string &microprogramsPath, const std::string &addressTablePath);

    /** Clears the micro-PC and busy flag. */
    void Reset();

    /** Points the micro-PC at the sequence for macroOpcode. */
    void Begin(uint8_t macroOpcode);

    /** Decodes the current microinstruction and advances the micro-PC. */
    ControlSignals Step();

    /** True while the current macroinstruction has micro-ops remaining. */
    bool Busy() const { return busy_; }
};

#endif

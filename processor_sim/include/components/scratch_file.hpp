#ifndef PROCESSOR_SIM_SCRATCH_FILE_HPP
#define PROCESSOR_SIM_SCRATCH_FILE_HPP

#include <array>
#include <cstdint>
#include <iosfwd>

#include "common/micro_types.hpp"

/**
 * @brief The 16 microarchitectural scratch registers (s0 - s15).
 *
 * These are not visible to the macroinstruction set; the microcode uses them
 * as temporaries (for example to materialise a 32-bit constant in two steps).
 */
class ScratchFile {
private:
    std::array<uint32_t, SCRATCH_COUNT> registers_{};

public:
    ScratchFile();

    /** @brief Reads a scratch register. */
    uint32_t Read(uint8_t index) const;

    /** @brief Writes a scratch register. */
    void Write(uint8_t index, uint32_t value);

    /** @brief Resets every scratch register to zero. */
    void Reset();

    /** @brief Prints every scratch register, one per line. */
    void Dump(std::ostream& out) const;
};

#endif

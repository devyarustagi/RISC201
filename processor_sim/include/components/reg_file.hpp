#ifndef PROCESSOR_SIM_REG_FILE_HPP
#define PROCESSOR_SIM_REG_FILE_HPP

#include <array>
#include <cstdint>
#include <iosfwd>

#include "common/micro_types.hpp"

/**
 * The 16 general-purpose registers (r0 - r15).
 * Note -> r0 is hardwired to zero: reads always return 0 and writes are ignored.
 */
class RegFile
{
private:
    std::array<uint32_t, GPR_COUNT> registers_{};

public:
    RegFile();
    uint32_t Read(uint8_t index) const;
    void Write(uint8_t index, uint32_t value);
    void Reset();
    void Dump(std::ostream &out) const;
};

#endif

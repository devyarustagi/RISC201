#ifndef PROCESSOR_SIM_CONTROL_UNIT_HPP
#define PROCESSOR_SIM_CONTROL_UNIT_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "common/micro_types.hpp"

/**
 * @brief Horizontally microprogrammed control unit (6-stage pipeline).
 *
 * The ROM word *is* the control word: there is no decode unit. Every 64-bit
 * microinstruction already carries aluOp, the AluSrcA/AluSrcB mux selects, the
 * write enables and the micro immediate. Only the micro-PC and the macro-opcode
 * -> start-address table remain.
 */
class ControlUnit
{
private:
    std::array<uint8_t, ADDRESS_TABLE_SIZE> addressTable_{};
    std::vector<uint64_t> microprogram_;
    uint32_t upc_ = 0;
    bool busy_ = false;

private:
    void loadAddressTable(const std::string &path);
    void loadMicroprograms(const std::string &path);
    static ControlSignals unpack(uint64_t word);

public:
    void Load(const std::string &microprogramsPath, const std::string &addressTablePath);
    void Reset();
    void Begin(uint8_t macroOpcode);
    ControlSignals Step();
    bool Busy() const { return busy_; }
};

#endif

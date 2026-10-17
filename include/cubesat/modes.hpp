// Operating modes of the simulated flight computer. The allowed transitions
// are documented in docs/state-machine.md and enforced in FlightComputer.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cubesat {

enum class Mode {
    Boot,      // Power-on: waiting for INIT and a power-on self test.
    Nominal,   // Normal simulated operation; payload may be powered.
    SafeMode,  // Restricted operation after a safety event; payload forced off.
    Fault,     // Unrecoverable software error; only RESET CONFIRM leaves it.
};

constexpr std::size_t kModeCount = 4;
constexpr std::array<Mode, kModeCount> kAllModes = {Mode::Boot, Mode::Nominal, Mode::SafeMode,
                                                    Mode::Fault};

constexpr std::string_view toString(Mode mode) {
    switch (mode) {
        case Mode::Boot: return "BOOT";
        case Mode::Nominal: return "NOMINAL";
        case Mode::SafeMode: return "SAFE_MODE";
        case Mode::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

constexpr std::size_t modeIndex(Mode mode) { return static_cast<std::size_t>(mode); }

}  // namespace cubesat

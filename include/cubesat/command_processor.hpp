// Command processor: turns one line of text into one structured JSON
// response, enforcing (in this order):
//   1. input hygiene      -> MALFORMED_INPUT   (too long, non-printable bytes)
//   2. command lookup     -> UNKNOWN_COMMAND
//   3. mode permissions   -> NOT_PERMITTED_IN_MODE
//   4. argument checks    -> INVALID_ARGUMENT / MISSING_FIELD / OUT_OF_RANGE
//   5. handler execution  -> handler-specific codes (e.g. ACTIVE_VIOLATIONS)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cubesat/flight_computer.hpp"
#include "cubesat/json.hpp"
#include "cubesat/modes.hpp"

namespace cubesat {

enum class CommandId {
    Help,
    Status,
    GetTelemetry,
    Telemetry,
    Tick,
    Init,
    EnterSafeMode,
    ExitSafeMode,
    Payload,
    Reset,
    FaultInject,
};

struct CommandSpec {
    CommandId id;
    std::string_view name;
    std::size_t min_args;
    std::size_t max_args;
    std::array<bool, kModeCount> permitted;  // indexed by modeIndex(Mode)
    std::string_view usage;
    std::string_view description;
};

// The complete, documented command set (see docs/state-machine.md).
const std::vector<CommandSpec>& commandTable();

// Case-insensitive lookup; returns nullptr for unknown commands.
const CommandSpec* findCommand(std::string_view name);

class CommandProcessor {
public:
    CommandProcessor(FlightComputer& computer, std::size_t max_line_length);

    // Processes one input line. Returns std::nullopt for blank lines and
    // '#' comments (they produce no response); otherwise one JSON response.
    std::optional<json::Value> processLine(std::string_view line);

private:
    Result<json::Value> dispatch(const CommandSpec& spec, const std::vector<std::string>& args);
    json::Value respond(const std::optional<std::string>& command,
                        const Result<json::Value>& result);

    FlightComputer& computer_;
    std::size_t max_line_length_;
    std::uint64_t response_seq_ = 0;
};

}  // namespace cubesat

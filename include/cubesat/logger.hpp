// Structured diagnostic logger.
//
// Every entry is one JSON object per line (JSON Lines) so logs are easy to
// grep, parse in Python and archive in CI. Entries use a deterministic
// sequence number and the SIMULATED clock instead of wall-clock time, so two
// runs of the same script produce byte-identical logs (useful for regression
// testing). Logs go to a separate stream from command responses.
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "cubesat/json.hpp"
#include "cubesat/modes.hpp"

namespace cubesat {

enum class Severity { Info, Warning, Error, Critical };

std::string_view toString(Severity severity);

class Logger {
public:
    explicit Logger(std::ostream& out) : out_(out) {}

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Writes one entry and flushes so logs survive a crash mid-run.
    void log(Severity severity, std::string_view component, std::string_view event,
             std::string_view message, Mode mode, std::int64_t sim_time_s,
             json::Value details = json::Value::object());

    std::uint64_t entriesWritten() const { return next_seq_ - 1; }

private:
    std::ostream& out_;
    std::uint64_t next_seq_ = 1;
};

}  // namespace cubesat

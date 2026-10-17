// Simulation configuration: demonstration safety thresholds and timing.
//
// IMPORTANT: every default and every value in config/simulation_config.json
// is invented for this educational simulator. None of them is derived from a
// real spacecraft, battery, or thermal specification.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cubesat/json.hpp"
#include "cubesat/result.hpp"

namespace cubesat {

constexpr int kSupportedConfigSchemaVersion = 1;

// Each safety rule has a "trip" threshold that raises the violation and a
// separate "recover" threshold that clears it (hysteresis), so a reading that
// hovers around one value cannot make the rule flicker on and off.
struct SafetyThresholds {
    double battery_min_v = 0.0;         // trips when battery_v <  battery_min_v
    double battery_recover_v = 0.0;     // clears when battery_v >= battery_recover_v
    double temp_max_c = 0.0;            // trips when temp_c >  temp_max_c
    double temp_max_recover_c = 0.0;    // clears when temp_c <= temp_max_recover_c
    double temp_min_c = 0.0;            // trips when temp_c <  temp_min_c
    double temp_min_recover_c = 0.0;    // clears when temp_c >= temp_min_recover_c
    double attitude_max_deg = 0.0;      // trips when attitude_deg >  attitude_max_deg
    double attitude_recover_deg = 0.0;  // clears when attitude_deg <= attitude_recover_deg
};

struct TimingConfig {
    std::int64_t telemetry_timeout_s = 0;  // frame older than this (strictly) is stale
    std::int64_t comm_timeout_s = 0;       // no ground contact for longer than this (strictly)
    std::int64_t max_tick_s = 0;           // largest single TICK step accepted
};

struct SimulationConfig {
    int schema_version = kSupportedConfigSchemaVersion;
    SafetyThresholds thresholds;
    TimingConfig timing;
    std::size_t max_command_length = 0;  // bytes per command line

    json::Value toJson() const;
};

// Upper bound accepted for max_command_length; keeps the input buffer bounded.
constexpr std::size_t kMaxCommandLengthLimit = 4096;
// Smallest useful max_command_length (a full TELEMETRY line must fit).
constexpr std::size_t kMinCommandLengthLimit = 128;
// Upper bound for any timing value: one simulated day.
constexpr std::int64_t kMaxTimingValueS = 86400;

// Parses and fully validates a configuration document. Unknown keys, missing
// keys, wrong types, non-finite numbers, implausible values and contradictory
// thresholds are all rejected with code CONFIG_INVALID.
Result<SimulationConfig> parseConfig(std::string_view text);

// Reads a configuration file from disk (CONFIG_NOT_FOUND / CONFIG_INVALID).
Result<SimulationConfig> loadConfigFile(const std::string& path);

}  // namespace cubesat

// Safety monitor: evaluates telemetry against the configured demonstration
// thresholds and reports which safety rules became active or cleared.
//
// The monitor only REPORTS changes; deciding what to do (e.g. entering
// SAFE_MODE) is the FlightComputer's job. Keeping detection and response
// separate makes each piece small and independently testable.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cubesat/config.hpp"
#include "cubesat/telemetry.hpp"

namespace cubesat {

// Declaration order is also the evaluation and reporting order, which keeps
// output deterministic when several rules change at once.
enum class SafetyRule {
    BatteryLow,
    TemperatureHigh,
    TemperatureLow,
    AttitudeExceeded,
    SensorInvalid,
    TelemetryStale,
    CommTimeout,
};

constexpr std::size_t kSafetyRuleCount = 7;
constexpr std::array<SafetyRule, kSafetyRuleCount> kAllSafetyRules = {
    SafetyRule::BatteryLow,     SafetyRule::TemperatureHigh, SafetyRule::TemperatureLow,
    SafetyRule::AttitudeExceeded, SafetyRule::SensorInvalid, SafetyRule::TelemetryStale,
    SafetyRule::CommTimeout,
};

std::string_view toString(SafetyRule rule);

struct SafetyChange {
    SafetyRule rule;
    bool now_active;     // true = violation raised, false = violation cleared
    std::string detail;  // e.g. "battery_v=6.7 < battery_min_v=6.8"
};

// Inputs to one evaluation. `latest` may be null when no frame exists.
struct SafetyInputs {
    const TelemetryFrame* latest = nullptr;
    std::int64_t now_s = 0;
    std::int64_t last_contact_s = 0;
};

class SafetyMonitor {
public:
    SafetyMonitor(SafetyThresholds thresholds, TimingConfig timing);

    // Evaluates every rule and returns the rules whose state changed, in
    // SafetyRule order. Rule semantics (see docs/state-machine.md):
    //  * Value rules (battery, temperature, attitude) use hysteresis and are
    //    only evaluated when the frame's sensors are VALID: an invalid frame
    //    raises SENSOR_INVALID and leaves the value rules unchanged.
    //  * TELEMETRY_STALE: now - frame time > telemetry_timeout_s.
    //  * COMM_TIMEOUT: now - last ground contact > comm_timeout_s.
    std::vector<SafetyChange> evaluate(const SafetyInputs& inputs);

    const std::set<SafetyRule>& active() const { return active_; }
    bool hasActive() const { return !active_.empty(); }
    bool isActive(SafetyRule rule) const { return active_.count(rule) != 0; }
    void clear() { active_.clear(); }

    // Pure threshold predicates. Public so unit tests can probe boundaries.
    static bool batteryTrips(double volts, const SafetyThresholds& t);
    static bool batteryClears(double volts, const SafetyThresholds& t);
    static bool tempHighTrips(double celsius, const SafetyThresholds& t);
    static bool tempHighClears(double celsius, const SafetyThresholds& t);
    static bool tempLowTrips(double celsius, const SafetyThresholds& t);
    static bool tempLowClears(double celsius, const SafetyThresholds& t);
    static bool attitudeTrips(double degrees, const SafetyThresholds& t);
    static bool attitudeClears(double degrees, const SafetyThresholds& t);
    static bool timeoutExceeded(std::int64_t elapsed_s, std::int64_t timeout_s);

private:
    void update(SafetyRule rule, bool trips, bool clears, const std::string& detail,
                std::vector<SafetyChange>& changes);

    SafetyThresholds thresholds_;
    TimingConfig timing_;
    std::set<SafetyRule> active_;
};

}  // namespace cubesat

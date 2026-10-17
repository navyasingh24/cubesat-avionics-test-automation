#include "cubesat/safety_monitor.hpp"

#include <utility>

namespace cubesat {

std::string_view toString(SafetyRule rule) {
    switch (rule) {
        case SafetyRule::BatteryLow: return "BATTERY_LOW";
        case SafetyRule::TemperatureHigh: return "TEMPERATURE_HIGH";
        case SafetyRule::TemperatureLow: return "TEMPERATURE_LOW";
        case SafetyRule::AttitudeExceeded: return "ATTITUDE_EXCEEDED";
        case SafetyRule::SensorInvalid: return "SENSOR_INVALID";
        case SafetyRule::TelemetryStale: return "TELEMETRY_STALE";
        case SafetyRule::CommTimeout: return "COMM_TIMEOUT";
    }
    return "UNKNOWN";
}

namespace {

std::string num(double value) { return json::Value(value).dump(); }

}  // namespace

SafetyMonitor::SafetyMonitor(SafetyThresholds thresholds, TimingConfig timing)
    : thresholds_(thresholds), timing_(timing) {}

// --- Threshold predicates -------------------------------------------------
// Trip comparisons are STRICT: a reading exactly equal to a limit is still
// acceptable. Recovery comparisons are INCLUSIVE. Tests in
// tests/test_safety_rules.py pin down both sides of every boundary.

bool SafetyMonitor::batteryTrips(double volts, const SafetyThresholds& t) {
#ifdef CUBESAT_SEEDED_DEFECT
    // Deliberately wrong boundary, compiled only when the CMake option
    // CUBESAT_SEEDED_DEFECT=ON is used to prove the test suite catches it.
    return volts <= t.battery_min_v;
#else
    return volts < t.battery_min_v;
#endif
}
bool SafetyMonitor::batteryClears(double volts, const SafetyThresholds& t) {
    return volts >= t.battery_recover_v;
}
bool SafetyMonitor::tempHighTrips(double celsius, const SafetyThresholds& t) {
    return celsius > t.temp_max_c;
}
bool SafetyMonitor::tempHighClears(double celsius, const SafetyThresholds& t) {
    return celsius <= t.temp_max_recover_c;
}
bool SafetyMonitor::tempLowTrips(double celsius, const SafetyThresholds& t) {
    return celsius < t.temp_min_c;
}
bool SafetyMonitor::tempLowClears(double celsius, const SafetyThresholds& t) {
    return celsius >= t.temp_min_recover_c;
}
bool SafetyMonitor::attitudeTrips(double degrees, const SafetyThresholds& t) {
    return degrees > t.attitude_max_deg;
}
bool SafetyMonitor::attitudeClears(double degrees, const SafetyThresholds& t) {
    return degrees <= t.attitude_recover_deg;
}
bool SafetyMonitor::timeoutExceeded(std::int64_t elapsed_s, std::int64_t timeout_s) {
    return elapsed_s > timeout_s;
}

void SafetyMonitor::update(SafetyRule rule, bool trips, bool clears, const std::string& detail,
                           std::vector<SafetyChange>& changes) {
    const bool wasActive = isActive(rule);
    if (!wasActive && trips) {
        active_.insert(rule);
        changes.push_back({rule, true, detail});
    } else if (wasActive && clears) {
        active_.erase(rule);
        changes.push_back({rule, false, detail});
    }
}

std::vector<SafetyChange> SafetyMonitor::evaluate(const SafetyInputs& inputs) {
    std::vector<SafetyChange> changes;
    const SafetyThresholds& t = thresholds_;
    const TelemetryFrame* frame = inputs.latest;

    if (frame != nullptr && frame->sensors == SensorStatus::Valid) {
        const double v = frame->battery_v;
        update(SafetyRule::BatteryLow, batteryTrips(v, t), batteryClears(v, t),
               "battery_v=" + num(v) + " (trip < " + num(t.battery_min_v) + ", clear >= " +
                   num(t.battery_recover_v) + ")",
               changes);
        const double c = frame->temp_c;
        update(SafetyRule::TemperatureHigh, tempHighTrips(c, t), tempHighClears(c, t),
               "temp_c=" + num(c) + " (trip > " + num(t.temp_max_c) + ", clear <= " +
                   num(t.temp_max_recover_c) + ")",
               changes);
        update(SafetyRule::TemperatureLow, tempLowTrips(c, t), tempLowClears(c, t),
               "temp_c=" + num(c) + " (trip < " + num(t.temp_min_c) + ", clear >= " +
                   num(t.temp_min_recover_c) + ")",
               changes);
        const double a = frame->attitude_deg;
        update(SafetyRule::AttitudeExceeded, attitudeTrips(a, t), attitudeClears(a, t),
               "attitude_deg=" + num(a) + " (trip > " + num(t.attitude_max_deg) + ", clear <= " +
                   num(t.attitude_recover_deg) + ")",
               changes);
    }

    const bool sensorsInvalid = frame != nullptr && frame->sensors == SensorStatus::Invalid;
    update(SafetyRule::SensorInvalid, sensorsInvalid, frame != nullptr && !sensorsInvalid,
           sensorsInvalid ? "frame reports sensors=INVALID" : "frame reports sensors=VALID",
           changes);

    if (frame == nullptr) {
        update(SafetyRule::TelemetryStale, true, false, "no telemetry frame available", changes);
    } else {
        const std::int64_t age = inputs.now_s - frame->received_at_s;
        const bool stale = timeoutExceeded(age, timing_.telemetry_timeout_s);
        update(SafetyRule::TelemetryStale, stale, !stale,
               "telemetry age " + std::to_string(age) + " s (limit " +
                   std::to_string(timing_.telemetry_timeout_s) + " s)",
               changes);
    }

    const std::int64_t silence = inputs.now_s - inputs.last_contact_s;
    const bool commLost = timeoutExceeded(silence, timing_.comm_timeout_s);
    update(SafetyRule::CommTimeout, commLost, !commLost,
           "no ground contact for " + std::to_string(silence) + " s (limit " +
               std::to_string(timing_.comm_timeout_s) + " s)",
           changes);

    return changes;
}

}  // namespace cubesat

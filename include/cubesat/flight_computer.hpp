// Simulated flight computer: owns the operating-mode state machine, the
// latest telemetry frame, the simulated clock and the safety monitor.
//
// All mode changes go through transitionTo(), which consults a single
// transition table (isTransitionAllowed). There are no ad-hoc string
// comparisons on mode names anywhere else in the code.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "cubesat/config.hpp"
#include "cubesat/json.hpp"
#include "cubesat/logger.hpp"
#include "cubesat/modes.hpp"
#include "cubesat/result.hpp"
#include "cubesat/safety_monitor.hpp"
#include "cubesat/telemetry.hpp"

namespace cubesat {

// Faults that can be injected on purpose through FAULT_INJECT. Faults that
// come from bad telemetry (low battery, timeouts, ...) are injected through
// TELEMETRY and TICK commands instead.
enum class InjectedFault {
    InternalError,    // simulated unrecoverable software error -> FAULT
    SelfTestFailure,  // the next INIT's power-on self test fails -> FAULT
};

struct Counters {
    std::uint64_t commands_accepted = 0;
    std::uint64_t commands_rejected = 0;
    std::uint64_t frames_accepted = 0;
    std::uint64_t frames_rejected = 0;
    std::uint64_t safe_mode_entries = 0;
    std::uint64_t faults = 0;
    std::uint64_t resets = 0;
};

class FlightComputer {
public:
    FlightComputer(const SimulationConfig& config, Logger& logger);

    FlightComputer(const FlightComputer&) = delete;
    FlightComputer& operator=(const FlightComputer&) = delete;

    Mode mode() const { return mode_; }
    std::int64_t simTime() const { return sim_time_s_; }
    bool payloadOn() const { return payload_on_; }
    const SafetyMonitor& safetyMonitor() const { return monitor_; }
    const Counters& counters() const { return counters_; }

    // The single source of truth for legal mode changes.
    static bool isTransitionAllowed(Mode from, Mode to);

    // Command handlers. Each returns response data on success or an Error
    // whose code is reported to the operator. Mode permissions are checked
    // by CommandProcessor first; handlers re-check them as defence in depth.
    Result<json::Value> init();
    Result<json::Value> ingestTelemetry(TelemetryFrame frame);
    Result<json::Value> advanceTime(std::int64_t seconds);
    Result<json::Value> enterSafeModeByCommand();
    Result<json::Value> exitSafeMode();
    Result<json::Value> setPayload(bool on);
    Result<json::Value> reset();
    Result<json::Value> injectFault(InjectedFault fault);

    json::Value status() const;
    json::Value telemetrySnapshot() const;

    void recordCommandOutcome(bool accepted);
    void recordRejectedFrame(const Error& error);

    // Verifies internal consistency after every command, e.g. "NOMINAL mode
    // never coexists with an active safety violation". A violated invariant
    // is a software defect, so the computer enters FAULT.
    void enforceInvariants();

    void logEvent(Severity severity, std::string_view component, std::string_view event,
                  std::string_view message, json::Value details = json::Value::object());

private:
    void transitionTo(Mode next, const std::string& reason);
    void enterFault(const std::string& reason);
    void runSafetyEvaluation();
    void updateCommLink(CommStatus comm);
    json::Value activeViolations() const;

    SimulationConfig config_;
    Logger& logger_;
    SafetyMonitor monitor_;

    Mode mode_ = Mode::Boot;
    std::int64_t sim_time_s_ = 0;
    std::optional<TelemetryFrame> latest_;
    std::optional<std::uint64_t> last_seq_;
    std::int64_t last_contact_s_ = 0;
    std::optional<CommStatus> comm_link_;
    bool payload_on_ = false;
    bool self_test_failure_armed_ = false;
    std::string last_safe_mode_reason_;
    std::string fault_reason_;
    Counters counters_;
};

}  // namespace cubesat

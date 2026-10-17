#include "cubesat/flight_computer.hpp"

#include <utility>

namespace cubesat {

namespace {

constexpr std::string_view kComponent = "FlightComputer";
constexpr std::string_view kSafetyComponent = "SafetyMonitor";

Result<json::Value> reject(std::string code, std::string message) {
    return Result<json::Value>::fail(std::move(code), std::move(message));
}

Result<json::Value> notPermitted(std::string_view action, Mode mode) {
    return reject("NOT_PERMITTED_IN_MODE",
                  std::string(action) + " is not permitted in mode " + std::string(toString(mode)));
}

}  // namespace

FlightComputer::FlightComputer(const SimulationConfig& config, Logger& logger)
    : config_(config), logger_(logger), monitor_(config.thresholds, config.timing) {
    json::Value details = json::Value::object();
    details.set("config", config_.toJson());
    logEvent(Severity::Info, kComponent, "POWER_ON",
             "Flight computer powered on; waiting for INIT", std::move(details));
}

bool FlightComputer::isTransitionAllowed(Mode from, Mode to) {
    switch (from) {
        case Mode::Boot:
            return to == Mode::Nominal || to == Mode::SafeMode || to == Mode::Fault;
        case Mode::Nominal:
            return to == Mode::SafeMode || to == Mode::Fault || to == Mode::Boot;
        case Mode::SafeMode:
            return to == Mode::Nominal || to == Mode::Fault || to == Mode::Boot;
        case Mode::Fault:
            return to == Mode::Boot;  // only a controlled RESET leaves FAULT
    }
    return false;
}

void FlightComputer::logEvent(Severity severity, std::string_view component,
                              std::string_view event, std::string_view message,
                              json::Value details) {
    logger_.log(severity, component, event, message, mode_, sim_time_s_, std::move(details));
}

void FlightComputer::transitionTo(Mode next, const std::string& reason) {
    if (next == mode_) return;
    if (!isTransitionAllowed(mode_, next)) {
        // Reaching this line means the code tried an undocumented transition.
        json::Value details = json::Value::object();
        details.set("from", std::string(toString(mode_)));
        details.set("to", std::string(toString(next)));
        details.set("reason", reason);
        logEvent(Severity::Critical, kComponent, "INVALID_TRANSITION_BLOCKED",
                 "Blocked an undocumented mode transition", std::move(details));
        if (mode_ != Mode::Fault) enterFault("INVALID_TRANSITION_ATTEMPT");
        return;
    }

    const Mode previous = mode_;
    mode_ = next;
    if (next == Mode::SafeMode) {
        ++counters_.safe_mode_entries;
        last_safe_mode_reason_ = reason;
    }

    // Log the transition first, then its side effects, so the log reads in
    // cause -> effect order.
    json::Value details = json::Value::object();
    details.set("from", std::string(toString(previous)));
    details.set("to", std::string(toString(next)));
    details.set("reason", reason);
    const Severity severity = next == Mode::Fault      ? Severity::Critical
                              : next == Mode::SafeMode ? Severity::Warning
                                                       : Severity::Info;
    logEvent(severity, kComponent, "MODE_CHANGE",
             "Mode changed " + std::string(toString(previous)) + " -> " +
                 std::string(toString(next)),
             std::move(details));

    if (next != Mode::Nominal && payload_on_) {
        payload_on_ = false;
        logEvent(Severity::Warning, "Payload", "PAYLOAD_POWER_OFF",
                 "Payload powered off automatically on leaving NOMINAL");
    }
}

void FlightComputer::enterFault(const std::string& reason) {
    fault_reason_ = reason;
    ++counters_.faults;
    transitionTo(Mode::Fault, reason);
}

json::Value FlightComputer::activeViolations() const {
    json::Value list = json::Value::array();
    for (const SafetyRule rule : monitor_.active()) list.push(std::string(toString(rule)));
    return list;
}

void FlightComputer::runSafetyEvaluation() {
    if (mode_ != Mode::Nominal && mode_ != Mode::SafeMode && mode_ != Mode::Boot) return;

    SafetyInputs inputs;
    inputs.latest = latest_ ? &*latest_ : nullptr;
    inputs.now_s = sim_time_s_;
    inputs.last_contact_s = last_contact_s_;

    std::string firstNewViolation;
    for (const SafetyChange& change : monitor_.evaluate(inputs)) {
        json::Value details = json::Value::object();
        details.set("rule", std::string(toString(change.rule)));
        details.set("detail", change.detail);
        if (change.now_active) {
            if (firstNewViolation.empty()) firstNewViolation = std::string(toString(change.rule));
            logEvent(Severity::Warning, kSafetyComponent, "SAFETY_VIOLATION",
                     "Safety rule " + std::string(toString(change.rule)) + " violated",
                     std::move(details));
        } else {
            logEvent(Severity::Info, kSafetyComponent, "SAFETY_CLEARED",
                     "Safety rule " + std::string(toString(change.rule)) + " cleared",
                     std::move(details));
        }
    }

    if (mode_ == Mode::Nominal && monitor_.hasActive()) {
        const std::string reason = firstNewViolation.empty()
                                       ? std::string(toString(*monitor_.active().begin()))
                                       : firstNewViolation;
        transitionTo(Mode::SafeMode, reason);
    }
}

void FlightComputer::updateCommLink(CommStatus comm) {
    if (comm == CommStatus::Up) last_contact_s_ = sim_time_s_;
    if (comm_link_ == comm) return;
    const bool hadState = comm_link_.has_value();
    comm_link_ = comm;
    if (!hadState) return;
    if (comm == CommStatus::Down) {
        logEvent(Severity::Warning, "CommLink", "COMM_LINK_DOWN",
                 "Ground link reported DOWN; COMM_TIMEOUT after " +
                     std::to_string(config_.timing.comm_timeout_s) + " s without contact");
    } else {
        logEvent(Severity::Info, "CommLink", "COMM_LINK_RESTORED", "Ground link reported UP");
    }
}

Result<json::Value> FlightComputer::init() {
    if (mode_ != Mode::Boot) return notPermitted("INIT", mode_);

    logEvent(Severity::Info, kComponent, "SELF_TEST_STARTED", "Power-on self test started");
    if (self_test_failure_armed_) {
        self_test_failure_armed_ = false;
        logEvent(Severity::Error, kComponent, "SELF_TEST_FAILED",
                 "Power-on self test failed (injected SELF_TEST_FAILURE)");
        enterFault("SELF_TEST_FAILURE");
        json::Value data = json::Value::object();
        data.set("self_test", "FAILED");
        data.set("active_violations", activeViolations());
        return Result<json::Value>::ok(std::move(data));
    }
    if (!latest_) {
        logEvent(Severity::Warning, kComponent, "SELF_TEST_ABORTED",
                 "INIT needs at least one telemetry frame");
        return reject("NO_TELEMETRY", "INIT requires at least one accepted telemetry frame");
    }

    logEvent(Severity::Info, kComponent, "SELF_TEST_PASSED", "Power-on self test passed");
    // Start the ground-contact timer at boot; a DOWN link at boot is
    // tolerated until comm_timeout_s expires, like any later link drop.
    last_contact_s_ = sim_time_s_;
    comm_link_ = latest_->comm;
    runSafetyEvaluation();
    transitionTo(monitor_.hasActive() ? Mode::SafeMode : Mode::Nominal,
                 monitor_.hasActive() ? std::string(toString(*monitor_.active().begin()))
                                      : "SELF_TEST_PASSED");

    json::Value data = json::Value::object();
    data.set("self_test", "PASSED");
    data.set("active_violations", activeViolations());
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::ingestTelemetry(TelemetryFrame frame) {
    if (mode_ == Mode::Fault) return notPermitted("TELEMETRY", mode_);
    if (last_seq_ && frame.seq <= *last_seq_) {
        const Error error{"STALE_SEQUENCE", "frame seq " + std::to_string(frame.seq) +
                                                " is not greater than last accepted seq " +
                                                std::to_string(*last_seq_)};
        recordRejectedFrame(error);
        return Result<json::Value>::fail(error);
    }

    frame.received_at_s = sim_time_s_;
    last_seq_ = frame.seq;
    latest_ = frame;
    ++counters_.frames_accepted;

    if (mode_ != Mode::Boot) {
        updateCommLink(frame.comm);
        runSafetyEvaluation();
    }

    json::Value data = json::Value::object();
    data.set("accepted_seq", frame.seq);
    data.set("active_violations", activeViolations());
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::advanceTime(std::int64_t seconds) {
    if (seconds < 1 || seconds > config_.timing.max_tick_s) {
        return reject("INVALID_ARGUMENT", "TICK seconds must be between 1 and " +
                                              std::to_string(config_.timing.max_tick_s));
    }
    sim_time_s_ += seconds;
    if (mode_ == Mode::Nominal || mode_ == Mode::SafeMode) runSafetyEvaluation();

    json::Value data = json::Value::object();
    data.set("sim_time_s", sim_time_s_);
    data.set("active_violations", activeViolations());
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::enterSafeModeByCommand() {
    if (mode_ != Mode::Nominal && mode_ != Mode::SafeMode) {
        return notPermitted("ENTER_SAFE_MODE", mode_);
    }
    const Mode previous = mode_;
    if (mode_ == Mode::SafeMode) {
        logEvent(Severity::Info, kComponent, "SAFE_MODE_ALREADY_ACTIVE",
                 "ENTER_SAFE_MODE received while already in SAFE_MODE; no change");
    } else {
        transitionTo(Mode::SafeMode, "GROUND_COMMAND");
    }
    json::Value data = json::Value::object();
    data.set("previous_mode", std::string(toString(previous)));
    data.set("changed", previous != mode_);
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::exitSafeMode() {
    if (mode_ != Mode::SafeMode) return notPermitted("EXIT_SAFE_MODE", mode_);

    logEvent(Severity::Info, kComponent, "RECOVERY_ATTEMPT", "Ground requested exit from SAFE_MODE");
    if (monitor_.hasActive()) {
        json::Value details = json::Value::object();
        details.set("active_violations", activeViolations());
        logEvent(Severity::Warning, kComponent, "RECOVERY_DENIED",
                 "Cannot leave SAFE_MODE while safety violations are active", std::move(details));
        return reject("ACTIVE_VIOLATIONS",
                      "cannot exit SAFE_MODE while violations are active: " +
                          activeViolations().dump());
    }
    transitionTo(Mode::Nominal, "GROUND_RECOVERY_COMMAND");
    logEvent(Severity::Info, kComponent, "RECOVERY_SUCCEEDED", "Returned to NOMINAL");

    json::Value data = json::Value::object();
    data.set("previous_mode", "SAFE_MODE");
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::setPayload(bool on) {
    if (mode_ != Mode::Nominal) return notPermitted("PAYLOAD", mode_);
    if (payload_on_ != on) {
        payload_on_ = on;
        logEvent(Severity::Info, "Payload", on ? "PAYLOAD_POWER_ON" : "PAYLOAD_POWER_OFF",
                 on ? "Payload powered on" : "Payload powered off");
    }
    json::Value data = json::Value::object();
    data.set("payload", on ? "ON" : "OFF");
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::reset() {
    if (mode_ == Mode::Boot) return notPermitted("RESET", mode_);

    const Mode previous = mode_;
    logEvent(Severity::Warning, kComponent, "RESET_REQUESTED",
             "Controlled reset requested from " + std::string(toString(previous)));
    ++counters_.resets;
    transitionTo(Mode::Boot, "CONTROLLED_RESET");

    // A reset is a reboot: volatile state is lost. The simulated clock and
    // the lifetime counters survive so the session history stays readable.
    latest_.reset();
    last_seq_.reset();
    comm_link_.reset();
    last_contact_s_ = sim_time_s_;
    monitor_.clear();
    payload_on_ = false;
    self_test_failure_armed_ = false;
    fault_reason_.clear();

    json::Value data = json::Value::object();
    data.set("previous_mode", std::string(toString(previous)));
    data.set("resets", counters_.resets);
    return Result<json::Value>::ok(std::move(data));
}

Result<json::Value> FlightComputer::injectFault(InjectedFault fault) {
    if (mode_ == Mode::Fault) return notPermitted("FAULT_INJECT", mode_);

    json::Value data = json::Value::object();
    switch (fault) {
        case InjectedFault::InternalError: {
            logEvent(Severity::Warning, "FaultInjector", "FAULT_INJECTED",
                     "Injected simulated INTERNAL_ERROR");
            enterFault("INJECTED_INTERNAL_ERROR");
            data.set("fault", "INTERNAL_ERROR");
            break;
        }
        case InjectedFault::SelfTestFailure: {
            if (mode_ != Mode::Boot) {
                return reject("NOT_PERMITTED_IN_MODE",
                              "SELF_TEST_FAILURE can only be armed in BOOT mode");
            }
            self_test_failure_armed_ = true;
            logEvent(Severity::Warning, "FaultInjector", "FAULT_INJECTED",
                     "Armed simulated SELF_TEST_FAILURE for the next INIT");
            data.set("fault", "SELF_TEST_FAILURE");
            break;
        }
    }
    return Result<json::Value>::ok(std::move(data));
}

json::Value FlightComputer::status() const {
    json::Value counters = json::Value::object();
    counters.set("commands_accepted", counters_.commands_accepted);
    counters.set("commands_rejected", counters_.commands_rejected);
    counters.set("frames_accepted", counters_.frames_accepted);
    counters.set("frames_rejected", counters_.frames_rejected);
    counters.set("safe_mode_entries", counters_.safe_mode_entries);
    counters.set("faults", counters_.faults);
    counters.set("resets", counters_.resets);

    json::Value data = json::Value::object();
    data.set("mode", std::string(toString(mode_)));
    data.set("sim_time_s", sim_time_s_);
    data.set("payload", payload_on_ ? "ON" : "OFF");
    data.set("active_violations", activeViolations());
    data.set("telemetry_available", latest_.has_value());
    data.set("comm_link", comm_link_ ? std::string(toString(*comm_link_)) : "UNKNOWN");
    data.set("last_safe_mode_reason",
             last_safe_mode_reason_.empty() ? json::Value(nullptr) : json::Value(last_safe_mode_reason_));
    data.set("fault_reason", fault_reason_.empty() ? json::Value(nullptr) : json::Value(fault_reason_));
    data.set("counters", std::move(counters));
    return data;
}

json::Value FlightComputer::telemetrySnapshot() const {
    json::Value data = json::Value::object();
    data.set("frame", latest_ ? latest_->toJson() : json::Value(nullptr));
    data.set("age_s", latest_ ? json::Value(sim_time_s_ - latest_->received_at_s)
                              : json::Value(nullptr));
    return data;
}

void FlightComputer::recordCommandOutcome(bool accepted) {
    if (accepted) {
        ++counters_.commands_accepted;
    } else {
        ++counters_.commands_rejected;
    }
}

void FlightComputer::recordRejectedFrame(const Error& error) {
    ++counters_.frames_rejected;
    json::Value details = json::Value::object();
    details.set("code", error.code);
    details.set("reason", error.message);
    logEvent(Severity::Warning, "Telemetry", "TELEMETRY_REJECTED", "Telemetry frame rejected",
             std::move(details));
}

void FlightComputer::enforceInvariants() {
    std::string problem;
    if (mode_ == Mode::Nominal && monitor_.hasActive()) {
        problem = "NOMINAL mode with active safety violations " + activeViolations().dump();
    } else if (payload_on_ && mode_ != Mode::Nominal) {
        problem = "payload powered while not in NOMINAL";
    }
    if (problem.empty()) return;

    json::Value details = json::Value::object();
    details.set("problem", problem);
    logEvent(Severity::Critical, kComponent, "INVARIANT_VIOLATION",
             "Internal consistency check failed", std::move(details));
    if (mode_ != Mode::Fault) enterFault("INVARIANT_VIOLATION");
}

}  // namespace cubesat

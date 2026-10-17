#include "cubesat/command_processor.hpp"

#include <cctype>
#include <utility>

#include "cubesat/telemetry.hpp"

namespace cubesat {

namespace {

// Column order: BOOT, NOMINAL, SAFE_MODE, FAULT.
constexpr std::array<bool, kModeCount> kAnyMode = {true, true, true, true};

std::string toUpper(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) tokens.emplace_back(line.substr(start, i - start));
    }
    return tokens;
}

Result<json::Value> invalidArgument(std::string message) {
    return Result<json::Value>::fail("INVALID_ARGUMENT", std::move(message));
}

json::Value permittedModes(const CommandSpec& spec) {
    json::Value modes = json::Value::array();
    for (const Mode mode : kAllModes) {
        if (spec.permitted[modeIndex(mode)]) modes.push(std::string(toString(mode)));
    }
    return modes;
}

}  // namespace

const std::vector<CommandSpec>& commandTable() {
    //                                         BOOT   NOMINAL SAFE   FAULT
    static const std::vector<CommandSpec> table = {
        {CommandId::Help, "HELP", 0, 0, kAnyMode, "HELP",
         "List commands, usage and the modes in which each is permitted"},
        {CommandId::Status, "STATUS", 0, 0, kAnyMode, "STATUS",
         "Report mode, active violations, payload state and counters"},
        {CommandId::GetTelemetry, "GET_TELEMETRY", 0, 0, kAnyMode, "GET_TELEMETRY",
         "Return the latest accepted telemetry frame and its age"},
        {CommandId::Telemetry, "TELEMETRY", 0, 6, {true, true, true, false},
         "TELEMETRY seq=<n> battery_v=<V> temp_c=<C> attitude_deg=<deg> comm=<UP|DOWN> "
         "sensors=<VALID|INVALID>",
         "Inject one simulated telemetry frame"},
        {CommandId::Tick, "TICK", 1, 1, kAnyMode, "TICK <seconds>",
         "Advance the simulated clock and re-evaluate safety rules"},
        {CommandId::Init, "INIT", 0, 0, {true, false, false, false}, "INIT",
         "Run the power-on self test and leave BOOT"},
        {CommandId::EnterSafeMode, "ENTER_SAFE_MODE", 0, 0, {false, true, true, false},
         "ENTER_SAFE_MODE", "Ground command to enter SAFE_MODE (idempotent)"},
        {CommandId::ExitSafeMode, "EXIT_SAFE_MODE", 0, 0, {false, false, true, false},
         "EXIT_SAFE_MODE", "Return to NOMINAL; refused while any violation is active"},
        {CommandId::Payload, "PAYLOAD", 1, 1, {false, true, false, false}, "PAYLOAD <ON|OFF>",
         "Switch the simulated imaging payload on or off"},
        {CommandId::Reset, "RESET", 1, 1, {false, true, true, true}, "RESET CONFIRM",
         "Controlled reboot to BOOT; the only way out of FAULT"},
        {CommandId::FaultInject, "FAULT_INJECT", 1, 1, {true, true, true, false},
         "FAULT_INJECT <INTERNAL_ERROR|SELF_TEST_FAILURE>",
         "Test hook: inject a simulated internal fault"},
    };
    return table;
}

const CommandSpec* findCommand(std::string_view name) {
    const std::string upper = toUpper(name);
    for (const CommandSpec& spec : commandTable()) {
        if (spec.name == upper) return &spec;
    }
    return nullptr;
}

CommandProcessor::CommandProcessor(FlightComputer& computer, std::size_t max_line_length)
    : computer_(computer), max_line_length_(max_line_length) {}

json::Value CommandProcessor::respond(const std::optional<std::string>& command,
                                      const Result<json::Value>& result) {
    const bool accepted = result.isOk();
    computer_.recordCommandOutcome(accepted);

    json::Value details = json::Value::object();
    details.set("command", command ? json::Value(*command) : json::Value(nullptr));
    if (accepted) {
        computer_.logEvent(Severity::Info, "CommandProcessor", "COMMAND_ACCEPTED",
                           "Command accepted", std::move(details));
    } else {
        details.set("code", result.error().code);
        details.set("reason", result.error().message);
        computer_.logEvent(Severity::Warning, "CommandProcessor", "COMMAND_REJECTED",
                           "Command rejected", std::move(details));
    }

    // Consistency check after every command, before the response reports
    // the mode, so the response can never show an inconsistent state.
    computer_.enforceInvariants();

    json::Value response = json::Value::object();
    response.set("response_seq", ++response_seq_);
    response.set("command", command ? json::Value(*command) : json::Value(nullptr));
    response.set("status", accepted ? "OK" : "REJECTED");
    response.set("mode", std::string(toString(computer_.mode())));
    response.set("sim_time_s", computer_.simTime());
    if (accepted) {
        response.set("data", result.value());
    } else {
        json::Value error = json::Value::object();
        error.set("code", result.error().code);
        error.set("message", result.error().message);
        response.set("error", std::move(error));
    }
    return response;
}

std::optional<json::Value> CommandProcessor::processLine(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);  // tolerate CRLF

    if (line.size() > max_line_length_) {
        return respond(std::nullopt,
                       Result<json::Value>::fail(
                           "MALFORMED_INPUT", "line of " + std::to_string(line.size()) +
                                                  " bytes exceeds max_command_length " +
                                                  std::to_string(max_line_length_)));
    }
    for (const char c : line) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte < 0x20 && c != '\t') || byte > 0x7E) {
            return respond(std::nullopt,
                           Result<json::Value>::fail(
                               "MALFORMED_INPUT",
                               "line contains a non-printable or non-ASCII byte"));
        }
    }

    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty() || tokens.front().front() == '#') return std::nullopt;

    const CommandSpec* spec = findCommand(tokens.front());
    if (spec == nullptr) {
        return respond(tokens.front(),
                       Result<json::Value>::fail("UNKNOWN_COMMAND",
                                                 "unknown command '" + tokens.front() +
                                                     "'; send HELP for the command list"));
    }
    const std::string name(spec->name);
    const Mode mode = computer_.mode();
    if (!spec->permitted[modeIndex(mode)]) {
        return respond(name, Result<json::Value>::fail(
                                 "NOT_PERMITTED_IN_MODE",
                                 name + " is not permitted in mode " + std::string(toString(mode)) +
                                     "; permitted in " + permittedModes(*spec).dump()));
    }

    const std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    if (args.size() < spec->min_args || args.size() > spec->max_args) {
        return respond(name, invalidArgument("wrong number of arguments; usage: " +
                                             std::string(spec->usage)));
    }
    return respond(name, dispatch(*spec, args));
}

Result<json::Value> CommandProcessor::dispatch(const CommandSpec& spec,
                                               const std::vector<std::string>& args) {
    switch (spec.id) {
        case CommandId::Help: {
            json::Value commands = json::Value::array();
            for (const CommandSpec& entry : commandTable()) {
                json::Value item = json::Value::object();
                item.set("name", std::string(entry.name));
                item.set("usage", std::string(entry.usage));
                item.set("description", std::string(entry.description));
                item.set("permitted_modes", permittedModes(entry));
                commands.push(std::move(item));
            }
            json::Value data = json::Value::object();
            data.set("commands", std::move(commands));
            return Result<json::Value>::ok(std::move(data));
        }
        case CommandId::Status:
            return Result<json::Value>::ok(computer_.status());
        case CommandId::GetTelemetry:
            return Result<json::Value>::ok(computer_.telemetrySnapshot());
        case CommandId::Telemetry: {
            Result<TelemetryFrame> frame = parseTelemetryArgs(args);
            if (!frame.isOk()) {
                computer_.recordRejectedFrame(frame.error());
                return Result<json::Value>::fail(frame.error());
            }
            return computer_.ingestTelemetry(frame.value());
        }
        case CommandId::Tick: {
            std::uint64_t seconds = 0;
            if (!parseStrictUnsigned(args[0], plausibility::kMaxSequence, seconds)) {
                return invalidArgument("TICK expects a positive whole number of seconds, got '" +
                                       args[0] + "'");
            }
            return computer_.advanceTime(static_cast<std::int64_t>(seconds));
        }
        case CommandId::Init:
            return computer_.init();
        case CommandId::EnterSafeMode:
            return computer_.enterSafeModeByCommand();
        case CommandId::ExitSafeMode:
            return computer_.exitSafeMode();
        case CommandId::Payload: {
            const std::string state = toUpper(args[0]);
            if (state != "ON" && state != "OFF") {
                return invalidArgument("PAYLOAD expects ON or OFF, got '" + args[0] + "'");
            }
            return computer_.setPayload(state == "ON");
        }
        case CommandId::Reset:
            if (toUpper(args[0]) != "CONFIRM") {
                return invalidArgument("RESET must be confirmed: send 'RESET CONFIRM'");
            }
            return computer_.reset();
        case CommandId::FaultInject: {
            const std::string fault = toUpper(args[0]);
            if (fault == "INTERNAL_ERROR") return computer_.injectFault(InjectedFault::InternalError);
            if (fault == "SELF_TEST_FAILURE") {
                return computer_.injectFault(InjectedFault::SelfTestFailure);
            }
            return invalidArgument("unknown fault type '" + args[0] +
                                   "'; expected INTERNAL_ERROR or SELF_TEST_FAILURE");
        }
    }
    return Result<json::Value>::fail("INTERNAL_ERROR", "unhandled command id");
}

}  // namespace cubesat

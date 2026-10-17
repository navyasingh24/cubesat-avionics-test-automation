// C++ unit tests for the simulator's building blocks.
//
// A deliberately tiny test runner (no third-party framework) so the project
// builds offline. The Python/pytest suite in tests/ is the main system-level
// verification; these tests check individual classes in isolation.
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cubesat/cli.hpp"
#include "cubesat/command_processor.hpp"
#include "cubesat/config.hpp"
#include "cubesat/flight_computer.hpp"
#include "cubesat/json.hpp"
#include "cubesat/logger.hpp"
#include "cubesat/safety_monitor.hpp"
#include "cubesat/telemetry.hpp"

namespace {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

int g_failures = 0;
std::vector<std::string> g_messages;  // failure messages of the running test

void expect(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        ++g_failures;
        const std::string message =
            std::string(expression) + " (" + file + ":" + std::to_string(line) + ")";
        g_messages.push_back(message);
        std::cerr << "    FAILED: " << message << '\n';
    }
}

std::string xmlEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

struct Outcome {
    std::string name;
    std::vector<std::string> messages;
};

// Writes one JUnit <testcase> per C++ test so CI shows individual results.
bool writeJunit(const std::string& path, const std::vector<Outcome>& outcomes, int failed) {
    std::ofstream out(path);
    if (!out) return false;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<testsuite name=\"cpp-unit-tests\" tests=\"" << outcomes.size() << "\" failures=\""
        << failed << "\" errors=\"0\" skipped=\"0\">\n";
    for (const Outcome& outcome : outcomes) {
        out << "  <testcase classname=\"unit_tests\" name=\"" << xmlEscape(outcome.name) << "\"";
        if (outcome.messages.empty()) {
            out << "/>\n";
            continue;
        }
        out << ">\n    <failure message=\"" << xmlEscape(outcome.messages.front()) << "\">";
        for (const std::string& message : outcome.messages) out << xmlEscape(message) << '\n';
        out << "</failure>\n  </testcase>\n";
    }
    out << "</testsuite>\n";
    return static_cast<bool>(out);
}

#define TEST(name)                                     \
    void name();                                       \
    const Registrar registrar_##name(#name, &name);    \
    void name()
#define EXPECT(cond) expect((cond), #cond, __FILE__, __LINE__)

using namespace cubesat;

const char* kValidConfig = R"({
  "schema_version": 1,
  "thresholds": {
    "battery_min_v": 6.8, "battery_recover_v": 7.0,
    "temp_max_c": 60.0, "temp_max_recover_c": 55.0,
    "temp_min_c": -20.0, "temp_min_recover_c": -15.0,
    "attitude_max_deg": 15.0, "attitude_recover_deg": 10.0
  },
  "timing": {"telemetry_timeout_s": 10, "comm_timeout_s": 60, "max_tick_s": 3600},
  "limits": {"max_command_length": 256}
})";

SimulationConfig validConfig() { return parseConfig(kValidConfig).value(); }

std::string replaceOnce(std::string text, const std::string& from, const std::string& to) {
    const auto pos = text.find(from);
    if (pos != std::string::npos) text.replace(pos, from.size(), to);
    return text;
}

// ---------------------------------------------------------------- JSON ----

TEST(json_parses_nested_document) {
    const auto result = json::parse(R"({"a": [1, 2.5, -3e2], "b": {"c": "x\ny"}, "d": null})");
    EXPECT(result.isOk());
    const json::Value& v = result.value();
    EXPECT(v.find("a")->asArray().size() == 3);
    EXPECT(v.find("a")->asArray()[2].asNumber() == -300.0);
    EXPECT(v.find("b")->find("c")->asString() == "x\ny");
    EXPECT(v.find("d")->isNull());
}

TEST(json_rejects_malformed_documents) {
    const char* bad[] = {"", "{", "{\"a\":}", "[1,]", "{\"a\":1,}", "01", "1.", "nan",
                         "\"unterminated", "{\"a\":1} trailing", "{'a':1}", "[1 2]"};
    for (const char* text : bad) EXPECT(!json::parse(text).isOk());
}

TEST(json_rejects_duplicate_keys) { EXPECT(!json::parse(R"({"a":1,"a":2})").isOk()); }

TEST(json_enforces_depth_limit) {
    std::string deep(json::kMaxNestingDepth + 2, '[');
    deep += std::string(json::kMaxNestingDepth + 2, ']');
    EXPECT(!json::parse(deep).isOk());
}

TEST(json_decodes_unicode_escapes) {
    const auto result = json::parse(R"("é🚀")");
    EXPECT(result.isOk());
    EXPECT(result.value().asString() == "\xC3\xA9\xF0\x9F\x9A\x80");
    EXPECT(!json::parse(R"("\ud83d")").isOk());  // unpaired surrogate
}

TEST(json_serializes_deterministically) {
    json::Value v = json::Value::object();
    v.set("z", 1);
    v.set("a", 7.4);
    v.set("s", "q\"uote");
    EXPECT(v.dump() == R"({"z":1,"a":7.4,"s":"q\"uote"})");
}

// -------------------------------------------------------------- Config ----

TEST(config_accepts_valid_document) {
    const auto result = parseConfig(kValidConfig);
    EXPECT(result.isOk());
    EXPECT(result.value().thresholds.battery_min_v == 6.8);
    EXPECT(result.value().timing.comm_timeout_s == 60);
}

TEST(config_rejects_contradictory_thresholds) {
    const std::string text =
        replaceOnce(kValidConfig, "\"battery_recover_v\": 7.0", "\"battery_recover_v\": 6.0");
    const auto result = parseConfig(text);
    EXPECT(!result.isOk());
    EXPECT(result.error().code == "CONFIG_INVALID");
}

TEST(config_rejects_unknown_and_missing_keys) {
    EXPECT(!parseConfig(replaceOnce(kValidConfig, "\"max_tick_s\"", "\"max_tik_s\"")).isOk());
    EXPECT(!parseConfig(replaceOnce(kValidConfig, "\"schema_version\": 1,", "")).isOk());
}

TEST(config_rejects_fractional_timing) {
    EXPECT(!parseConfig(replaceOnce(kValidConfig, "\"comm_timeout_s\": 60",
                                    "\"comm_timeout_s\": 60.5"))
                .isOk());
}

TEST(config_missing_file_reports_not_found) {
    const auto result = loadConfigFile("/nonexistent/definitely/missing.json");
    EXPECT(!result.isOk());
    EXPECT(result.error().code == "CONFIG_NOT_FOUND");
}

// ----------------------------------------------------------- Telemetry ----

TEST(telemetry_parses_complete_frame) {
    const auto frame = parseTelemetryArgs({"seq=3", "battery_v=7.4", "temp_c=-1.5",
                                           "attitude_deg=2", "comm=DOWN", "sensors=INVALID"});
    EXPECT(frame.isOk());
    EXPECT(frame.value().seq == 3);
    EXPECT(frame.value().temp_c == -1.5);
    EXPECT(frame.value().comm == CommStatus::Down);
    EXPECT(frame.value().sensors == SensorStatus::Invalid);
}

TEST(telemetry_rejects_bad_frames_with_specific_codes) {
    const std::vector<std::string> base = {"seq=1",          "battery_v=7.4", "temp_c=20",
                                           "attitude_deg=1", "comm=UP",       "sensors=VALID"};
    auto with = [&](std::size_t index, const std::string& replacement) {
        auto copy = base;
        copy[index] = replacement;
        return parseTelemetryArgs(copy);
    };
    EXPECT(with(1, "battery_v=abc").error().code == "INVALID_ARGUMENT");
    EXPECT(with(1, "battery_v=nan").error().code == "INVALID_ARGUMENT");
    EXPECT(with(1, "battery_v=0x10").error().code == "INVALID_ARGUMENT");
    EXPECT(with(1, "battery_v=25").error().code == "OUT_OF_RANGE");
    EXPECT(with(4, "comm=MAYBE").error().code == "INVALID_ARGUMENT");
    EXPECT(with(0, "seq=-1").error().code == "INVALID_ARGUMENT");
    EXPECT(with(5, "battery_v=7.0").error().code == "INVALID_ARGUMENT");  // duplicate
    EXPECT(parseTelemetryArgs({"seq=1"}).error().code == "MISSING_FIELD");
}

TEST(strict_number_parsers) {
    double d = 0;
    EXPECT(parseStrictDouble("7.4", d) && d == 7.4);
    EXPECT(parseStrictDouble("-1e2", d) && d == -100.0);
    EXPECT(!parseStrictDouble("", d));
    EXPECT(!parseStrictDouble("7.4V", d));
    EXPECT(!parseStrictDouble("inf", d));
    EXPECT(!parseStrictDouble("1e999", d));
    std::uint64_t u = 0;
    EXPECT(parseStrictUnsigned("42", 100, u) && u == 42);
    EXPECT(!parseStrictUnsigned("101", 100, u));
    EXPECT(!parseStrictUnsigned("+1", 100, u));
    EXPECT(!parseStrictUnsigned("99999999999999999999999", plausibility::kMaxSequence, u));
}

// -------------------------------------------------------- Safety rules ----

TEST(safety_boundaries_are_strict_for_trip_inclusive_for_clear) {
    const SafetyThresholds t = validConfig().thresholds;
    EXPECT(!SafetyMonitor::batteryTrips(6.8, t));
    EXPECT(SafetyMonitor::batteryTrips(6.79, t));
    EXPECT(SafetyMonitor::batteryClears(7.0, t));
    EXPECT(!SafetyMonitor::batteryClears(6.99, t));
    EXPECT(!SafetyMonitor::tempHighTrips(60.0, t));
    EXPECT(SafetyMonitor::tempHighTrips(60.01, t));
    EXPECT(SafetyMonitor::tempHighClears(55.0, t));
    EXPECT(!SafetyMonitor::tempLowTrips(-20.0, t));
    EXPECT(SafetyMonitor::tempLowTrips(-20.01, t));
    EXPECT(!SafetyMonitor::attitudeTrips(15.0, t));
    EXPECT(SafetyMonitor::attitudeTrips(15.01, t));
    EXPECT(!SafetyMonitor::timeoutExceeded(10, 10));
    EXPECT(SafetyMonitor::timeoutExceeded(11, 10));
}

TEST(safety_monitor_applies_hysteresis) {
    const SimulationConfig config = validConfig();
    SafetyMonitor monitor(config.thresholds, config.timing);
    TelemetryFrame frame;
    frame.battery_v = 6.7;
    frame.temp_c = 20;
    SafetyInputs inputs{&frame, 0, 0};
    auto changes = monitor.evaluate(inputs);
    EXPECT(changes.size() == 1 && changes[0].rule == SafetyRule::BatteryLow && changes[0].now_active);
    frame.battery_v = 6.9;  // above trip, below recover: stays active
    EXPECT(monitor.evaluate(inputs).empty());
    EXPECT(monitor.isActive(SafetyRule::BatteryLow));
    frame.battery_v = 7.0;
    changes = monitor.evaluate(inputs);
    EXPECT(changes.size() == 1 && !changes[0].now_active);
    EXPECT(!monitor.hasActive());
}

TEST(safety_monitor_holds_value_rules_when_sensors_invalid) {
    const SimulationConfig config = validConfig();
    SafetyMonitor monitor(config.thresholds, config.timing);
    TelemetryFrame frame;
    frame.battery_v = 1.0;  // would trip, but the frame says sensors are invalid
    frame.sensors = SensorStatus::Invalid;
    monitor.evaluate({&frame, 0, 0});
    EXPECT(monitor.isActive(SafetyRule::SensorInvalid));
    EXPECT(!monitor.isActive(SafetyRule::BatteryLow));
}

// ------------------------------------------------------- State machine ----

TEST(transition_table_matches_documentation) {
    EXPECT(FlightComputer::isTransitionAllowed(Mode::Boot, Mode::Nominal));
    EXPECT(FlightComputer::isTransitionAllowed(Mode::Nominal, Mode::SafeMode));
    EXPECT(FlightComputer::isTransitionAllowed(Mode::SafeMode, Mode::Nominal));
    EXPECT(FlightComputer::isTransitionAllowed(Mode::Fault, Mode::Boot));
    EXPECT(!FlightComputer::isTransitionAllowed(Mode::Fault, Mode::Nominal));
    EXPECT(!FlightComputer::isTransitionAllowed(Mode::Fault, Mode::SafeMode));
    EXPECT(!FlightComputer::isTransitionAllowed(Mode::Boot, Mode::Boot));
}

TEST(flight_computer_boot_to_nominal_to_safe) {
    std::ostringstream logs;
    Logger logger(logs);
    FlightComputer fc(validConfig(), logger);
    EXPECT(fc.mode() == Mode::Boot);
    EXPECT(fc.init().error().code == "NO_TELEMETRY");
    TelemetryFrame frame;
    frame.seq = 1;
    frame.battery_v = 7.5;
    frame.temp_c = 20;
    EXPECT(fc.ingestTelemetry(frame).isOk());
    EXPECT(fc.init().isOk());
    EXPECT(fc.mode() == Mode::Nominal);
    frame.seq = 2;
    frame.temp_c = 70;
    EXPECT(fc.ingestTelemetry(frame).isOk());
    EXPECT(fc.mode() == Mode::SafeMode);
    EXPECT(logs.str().find("\"event\":\"MODE_CHANGE\"") != std::string::npos);
}

TEST(command_processor_reports_structured_errors) {
    std::ostringstream logs;
    Logger logger(logs);
    FlightComputer fc(validConfig(), logger);
    CommandProcessor processor(fc, 256);
    EXPECT(!processor.processLine("   ").has_value());
    EXPECT(!processor.processLine("# comment").has_value());
    auto response = processor.processLine("FLY_TO_MOON");
    EXPECT(response->find("error")->find("code")->asString() == "UNKNOWN_COMMAND");
    response = processor.processLine("PAYLOAD ON");
    EXPECT(response->find("error")->find("code")->asString() == "NOT_PERMITTED_IN_MODE");
    response = processor.processLine(std::string(300, 'A'));
    EXPECT(response->find("error")->find("code")->asString() == "MALFORMED_INPUT");
    response = processor.processLine("status");
    EXPECT(response->find("status")->asString() == "OK");
}

TEST(cli_exit_codes) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    EXPECT(cli::run({"--help"}, in, out, err) == cli::kExitOk);
    EXPECT(cli::run({}, in, out, err) == cli::kExitUsage);
    EXPECT(cli::run({"--bogus"}, in, out, err) == cli::kExitUsage);
    EXPECT(cli::run({"--config", "/nonexistent.json"}, in, out, err) == cli::kExitConfigError);
}

}  // namespace

// Usage: cubesat_unit_tests [--junit <file>]
int main(int argc, char** argv) {
    std::string junitPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--junit") == 0 && i + 1 < argc) {
            junitPath = argv[++i];
        } else {
            std::cerr << "usage: cubesat_unit_tests [--junit <file>]\n";
            return 2;
        }
    }

    int failedTests = 0;
    std::vector<Outcome> outcomes;
    for (const TestCase& test : registry()) {
        const int before = g_failures;
        g_messages.clear();
        try {
            test.body();
        } catch (const std::exception& ex) {
            ++g_failures;
            g_messages.push_back(std::string("unexpected exception: ") + ex.what());
            std::cerr << "    EXCEPTION: " << ex.what() << '\n';
        }
        const bool passed = g_failures == before;
        if (!passed) ++failedTests;
        outcomes.push_back({test.name, g_messages});
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << '\n';
    }
    std::cout << registry().size() - static_cast<std::size_t>(failedTests) << " passed, "
              << failedTests << " failed\n";
    if (!junitPath.empty() && !writeJunit(junitPath, outcomes, failedTests)) {
        std::cerr << "cannot write JUnit report to " << junitPath << '\n';
        return 1;
    }
    return failedTests == 0 ? 0 : 1;
}

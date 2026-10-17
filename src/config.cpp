#include "cubesat/config.hpp"

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>

#include "cubesat/telemetry.hpp"

namespace cubesat {

namespace {

using json::Value;

Result<SimulationConfig> invalid(const std::string& message) {
    return Result<SimulationConfig>::fail("CONFIG_INVALID", message);
}

// Returns an empty string when every key of `object` is in `allowed` and
// every allowed key is present; otherwise a description of the problem.
std::string checkKeys(const Value& object, const std::string& path,
                      std::initializer_list<const char*> allowed) {
    for (const auto& member : object.asObject()) {
        bool known = false;
        for (const char* key : allowed) {
            if (member.first == key) known = true;
        }
        if (!known) return "unknown key '" + path + member.first + "'";
    }
    for (const char* key : allowed) {
        if (object.find(key) == nullptr) return "missing key '" + path + key + "'";
    }
    return {};
}

bool readNumber(const Value& object, const char* key, double& out, std::string& error,
                const std::string& path) {
    const Value* value = object.find(key);
    if (value == nullptr || !value->isNumber()) {
        error = "'" + path + key + "' must be a number";
        return false;
    }
    out = value->asNumber();
    return true;
}

bool readInteger(const Value& object, const char* key, std::int64_t min, std::int64_t max,
                 std::int64_t& out, std::string& error, const std::string& path) {
    double number = 0.0;
    if (!readNumber(object, key, number, error, path)) return false;
    if (std::floor(number) != number) {
        error = "'" + path + key + "' must be a whole number";
        return false;
    }
    if (number < static_cast<double>(min) || number > static_cast<double>(max)) {
        error = "'" + path + key + "' must be between " + std::to_string(min) + " and " +
                std::to_string(max);
        return false;
    }
    out = static_cast<std::int64_t>(number);
    return true;
}

bool inRange(double value, double min, double max) { return value >= min && value <= max; }

std::string validateThresholds(const SafetyThresholds& t) {
    namespace p = plausibility;
    if (!inRange(t.battery_min_v, p::kBatteryMinV, p::kBatteryMaxV) ||
        !inRange(t.battery_recover_v, p::kBatteryMinV, p::kBatteryMaxV)) {
        return "battery thresholds must lie within the plausible battery range";
    }
    if (t.battery_recover_v < t.battery_min_v) {
        return "contradictory thresholds: battery_recover_v must be >= battery_min_v";
    }
    if (!inRange(t.temp_max_c, p::kTempMinC, p::kTempMaxC) ||
        !inRange(t.temp_max_recover_c, p::kTempMinC, p::kTempMaxC) ||
        !inRange(t.temp_min_c, p::kTempMinC, p::kTempMaxC) ||
        !inRange(t.temp_min_recover_c, p::kTempMinC, p::kTempMaxC)) {
        return "temperature thresholds must lie within the plausible temperature range";
    }
    if (t.temp_max_recover_c > t.temp_max_c) {
        return "contradictory thresholds: temp_max_recover_c must be <= temp_max_c";
    }
    if (t.temp_min_recover_c < t.temp_min_c) {
        return "contradictory thresholds: temp_min_recover_c must be >= temp_min_c";
    }
    if (t.temp_min_recover_c >= t.temp_max_recover_c) {
        return "contradictory thresholds: temp_min_recover_c must be < temp_max_recover_c "
               "(otherwise no temperature could ever clear both rules)";
    }
    if (!inRange(t.attitude_max_deg, p::kAttitudeMinDeg, p::kAttitudeMaxDeg) ||
        !inRange(t.attitude_recover_deg, p::kAttitudeMinDeg, p::kAttitudeMaxDeg)) {
        return "attitude thresholds must lie within the plausible attitude range";
    }
    if (t.attitude_recover_deg > t.attitude_max_deg) {
        return "contradictory thresholds: attitude_recover_deg must be <= attitude_max_deg";
    }
    return {};
}

}  // namespace

json::Value SimulationConfig::toJson() const {
    Value thresholdsJson = Value::object();
    thresholdsJson.set("battery_min_v", thresholds.battery_min_v);
    thresholdsJson.set("battery_recover_v", thresholds.battery_recover_v);
    thresholdsJson.set("temp_max_c", thresholds.temp_max_c);
    thresholdsJson.set("temp_max_recover_c", thresholds.temp_max_recover_c);
    thresholdsJson.set("temp_min_c", thresholds.temp_min_c);
    thresholdsJson.set("temp_min_recover_c", thresholds.temp_min_recover_c);
    thresholdsJson.set("attitude_max_deg", thresholds.attitude_max_deg);
    thresholdsJson.set("attitude_recover_deg", thresholds.attitude_recover_deg);

    Value timingJson = Value::object();
    timingJson.set("telemetry_timeout_s", timing.telemetry_timeout_s);
    timingJson.set("comm_timeout_s", timing.comm_timeout_s);
    timingJson.set("max_tick_s", timing.max_tick_s);

    Value limitsJson = Value::object();
    limitsJson.set("max_command_length", max_command_length);

    Value out = Value::object();
    out.set("schema_version", schema_version);
    out.set("thresholds", thresholdsJson);
    out.set("timing", timingJson);
    out.set("limits", limitsJson);
    return out;
}

Result<SimulationConfig> parseConfig(std::string_view text) {
    const Result<Value> parsed = json::parse(text);
    if (!parsed.isOk()) {
        return invalid("configuration is not valid JSON: " + parsed.error().message);
    }
    const Value& root = parsed.value();
    if (!root.isObject()) return invalid("configuration root must be a JSON object");

    std::string error = checkKeys(root, "", {"schema_version", "thresholds", "timing", "limits"});
    if (!error.empty()) return invalid(error);

    SimulationConfig config;
    std::int64_t schema = 0;
    if (!readInteger(root, "schema_version", 0, 1000, schema, error, "")) return invalid(error);
    if (schema != kSupportedConfigSchemaVersion) {
        return invalid("unsupported schema_version " + std::to_string(schema) + " (expected " +
                       std::to_string(kSupportedConfigSchemaVersion) + ")");
    }
    config.schema_version = static_cast<int>(schema);

    const Value& thresholds = *root.find("thresholds");
    const Value& timing = *root.find("timing");
    const Value& limits = *root.find("limits");
    if (!thresholds.isObject()) return invalid("'thresholds' must be an object");
    if (!timing.isObject()) return invalid("'timing' must be an object");
    if (!limits.isObject()) return invalid("'limits' must be an object");

    error = checkKeys(thresholds, "thresholds.",
                      {"battery_min_v", "battery_recover_v", "temp_max_c", "temp_max_recover_c",
                       "temp_min_c", "temp_min_recover_c", "attitude_max_deg",
                       "attitude_recover_deg"});
    if (!error.empty()) return invalid(error);
    SafetyThresholds& t = config.thresholds;
    const std::string tp = "thresholds.";
    if (!readNumber(thresholds, "battery_min_v", t.battery_min_v, error, tp) ||
        !readNumber(thresholds, "battery_recover_v", t.battery_recover_v, error, tp) ||
        !readNumber(thresholds, "temp_max_c", t.temp_max_c, error, tp) ||
        !readNumber(thresholds, "temp_max_recover_c", t.temp_max_recover_c, error, tp) ||
        !readNumber(thresholds, "temp_min_c", t.temp_min_c, error, tp) ||
        !readNumber(thresholds, "temp_min_recover_c", t.temp_min_recover_c, error, tp) ||
        !readNumber(thresholds, "attitude_max_deg", t.attitude_max_deg, error, tp) ||
        !readNumber(thresholds, "attitude_recover_deg", t.attitude_recover_deg, error, tp)) {
        return invalid(error);
    }
    error = validateThresholds(t);
    if (!error.empty()) return invalid(error);

    error = checkKeys(timing, "timing.", {"telemetry_timeout_s", "comm_timeout_s", "max_tick_s"});
    if (!error.empty()) return invalid(error);
    const std::string mp = "timing.";
    if (!readInteger(timing, "telemetry_timeout_s", 1, kMaxTimingValueS,
                     config.timing.telemetry_timeout_s, error, mp) ||
        !readInteger(timing, "comm_timeout_s", 1, kMaxTimingValueS, config.timing.comm_timeout_s,
                     error, mp) ||
        !readInteger(timing, "max_tick_s", 1, kMaxTimingValueS, config.timing.max_tick_s, error,
                     mp)) {
        return invalid(error);
    }

    error = checkKeys(limits, "limits.", {"max_command_length"});
    if (!error.empty()) return invalid(error);
    std::int64_t maxLength = 0;
    if (!readInteger(limits, "max_command_length",
                     static_cast<std::int64_t>(kMinCommandLengthLimit),
                     static_cast<std::int64_t>(kMaxCommandLengthLimit), maxLength, error,
                     "limits.")) {
        return invalid(error);
    }
    config.max_command_length = static_cast<std::size_t>(maxLength);

    return Result<SimulationConfig>::ok(config);
}

Result<SimulationConfig> loadConfigFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Result<SimulationConfig>::fail("CONFIG_NOT_FOUND",
                                              "cannot open configuration file '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        return Result<SimulationConfig>::fail("CONFIG_NOT_FOUND",
                                              "error while reading configuration file '" + path +
                                                  "'");
    }
    return parseConfig(buffer.str());
}

}  // namespace cubesat

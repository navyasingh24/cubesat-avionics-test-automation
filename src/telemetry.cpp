#include "cubesat/telemetry.hpp"

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

namespace cubesat {

std::string_view toString(CommStatus status) {
    return status == CommStatus::Up ? "UP" : "DOWN";
}

std::string_view toString(SensorStatus status) {
    return status == SensorStatus::Valid ? "VALID" : "INVALID";
}

json::Value TelemetryFrame::toJson() const {
    json::Value out = json::Value::object();
    out.set("seq", seq);
    out.set("battery_v", battery_v);
    out.set("temp_c", temp_c);
    out.set("attitude_deg", attitude_deg);
    out.set("comm", std::string(toString(comm)));
    out.set("sensors", std::string(toString(sensors)));
    out.set("received_at_s", received_at_s);
    return out;
}

bool parseStrictDouble(std::string_view text, double& out) {
    // Grammar: [+-]? digits [. digits]? ([eE] [+-]? digits)?
    std::size_t i = 0;
    const auto isDigit = [&](std::size_t idx) {
        return idx < text.size() && text[idx] >= '0' && text[idx] <= '9';
    };
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
    if (!isDigit(i)) return false;
    while (isDigit(i)) ++i;
    if (i < text.size() && text[i] == '.') {
        ++i;
        if (!isDigit(i)) return false;
        while (isDigit(i)) ++i;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        if (!isDigit(i)) return false;
        while (isDigit(i)) ++i;
    }
    if (i != text.size()) return false;

    const std::string token(text);
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parseStrictUnsigned(std::string_view text, std::uint64_t max_value, std::uint64_t& out) {
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (max_value - digit) / 10) return false;  // would exceed max_value
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

namespace {

struct NumericField {
    double TelemetryFrame::*member;
    double min;
    double max;
    const char* unit;
};

const std::map<std::string, NumericField>& numericFields() {
    static const std::map<std::string, NumericField> fields = {
        {"battery_v",
         {&TelemetryFrame::battery_v, plausibility::kBatteryMinV, plausibility::kBatteryMaxV, "V"}},
        {"temp_c", {&TelemetryFrame::temp_c, plausibility::kTempMinC, plausibility::kTempMaxC, "C"}},
        {"attitude_deg",
         {&TelemetryFrame::attitude_deg, plausibility::kAttitudeMinDeg,
          plausibility::kAttitudeMaxDeg, "deg"}},
    };
    return fields;
}

constexpr const char* kRequiredFields[] = {"seq",    "battery_v", "temp_c",
                                           "attitude_deg", "comm", "sensors"};

std::string formatRange(double min, double max, const char* unit) {
    json::Value lo(min);
    json::Value hi(max);
    return "[" + lo.dump() + ", " + hi.dump() + "] " + unit;
}

}  // namespace

Result<TelemetryFrame> parseTelemetryArgs(const std::vector<std::string>& args) {
    TelemetryFrame frame;
    std::map<std::string, bool> seen;

    for (const std::string& arg : args) {
        const std::size_t eq = arg.find('=');
        if (eq == std::string::npos || eq == 0) {
            return Result<TelemetryFrame>::fail(
                "INVALID_ARGUMENT", "telemetry field '" + arg + "' is not in key=value form");
        }
        const std::string key = arg.substr(0, eq);
        const std::string value = arg.substr(eq + 1);
        if (seen.count(key) != 0) {
            return Result<TelemetryFrame>::fail("INVALID_ARGUMENT",
                                                "duplicate telemetry field '" + key + "'");
        }
        seen[key] = true;

        if (key == "seq") {
            std::uint64_t seq = 0;
            if (!parseStrictUnsigned(value, plausibility::kMaxSequence, seq)) {
                return Result<TelemetryFrame>::fail(
                    "INVALID_ARGUMENT", "seq must be a non-negative integer, got '" + value + "'");
            }
            frame.seq = seq;
        } else if (key == "comm") {
            if (value == "UP") frame.comm = CommStatus::Up;
            else if (value == "DOWN") frame.comm = CommStatus::Down;
            else {
                return Result<TelemetryFrame>::fail("INVALID_ARGUMENT",
                                                    "comm must be UP or DOWN, got '" + value + "'");
            }
        } else if (key == "sensors") {
            if (value == "VALID") frame.sensors = SensorStatus::Valid;
            else if (value == "INVALID") frame.sensors = SensorStatus::Invalid;
            else {
                return Result<TelemetryFrame>::fail(
                    "INVALID_ARGUMENT", "sensors must be VALID or INVALID, got '" + value + "'");
            }
        } else {
            const auto& fields = numericFields();
            const auto it = fields.find(key);
            if (it == fields.end()) {
                return Result<TelemetryFrame>::fail("INVALID_ARGUMENT",
                                                    "unknown telemetry field '" + key + "'");
            }
            double number = 0.0;
            if (!parseStrictDouble(value, number)) {
                return Result<TelemetryFrame>::fail(
                    "INVALID_ARGUMENT",
                    key + " must be a finite decimal number, got '" + value + "'");
            }
            const NumericField& field = it->second;
            if (number < field.min || number > field.max) {
                return Result<TelemetryFrame>::fail(
                    "OUT_OF_RANGE", key + "=" + value + " is outside the plausible range " +
                                        formatRange(field.min, field.max, field.unit));
            }
            frame.*(field.member) = number;
        }
    }

    for (const char* required : kRequiredFields) {
        if (seen.count(required) == 0) {
            return Result<TelemetryFrame>::fail(
                "MISSING_FIELD", std::string("telemetry field '") + required + "' is missing");
        }
    }
    return Result<TelemetryFrame>::ok(frame);
}

}  // namespace cubesat

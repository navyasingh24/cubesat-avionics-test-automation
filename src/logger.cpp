#include "cubesat/logger.hpp"

namespace cubesat {

std::string_view toString(Severity severity) {
    switch (severity) {
        case Severity::Info: return "INFO";
        case Severity::Warning: return "WARNING";
        case Severity::Error: return "ERROR";
        case Severity::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

void Logger::log(Severity severity, std::string_view component, std::string_view event,
                 std::string_view message, Mode mode, std::int64_t sim_time_s,
                 json::Value details) {
    json::Value entry = json::Value::object();
    entry.set("log_seq", next_seq_++);
    entry.set("sim_time_s", sim_time_s);
    entry.set("severity", std::string(toString(severity)));
    entry.set("component", std::string(component));
    entry.set("event", std::string(event));
    entry.set("mode", std::string(toString(mode)));
    entry.set("message", std::string(message));
    entry.set("details", std::move(details));
    out_ << entry.dump() << '\n';
    out_.flush();
}

}  // namespace cubesat

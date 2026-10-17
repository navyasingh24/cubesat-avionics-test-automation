#include "cubesat/cli.hpp"

#include <fstream>
#include <optional>
#include <string_view>

#include "cubesat/command_processor.hpp"
#include "cubesat/config.hpp"
#include "cubesat/flight_computer.hpp"
#include "cubesat/logger.hpp"

#ifndef CUBESAT_VERSION
#define CUBESAT_VERSION "0.0.0-dev"
#endif

namespace cubesat::cli {

namespace {

constexpr std::string_view kUsage =
    "Usage:\n"
    "  cubesat_sim --config <file> [--script <file>|-] [--log-file <file>]\n"
    "  cubesat_sim --validate-config <file>\n"
    "  cubesat_sim --help | --version\n"
    "\n"
    "Runs a simulated CubeSat flight computer (educational software-in-the-loop\n"
    "simulator, NOT flight software). Commands are read one per line from the\n"
    "script (default '-' = stdin). Each command produces exactly one JSON object\n"
    "on stdout; diagnostic logs are JSON Lines on stderr (or --log-file).\n"
    "Blank lines and lines starting with '#' are ignored. Send HELP for commands.\n"
    "\n"
    "Exit codes: 0 ok, 2 usage error, 3 configuration error,\n"
    "            4 script/log file error, 5 internal error.\n";

struct Options {
    std::optional<std::string> config;
    std::string script = "-";
    std::optional<std::string> log_file;
    std::optional<std::string> validate_config;
    bool help = false;
    bool version = false;
};

void printError(std::ostream& out, std::string_view code, const std::string& message) {
    json::Value error = json::Value::object();
    error.set("code", std::string(code));
    error.set("message", message);
    json::Value response = json::Value::object();
    response.set("status", "ERROR");
    response.set("error", std::move(error));
    out << response.dump() << '\n';
}

// Returns an error message, or an empty string on success.
std::string parseOptions(const std::vector<std::string>& args, Options& options) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto needValue = [&](std::string& target) -> bool {
            if (i + 1 >= args.size()) return false;
            target = args[++i];
            return true;
        };
        std::string value;
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--version") {
            options.version = true;
        } else if (arg == "--config") {
            if (!needValue(value)) return "--config requires a file path";
            options.config = value;
        } else if (arg == "--script") {
            if (!needValue(value)) return "--script requires a file path or '-'";
            options.script = value;
        } else if (arg == "--log-file") {
            if (!needValue(value)) return "--log-file requires a file path";
            options.log_file = value;
        } else if (arg == "--validate-config") {
            if (!needValue(value)) return "--validate-config requires a file path";
            options.validate_config = value;
        } else {
            return "unknown option '" + arg + "'";
        }
    }
    if (options.help || options.version) return {};
    if (options.validate_config && options.config) {
        return "--validate-config cannot be combined with --config";
    }
    if (!options.validate_config && !options.config) return "--config <file> is required";
    return {};
}

int runSession(const SimulationConfig& config, std::istream& script, std::ostream& out,
               std::ostream& logStream) {
    Logger logger(logStream);
    FlightComputer computer(config, logger);
    CommandProcessor processor(computer, config.max_command_length);

    std::string line;
    while (std::getline(script, line)) {
        if (const std::optional<json::Value> response = processor.processLine(line)) {
            out << response->dump() << '\n';
            out.flush();
        }
    }

    json::Value details = computer.status();
    computer.logEvent(Severity::Info, "Simulator", "SESSION_END",
                      "End of command script reached", std::move(details));
    return kExitOk;
}

}  // namespace

int run(const std::vector<std::string>& args, std::istream& stdin_stream, std::ostream& out,
        std::ostream& err) {
    Options options;
    const std::string usageError = parseOptions(args, options);
    if (!usageError.empty()) {
        err << "cubesat_sim: " << usageError << "\n\n" << kUsage;
        return kExitUsage;
    }
    if (options.help) {
        out << kUsage;
        return kExitOk;
    }
    if (options.version) {
        out << "cubesat_sim " << CUBESAT_VERSION << '\n';
        return kExitOk;
    }

    const std::string configPath = options.validate_config ? *options.validate_config
                                                           : *options.config;
    const Result<SimulationConfig> config = loadConfigFile(configPath);
    if (!config.isOk()) {
        printError(out, config.error().code, config.error().message);
        err << "cubesat_sim: " << config.error().message << '\n';
        return kExitConfigError;
    }
    if (options.validate_config) {
        json::Value response = json::Value::object();
        response.set("status", "OK");
        response.set("config", config.value().toJson());
        out << response.dump() << '\n';
        return kExitOk;
    }

    std::ifstream scriptFile;
    std::istream* script = &stdin_stream;
    if (options.script != "-") {
        scriptFile.open(options.script, std::ios::binary);
        if (!scriptFile) {
            const std::string message = "cannot open script file '" + options.script + "'";
            printError(out, "INPUT_NOT_FOUND", message);
            err << "cubesat_sim: " << message << '\n';
            return kExitInputError;
        }
        script = &scriptFile;
    }

    std::ofstream logFile;
    std::ostream* logStream = &err;
    if (options.log_file) {
        logFile.open(*options.log_file, std::ios::binary | std::ios::trunc);
        if (!logFile) {
            const std::string message = "cannot open log file '" + *options.log_file + "'";
            printError(out, "LOG_FILE_ERROR", message);
            err << "cubesat_sim: " << message << '\n';
            return kExitInputError;
        }
        logStream = &logFile;
    }

    return runSession(config.value(), *script, out, *logStream);
}

}  // namespace cubesat::cli

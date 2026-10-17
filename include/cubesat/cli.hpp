// Command-line front end. Kept separate from main() so it can be exercised
// with in-memory streams by the C++ unit tests.
#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace cubesat::cli {

// Process exit codes. Command rejections are NOT process failures: they are
// reported in-band in the JSON responses and the session continues.
enum ExitCode : int {
    kExitOk = 0,             // session ran to completion (or --help/--version)
    kExitUsage = 2,          // invalid command-line invocation
    kExitConfigError = 3,    // configuration missing, unreadable or invalid
    kExitInputError = 4,     // script or log file could not be opened
    kExitInternalError = 5,  // unexpected exception (a simulator defect)
};

// `stdin_stream` is used when the script is "-" (the default).
int run(const std::vector<std::string>& args, std::istream& stdin_stream, std::ostream& out,
        std::ostream& err);

}  // namespace cubesat::cli

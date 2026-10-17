// Entry point for the cubesat_sim command-line simulator.
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "cubesat/cli.hpp"

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        return cubesat::cli::run(args, std::cin, std::cout, std::cerr);
    } catch (const std::exception& ex) {
        std::cerr << "cubesat_sim: internal error: " << ex.what() << '\n';
        return cubesat::cli::kExitInternalError;
    } catch (...) {
        std::cerr << "cubesat_sim: internal error: unknown exception\n";
        return cubesat::cli::kExitInternalError;
    }
}

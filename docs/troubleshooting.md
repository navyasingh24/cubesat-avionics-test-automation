# Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `'cmake' not found` | CMake not installed | macOS: `brew install cmake`; Ubuntu: `sudo apt-get install cmake` |
| `No C++ compiler found` | Missing toolchain | macOS: `xcode-select --install`; Ubuntu: `sudo apt-get install build-essential` |
| `pytest is not available for python3` | pytest not installed | `scripts/setup_macos.sh --create-venv` (or `setup_linux.sh`). The scripts automatically use `.venv` |
| `python3 -m venv` fails on Ubuntu | `python3-venv` package missing | `sudo apt-get install python3-venv` |
| `Simulator not built at .../build/cubesat_sim` | `run_tests.sh` runs on an existing build | `scripts/build.sh` first |
| `build is not configured` | `build.sh compile` without configure | `scripts/build.sh` (or `scripts/build.sh configure`) |
| CMake error about a different source directory / generator | `build/` was created by another CMake or on another machine | `scripts/build.sh --clean` |
| Build fails only in CI with a warning turned error | CI sets `CUBESAT_WERROR=ON` | Fix the warning. Reproduce with `CUBESAT_WERROR=ON scripts/build.sh` |
| Sanitizer build fails to link with Clang on Linux (`libclang_rt.asan... not found`) | Clang sanitizer runtime package missing | Use GCC (`CXX=g++`) or install the `libclang-rt-<ver>-dev` package |
| `CUBESAT_SIM_BIN=... is not an executable file` | Variable points to a missing binary | `unset CUBESAT_SIM_BIN` to let pytest build, or point it at `build/cubesat_sim` |
| A golden regression test fails after an intended change | Behaviour changed on purpose | Run `python3 tools/update_golden.py`, **review `git diff tests/golden`**, commit |
| A golden regression test fails unexpectedly | Behaviour changed by accident | Look at the first differing line in the failure message; fix the code |
| `unknown requirement IDs` at collection | Typo in `@pytest.mark.req(...)` | Use IDs from `tests/requirements.json` |
| `traceability` stage fails with NOT COVERED | New requirement without a test | Add a test with `@pytest.mark.req("REQ-...")` |
| `verify_defect_detection.sh` says the defect was NOT detected | Boundary tests were weakened or removed | Restore `test_battery_boundary`. This check guards test strength |
| Interactive simulator seems to hang | It is waiting for commands on stdin | Type commands, one per line; press Ctrl-D to end the session |
| Simulator exits 3 immediately | Invalid configuration | `./build/cubesat_sim --validate-config <file>` prints the exact problem |
| JSON parse errors when piping output | stderr mixed into stdout | Keep stderr separate (`2>log.jsonl`) or use `--log-file` |
| Jenkins: `sh: scripts/build.sh: Permission denied` | Executable bit lost (e.g. ZIP upload) | The pipeline runs `chmod +x scripts/*.sh`. Locally: `chmod +x scripts/*.sh tools/*.py` |
| Jenkins: no test results published | Earlier stage failed before tests | Check the first red stage. `allowEmptyResults: false` flags this on purpose |
| macOS: `scripts/...: bad interpreter` | Windows line endings after editing | `sed -i '' 's/\r$//' scripts/*.sh` |

## Reproducing a failing test by hand

1. Copy the commands from the test (or from the scenario JSON `setup` +
   `inject`) into `repro.txt`, one per line.
2. Run
   ```bash
   ./build/cubesat_sim --config config/simulation_config.json --script repro.txt --log-file repro.log.jsonl \
       | python3 tools/format_session.py
   ```
3. Inspect `repro.log.jsonl` for `SAFETY_VIOLATION`, `MODE_CHANGE`,
   `COMMAND_REJECTED` and other events.

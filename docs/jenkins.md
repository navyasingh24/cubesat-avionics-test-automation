# Jenkins Setup

> **Verification status:** the `Jenkinsfile` has **not yet been executed by a
> real Jenkins instance**. Every `sh` step it runs was executed in order in a
> fresh clone on Ubuntu 24.04 and passed (see the README "Verified results").
> The first real Jenkins run is still to be done; follow the steps below and
> treat any problem as a normal CI bring-up issue.

This job is completely separate from any other Jenkins job you have (for
example a Smart Campus Complaint Portal pipeline). It needs **no
credentials**, no cloud account and no hardware. Do not reuse or edit
another project's job, credentials or workspace.

## Controller vs agent (beginner explanation)

* The **controller** is the Jenkins server: web UI, job configuration,
  scheduling, storing results.
* An **agent** (build node) is the machine or container where the pipeline
  steps actually run: compiling C++, running pytest.
* Jenkins can run builds on the controller itself (the "built-in node"),
  which is fine for a personal setup. In teams, builds run on separate
  agents so they cannot harm the controller and so each project gets the
  exact toolchain it needs.
* **Why a Linux agent helps consistency:** the agent's compiler, CMake and
  Python versions decide what is tested. A pinned Linux container
  (`ci/Dockerfile`) gives the same toolchain on every run and on every
  developer's machine, which is closer to how embedded teams build firmware
  in controlled environments.

## Required plugins

| Plugin | Used for |
|--------|----------|
| Pipeline (`workflow-aggregator`) | Declarative `pipeline { }` syntax, `catchError`, `sh` |
| Git (`git`) | `checkout scm` from GitHub |
| JUnit (`junit`) | `junit 'reports/*.xml'` test results and trends |
| *Optional:* Docker Pipeline (`docker-workflow`) | Only if you switch to `agent { dockerfile ... }` |

`archiveArtifacts`, `buildDiscarder`, `timeout` and `disableConcurrentBuilds`
are part of core Jenkins and the Pipeline plugin.

## Required tools on the agent

`bash`, `git`, CMake >= 3.16, a C++17 compiler (GCC >= 9 or Clang/Apple Clang),
Python >= 3.9 with the `venv` module, and network access to PyPI the first
time (to install pytest into a workspace-local `.venv`) unless pytest is
already installed. The "Environment & tool checks" stage stops with a clear
message if anything is missing.

## Option A: Jenkins running in Docker on your Mac (common personal setup)

If your Jenkins controller runs in a Docker container (for example the
official `jenkins/jenkins:lts` image), the build runs **inside that Linux
container**, not on macOS. The official image does not include a C++
compiler or CMake, so install them into a derived image:

```dockerfile
# jenkins-with-cpp.Dockerfile (example; build it yourself)
FROM jenkins/jenkins:lts-jdk17
USER root
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*
USER jenkins
```

```bash
docker build -t jenkins-cpp -f jenkins-with-cpp.Dockerfile .
docker run -d --name jenkins-cubesat -p 8081:8080 -v jenkins_cubesat_home:/var/jenkins_home jenkins-cpp
```

This uses a **new container name, port and volume** so it cannot interfere
with an existing Jenkins container. If you prefer to reuse your existing
controller, only add a new job; do not change other jobs.

## Option B: Jenkins installed natively on macOS

Install the tools on the Mac (`xcode-select --install`, `brew install cmake
python`). The pipeline detects macOS (`uname -s`) and uses
`scripts/setup_macos.sh`.

## Option C: Docker-based Linux agent (most reproducible)

On an agent with Docker and the Docker Pipeline plugin, change the agent line
in `Jenkinsfile`:

```groovy
agent { dockerfile { filename 'ci/Dockerfile' } }
```

The image already contains GCC, CMake, Python and pytest, so the pipeline
skips the virtualenv step.

## Creating the job

1. Push this repository to GitHub (public repositories need no credentials).
2. Jenkins → **New Item** → name `cubesat-avionics-test-automation` →
   **Pipeline** → OK.
3. **Pipeline** section → Definition: **Pipeline script from SCM** →
   SCM: **Git** → Repository URL: your GitHub HTTPS URL →
   Branch: `*/main` → Script Path: `Jenkinsfile`.
4. (Optional) **Build Triggers** → *Poll SCM* `H/15 * * * *`, or a GitHub
   webhook if your Jenkins is reachable from the internet (usually not for a
   laptop setup).
5. Save → **Build Now**.

## What the pipeline does

| Stage | Command | On failure |
|-------|---------|------------|
| Checkout | `checkout scm` | stops |
| Environment & tool checks | `scripts/setup_{macos,linux}.sh [--create-venv]`, `scripts/clean.sh` | stops |
| Configure (CMake) | `scripts/build.sh configure` | stops |
| Compile | `scripts/build.sh compile` (`-Werror`) | **stops: no tests run on a broken build** |
| Unit tests (C++) | `scripts/run_tests.sh unit` | stage + build FAILURE, later stages still run |
| System verification tests | `scripts/run_tests.sh system` | stage + build FAILURE, continue |
| Fault injection & regression | `scripts/run_tests.sh fault`, `... regression` | stage + build FAILURE, continue |
| Defect-detection self-check | `scripts/verify_defect_detection.sh` | stage + build FAILURE, continue |
| Traceability report | `scripts/run_tests.sh report` | stage + build FAILURE |
| post / always | `junit 'reports/*.xml'`, `archiveArtifacts 'reports/**'` | results are published even after failures |
| post / cleanup | `scripts/clean.sh` | removes build dirs and reports in this workspace only |

Test stages use `catchError(buildResult: 'FAILURE', stageResult: 'FAILURE')`.
Every suite therefore runs and reports, and any failure still turns the
stage red and the build **FAILURE**. Nothing is swallowed. The `junit` step
uses `allowEmptyResults: false`, so a run that produced no test results is
visible rather than silently green.

## Checking the result

* **Build page → Test Result:** per-test results, including the requirement
  IDs recorded as properties.
* **Build Artifacts → reports/:** `fault_injection_report.md`,
  `traceability.md`, `seeded_defect_run.txt`, all JUnit XML.
* A deliberately failing change (for example editing a threshold comparison)
  should turn the "System verification tests" stage red. That is the
  expected behaviour.

## Running the pipeline steps locally first

Before configuring Jenkins, run the same commands locally:

```bash
scripts/setup_macos.sh --create-venv     # or scripts/setup_linux.sh --create-venv
CUBESAT_WERROR=ON scripts/build.sh
scripts/run_tests.sh all
scripts/verify_defect_detection.sh
```

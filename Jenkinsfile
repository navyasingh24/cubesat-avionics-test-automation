// Declarative Jenkins pipeline for the CubeSat Avionics Test Automation Simulator.
//
// Every stage calls a script from scripts/, the same scripts used locally and
// by GitHub Actions, so there is one test procedure, not three.
//
// Required plugins: Pipeline (workflow-aggregator), Git, JUnit.
// Required agent tools: bash, git, CMake >= 3.16 (3.21+ for C++ JUnit XML),
// a C++17 compiler, Python >= 3.9 (venv module) and network access to PyPI
// on the first run unless pytest is already installed.
// No credentials, cloud services or hardware are needed. See docs/jenkins.md.

pipeline {
    // Runs on any agent that has the tools above. To build inside a
    // reproducible Linux container instead (Docker Pipeline plugin + Docker
    // on the agent), replace this with:
    //   agent { dockerfile { filename 'ci/Dockerfile' } }
    agent any

    options {
        buildDiscarder(logRotator(numToKeepStr: '20'))
        timeout(time: 30, unit: 'MINUTES')
        disableConcurrentBuilds()
    }

    environment {
        BUILD_DIR = "${WORKSPACE}/build"
        REPORT_DIR = "${WORKSPACE}/reports"
        CUBESAT_WERROR = 'ON'            // compiler warnings fail the build in CI
        PYTHONDONTWRITEBYTECODE = '1'
    }

    stages {
        stage('Checkout') {
            steps {
                checkout scm
                sh 'git log -1 --oneline'
            }
        }

        stage('Environment & tool checks') {
            steps {
                sh '''#!/usr/bin/env bash
                    set -euo pipefail
                    chmod +x scripts/*.sh
                    case "$(uname -s)" in
                        Darwin) setup=scripts/setup_macos.sh ;;
                        *)      setup=scripts/setup_linux.sh ;;
                    esac
                    # Reuse an existing pytest if the agent image provides one;
                    # otherwise create a workspace-local virtualenv.
                    if python3 -c 'import pytest' 2>/dev/null; then
                        "$setup"
                    else
                        "$setup" --create-venv
                    fi
                    rm -rf "$REPORT_DIR" && mkdir -p "$REPORT_DIR"
                '''
            }
        }

        stage('Configure (CMake)') {
            steps {
                sh 'scripts/build.sh configure'
            }
        }

        stage('Compile') {
            // No catchError: a compilation failure stops the pipeline here.
            steps {
                sh 'scripts/build.sh compile'
            }
        }

        // Test stages use catchError so that EVERY suite runs and reports even
        // when an earlier suite fails. A failure still marks the stage and the
        // whole build as FAILURE; nothing is swallowed.
        stage('Unit tests (C++)') {
            steps {
                catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
                    sh 'scripts/run_tests.sh unit'
                }
            }
        }

        stage('System verification tests') {
            steps {
                catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
                    sh 'scripts/run_tests.sh system'
                }
            }
        }

        stage('Fault injection & regression') {
            steps {
                catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
                    sh 'scripts/run_tests.sh fault'
                }
                catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
                    sh 'scripts/run_tests.sh regression'
                }
            }
        }

        stage('Defect-detection self-check') {
            // Builds a deliberately broken simulator in build-defect/ and
            // passes only if the tests FAIL against it.
            steps {
                catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
                    sh 'scripts/verify_defect_detection.sh'
                }
            }
        }

        stage('Traceability report') {
            steps {
                catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
                    sh 'scripts/run_tests.sh report'
                }
            }
        }
    }

    post {
        always {
            // Published even when tests failed. allowEmptyResults: false makes
            // a run that produced no results visible instead of silently green.
            junit testResults: 'reports/*.xml', allowEmptyResults: false
            archiveArtifacts artifacts: 'reports/**', allowEmptyArchive: true, fingerprint: true
        }
        success {
            echo 'All build, verification and defect-detection stages passed.'
        }
        unstable {
            echo 'Some tests failed: see the Test Result trend and reports/ artifacts.'
        }
        failure {
            echo 'Build FAILED: see the failing stage log and archived reports/.'
        }
        cleanup {
            // Removes build directories, Python caches and reports inside this
            // workspace only (reports are archived above first).
            sh 'scripts/clean.sh'
        }
    }
}

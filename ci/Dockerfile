# OPTIONAL reproducible Linux build/test environment.
#
# Not required for local development. Useful as a Jenkins Docker agent
# (agent { dockerfile { filename 'ci/Dockerfile' } }) or to check Linux
# compatibility from a Mac:
#   docker build -t cubesat-sil -f ci/Dockerfile .
#   docker run --rm -v "$PWD":/work -w /work cubesat-sil \
#       bash -c 'scripts/build.sh && scripts/run_tests.sh'
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PYTHONDONTWRITEBYTECODE=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential cmake git python3 python3-venv python3-pip ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# pytest lives in a virtualenv inside the image (no system-wide pip install).
COPY requirements.txt /tmp/requirements.txt
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir -r /tmp/requirements.txt
ENV PATH="/opt/venv/bin:${PATH}"

# Run as an unprivileged user.
RUN useradd --create-home --uid 1001 builder
USER builder
WORKDIR /work

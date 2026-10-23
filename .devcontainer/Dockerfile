# Toolchain + bench tier + GitHub Actions runner, one container per project.
FROM espressif/idf:v6.0.2

ARG RUNNER_VERSION=2.338.0
# Harness commit the TestbenchDriver is taken from (pinned, not a branch).
ARG HARNESS_COMMIT=db6a093

RUN apt-get update \
 && apt-get install -y --no-install-recommends curl jq ca-certificates gcc libc6-dev \
    python3-venv \
 && rm -rf /var/lib/apt/lists/*

# Bench-tier tools live in their own venv: the IDF Python env is only
# activated by the image's entrypoint, so `pip` is not on PATH at build time.
RUN python3 -m venv /opt/venv \
 && /opt/venv/bin/pip install --no-cache-dir 'pytest==8.*' 'ruff==0.16.1' 'pyyaml==6.*' pyserial
ENV PATH=/opt/venv/bin:$PATH

RUN mkdir -p /opt/testbench \
 && curl -fsSL -o /opt/testbench/testbench_driver.py \
    "https://raw.githubusercontent.com/SensorsIot/Embedded-AI-Harness/${HARNESS_COMMIT}/pytest/testbench_driver.py"
ENV PYTHONPATH=/opt/testbench
ENV TESTBENCH_URL=http://192.168.2.230:8080

RUN useradd -m runner \
 && mkdir -p /opt/actions-runner \
 && curl -fsSL "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-x64-${RUNNER_VERSION}.tar.gz" \
    | tar -xz -C /opt/actions-runner \
 && /opt/actions-runner/bin/installdependencies.sh \
 && chown -R runner:runner /opt/actions-runner

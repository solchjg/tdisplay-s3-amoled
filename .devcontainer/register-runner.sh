#!/usr/bin/env bash
# Registers an ephemeral, per-repo self-hosted runner labelled `testbench`
# and keeps it in a restart loop (an ephemeral runner exits after one job).
#
# Registering is a standing grant: workflow code from this repo will then
# physically drive the testbench. Only the repo owner runs this.
#
# Needs: GH_TOKEN with admin rights on the repo (used once per loop to mint
# a short-lived registration token).
set -euo pipefail

REPO="solchjg/tdisplay-s3-amoled"
cd /opt/actions-runner

while true; do
  TOKEN=$(curl -fsS -X POST \
    -H "Authorization: Bearer ${GH_TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/${REPO}/actions/runners/registration-token" | jq -r .token)
  su runner -c "./config.sh --unattended --ephemeral --replace \
    --url https://github.com/${REPO} --token ${TOKEN} \
    --name tdisplay-s3-amoled-testbench --labels testbench"
  su runner -c "./run.sh" || true
  sleep 5
done

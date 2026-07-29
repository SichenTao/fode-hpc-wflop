#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMAL_SSH_TARGET="${WAFFLE_SSH_TARGET:-waffle}" \
FORMAL_REMOTE_DIR="${WAFFLE_REMOTE_DIR:-fode-hpc-wflop-formal}" \
FORMAL_EXPECTED_HOST="waffle" \
FORMAL_SUITE_ID="waffle_campaign_suite_v1" \
FORMAL_RUNNER="scripts/run_all_waffle_formal_campaigns.sh" \
FORMAL_LAUNCHER="scripts/launch_waffle_formal_suite.sh" \
  exec "${repo_root}/scripts/deploy_and_launch_formal_suite.sh"

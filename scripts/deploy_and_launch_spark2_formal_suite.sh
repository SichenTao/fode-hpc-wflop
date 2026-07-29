#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMAL_SSH_TARGET="${SPARK2_SSH_TARGET:-spark2}" \
FORMAL_REMOTE_DIR="${SPARK2_REMOTE_DIR:-fode-hpc-wflop-spark2-formal}" \
FORMAL_EXPECTED_HOST="spark-9ab3" \
FORMAL_SUITE_ID="spark2_campaign_suite_v1" \
FORMAL_RUNNER="scripts/run_all_spark2_formal_campaigns.sh" \
FORMAL_LAUNCHER="scripts/launch_spark2_formal_suite.sh" \
  exec "${repo_root}/scripts/deploy_and_launch_formal_suite.sh"

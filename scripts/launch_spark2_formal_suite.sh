#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMAL_EXPECTED_HOST="spark-9ab3" \
FORMAL_SUITE_ID="spark2_campaign_suite_v1" \
FORMAL_RUNNER="scripts/run_all_spark2_formal_campaigns.sh" \
  exec "${repo_root}/scripts/launch_formal_suite.sh"

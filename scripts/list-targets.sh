#!/usr/bin/env bash
# Print the test-target registry in a compact form.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TSV="$ROOT/tests/targets.tsv"

[[ -f "$TSV" ]] || { echo "list-targets: missing $TSV" >&2; exit 2; }

printf '%-30s %-16s %-8s %s\n' "TARGET" "STATUS" "OWNER" "COMMAND"
printf '%-30s %-16s %-8s %s\n' "------------------------------" "----------------" "--------" "-------"
awk -F'\t' 'NR > 1 { printf "%-30s %-16s %-8s %s\n", $1, $2, $3, $5 }' "$TSV"

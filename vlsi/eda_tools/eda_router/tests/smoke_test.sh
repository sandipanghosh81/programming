#!/usr/bin/env bash
set -euo pipefail

target="${1:-./build/bin/routing_genetic_astar}"
output="$($target)"

printf '%s\n' "$output"

grep -q "Routing project scaffold ready." <<< "$output"
grep -q "Hybrid evolutionary router" <<< "$output"

echo "smoke test passed"

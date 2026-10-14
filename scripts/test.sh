#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/stopwatch-tests.XXXXXX")"
trap 'rm -rf "$test_dir"' EXIT
for script in scripts/*.sh; do bash -n "$script"; done
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I bloom tests/bloom_test.cpp -o "$test_dir/bloom_test"
"$test_dir/bloom_test"
python3 -B tests/bloom_relay_test.py
for app in bloom pulse_station reactions metronome cat_toy; do
  cmp flight_radar/partitions.csv "$app/partitions.csv"
done

#!/bin/sh
# Runs every spike test. Usage: spike/tests/run_all.sh  (from the repository root)
set -e
cd "$(dirname "$0")/../.."
cmake --build spike/build -j 12 > /dev/null
ctest --test-dir spike/build --output-on-failure | tail -3
for t in spike/tests/test_*.py spike/tests/check_rules.py; do
	[ -f "$t" ] || continue
	echo "== $t"
	python3 "$t" 2>&1 | tail -3
done

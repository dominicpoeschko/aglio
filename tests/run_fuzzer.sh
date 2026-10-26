#!/usr/bin/env bash
# Runs a libFuzzer binary with its output in a log file instead of the CI log: a pass prints the summary (seed,
# INITED/DONE coverage, run count, -print_final_stats), a failure the end of the log, where the sanitizer report, the
# crashing input and its file name are.
#
#   tests/run_fuzzer.sh <log file> <fuzzer> [libFuzzer arguments...]
#
# The exit code is the fuzzer's. FUZZ_LOG_TAIL sets how many lines a failure prints (default 200).
set -uo pipefail

if [ $# -lt 2 ]; then
    echo "usage: $0 <log file> <fuzzer> [arguments...]" >&2
    exit 2
fi

log=$1
shift
mkdir -p "$(dirname "$log")"

"$@" >"$log" 2>&1
rc=$?

if [ "$rc" -eq 0 ]; then
    grep -E '^(INFO: Seed:|INFO: +[0-9]+ files found|#[0-9]+[[:space:]]+(INITED|DONE)|Done [0-9]+ runs|stat::)' "$log"
else
    echo "$1 failed (exit $rc); the last ${FUZZ_LOG_TAIL:-200} lines of $log:"
    tail -n "${FUZZ_LOG_TAIL:-200}" "$log"
fi
exit "$rc"

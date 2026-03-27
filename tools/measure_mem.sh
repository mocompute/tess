#!/bin/bash
# measure_mem.sh — Measure peak memory consumption of tess compilation for each test file.
#
# Usage:
#   ./tools/measure_mem.sh [options] [glob_pattern]
#
# Options:
#   -j N          Run N compilations in parallel (default: 1, sequential)
#   -n N          Show only top N results (default: all)
#   -s SUITE      Test suite: pass, fail, fail_runtime, pass_optimized,
#                 known_failures, known_fail_failures, or "all" (default: all)
#   -q            Quiet — suppress progress, only show results
#
# Examples:
#   ./tools/measure_mem.sh                           # all suites, sequential
#   ./tools/measure_mem.sh -s pass 'test_f_string_*' # pass suite, f-string pattern
#   ./tools/measure_mem.sh -j 4 -n 20               # parallel, top 20
#   ./tools/measure_mem.sh -s fail                   # fail suite only

set -euo pipefail

cd "$(dirname "$0")/.."

TESS=./tess
TEST_BASE=src/tess/tl/test
OUT_DIR=$(mktemp -d)
RESULTS_DIR=$(mktemp -d)

# Defaults
PARALLEL=1
TOP_N=0  # 0 = show all
SUITE="all"
PATTERN=""
QUIET=0

while getopts "j:n:s:q" opt; do
    case $opt in
        j) PARALLEL=$OPTARG ;;
        n) TOP_N=$OPTARG ;;
        s) SUITE=$OPTARG ;;
        q) QUIET=1 ;;
        *) echo "Unknown option: -$opt" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
PATTERN="${1:-}"

if [ ! -x "$TESS" ]; then
    echo "Error: tess binary not found at $TESS — run 'make -j all' first" >&2
    exit 1
fi

# Build list of test files with their suite labels
declare -a test_files=()
declare -a test_suites=()

add_tests() {
    local suite_name=$1
    local dir=$2

    if [ ! -d "$dir" ]; then
        return
    fi

    local pat="${PATTERN:-test_*}"
    local matches=( "$dir"/${pat}.tl )

    # Check glob actually matched (bash returns the literal pattern if no match)
    if [ ${#matches[@]} -eq 1 ] && [ ! -e "${matches[0]}" ]; then
        return
    fi

    for f in "${matches[@]}"; do
        test_files+=("$f")
        test_suites+=("$suite_name")
    done
}

case "$SUITE" in
    all)
        add_tests "pass"         "$TEST_BASE/pass"
        add_tests "pass_opt"     "$TEST_BASE/pass_optimized"
        add_tests "fail"         "$TEST_BASE/fail"
        add_tests "fail_rt"      "$TEST_BASE/fail_runtime"
        add_tests "known_fail"   "$TEST_BASE/known_failures"
        add_tests "known_ff"     "$TEST_BASE/known_fail_failures"
        ;;
    pass)           add_tests "pass"       "$TEST_BASE/pass" ;;
    pass_optimized) add_tests "pass_opt"   "$TEST_BASE/pass_optimized" ;;
    fail)           add_tests "fail"       "$TEST_BASE/fail" ;;
    fail_runtime)   add_tests "fail_rt"    "$TEST_BASE/fail_runtime" ;;
    known_failures) add_tests "known_fail" "$TEST_BASE/known_failures" ;;
    known_fail_failures) add_tests "known_ff" "$TEST_BASE/known_fail_failures" ;;
    *)
        echo "Unknown suite: $SUITE" >&2
        echo "Valid: pass, pass_optimized, fail, fail_runtime, known_failures, known_fail_failures, all" >&2
        exit 1
        ;;
esac

total=${#test_files[@]}
if [ "$total" -eq 0 ]; then
    echo "No tests found (suite=$SUITE, pattern=${PATTERN:-*})" >&2
    exit 1
fi

if [ "$QUIET" -eq 0 ]; then
    echo "Measuring peak memory for $total tests (parallelism: $PARALLEL)"
    [ -n "$PATTERN" ] && echo "Pattern: $PATTERN"
    echo "Suites: $SUITE"
    echo "---"
fi

# measure_one: compile a single test and record peak RSS.
# Args: $1=test_file $2=suite $3=index $4=result_file
measure_one() {
    local tl_file=$1
    local suite=$2
    local idx=$3
    local result_file=$4
    local name
    name=$(basename "$tl_file" .tl)
    local out_file="$OUT_DIR/$name"

    # Launch tess in background — fail tests will exit non-zero, that's expected
    "$TESS" exe -o "$out_file" "$tl_file" 2>/dev/null &
    local tess_pid=$!

    # Poll peak RSS (process tree)
    local peak_kb=0
    while kill -0 "$tess_pid" 2>/dev/null; do
        local total_kb=0
        local pids
        pids=$(pgrep -P "$tess_pid" 2>/dev/null || true)
        pids="$tess_pid $pids"

        for pid in $pids; do
            local rss
            rss=$(awk '/^VmRSS:/ { print $2 }' "/proc/$pid/status" 2>/dev/null || echo 0)
            total_kb=$((total_kb + rss))
        done

        [ "$total_kb" -gt "$peak_kb" ] && peak_kb=$total_kb
        sleep 0.05
    done

    local rc=0
    wait "$tess_pid" 2>/dev/null || rc=$?

    # Write result to file (atomic via temp+rename)
    echo "${peak_kb} ${rc} ${suite} ${name}" > "$result_file"

    if [ "$QUIET" -eq 0 ]; then
        printf "  [%d/%d] %s\n" "$idx" "$total" "$name" >&2
    fi
}

# Run measurements with controlled parallelism
running=0
for i in "${!test_files[@]}"; do
    idx=$((i + 1))
    result_file="$RESULTS_DIR/result_$(printf '%04d' "$i")"
    measure_one "${test_files[$i]}" "${test_suites[$i]}" "$idx" "$result_file" &

    running=$((running + 1))
    if [ "$running" -ge "$PARALLEL" ]; then
        wait -n 2>/dev/null || true
        running=$((running - 1))
    fi
done
wait || true

# Collect and sort results
{
    for f in "$RESULTS_DIR"/result_*; do
        [ -f "$f" ] && cat "$f"
    done
} | sort -rn -k1,1 | {
    echo ""
    echo "Results (sorted by peak memory, descending):"
    echo "======================================================================"
    printf '%10s  %8s  %-10s  %-6s  %s\n' "PEAK_KB" "PEAK_MB" "SUITE" "STATUS" "TEST"
    echo "----------------------------------------------------------------------"

    total_kb=0
    count=0
    shown=0

    while read -r peak_kb rc suite name; do
        total_kb=$((total_kb + peak_kb))
        count=$((count + 1))

        if [ "$TOP_N" -gt 0 ] && [ "$shown" -ge "$TOP_N" ]; then
            continue
        fi
        shown=$((shown + 1))

        peak_mb=$(awk "BEGIN { printf \"%.1f\", $peak_kb / 1024 }")
        if [ "$rc" -eq 0 ]; then
            status="OK"
        else
            status="rc=$rc"
        fi

        printf '%10s  %6s MB  %-10s  %-6s  %s\n' "$peak_kb" "$peak_mb" "$suite" "$status" "$name"
    done

    echo "======================================================================"
    avg_kb=$((total_kb / (count > 0 ? count : 1)))
    total_mb=$(awk "BEGIN { printf \"%.0f\", $total_kb / 1024 }")
    avg_mb=$(awk "BEGIN { printf \"%.1f\", $avg_kb / 1024 }")
    echo "Total: $count tests, ${total_mb} MB cumulative, ${avg_mb} MB average"

    if [ "$PARALLEL" -gt 1 ]; then
        parallel_est_mb=$(awk "BEGIN { printf \"%.0f\", $avg_kb * $PARALLEL / 1024 }")
        echo "Estimated concurrent usage at -j$PARALLEL: ~${parallel_est_mb} MB"
    fi
}

# Cleanup
rm -rf "$OUT_DIR" "$RESULTS_DIR"

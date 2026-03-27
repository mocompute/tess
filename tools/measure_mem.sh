#!/bin/bash
# measure_mem.sh — Measure peak memory consumption of tess test compilation and/or execution.
#
# Usage:
#   ./tools/measure_mem.sh [options] [glob_pattern]
#
# Options:
#   -j N          Run N measurements in parallel (default: 1, sequential)
#   -n N          Show only top N results (default: all)
#   -s SUITE      Test suite: pass, fail, fail_runtime, pass_optimized,
#                 known_failures, known_fail_failures, or "all" (default: all)
#   -m MODE       Measurement mode: compile, run, or both (default: compile)
#   -q            Quiet — suppress progress, only show results
#
# Examples:
#   ./tools/measure_mem.sh                              # compile, all suites
#   ./tools/measure_mem.sh -m run -s pass               # run pass tests
#   ./tools/measure_mem.sh -m both -n 20                # compile+run, top 20
#   ./tools/measure_mem.sh -m run 'test_f_string_*'     # run f-string tests

set -euo pipefail

cd "$(dirname "$0")/.."

TESS=./tess
TEST_BASE=src/tess/tl/test
OUT_DIR=$(mktemp -d)
RESULTS_DIR=$(mktemp -d)
# Find GNU time binary (not the shell builtin)
GNU_TIME=""
for candidate in /usr/bin/time /run/current-system/sw/bin/time; do
    if [ -x "$candidate" ]; then
        GNU_TIME="$candidate"
        break
    fi
done
if [ -z "$GNU_TIME" ]; then
    # Fall back to PATH lookup, skipping shell builtins
    GNU_TIME=$(type -P time 2>/dev/null || true)
fi

# Defaults
PARALLEL=1
TOP_N=0  # 0 = show all
SUITE="all"
PATTERN=""
QUIET=0
MODE="compile"

while getopts "j:n:s:m:q" opt; do
    case $opt in
        j) PARALLEL=$OPTARG ;;
        n) TOP_N=$OPTARG ;;
        s) SUITE=$OPTARG ;;
        m) MODE=$OPTARG ;;
        q) QUIET=1 ;;
        *) echo "Unknown option: -$opt" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
PATTERN="${1:-}"

case "$MODE" in
    compile|run|both) ;;
    *) echo "Unknown mode: $MODE (valid: compile, run, both)" >&2; exit 1 ;;
esac

if [ ! -x "$TESS" ]; then
    echo "Error: tess binary not found at $TESS — run 'make -j all' first" >&2
    exit 1
fi

if [ -z "$GNU_TIME" ]; then
    echo "Error: GNU time not found — install the 'time' package" >&2
    exit 1
fi

# Verify GNU time supports -v
TIME_CHECK=$("$GNU_TIME" -v true 2>&1) || true
if ! echo "$TIME_CHECK" | grep -q "Maximum resident"; then
    echo "Error: 'time' does not support -v (need GNU time, not shell builtin)" >&2
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
    echo "Measuring peak memory for $total tests (mode: $MODE, parallelism: $PARALLEL)"
    [ -n "$PATTERN" ] && echo "Pattern: $PATTERN"
    echo "Suites: $SUITE"
    echo "---"
fi

# maxrss_kb: Run a command via GNU time and extract max RSS in KB.
# Sets MAXRSS_KB and MAXRSS_RC as side effects.
maxrss_kb() {
    local time_out
    time_out=$(mktemp)
    local rc=0
    "$GNU_TIME" -v "$@" >/dev/null 2>"$time_out" || rc=$?
    MAXRSS_KB=$(awk '/Maximum resident set size/ { print $NF }' "$time_out")
    MAXRSS_KB=${MAXRSS_KB:-0}
    MAXRSS_RC=$rc
    rm -f "$time_out"
}

# measure_one: compile and/or run a single test and record peak RSS.
# Args: $1=test_file $2=suite $3=index $4=result_file
measure_one() {
    local tl_file=$1
    local suite=$2
    local idx=$3
    local result_file=$4
    local name
    name=$(basename "$tl_file" .tl)
    local out_file="$OUT_DIR/$name"

    local compile_kb=0
    local compile_rc=0
    local run_kb=0
    local run_rc=0

    # --- Compile phase ---
    if [ "$MODE" = "compile" ] || [ "$MODE" = "both" ]; then
        maxrss_kb "$TESS" exe -o "$out_file" "$tl_file"
        compile_kb=$MAXRSS_KB
        compile_rc=$MAXRSS_RC
    elif [ "$MODE" = "run" ]; then
        # Compile silently (no measurement) so we have a binary to run
        "$TESS" exe -o "$out_file" "$tl_file" 2>/dev/null || compile_rc=$?
    fi

    # --- Run phase ---
    if [ "$MODE" = "run" ] || [ "$MODE" = "both" ]; then
        if [ "$compile_rc" -eq 0 ] && [ -x "$out_file" ]; then
            maxrss_kb "$out_file"
            run_kb=$MAXRSS_KB
            run_rc=$MAXRSS_RC
        else
            run_rc=-1  # compilation failed, can't run
        fi
    fi

    # Write result: compile_kb compile_rc run_kb run_rc suite name
    echo "${compile_kb} ${compile_rc} ${run_kb} ${run_rc} ${suite} ${name}" > "$result_file"

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

# --- Format and display results ---

format_status() {
    local rc=$1
    if [ "$rc" -eq 0 ]; then
        echo "OK"
    elif [ "$rc" -eq -1 ]; then
        echo "SKIP"
    else
        echo "rc=$rc"
    fi
}

# Collect raw results into a temp file for sorting
RAW=$(mktemp)
for f in "$RESULTS_DIR"/result_*; do
    [ -f "$f" ] && cat "$f"
done > "$RAW"

echo ""

if [ "$MODE" = "compile" ]; then
    # Sort by compile_kb descending
    sort -rn -k1,1 "$RAW" | {
        echo "Results (sorted by peak memory, descending):"
        echo "======================================================================"
        printf '%10s  %8s  %-10s  %-6s  %s\n' "PEAK_KB" "PEAK_MB" "SUITE" "STATUS" "TEST"
        echo "----------------------------------------------------------------------"

        total_kb=0; count=0; shown=0
        while read -r compile_kb compile_rc run_kb run_rc suite name; do
            total_kb=$((total_kb + compile_kb))
            count=$((count + 1))
            if [ "$TOP_N" -gt 0 ] && [ "$shown" -ge "$TOP_N" ]; then continue; fi
            shown=$((shown + 1))
            peak_mb=$(awk "BEGIN { printf \"%.1f\", $compile_kb / 1024 }")
            status=$(format_status "$compile_rc")
            printf '%10s  %6s MB  %-10s  %-6s  %s\n' "$compile_kb" "$peak_mb" "$suite" "$status" "$name"
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

elif [ "$MODE" = "run" ]; then
    # Sort by run_kb descending
    sort -rn -k3,3 "$RAW" | {
        echo "Results (sorted by peak runtime memory, descending):"
        echo "======================================================================"
        printf '%10s  %8s  %-10s  %-6s  %s\n' "PEAK_KB" "PEAK_MB" "SUITE" "STATUS" "TEST"
        echo "----------------------------------------------------------------------"

        total_kb=0; count=0; shown=0
        while read -r compile_kb compile_rc run_kb run_rc suite name; do
            # Skip tests that couldn't be compiled
            if [ "$run_rc" -eq -1 ]; then continue; fi
            total_kb=$((total_kb + run_kb))
            count=$((count + 1))
            if [ "$TOP_N" -gt 0 ] && [ "$shown" -ge "$TOP_N" ]; then continue; fi
            shown=$((shown + 1))
            peak_mb=$(awk "BEGIN { printf \"%.1f\", $run_kb / 1024 }")
            status=$(format_status "$run_rc")
            printf '%10s  %6s MB  %-10s  %-6s  %s\n' "$run_kb" "$peak_mb" "$suite" "$status" "$name"
        done

        echo "======================================================================"
        if [ "$count" -gt 0 ]; then
            avg_kb=$((total_kb / count))
            total_mb=$(awk "BEGIN { printf \"%.0f\", $total_kb / 1024 }")
            avg_mb=$(awk "BEGIN { printf \"%.1f\", $avg_kb / 1024 }")
            echo "Total: $count tests, ${total_mb} MB cumulative, ${avg_mb} MB average"
        else
            echo "Total: 0 runnable tests"
        fi
    }

elif [ "$MODE" = "both" ]; then
    # Sort by total (compile + run) descending
    awk '{ print $1+$3, $0 }' "$RAW" | sort -rn -k1,1 | cut -d' ' -f2- | {
        echo "Results (sorted by total peak memory, descending):"
        echo "================================================================================================"
        printf '%10s  %8s  %10s  %8s  %-10s  %-6s  %s\n' "COMPILE" "" "RUN" "" "SUITE" "STATUS" "TEST"
        printf '%10s  %8s  %10s  %8s  %-10s  %-6s  %s\n' "KB" "MB" "KB" "MB" "" "" ""
        echo "------------------------------------------------------------------------------------------------"

        total_compile=0; total_run=0; count=0; shown=0
        while read -r compile_kb compile_rc run_kb run_rc suite name; do
            total_compile=$((total_compile + compile_kb))
            total_run=$((total_run + run_kb))
            count=$((count + 1))
            if [ "$TOP_N" -gt 0 ] && [ "$shown" -ge "$TOP_N" ]; then continue; fi
            shown=$((shown + 1))
            cmb=$(awk "BEGIN { printf \"%.1f\", $compile_kb / 1024 }")
            rmb=$(awk "BEGIN { printf \"%.1f\", $run_kb / 1024 }")
            cstatus=$(format_status "$compile_rc")
            rstatus=$(format_status "$run_rc")
            if [ "$rstatus" = "OK" ]; then
                status="$cstatus"
            else
                status="$cstatus/$rstatus"
            fi
            printf '%10s  %6s MB  %10s  %6s MB  %-10s  %-6s  %s\n' \
                "$compile_kb" "$cmb" "$run_kb" "$rmb" "$suite" "$status" "$name"
        done

        echo "================================================================================================"
        if [ "$count" -gt 0 ]; then
            avg_c=$((total_compile / count))
            avg_r=$((total_run / count))
            tc_mb=$(awk "BEGIN { printf \"%.0f\", $total_compile / 1024 }")
            tr_mb=$(awk "BEGIN { printf \"%.0f\", $total_run / 1024 }")
            ac_mb=$(awk "BEGIN { printf \"%.1f\", $avg_c / 1024 }")
            ar_mb=$(awk "BEGIN { printf \"%.1f\", $avg_r / 1024 }")
            echo "Total: $count tests"
            echo "  Compile: ${tc_mb} MB cumulative, ${ac_mb} MB average"
            echo "  Run:     ${tr_mb} MB cumulative, ${ar_mb} MB average"
        else
            echo "Total: 0 tests"
        fi
    }
fi

# Cleanup
rm -f "$RAW"
rm -rf "$OUT_DIR" "$RESULTS_DIR"

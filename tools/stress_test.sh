#!/bin/bash
# stress_test.sh — Run stress_gen.tl at escalating scales and report compiler limits.
#
# Usage:
#   ./tools/stress_test.sh [options]
#
# Options:
#   -a AXIS       Test axis: volume, specialize, nesting, inference, or all (default: all)
#   -s SCALES     Comma-separated scale values (default: axis-specific)
#   -o DIR        Output directory for generated files (default: /tmp/stress_test)
#   -k            Keep generated .tl files after run
#   -q            Quiet — skip per-scale progress
#
# Axes:
#   volume      N independent functions — tests parser, AST memory, transpiler throughput
#   specialize  N struct types x generic functions — tests monomorphisation explosion
#   nesting     depth-N nested ifs + call chain — tests stack overflow in recursive traversals
#   inference   depth-N unannotated function chain — tests constraint solver depth
#
# The harness generates .tl files via stress_gen.tl, compiles each with --stats,
# measures peak RSS via GNU time, and verifies correctness. It stops an axis on
# first failure and reports the last successful scale in the summary.
#
# Examples:
#   ./tools/stress_test.sh                              # all axes, default scales
#   ./tools/stress_test.sh -a volume -s 100,1000,5000   # volume only, custom scales
#   ./tools/stress_test.sh -a nesting                   # find nesting depth limit
#   ./tools/stress_test.sh -a all -k                    # keep generated files

set -euo pipefail
cd "$(dirname "$0")/.."

TESS=./tess
STRESS_GEN=tools/stress_gen.tl

# Find GNU time binary (same strategy as measure_mem.sh)
GNU_TIME=""
for candidate in /run/current-system/sw/bin/time /usr/bin/time; do
    if [ -x "$candidate" ]; then
        GNU_TIME="$candidate"
        break
    fi
done
if [ -z "$GNU_TIME" ]; then
    GNU_TIME=$(type -P time 2>/dev/null || true)
fi

# Defaults
AXIS="all"
CUSTOM_SCALES=""
OUT_DIR="/tmp/stress_test"
KEEP=0
QUIET=0

while getopts "a:s:o:kq" opt; do
    case $opt in
        a) AXIS=$OPTARG ;;
        s) CUSTOM_SCALES=$OPTARG ;;
        o) OUT_DIR=$OPTARG ;;
        k) KEEP=1 ;;
        q) QUIET=1 ;;
        *) echo "Unknown option: -$opt" >&2; exit 1 ;;
    esac
done

# Validate
case "$AXIS" in
    volume|specialize|nesting|inference|all) ;;
    *) echo "Error: unknown axis '$AXIS' (valid: volume, specialize, nesting, inference, all)" >&2; exit 1 ;;
esac

if [ ! -x "$TESS" ]; then
    echo "Error: tess binary not found at $TESS — run 'make -j all' first" >&2
    exit 1
fi

if [ -z "$GNU_TIME" ]; then
    echo "Error: GNU time not found" >&2
    exit 1
fi

# Default scales per axis — designed to ramp up toward breaking points
default_scales() {
    case $1 in
        volume)     echo "100 500 1000 2000 5000 10000 20000" ;;
        specialize) echo "100 200 300 400 500 600 700 800 900 1000" ;;
        nesting)    echo "10 50 100 200 500 1000 2000" ;;
        inference)  echo "100 500 1000 2000 5000 10000" ;;
    esac
}

# Expand axis list
if [ "$AXIS" = "all" ]; then
    AXES="volume specialize nesting inference"
else
    AXES="$AXIS"
fi

mkdir -p "$OUT_DIR"

# ── Temp directory and results storage ──

TMPWORK=$(mktemp -d)
RESULTS_FILE="$TMPWORK/results"
touch "$RESULTS_FILE"
trap 'rm -rf "$TMPWORK"' EXIT

# ── Core: generate, compile, verify one (axis, scale) pair ──

run_one() {
    local axis=$1
    local scale=$2
    local gen_dir="$OUT_DIR/${axis}_${scale}"
    local tl_file="$gen_dir/stress_${axis}.tl"
    local bin_file="$TMPWORK/stress_${axis}_${scale}"
    local time_file="$TMPWORK/time_${axis}_${scale}"
    local stats_file="$TMPWORK/stats_${axis}_${scale}"
    local status="OK"
    local gen_ms=0 compile_ms=0 peak_kb=0 lines=0

    mkdir -p "$gen_dir"

    # 1. Generate
    local gen_start gen_end
    gen_start=$(date +%s%N)
    if ! "$TESS" run "$STRESS_GEN" -- -a "$axis" -s "$scale" -o "$gen_dir" >/dev/null 2>&1; then
        echo "$axis $scale GEN_FAIL 0 0 0 0"
        return
    fi
    gen_end=$(date +%s%N)
    gen_ms=$(( (gen_end - gen_start) / 1000000 ))

    if [ -f "$tl_file" ]; then
        lines=$(wc -l < "$tl_file")
    fi

    # 2. Compile to real binary under GNU time for peak RSS
    local compile_rc=0
    "$GNU_TIME" -v "$TESS" exe -o "$bin_file" "$tl_file" >/dev/null 2>"$time_file" || compile_rc=$?

    # Get --stats in a separate run (--stats writes to stderr, same fd as GNU time)
    if [ "$compile_rc" -eq 0 ]; then
        "$TESS" exe -o /dev/null --stats "$tl_file" 2>"$stats_file" || true
    fi

    if [ "$compile_rc" -ne 0 ]; then
        local signal
        signal=$(grep -oP 'Command terminated by signal \K\d+' "$time_file" 2>/dev/null || true)
        if [ -n "$signal" ]; then
            case "$signal" in
                11) status="SEGFAULT" ;;
                6)  status="ABORT" ;;
                9)  status="KILLED" ;;
                *)  status="SIGNAL_$signal" ;;
            esac
        else
            status="COMPILE_FAIL_$compile_rc"
        fi
    fi

    peak_kb=$(awk '/Maximum resident set size/ { print $NF }' "$time_file" 2>/dev/null || echo 0)
    peak_kb=${peak_kb:-0}

    compile_ms=$(awk '/^TOTAL/ { printf "%.0f", $2 }' "$stats_file" 2>/dev/null || echo 0)
    compile_ms=${compile_ms:-0}

    # 3. Verify by running the already-compiled binary
    if [ "$status" = "OK" ] && [ -x "$bin_file" ]; then
        if ! "$bin_file" >/dev/null 2>&1; then
            status="WRONG_RESULT"
        fi
    fi

    rm -f "$time_file" "$stats_file" "$bin_file"

    if [ "$KEEP" -eq 0 ]; then
        rm -rf "$gen_dir"
    fi

    echo "$axis $scale $status $lines $gen_ms $compile_ms $peak_kb"
}

# ── Run all scales for each axis ──

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║              Tess Compiler Stress Test                       ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Compiler: $("$TESS" --version 2>&1 || echo 'unknown')"
echo "Output:   $OUT_DIR"
echo ""

for axis in $AXES; do
    # Determine scales
    if [ -n "$CUSTOM_SCALES" ]; then
        scales=$(echo "$CUSTOM_SCALES" | tr ',' ' ')
    else
        scales=$(default_scales "$axis")
    fi

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Axis: $axis"
    echo "  Scales: $scales"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    printf '  %8s  %8s  %10s  %10s  %10s  %s\n' "SCALE" "LINES" "GEN(ms)" "COMPILE(ms)" "PEAK(MB)" "STATUS"
    printf '  %8s  %8s  %10s  %10s  %10s  %s\n' "--------" "--------" "----------" "-----------" "----------" "------"

    for scale in $scales; do
        result=$(run_one "$axis" "$scale")
        echo "$result" >> "$RESULTS_FILE"

        # Parse and display
        read -r r_axis r_scale r_status r_lines r_gen r_compile r_peak <<< "$result"
        peak_mb=$(awk "BEGIN { printf \"%.1f\", $r_peak / 1024 }")
        printf '  %8s  %8s  %10s  %10s  %8s MB  %s\n' \
            "$r_scale" "$r_lines" "$r_gen" "$r_compile" "$peak_mb" "$r_status"

        # Stop this axis if we hit a failure (no point testing larger scales)
        if [ "$r_status" != "OK" ]; then
            echo "  *** STOPPED: $r_status at scale=$r_scale ***"
            break
        fi
    done
    echo ""
done

# ── Summary ──

echo "═══════════════════════════════════════════════════════════════"
echo "  SUMMARY"
echo "═══════════════════════════════════════════════════════════════"

for axis in $AXES; do
    # Find the last OK and first failure for this axis
    last_ok=$(awk -v a="$axis" '$1==a && $3=="OK" { scale=$2 } END { print scale+0 }' "$RESULTS_FILE")
    first_fail=$(awk -v a="$axis" '$1==a && $3!="OK" { print $2, $3; exit }' "$RESULTS_FILE")
    max_peak=$(awk -v a="$axis" '$1==a && $3=="OK" { if ($7>m) m=$7 } END { printf "%.1f", m/1024 }' "$RESULTS_FILE")
    max_lines=$(awk -v a="$axis" '$1==a && $3=="OK" { if ($4>m) m=$4 } END { print m+0 }' "$RESULTS_FILE")

    printf '  %-12s  max_ok_scale=%-8s  max_lines=%-8s  peak=%-8s MB' "$axis" "$last_ok" "$max_lines" "$max_peak"
    if [ -n "$first_fail" ]; then
        printf '  BROKE at %s' "$first_fail"
    else
        printf '  (no failure found — try larger scales)'
    fi
    echo ""
done
echo ""

if [ "$KEEP" -eq 1 ]; then
    echo "Generated files kept in: $OUT_DIR"
fi

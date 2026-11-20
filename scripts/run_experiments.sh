#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_ONE="$SCRIPT_DIR/run_one.sh"
METRICS_PY="$SCRIPT_DIR/analysis/metrics.py"
PLOTS_PY="$SCRIPT_DIR/analysis/plots.py"

INPUT_FILE="$REPO_ROOT/data/testfile_256k.bin"
OUT_DIR="$REPO_ROOT/runs"
PROTOCOLS_CSV="SW,GBN,SR"
MIN_RTO="0.10"
MAX_RTO="10.0"
MAX_RETRIES=25
LINGER="1.5"
RECV_TIMEOUT="20.0"
SKIP_EXISTING=0
NO_PLOTS=0
DRY_RUN=0

EXPERIMENTS=()

usage() {
    cat <<EOF
Usage: $0 [options] [exp1|exp2|exp3|exp4|exp5|all ...]
Options:
  --protocols <list>   Comma-separated protocols: SW,GBN,SR (default: SW,GBN,SR)
  --input <path>       Input file path (default: data/testfile_256k.bin)
  --out-dir <path>     Root output directory for runs (default: runs)
  --min-rto <sec>      Minimum RTO in seconds (default: $MIN_RTO)
  --max-rto <sec>      Maximum RTO in seconds (default: $MAX_RTO)
  --max-retries <n>    Sender max retries per packet (default: $MAX_RETRIES)
  --linger <sec>       Receiver linger duration in seconds (default: $LINGER)
  --timeout <sec>      Receiver inactivity timeout in seconds (default: $RECV_TIMEOUT)
  --skip-existing      Skip runs that already have metrics.json
  --no-plots           Skip running plots.py after experiments
  --dry-run            Print planned commands without running them
  -h, --help           Show this help message
EOF
}

norm_proto_short() {
    case "$1" in
        SW|STOP_AND_WAIT|stop_and_wait) echo "SW" ;;
        GBN|GO_BACK_N|go_back_n)        echo "GBN" ;;
        SR|SELECTIVE|selective|SELECTIVE_REPEAT) echo "SR" ;;
        *)
            echo "Unknown protocol token: $1" >&2
            exit 1
            ;;
    esac
}

proto_flag_name() {
    case "$1" in
        SW)  echo "STOP_AND_WAIT" ;;
        GBN) echo "GO_BACK_N" ;;
        SR)  echo "SELECTIVE" ;;
    esac
}

has_proto() {
    local target="$1"
    for p in "${SELECTED_PROTOS[@]}"; do
        if [[ "$p" == "$target" ]]; then
            return 0
        fi
    done
    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --protocols)   PROTOCOLS_CSV="$2"; shift 2 ;;
        --input)       INPUT_FILE="$2"; shift 2 ;;
        --out-dir)     OUT_DIR="$2"; shift 2 ;;
        --min-rto)     MIN_RTO="$2"; shift 2 ;;
        --max-rto)     MAX_RTO="$2"; shift 2 ;;
        --max-retries) MAX_RETRIES="$2"; shift 2 ;;
        --linger)      LINGER="$2"; shift 2 ;;
        --timeout)     RECV_TIMEOUT="$2"; shift 2 ;;
        --skip-existing) SKIP_EXISTING=1; shift ;;
        --no-plots)    NO_PLOTS=1; shift ;;
        --dry-run)     DRY_RUN=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        exp1|exp2|exp3|exp4|exp5|all)
            EXPERIMENTS+=("$1"); shift ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ ${#EXPERIMENTS[@]} -eq 0 ]]; then
    EXPERIMENTS=("all")
fi

EXPAND_EXP=()
for item in "${EXPERIMENTS[@]}"; do
    if [[ "$item" == "all" ]]; then
        EXPAND_EXP=(exp1 exp2 exp3 exp4 exp5)
        break
    fi
    EXPAND_EXP+=("$item")
done

SELECTED_PROTOS=()
IFS=',' read -ra RAW_PROTOS <<< "$PROTOCOLS_CSV"
for raw_p in "${RAW_PROTOS[@]}"; do
    p_trimmed="$(echo "$raw_p" | tr -d '[:space:]')"
    if [[ -n "$p_trimmed" ]]; then
        SELECTED_PROTOS+=("$(norm_proto_short "$p_trimmed")")
    fi
done

if [[ ${#SELECTED_PROTOS[@]} -eq 0 ]]; then
    echo "No valid protocols selected." >&2
    exit 1
fi

if [[ $DRY_RUN -eq 0 ]]; then
    if [[ ! -f "$INPUT_FILE" ]]; then
        echo "Input file not found: $INPUT_FILE" >&2
        exit 1
    fi
    make -C "$REPO_ROOT" all
fi

TOTAL_PLANNED=0
TOTAL_EXECUTED=0
TOTAL_SKIPPED=0
TOTAL_WARNINGS=0

execute_run() {
    local exp_name="$1"
    local run_id="$2"
    local short_proto="$3"
    local window="$4"
    local k_bits="$5"
    local scenario_file="$6"
    local seed="$7"

    local full_proto
    full_proto="$(proto_flag_name "$short_proto")"
    local run_dir="$OUT_DIR/$exp_name/$run_id"
    local scn_path="$REPO_ROOT/scenarios/$scenario_file"

    TOTAL_PLANNED=$((TOTAL_PLANNED + 1))

    if [[ $SKIP_EXISTING -eq 1 && -f "$run_dir/metrics.json" ]]; then
        echo "[SKIP] $exp_name/$run_id (metrics.json exists)"
        TOTAL_SKIPPED=$((TOTAL_SKIPPED + 1))
        return 0
    fi

    if [[ $DRY_RUN -eq 1 ]]; then
        echo "[DRY-RUN] $exp_name/$run_id : proto=$full_proto W=$window k=$k_bits scn=$scenario_file seed=$seed"
        return 0
    fi

    echo "=== Running $exp_name/$run_id (proto=$full_proto W=$window k=$k_bits scn=$scenario_file seed=$seed) ==="
    set +e
    "$RUN_ONE" \
        --protocol "$full_proto" \
        --window "$window" \
        --k "$k_bits" \
        --payload 1024 \
        --scenario "$scn_path" \
        --seed "$seed" \
        --input "$INPUT_FILE" \
        --run-dir "$run_dir" \
        --min-rto "$MIN_RTO" \
        --max-rto "$MAX_RTO" \
        --max-retries "$MAX_RETRIES" \
        --linger "$LINGER" \
        --timeout "$RECV_TIMEOUT"
    local rc=$?
    set -e

    TOTAL_EXECUTED=$((TOTAL_EXECUTED + 1))

    if [[ $rc -ne 0 ]]; then
        echo "  [WARN] run_one.sh exited with status $rc for $exp_name/$run_id"
        TOTAL_WARNINGS=$((TOTAL_WARNINGS + 1))
    fi

    set +e
    python3 "$METRICS_PY" "$run_dir"
    local mrc=$?
    set -e
    if [[ $mrc -ne 0 ]]; then
        echo "  [WARN] metrics.py exited with status $mrc for $exp_name/$run_id (incomplete logs?)"
        TOTAL_WARNINGS=$((TOTAL_WARNINGS + 1))
    fi
}

run_exp1() {
    local seed=101
    if has_proto SW; then
        execute_run "exp1" "SW_W1_k7" "SW" 1 7 "baseline.scn" "$seed"
    fi
    if has_proto GBN; then
        for w in 1 2 4 8 16 32; do
            execute_run "exp1" "GBN_W${w}_k7" "GBN" "$w" 7 "baseline.scn" "$seed"
        done
    fi
    if has_proto SR; then
        for w in 1 2 4 8 16 32; do
            execute_run "exp1" "SR_W${w}_k7" "SR" "$w" 7 "baseline.scn" "$seed"
        done
    fi
}

run_exp2() {
    local seed=202
    for loss in 0 5 10; do
        local scn="exp2_loss${loss}.scn"
        if has_proto SW; then
            execute_run "exp2" "SW_loss${loss}" "SW" 1 7 "$scn" "$seed"
        fi
        if has_proto GBN; then
            execute_run "exp2" "GBN_loss${loss}" "GBN" 16 7 "$scn" "$seed"
        fi
        if has_proto SR; then
            execute_run "exp2" "SR_loss${loss}" "SR" 16 7 "$scn" "$seed"
        fi
    done
}

run_exp3() {
    # Note: Go-Back-N is intentionally omitted from Experiment 3 per project spec Section 3.2.3.
    local seed=303
    for d in 20 50 100 200; do
        local scn="exp3_delay${d}.scn"
        if has_proto SW; then
            execute_run "exp3" "SW_W1_d${d}" "SW" 1 7 "$scn" "$seed"
        fi
        if has_proto SR; then
            execute_run "exp3" "SR_W16_d${d}" "SR" 16 7 "$scn" "$seed"
            execute_run "exp3" "SR_W64_d${d}" "SR" 64 7 "$scn" "$seed"
        fi
    done
}

run_exp4() {
    local seed=404
    for imp in corrupt5 reorder5 duplicate5 ackloss5; do
        local scn="exp4_${imp}.scn"
        if has_proto SW; then
            execute_run "exp4" "SW_${imp}" "SW" 1 7 "$scn" "$seed"
        fi
        if has_proto GBN; then
            execute_run "exp4" "GBN_${imp}" "GBN" 16 7 "$scn" "$seed"
        fi
        if has_proto SR; then
            execute_run "exp4" "SR_${imp}" "SR" 16 7 "$scn" "$seed"
        fi
    done
}

run_exp5() {
    local seed=505
    if has_proto GBN; then
        for w in 1 2 3 4 5 6 7 8; do
            execute_run "exp5" "GBN_k3_W${w}" "GBN" "$w" 3 "exp5_gbn_k3.scn" "$seed"
        done
        for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
            execute_run "exp5" "GBN_k4_W${w}" "GBN" "$w" 4 "exp5_gbn_k4.scn" "$seed"
        done
    fi
    if has_proto SR; then
        for w in 1 2 3 4 5; do
            execute_run "exp5" "SR_k3_W${w}" "SR" "$w" 3 "exp5_sr_k3.scn" "$seed"
        done
        for w in 1 2 3 4 5 6 7 8 9; do
            execute_run "exp5" "SR_k4_W${w}" "SR" "$w" 4 "exp5_sr_k4.scn" "$seed"
        done
    fi
}

for exp in "${EXPAND_EXP[@]}"; do
    case "$exp" in
        exp1) run_exp1 ;;
        exp2) run_exp2 ;;
        exp3) run_exp3 ;;
        exp4) run_exp4 ;;
        exp5) run_exp5 ;;
    esac
done

echo "------------------------------------------------------------"
echo "Experiments finished: planned=$TOTAL_PLANNED executed=$TOTAL_EXECUTED skipped=$TOTAL_SKIPPED warnings=$TOTAL_WARNINGS"

if [[ $DRY_RUN -eq 0 && $NO_PLOTS -eq 0 ]]; then
    echo "Generating plots and summary tables in $OUT_DIR/plots ..."
    python3 "$PLOTS_PY" "$OUT_DIR" --out-dir "$OUT_DIR/plots"
fi

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROTOCOL="STOP_AND_WAIT"
WINDOW=16
K_BITS=7
PAYLOAD=1024
SCENARIO="$REPO_ROOT/scenarios/baseline.scn"
SEED=42
INPUT_FILE="$REPO_ROOT/data/testfile_256k.bin"
RUN_DIR=""
SENDER_PORT=5000
ACK_PORT=8000
RECEIVER_PORT=7000
MIN_RTO="0.10"
MAX_RTO="10.0"
MAX_RETRIES=20
LINGER="2.0"
RECV_TIMEOUT="20.0"
EMU_EXTRA_ARGS=""

usage() {
    cat <<EOF
Usage: $0 [options]
  --protocol <STOP_AND_WAIT|GO_BACK_N|SELECTIVE|SW|GBN|SR>
  --window <W>             Window size (default: $WINDOW)
  --k <bits>               Sequence-number width 3..8 (default: $K_BITS)
  --payload <bytes>        Max payload size (default: $PAYLOAD)
  --scenario <path>        Scenario file (default: scenarios/baseline.scn)
  --seed <int>             PRNG seed (default: $SEED)
  --input <path>           Input file (default: data/testfile_256k.bin)
  --run-dir <path>         Output directory for run artifacts (required or auto-created)
  --sender-port <port>     Sender->Emulator port (default: $SENDER_PORT)
  --ack-port <port>        Emulator receiver-side port (default: $ACK_PORT)
  --receiver-port <port>   Receiver listen port (default: $RECEIVER_PORT)
  --min-rto <sec>          Minimum RTO in seconds (default: $MIN_RTO)
  --max-rto <sec>          Maximum RTO in seconds (default: $MAX_RTO)
  --max-retries <n>        Sender max retries per packet (default: $MAX_RETRIES)
  --linger <sec>           Receiver linger duration in seconds (default: $LINGER)
  --timeout <sec>          Receiver inactivity timeout in seconds (default: $RECV_TIMEOUT)
  --emu-args "<args>"      Extra CLI arguments forwarded to emulator
EOF
}

normalize_protocol() {
    case "$1" in
        SW|STOP_AND_WAIT|stop_and_wait) echo "STOP_AND_WAIT" ;;
        GBN|GO_BACK_N|go_back_n)        echo "GO_BACK_N" ;;
        SR|SELECTIVE|selective|SELECTIVE_REPEAT) echo "SELECTIVE" ;;
        *)
            echo "Unknown protocol: $1" >&2
            exit 1
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --protocol)      PROTOCOL="$(normalize_protocol "$2")"; shift 2 ;;
        --window)        WINDOW="$2"; shift 2 ;;
        --k)             K_BITS="$2"; shift 2 ;;
        --payload)       PAYLOAD="$2"; shift 2 ;;
        --scenario)      SCENARIO="$2"; shift 2 ;;
        --seed)          SEED="$2"; shift 2 ;;
        --input)         INPUT_FILE="$2"; shift 2 ;;
        --run-dir)       RUN_DIR="$2"; shift 2 ;;
        --sender-port)   SENDER_PORT="$2"; shift 2 ;;
        --ack-port)      ACK_PORT="$2"; shift 2 ;;
        --receiver-port) RECEIVER_PORT="$2"; shift 2 ;;
        --min-rto)       MIN_RTO="$2"; shift 2 ;;
        --max-rto)       MAX_RTO="$2"; shift 2 ;;
        --max-retries)   MAX_RETRIES="$2"; shift 2 ;;
        --linger)        LINGER="$2"; shift 2 ;;
        --timeout)       RECV_TIMEOUT="$2"; shift 2 ;;
        --emu-args)      EMU_EXTRA_ARGS="$2"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$RUN_DIR" ]]; then
    STAMP="$(date +%Y%m%d_%H%M%S)"
    RUN_DIR="$REPO_ROOT/runs/${PROTOCOL}_W${WINDOW}_k${K_BITS}_seed${SEED}_${STAMP}"
fi

# Convert relative input/scenario paths before changing directory if needed.
if [[ ! -f "$INPUT_FILE" ]]; then
    echo "Input file not found: $INPUT_FILE" >&2
    exit 1
fi
if [[ ! -f "$SCENARIO" ]]; then
    echo "Scenario file not found: $SCENARIO" >&2
    exit 1
fi

for bin in "$REPO_ROOT/bin/emulator" "$REPO_ROOT/bin/receiver" "$REPO_ROOT/bin/sender"; do
    if [[ ! -x "$bin" ]]; then
        echo "Missing binary $bin. Building with make all..." >&2
        make -C "$REPO_ROOT" all
        break
    fi
done

mkdir -p "$RUN_DIR"
cp "$SCENARIO" "$RUN_DIR/scenario.scn"

# Make paths short/relative when possible because sender/receiver use char[100] path buffers.
INPUT_REL="$(python3 -c 'import os,sys; print(os.path.relpath(os.path.abspath(sys.argv[1]), sys.argv[2]))' "$INPUT_FILE" "$REPO_ROOT")"
RUN_REL="$(python3 -c 'import os,sys; print(os.path.relpath(os.path.abspath(sys.argv[1]), sys.argv[2]))' "$RUN_DIR" "$REPO_ROOT")"

if [[ ${#INPUT_REL} -lt ${#INPUT_FILE} ]]; then INPUT_ARG="$INPUT_REL"; else INPUT_ARG="$INPUT_FILE"; fi
if [[ ${#RUN_REL} -lt ${#RUN_DIR} ]]; then RUN_ARG="$RUN_REL"; else RUN_ARG="$RUN_DIR"; fi

RECV_OUT_ARG="$RUN_ARG/received.bin"
SENDER_LOG_ARG="$RUN_ARG/sender.csv"
RECV_LOG_ARG="$RUN_ARG/receiver.csv"
EMU_LOG_ARG="$RUN_ARG/emulator.csv"

for p in "$INPUT_ARG" "$RECV_OUT_ARG" "$SENDER_LOG_ARG" "$RECV_LOG_ARG"; do
    if [[ ${#p} -ge 96 ]]; then
        echo "ERROR: Path exceeds 95 chars (sender/receiver char[100] buffer limit): $p" >&2
        exit 1
    fi
done

EMU_PID=""
RECV_PID=""
cleanup() {
    if [[ -n "$RECV_PID" ]] && kill -0 "$RECV_PID" 2>/dev/null; then
        kill -TERM "$RECV_PID" 2>/dev/null || true
        wait "$RECV_PID" 2>/dev/null || true
    fi
    if [[ -n "$EMU_PID" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill -TERM "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cd "$REPO_ROOT"

python3 - "$RUN_DIR/params.json" "$PROTOCOL" "$WINDOW" "$K_BITS" "$PAYLOAD" \
    "$SCENARIO" "$SEED" "$INPUT_FILE" "$SENDER_PORT" "$ACK_PORT" "$RECEIVER_PORT" \
    "$MIN_RTO" "$MAX_RTO" "$MAX_RETRIES" "$LINGER" "$RECV_TIMEOUT" "$EMU_EXTRA_ARGS" <<'PYEOF'
import json, os, sys
out_path = sys.argv[1]
data = {
    "protocol": sys.argv[2],
    "window_size": int(sys.argv[3]),
    "k": int(sys.argv[4]),
    "max_payload": int(sys.argv[5]),
    "scenario_source": os.path.abspath(sys.argv[6]),
    "seed": int(sys.argv[7]),
    "input_file": os.path.abspath(sys.argv[8]),
    "input_size_bytes": os.path.getsize(sys.argv[8]),
    "sender_port": int(sys.argv[9]),
    "ack_port": int(sys.argv[10]),
    "receiver_port": int(sys.argv[11]),
    "min_rto": float(sys.argv[12]),
    "max_rto": float(sys.argv[13]),
    "max_retries": int(sys.argv[14]),
    "linger_duration": float(sys.argv[15]),
    "receiver_timeout": float(sys.argv[16]),
    "emu_extra_args": sys.argv[17],
}
with open(out_path, "w") as f:
    json.dump(data, f, indent=2)
    f.write("\n")
PYEOF

# 1. Start emulator
# shellcheck disable=SC2086
./bin/emulator \
    --scenario "$RUN_ARG/scenario.scn" \
    --seed "$SEED" \
    --senderPort "$SENDER_PORT" \
    --ackPort "$ACK_PORT" \
    --receiverIP 127.0.0.1 \
    --receiverPort "$RECEIVER_PORT" \
    --logFile "$EMU_LOG_ARG" \
    $EMU_EXTRA_ARGS \
    > "$RUN_DIR/emulator.stdout.log" 2>&1 &
EMU_PID=$!

sleep 0.1
if ! kill -0 "$EMU_PID" 2>/dev/null; then
    echo "Emulator failed to start. Check $RUN_DIR/emulator.stdout.log" >&2
    cat "$RUN_DIR/emulator.stdout.log" >&2 || true
    exit 1
fi

# 2. Start receiver
./bin/receiver \
    -f "$RECV_OUT_ARG" \
    -k "$K_BITS" \
    -p "$PAYLOAD" \
    -q "$PROTOCOL" \
    -w "$WINDOW" \
    -t "$RECV_TIMEOUT" \
    -l "$LINGER" \
    --emulatorIP 127.0.0.1 \
    --emulatorPort "$ACK_PORT" \
    --receiverIP 127.0.0.1 \
    --receiverPort "$RECEIVER_PORT" \
    --logFileName "$RECV_LOG_ARG" \
    > "$RUN_DIR/receiver.stdout.log" 2>&1 &
RECV_PID=$!

sleep 0.1
if ! kill -0 "$RECV_PID" 2>/dev/null; then
    echo "Receiver failed to start. Check $RUN_DIR/receiver.stdout.log" >&2
    cat "$RUN_DIR/receiver.stdout.log" >&2 || true
    exit 1
fi

# 3. Run sender in foreground
set +e
./bin/sender \
    -f "$INPUT_ARG" \
    -k "$K_BITS" \
    -p "$PAYLOAD" \
    -q "$PROTOCOL" \
    -w "$WINDOW" \
    -r "$MAX_RETRIES" \
    --emulatorIP 127.0.0.1 \
    --emulatorPort "$SENDER_PORT" \
    --minimumRTO "$MIN_RTO" \
    --maximumRTO "$MAX_RTO" \
    --logFileName "$SENDER_LOG_ARG" \
    > "$RUN_DIR/sender.stdout.log" 2>&1
SENDER_RC=$?
set -e

# 4. Wait for receiver to complete linger cleanly (with bounded deadline in 0.1s steps)
WAIT_LIMIT_TENTHS="$(python3 -c "print(int((float('$LINGER') + float('$RECV_TIMEOUT') + 5) * 10))")"
ELAPSED=0
while kill -0 "$RECV_PID" 2>/dev/null; do
    if [[ $ELAPSED -ge $WAIT_LIMIT_TENTHS ]]; then
        echo "Receiver exceeded wait limit; terminating." >&2
        kill -TERM "$RECV_PID" 2>/dev/null || true
        break
    fi
    sleep 0.1
    ELAPSED=$((ELAPSED + 1))
done
set +e
wait "$RECV_PID" 2>/dev/null
RECV_RC=$?
set -e
RECV_PID=""

# 5. Stop emulator cleanly so it flushes logs and closes sockets
kill -TERM "$EMU_PID" 2>/dev/null || true
wait "$EMU_PID" 2>/dev/null || true
EMU_PID=""

# 6. Compute SHA-256 verification
SHA_MATCH="$(python3 - "$INPUT_FILE" "$RUN_DIR/received.bin" "$RUN_DIR/sha256.json" <<'PYEOF'
import hashlib, json, os, sys

def sha256_file(path):
    if not os.path.exists(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

in_path, out_path, json_path = sys.argv[1], sys.argv[2], sys.argv[3]
in_hash = sha256_file(in_path)
out_hash = sha256_file(out_path)
match = (in_hash is not None and out_hash is not None and in_hash == out_hash)
payload = {
    "input_file": os.path.abspath(in_path),
    "received_file": os.path.abspath(out_path),
    "input_sha256": in_hash,
    "received_sha256": out_hash,
    "input_bytes": os.path.getsize(in_path) if os.path.exists(in_path) else 0,
    "received_bytes": os.path.getsize(out_path) if os.path.exists(out_path) else 0,
    "match": match,
}
with open(json_path, "w") as f:
    json.dump(payload, f, indent=2)
    f.write("\n")
print("true" if match else "false")
PYEOF
)"

echo "Run complete: dir=$RUN_DIR protocol=$PROTOCOL W=$WINDOW k=$K_BITS seed=$SEED sender_rc=$SENDER_RC recv_rc=$RECV_RC sha256_match=$SHA_MATCH"
exit "$SENDER_RC"

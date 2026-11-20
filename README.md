# BLG 337E — Project 1: Reliable Data Transfer over UDP

Implementation and experimental evaluation of a reliable unidirectional file transfer protocol (**Stop-and-Wait**, **Go-Back-N**, and **Selective Repeat**) over UDP (`127.0.0.1`) with custom IEEE 802.3 CRC-32, RFC 6298 adaptive RTO, a full-duplex impairment emulator, reproducible automation scripts, and Python log analysis/plotting tools.

---

## 1. System Architecture & Loopback Port Map

Three independent POSIX C processes communicate strictly via UDP datagrams over `127.0.0.1`:

```text
  +----------+  127.0.0.1:5000 (DATA / FIN)   +----------+  127.0.0.1:7000  +----------+
  |  sender  | -----------------------------> | emulator | ---------------> | receiver |
  +----------+ <----------------------------- |          | <--------------- +----------+
                 (ACKs to sender src port)    +----------+  127.0.0.1:8000    (ACKs)
```

| Endpoint / Socket | Default Port | CLI Option |
|---|---|---|
| Sender -> Emulator (bind on emulator) | `5000` | Sender: `--emulatorPort 5000`, Emulator: `--senderPort 5000` |
| Emulator receiver-side socket (bind on emulator) | `8000` | Emulator: `--ackPort 8000`, Receiver: `--emulatorPort 8000` |
| Emulator -> Receiver (bind on receiver) | `7000` | Emulator: `--receiverPort 7000`, Receiver: `--receiverPort 7000` |

> **Note:** `bin/receiver` defaults `--receiverPort` to `5000` for standalone direct testing without the emulator. Whenever running with the emulator, pass `--receiverPort 7000` (handled automatically by `scripts/run_one.sh`).

---

## 2. Repository Structure

```text
comm.-project/
├── Makefile
├── README.md
├── common_folder/          # Shared packet header, IEEE 802.3 CRC-32, CSV logger, utilities
├── sender_folder/          # Sender binary sources (ARQ state machines, RTO, config)
├── receiver_folder/        # Receiver binary sources (validation, ACKs, file writer, config)
├── emulator_folder/        # Impairment emulator (xoshiro256** PRNG, scenario parser, min-heap, link/FIFO, pselect)
├── scenarios/              # .scn scenario files (passthrough, baseline, smoke_impair, exp2..exp5)
├── scripts/
│   ├── generate_testfile.py
│   ├── run_one.sh          # Single end-to-end run + SHA-256 verification
│   ├── run_experiments.sh  # Full Experiment 1-5 automation matrix
│   └── analysis/
│       ├── logparse.py     # CSV log & scenario parsers
│       ├── metrics.py      # Completion time, goodput, retrans overhead, utilization, RTT/RTO
│       └── plots.py        # Summary tables + all experiment PNG figures
├── tests/                  # C unit tests (test_crc32, test_emulator_unit)
├── data/                   # Deterministic test files (testfile_256k.bin, testfile_16k.bin)
└── bin/                    # Compiled binaries (gitignored)
```

---

## 3. Build & Unit Tests

Requirements: `gcc` (C11 / POSIX, standard C library + `-lm`), `python3` (`matplotlib` for plotting).

```bash
make all      # Build bin/sender, bin/receiver, bin/emulator
make test     # Build and run bin/test_crc32 and bin/test_emulator_unit
make clean    # Remove bin/
```

---

## 4. Quick Start

### 4.1 Automated Single Run (`scripts/run_one.sh`)

```bash
./scripts/run_one.sh \
  --protocol STOP_AND_WAIT \
  --window 16 \
  --k 7 \
  --payload 1024 \
  --scenario scenarios/baseline.scn \
  --seed 42 \
  --input data/testfile_256k.bin \
  --linger 1.5 \
  --run-dir runs/manual_baseline

python3 scripts/analysis/metrics.py runs/manual_baseline
```

### 4.2 Manual 3-Terminal Execution

```bash
# Terminal 1: Emulator
./bin/emulator --scenario scenarios/baseline.scn --seed 42 \
  --senderPort 5000 --ackPort 8000 --receiverPort 7000 --logFile emu.csv

# Terminal 2: Receiver
./bin/receiver -f received.bin -k 7 -p 1024 -q STOP_AND_WAIT -w 16 -l 2 \
  --receiverPort 7000 --emulatorPort 8000 --logFileName receiver.csv

# Terminal 3: Sender
./bin/sender -f data/testfile_256k.bin -k 7 -p 1024 -q STOP_AND_WAIT -w 16 -r 20 \
  --emulatorPort 5000 --minimumRTO 0.10 --logFileName sender.csv
```

> Any scenario parameter can also be overridden on the emulator command line without editing the `.scn` file (e.g. `--dataLoss 0.05 --propDelay 100 --jitter 10 --seed 99`).

### 4.3 Full Experiment Suite & Plots (`scripts/run_experiments.sh`)

```bash
# Preview all 84 planned runs across Experiments 1-5
./scripts/run_experiments.sh --dry-run all

# Run all experiments and automatically generate plots in runs/plots/
./scripts/run_experiments.sh all

# Run a subset (e.g. only Exp 1 and Exp 3, or only Stop-and-Wait)
./scripts/run_experiments.sh --protocols SW exp1 exp3

# Recompute plots/tables from existing runs
python3 scripts/analysis/plots.py runs --out-dir runs/plots
```

---

## 5. Runtime CLI Parameter Tables

### 5.1 `bin/sender`

| Flag | Default | Description |
|---|---|---|
| `-f <path>` | `default_fileInput.txt` | Input file path (max 99 chars in `char[100]` buffer) |
| `-q <protocol>` | `STOP_AND_WAIT` | ARQ protocol: `STOP_AND_WAIT`, `GO_BACK_N`, `SELECTIVE` |
| `-w <W>` | `1` | Send window size ($W \ge 1$) |
| `-k <bits>` | `8` | Sequence-number bit width ($3 \le k \le 8$; baseline uses `7`) |
| `-p <bytes>` | `1024` | Max payload size per packet (`1..1400`) |
| `-r <retries>` | `3` | Max retransmissions before aborting |
| `--emulatorIP <ip>` | `127.0.0.1` | Emulator IPv4 address |
| `--emulatorPort <port>` | `5000` | Emulator sender-side UDP port |
| `--minimumRTO <sec>` | `1.0` | Lower bound on adaptive RTO in seconds |
| `--maximumRTO <sec>` | `60.0` | Upper bound on adaptive RTO in seconds |
| `--logFileName <path>` | `default_senderLog.txt` | Sender CSV log output path (max 99 chars) |

### 5.2 `bin/receiver`

| Flag | Default | Description |
|---|---|---|
| `-f <path>` | `default_fileOutput.txt` | Output file path (max 99 chars) |
| `-q <protocol>` | `STOP_AND_WAIT` | ARQ protocol: `STOP_AND_WAIT`, `GO_BACK_N`, `SELECTIVE` |
| `-w <W>` | `1` | Receive window size ($W \ge 1$) |
| `-k <bits>` | `8` | Sequence-number bit width ($3 \le k \le 8$) |
| `-p <bytes>` | `1024` | Max payload size (`1..1400`) |
| `-t <sec>` | `20.0` | Inactivity receive timeout before aborting |
| `-l <sec>` | `5.0` | Post-`FIN` linger duration for re-ACKing duplicate `FIN` packets |
| `--receiverIP <ip>` | `127.0.0.1` | Local IPv4 bind address |
| `--receiverPort <port>` | `5000` | Local UDP listen port (pass `7000` with emulator) |
| `--emulatorIP <ip>` | `127.0.0.1` | Emulator IPv4 address |
| `--emulatorPort <port>` | `8000` | Emulator receiver-side port |
| `--logFileName <path>` | `default_receiverLog.txt` | Receiver CSV log output path (max 99 chars) |

### 5.3 `bin/emulator`

| Flag | Default | Description |
|---|---|---|
| `--scenario <path>` | *(none: baseline)* | Path to `.scn` configuration file |
| `--seed <uint64>` | `42` | Master PRNG seed (`xoshiro256**`, 12 independent streams) |
| `--senderPort <port>` | `5000` | UDP bind port facing `sender` |
| `--ackPort <port>` | `8000` | UDP bind port facing `receiver` |
| `--receiverIP <ip>` | `127.0.0.1` | Destination IPv4 for `S2R` forwarding |
| `--receiverPort <port>` | `7000` | Destination UDP port for `S2R` forwarding |
| `--bindIP <ip>` | `127.0.0.1` | Local bind IP for both emulator sockets |
| `--logFile <path>` | `emulator_log.csv` | Emulator CSV event log path |
| `--linkRate <bps>` | `1000000` | Override `link_rate_bps` (both directions; `0` = zero serialization delay) |
| `--queueCap <pkts>` | `128` | Override FIFO queue capacity per direction (`0` = unlimited) |
| `--propDelay <ms>` | `20` | Override one-way propagation delay in ms |
| `--jitter <ms>` | `0` | Override uniform order-preserving jitter half-width `j` (`[-j, +j]` ms) |
| `--dataLoss <p>` | `0` | Override `S2R` packet loss probability (`0.0..1.0`) |
| `--ackLoss <p>` | `0` | Override `R2S` ACK loss probability (`0.0..1.0`) |
| `--corrupt <p>` | `0` | Override single-bit corruption probability (both directions) |
| `--duplicate <p>` | `0` | Override packet duplication probability (both directions) |
| `--reorder <p>` | `0` | Override packet reordering probability (both directions) |
| `--reorderTimeout <ms>` | `50` | Override lone held packet release timeout in ms |

### 5.4 `scripts/run_one.sh` & `scripts/run_experiments.sh`

* **`run_one.sh`**: `--protocol <SW|GBN|SR|STOP_AND_WAIT|GO_BACK_N|SELECTIVE>`, `--window <W>`, `--k <bits>`, `--payload <bytes>`, `--scenario <path>`, `--seed <int>`, `--input <path>`, `--run-dir <path>`, `--sender-port <p>`, `--ack-port <p>`, `--receiver-port <p>`, `--min-rto <sec>`, `--max-rto <sec>`, `--max-retries <n>`, `--linger <sec>`, `--timeout <sec>`, `--emu-args "<args>"`.
* **`run_experiments.sh`**: `[exp1|exp2|exp3|exp4|exp5|all ...]`, `--protocols <SW,GBN,SR>`, `--input <path>`, `--out-dir <path>`, `--min-rto <sec>`, `--max-rto <sec>`, `--max-retries <n>`, `--linger <sec>`, `--timeout <sec>`, `--skip-existing`, `--no-plots`, `--dry-run`.

---

## 6. Scenario File Format (`.scn`)

Scenario files accept `key = value` lines, `#` comments, and deterministic per-packet `action` directives:

* **Global keys:** `link_rate_bps`, `queue_capacity`, `prop_delay_ms`, `jitter_ms`, `data_loss`, `ack_loss`, `corrupt`, `duplicate`, `reorder`, `reorder_timeout_ms`.
* **Per-direction overrides:** `link_rate_s2r_bps`, `link_rate_r2s_bps`, `queue_capacity_s2r`, `queue_capacity_r2s`, `prop_delay_s2r_ms`, `prop_delay_r2s_ms`, `jitter_s2r_ms`, `jitter_r2s_ms`, `corrupt_s2r`, `corrupt_r2s`, `duplicate_s2r`, `duplicate_r2s`, `reorder_s2r`, `reorder_r2s`, `reorder_timeout_s2r_ms`, `reorder_timeout_r2s_ms`.
* **Per-packet deterministic actions:**
  ```text
  action <S2R|R2S> pkt <1-based_idx> <drop | delay <ms> | corrupt [bit] | duplicate | reorder>
  action <S2R|R2S> seq <seq_num> [occ <n>] <drop | delay <ms> | corrupt [bit] | duplicate | reorder>
  ```

---

## 7. Run Directory Layout, Log Schemas & Metrics

### 7.1 Run Directory Layout (`<run_dir>/`)

Each run writes: `params.json`, `scenario.scn`, `sender.csv`, `receiver.csv`, `emulator.csv`, `received.bin`, `sha256.json`, `metrics.json`, plus stdout/stderr logs (`*.stdout.log`).

### 7.2 CSV Log Schemas (`CLOCK_MONOTONIC` in microseconds)

* **`sender.csv` / `receiver.csv`**: `time_us,event,seq,bytes,is_retransmission,rtt_us,srtt_us,rto_us`
  * Sender events: `SEND_DATA`, `SEND_FIN`, `ACK_RECV`, `ACK_STALE` (or `AKC_STALE`), `ACK_CORRUPT`, `TIMEOUT`, `RTT_SAMPLE`.
  * Receiver events: `RECV_DATA`, `RECV_DUP`, `RECV_CORRUPT`, `ACK_SEND`, `FIN_RECV`, `FIN_REACK`.
* **`emulator.csv`**: `time_us,event,dir,pkt_idx,pkt_type,seq,bytes,detail`
  * Events: `ARRIVE`, `ENQUEUE`, `QUEUE_DROP`, `DROP_LOSS`, `CORRUPT`, `DUPLICATE`, `REORDER_HOLD`, `REORDER_RELEASE`, `REORDER_TIMEOUT`, `ACTION_DROP`, `ACTION_DELAY`, `ACTION_CORRUPT`, `ACTION_DUPLICATE`, `DELIVER`.

### 7.3 Metric Definitions (`scripts/analysis/metrics.py`)

Let packet length $L = \text{sizeof(packet\_header)} + \text{payload\_length}$ ($11 + 1024 = 1035\text{ B}$ for full baseline DATA packets):

* **Completion time ($T_{\text{comp}}$):** $t(\text{last data ACK\_RECV}) - t(\text{first SEND\_DATA})$ (excluding post-transfer `FIN`/linger).
* **Goodput:** $\text{file\_size\_bytes} / T_{\text{comp}}$ (reported in `B/s` and `kbit/s`).
* **Retransmission overhead:** $\frac{\sum \text{bytes of retransmitted SEND\_DATA}}{\sum \text{bytes of all SEND\_DATA}}$.
* **Link utilization:** $\frac{8 \cdot \sum \text{bytes of non-retransmitted SEND\_DATA}}{R \cdot T_{\text{comp}}}$ (for $R > 0$).
* **Integrity:** Exact SHA-256 comparison between input file and `received.bin`.
* **RTT / RTO:** `RTT_SAMPLE` (`rtt_us`, `srtt_us`, `rto_us`) and `TIMEOUT` time-series.

---

## 8. Reproducible Experiment Matrix (`scripts/run_experiments.sh`)

Fixed test payload: `data/testfile_256k.bin` (`262144` bytes, SHA-256 `2dd0334c371fd1e59c16adaf2e070596dd947b2015ef64bf941b96cb9abbfd80`).

| Experiment | Fixed Seed | Scenarios | Protocols & Sweeps |
|---|---|---|---|
| **Exp 1: Ideal Network** | `101` | `baseline.scn` | `SW` ($W=1$), `GBN` ($W \in \{1,2,4,8,16,32\}$), `SR` ($W \in \{1,2,4,8,16,32\}$), $k=7$ |
| **Exp 2: Packet Loss** | `202` | `exp2_loss{0,5,10}.scn` | `SW` ($W=1$), `GBN` ($W=16$), `SR` ($W=16$), $k=7$ |
| **Exp 3: Propagation Delay** | `303` | `exp3_delay{20,50,100,200}.scn` ($j=10\%$) | `SW` ($W=1$), `SR` ($W=16$), `SR` ($W=64$), $k=7$ |
| **Exp 4: Impairments** | `404` | `exp4_{corrupt5,reorder5,duplicate5,ackloss5}.scn` | `SW` ($W=1$), `GBN` ($W=16$), `SR` ($W=16$), $k=7$ |
| **Exp 5: Seq-Num Limits** | `505` | `exp5_gbn_k{3,4}.scn`, `exp5_sr_k{3,4}.scn` | `GBN` $k=3$ ($W=1..8$), $k=4$ ($W=1..16$); `SR` $k=3$ ($W=1..5$), $k=4$ ($W=1..9$) |

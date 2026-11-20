#!/usr/bin/env python3
"""Compute project metrics from a run directory's logs and metadata."""

import json
import os
import shlex
import statistics
import sys
from collections import Counter
from typing import Any, Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from logparse import parse_emulator_log, parse_endpoint_log, parse_scenario


def _extract_effective_link_rate(scn_params: Dict[str, float], params_json: Dict[str, Any]) -> float:
    rate = float(scn_params.get("link_rate_s2r_bps", scn_params.get("link_rate_bps", 0.0)))
    extra = params_json.get("emu_extra_args") or ""
    if extra:
        tokens = shlex.split(extra)
        for i, tok in enumerate(tokens):
            if tok == "--linkRate" and i + 1 < len(tokens):
                rate = float(tokens[i + 1])
    return rate


def compute_run_metrics(run_dir: str) -> Dict[str, Any]:
    with open(os.path.join(run_dir, "params.json"), "r") as f:
        params = json.load(f)
    with open(os.path.join(run_dir, "sha256.json"), "r") as f:
        sha_info = json.load(f)

    scn_path = os.path.join(run_dir, "scenario.scn")
    scn_params = parse_scenario(scn_path) if os.path.exists(scn_path) else {}

    sender_events = parse_endpoint_log(os.path.join(run_dir, "sender.csv"))
    recv_events = parse_endpoint_log(os.path.join(run_dir, "receiver.csv"))
    emu_events = parse_emulator_log(os.path.join(run_dir, "emulator.csv"))

    send_data = [e for e in sender_events if e.event == "SEND_DATA"]
    if not send_data:
        raise ValueError(f"No SEND_DATA events found in {run_dir}/sender.csv")

    t_start_us = send_data[0].time_us

    first_fin_idx: Optional[int] = None
    for idx, ev in enumerate(sender_events):
        if ev.event == "SEND_FIN":
            first_fin_idx = idx
            break

    ack_search_slice = sender_events[:first_fin_idx] if first_fin_idx is not None else sender_events
    data_acks = [e for e in ack_search_slice if e.event == "ACK_RECV"]
    if not data_acks:
        data_acks = [e for e in sender_events if e.event == "ACK_RECV"]
    if not data_acks:
        raise ValueError(f"No ACK_RECV events found in {run_dir}/sender.csv")

    t_end_us = data_acks[-1].time_us
    completion_time_us = max(1, t_end_us - t_start_us)
    completion_time_s = completion_time_us / 1e6

    file_size_bytes = int(params["input_size_bytes"])
    goodput_Bps = file_size_bytes / completion_time_s
    goodput_bps = goodput_Bps * 8.0

    new_data_bytes = sum(e.bytes for e in send_data if e.is_retransmission == 0)
    retrans_data_bytes = sum(e.bytes for e in send_data if e.is_retransmission != 0)
    total_data_bytes = new_data_bytes + retrans_data_bytes
    retrans_overhead = (retrans_data_bytes / total_data_bytes) if total_data_bytes > 0 else 0.0

    link_rate_bps = _extract_effective_link_rate(scn_params, params)
    link_utilization: Optional[float] = None
    if link_rate_bps > 0.0:
        tx_new_time_s = (new_data_bytes * 8.0) / link_rate_bps
        link_utilization = tx_new_time_s / completion_time_s

    rtt_samples = [e for e in sender_events if e.event == "RTT_SAMPLE"]
    timeouts = [e for e in sender_events if e.event == "TIMEOUT"]
    rtt_vals_ms = [e.rtt_us / 1000.0 for e in rtt_samples if e.rtt_us >= 0]
    srtt_vals_ms = [e.srtt_us / 1000.0 for e in rtt_samples if e.srtt_us >= 0]
    rto_vals_ms = [e.rto_us / 1000.0 for e in rtt_samples if e.rto_us >= 0]

    def _stats(vals: List[float]) -> Dict[str, Optional[float]]:
        if not vals:
            return {"count": 0, "min": None, "mean": None, "max": None}
        return {
            "count": len(vals),
            "min": min(vals),
            "mean": statistics.mean(vals),
            "max": max(vals),
        }

    emu_counts = Counter(e.event for e in emu_events)
    recv_counts = Counter(e.event for e in recv_events)

    s2r_delays_ms = [
        int(e.detail_map["delay_us"]) / 1000.0
        for e in emu_events
        if e.event == "DELIVER" and e.dir == "S2R" and "delay_us" in e.detail_map
    ]
    r2s_delays_ms = [
        int(e.detail_map["delay_us"]) / 1000.0
        for e in emu_events
        if e.event == "DELIVER" and e.dir == "R2S" and "delay_us" in e.detail_map
    ]

    metrics = {
        "run_dir": os.path.abspath(run_dir),
        "protocol": params["protocol"],
        "window_size": params["window_size"],
        "k": params["k"],
        "seed": params["seed"],
        "file_size_bytes": file_size_bytes,
        "completion_time_us": completion_time_us,
        "completion_time_s": completion_time_s,
        "goodput_Bps": goodput_Bps,
        "goodput_kbps": goodput_bps / 1000.0,
        "new_data_packets": sum(1 for e in send_data if e.is_retransmission == 0),
        "retrans_data_packets": sum(1 for e in send_data if e.is_retransmission != 0),
        "new_data_bytes": new_data_bytes,
        "retrans_data_bytes": retrans_data_bytes,
        "total_data_bytes": total_data_bytes,
        "retransmission_overhead": retrans_overhead,
        "link_rate_bps": link_rate_bps,
        "link_utilization": link_utilization,
        "integrity_match": bool(sha_info.get("match", False)),
        "input_sha256": sha_info.get("input_sha256"),
        "received_sha256": sha_info.get("received_sha256"),
        "timeout_count": len(timeouts),
        "rtt_ms": _stats(rtt_vals_ms),
        "srtt_ms": _stats(srtt_vals_ms),
        "rto_ms": _stats(rto_vals_ms),
        "receiver_event_counts": dict(recv_counts),
        "emulator_event_counts": dict(emu_counts),
        "emulator_s2r_delay_ms": _stats(s2r_delays_ms),
        "emulator_r2s_delay_ms": _stats(r2s_delays_ms),
    }

    with open(os.path.join(run_dir, "metrics.json"), "w") as f:
        json.dump(metrics, f, indent=2)
        f.write("\n")

    return metrics


def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <run_dir> [run_dir ...]", file=sys.stderr)
        sys.exit(1)

    for run_dir in sys.argv[1:]:
        m = compute_run_metrics(run_dir)
        util_str = f"{m['link_utilization'] * 100:.2f}%" if m["link_utilization"] is not None else "N/A"
        rtt_mean = f"{m['rtt_ms']['mean']:.2f} ms" if m["rtt_ms"]["mean"] is not None else "N/A"
        s2r_mean = f"{m['emulator_s2r_delay_ms']['mean']:.2f} ms" if m["emulator_s2r_delay_ms"]["mean"] is not None else "N/A"
        r2s_mean = f"{m['emulator_r2s_delay_ms']['mean']:.2f} ms" if m["emulator_r2s_delay_ms"]["mean"] is not None else "N/A"

        print(f"=== Metrics for {m['run_dir']} ===")
        print(f"  Protocol / Params : {m['protocol']} (W={m['window_size']}, k={m['k']}, seed={m['seed']})")
        print(f"  SHA-256 Integrity : {'MATCH' if m['integrity_match'] else 'MISMATCH'}")
        print(f"  Completion Time   : {m['completion_time_s']:.4f} s ({m['completion_time_us']} us)")
        print(f"  Goodput           : {m['goodput_kbps']:.2f} kbit/s ({m['goodput_Bps']:.1f} B/s)")
        print(f"  Retrans Overhead  : {m['retransmission_overhead'] * 100:.2f}% "
              f"({m['retrans_data_packets']} retrans / {m['new_data_packets']} new pkts)")
        print(f"  Link Utilization  : {util_str}")
        print(f"  RTT Mean / Timeouts: {rtt_mean} / {m['timeout_count']} timeouts")
        print(f"  Emulator Delay    : S2R mean={s2r_mean}, R2S mean={r2s_mean}")
        print(f"  Emulator Events   : {m['emulator_event_counts']}")


if __name__ == "__main__":
    main()

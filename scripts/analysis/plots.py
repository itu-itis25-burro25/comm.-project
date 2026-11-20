#!/usr/bin/env python3
"""Generate experiment summary tables and publication-quality plots from run logs."""

import argparse
import csv
import glob
import json
import os
import re
import sys
from typing import Any, Dict, List, Optional, Tuple

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from logparse import parse_endpoint_log

try:
    import theory  # Optional Student A theoretical curve module
except ImportError:
    theory = None


PROTO_LABELS = {
    "STOP_AND_WAIT": "Stop-and-Wait",
    "GO_BACK_N": "Go-Back-N",
    "SELECTIVE": "Selective Repeat",
}

PROTO_COLORS = {
    "STOP_AND_WAIT": "#1f77b4",
    "GO_BACK_N": "#ff7f0e",
    "SELECTIVE": "#2ca02c",
}

PROTO_MARKERS = {
    "STOP_AND_WAIT": "s",
    "GO_BACK_N": "o",
    "SELECTIVE": "^",
}


def setup_style() -> None:
    plt.rcParams.update({
        "figure.dpi": 150,
        "savefig.dpi": 300,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "grid.linestyle": "--",
        "axes.spines.top": False,
        "axes.spines.right": False,
        "font.size": 10,
        "axes.titlesize": 11,
        "axes.labelsize": 10,
        "legend.fontsize": 9,
    })


def load_all_runs(runs_dir: str) -> Dict[str, List[Dict[str, Any]]]:
    grouped: Dict[str, List[Dict[str, Any]]] = {
        f"exp{i}": [] for i in range(1, 6)
    }
    for exp_idx in range(1, 6):
        exp_name = f"exp{exp_idx}"
        pattern = os.path.join(runs_dir, exp_name, "*", "metrics.json")
        for mpath in sorted(glob.glob(pattern)):
            with open(mpath, "r") as f:
                m = json.load(f)
            m["_exp"] = exp_name
            m["_run_id"] = os.path.basename(os.path.dirname(mpath))
            grouped[exp_name].append(m)
    return grouped


def write_summary_tables(grouped: Dict[str, List[Dict[str, Any]]], out_dir: str) -> None:
    rows: List[Dict[str, Any]] = []
    for exp_name in sorted(grouped.keys()):
        for m in sorted(
            grouped[exp_name],
            key=lambda x: (x["protocol"], x["k"], x["window_size"], x["_run_id"]),
        ):
            util_pct = (m["link_utilization"] * 100.0) if m.get("link_utilization") is not None else None
            rtt_mean = m.get("rtt_ms", {}).get("mean")
            rows.append({
                "experiment": exp_name,
                "run_id": m["_run_id"],
                "protocol": m["protocol"],
                "W": m["window_size"],
                "k": m["k"],
                "seed": m["seed"],
                "completion_time_s": round(m["completion_time_s"], 6),
                "goodput_kbps": round(m["goodput_kbps"], 3),
                "retrans_overhead_pct": round(m["retransmission_overhead"] * 100.0, 3),
                "link_utilization_pct": round(util_pct, 3) if util_pct is not None else "",
                "rtt_mean_ms": round(rtt_mean, 3) if rtt_mean is not None else "",
                "timeouts": m["timeout_count"],
                "sha256_match": m["integrity_match"],
            })

    csv_path = os.path.join(out_dir, "results_summary.csv")
    fieldnames = [
        "experiment", "run_id", "protocol", "W", "k", "seed",
        "completion_time_s", "goodput_kbps", "retrans_overhead_pct",
        "link_utilization_pct", "rtt_mean_ms", "timeouts", "sha256_match",
    ]
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    md_path = os.path.join(out_dir, "results_summary.md")
    with open(md_path, "w") as f:
        f.write("# Experiment Results Summary\n\n")
        if not rows:
            f.write("_No runs found._\n")
            return
        headers = [
            "Exp", "Run ID", "Protocol", "W", "k", "Seed",
            "Time (s)", "Goodput (kbps)", "Retrans (%)",
            "Link Util (%)", "RTT Mean (ms)", "Timeouts", "SHA-256",
        ]
        f.write("| " + " | ".join(headers) + " |\n")
        f.write("|" + "|".join(["---"] * len(headers)) + "|\n")
        for r in rows:
            sha_str = "MATCH" if r["sha256_match"] else "FAIL"
            util_str = f"{r['link_utilization_pct']:.2f}" if r["link_utilization_pct"] != "" else "N/A"
            rtt_str = f"{r['rtt_mean_ms']:.2f}" if r["rtt_mean_ms"] != "" else "N/A"
            vals = [
                r["experiment"], r["run_id"], r["protocol"], str(r["W"]), str(r["k"]), str(r["seed"]),
                f"{r['completion_time_s']:.4f}", f"{r['goodput_kbps']:.2f}", f"{r['retrans_overhead_pct']:.2f}",
                util_str, rtt_str, str(r["timeouts"]), sha_str,
            ]
            f.write("| " + " | ".join(vals) + " |\n")


def plot_exp1(runs: List[Dict[str, Any]], out_dir: str) -> Optional[str]:
    if not runs:
        return None

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8))

    sw_runs = [r for r in runs if r["protocol"] == "STOP_AND_WAIT"]
    if sw_runs:
        sw_util = (sw_runs[0]["link_utilization"] or 0.0) * 100.0
        sw_gp = sw_runs[0]["goodput_kbps"]
        ax1.axhline(sw_util, color=PROTO_COLORS["STOP_AND_WAIT"], linestyle="--",
                    linewidth=1.8, label=f"Stop-and-Wait baseline ({sw_util:.1f}%)")
        ax2.axhline(sw_gp, color=PROTO_COLORS["STOP_AND_WAIT"], linestyle="--",
                    linewidth=1.8, label=f"Stop-and-Wait baseline ({sw_gp:.1f} kbps)")

    for proto in ("GO_BACK_N", "SELECTIVE"):
        pruns = sorted([r for r in runs if r["protocol"] == proto], key=lambda x: x["window_size"])
        if not pruns:
            continue
        ws = [r["window_size"] for r in pruns]
        utils = [(r["link_utilization"] or 0.0) * 100.0 for r in pruns]
        gps = [r["goodput_kbps"] for r in pruns]
        ax1.plot(ws, utils, marker=PROTO_MARKERS[proto], color=PROTO_COLORS[proto],
                 linewidth=2.0, label=f"{PROTO_LABELS[proto]} (measured)")
        ax2.plot(ws, gps, marker=PROTO_MARKERS[proto], color=PROTO_COLORS[proto],
                 linewidth=2.0, label=f"{PROTO_LABELS[proto]} (measured)")

    if theory is not None and hasattr(theory, "exp1_utilization"):
        try:
            w_grid = [1, 2, 4, 8, 16, 32]
            t_vals = [float(theory.exp1_utilization(w)) * 100.0 for w in w_grid]
            ax1.plot(w_grid, t_vals, "k:", linewidth=1.8, label="Theory")
        except Exception:
            pass

    for ax in (ax1, ax2):
        ax.set_xscale("log", base=2)
        ax.set_xticks([1, 2, 4, 8, 16, 32])
        ax.set_xticklabels(["1", "2", "4", "8", "16", "32"])
        ax.set_xlabel("Window Size W")
        ax.legend(loc="best")

    ax1.set_ylabel("Link Utilization (%)")
    ax1.set_title("Exp 1: Ideal Network Utilization vs W")
    ax1.set_ylim(0, 108)

    ax2.set_ylabel("Goodput (kbit/s)")
    ax2.set_title("Exp 1: Ideal Network Goodput vs W")

    fig.tight_layout()
    out_path = os.path.join(out_dir, "exp1_ideal_utilization_goodput.png")
    fig.savefig(out_path)
    plt.close(fig)
    return out_path


def _parse_loss_pct(run: Dict[str, Any]) -> Optional[int]:
    m = re.search(r"loss(\d+)", run["_run_id"])
    return int(m.group(1)) if m else None


def plot_exp2(runs: List[Dict[str, Any]], out_dir: str) -> Optional[str]:
    if not runs:
        return None

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8))

    for proto in ("STOP_AND_WAIT", "GO_BACK_N", "SELECTIVE"):
        pruns = []
        for r in runs:
            if r["protocol"] == proto:
                lp = _parse_loss_pct(r)
                if lp is not None:
                    pruns.append((lp, r))
        pruns.sort(key=lambda x: x[0])
        if not pruns:
            continue
        xs = [x[0] for x in pruns]
        gps = [x[1]["goodput_kbps"] for x in pruns]
        ohs = [x[1]["retransmission_overhead"] * 100.0 for x in pruns]
        ax1.plot(xs, gps, marker=PROTO_MARKERS[proto], color=PROTO_COLORS[proto],
                 linewidth=2.0, label=PROTO_LABELS[proto])
        ax2.plot(xs, ohs, marker=PROTO_MARKERS[proto], color=PROTO_COLORS[proto],
                 linewidth=2.0, label=PROTO_LABELS[proto])

        if theory is not None and hasattr(theory, "exp2_retrans_overhead"):
            try:
                t_oh = [float(theory.exp2_retrans_overhead(proto, p / 100.0)) * 100.0 for p in xs]
                ax2.plot(xs, t_oh, linestyle=":", color=PROTO_COLORS[proto],
                         linewidth=1.5, label=f"{PROTO_LABELS[proto]} (theory)")
            except Exception:
                pass

    for ax in (ax1, ax2):
        ax.set_xticks([0, 5, 10])
        ax.set_xlabel("Data Packet Loss Probability (%)")
        ax.legend(loc="best")

    ax1.set_ylabel("Goodput (kbit/s)")
    ax1.set_title("Exp 2: Goodput vs Data Loss")

    ax2.set_ylabel("Retransmission Overhead (%)")
    ax2.set_title("Exp 2: Retransmission Overhead vs Data Loss")

    fig.tight_layout()
    out_path = os.path.join(out_dir, "exp2_packet_loss.png")
    fig.savefig(out_path)
    plt.close(fig)
    return out_path


def _parse_delay_ms(run: Dict[str, Any]) -> Optional[int]:
    m = re.search(r"_d(\d+)", run["_run_id"])
    return int(m.group(1)) if m else None


def plot_exp3(runs: List[Dict[str, Any]], out_dir: str) -> Tuple[Optional[str], Optional[str]]:
    if not runs:
        return None, None

    # 1. Link utilization & Goodput vs propagation delay
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8))
    series_defs = [
        ("STOP_AND_WAIT", 1, "Stop-and-Wait (W=1)", "#1f77b4", "s"),
        ("SELECTIVE", 16, "Selective Repeat (W=16)", "#2ca02c", "^"),
        ("SELECTIVE", 64, "Selective Repeat (W=64)", "#9467bd", "D"),
    ]
    for proto, win, label, color, marker in series_defs:
        pts = []
        for r in runs:
            if r["protocol"] == proto and r["window_size"] == win:
                d = _parse_delay_ms(r)
                if d is not None:
                    pts.append((d, r))
        pts.sort(key=lambda x: x[0])
        if not pts:
            continue
        ds = [p[0] for p in pts]
        utils = [(p[1]["link_utilization"] or 0.0) * 100.0 for p in pts]
        gps = [p[1]["goodput_kbps"] for p in pts]
        ax1.plot(ds, utils, marker=marker, color=color, linewidth=2.0, label=label)
        ax2.plot(ds, gps, marker=marker, color=color, linewidth=2.0, label=label)

        if theory is not None and hasattr(theory, "exp3_utilization"):
            try:
                t_u = [float(theory.exp3_utilization(proto, win, d)) * 100.0 for d in ds]
                ax1.plot(ds, t_u, linestyle=":", color=color, linewidth=1.5, label=f"{label} (theory)")
            except Exception:
                pass

    for ax in (ax1, ax2):
        ax.set_xticks([20, 50, 100, 200])
        ax.set_xlabel("One-Way Propagation Delay (ms)")
        ax.legend(loc="best")

    ax1.set_ylabel("Link Utilization (%)")
    ax1.set_title("Exp 3: Link Utilization vs Propagation Delay (j=10%)")
    ax1.set_ylim(0, 108)

    ax2.set_ylabel("Goodput (kbit/s)")
    ax2.set_title("Exp 3: Goodput vs Propagation Delay (j=10%)")

    fig.tight_layout()
    summary_plot = os.path.join(out_dir, "exp3_propagation_delay.png")
    fig.savefig(summary_plot)
    plt.close(fig)

    # 2. RTT & RTO time-series tracking plot
    delays = sorted({d for d in (_parse_delay_ms(r) for r in runs) if d is not None})
    selected_runs: List[Tuple[int, Dict[str, Any]]] = []
    for d in delays:
        candidates = [r for r in runs if _parse_delay_ms(r) == d]
        # Prefer SR W=16, then SW W=1, then SR W=64
        candidates.sort(key=lambda r: (
            0 if (r["protocol"] == "SELECTIVE" and r["window_size"] == 16) else
            1 if r["protocol"] == "STOP_AND_WAIT" else 2
        ))
        if candidates:
            selected_runs.append((d, candidates[0]))

    rtt_plot = None
    if selected_runs:
        n = len(selected_runs)
        ncols = 2 if n > 1 else 1
        nrows = (n + ncols - 1) // ncols
        fig2, axes = plt.subplots(nrows, ncols, figsize=(6.2 * ncols, 4.0 * nrows), squeeze=False)
        for idx, (d, r) in enumerate(selected_runs):
            ax = axes[idx // ncols][idx % ncols]
            sender_csv = os.path.join(r["run_dir"], "sender.csv")
            if not os.path.exists(sender_csv):
                ax.set_title(f"Delay {d} ms (no sender.csv)")
                continue
            events = parse_endpoint_log(sender_csv)
            send_ev = [e for e in events if e.event == "SEND_DATA"]
            t0 = send_ev[0].time_us if send_ev else events[0].time_us
            samples = [e for e in events if e.event == "RTT_SAMPLE" and e.rtt_us >= 0]
            if not samples:
                ax.set_title(f"Delay {d} ms (no RTT samples)")
                continue
            ts = [(e.time_us - t0) / 1e6 for e in samples]
            rtts = [e.rtt_us / 1000.0 for e in samples]
            srtts = [e.srtt_us / 1000.0 for e in samples]
            rtos = [e.rto_us / 1000.0 for e in samples]

            ax.plot(ts, rtts, color="#1f77b4", alpha=0.65, linewidth=1.2, marker=".", markersize=4, label="Measured RTT")
            ax.plot(ts, srtts, color="#2ca02c", linewidth=2.0, label="SRTT")
            ax.plot(ts, rtos, color="#d62728", linestyle="--", linewidth=1.8, label="RTO")

            timeouts = [e for e in events if e.event == "TIMEOUT" and e.rto_us >= 0]
            if timeouts:
                to_t = [(e.time_us - t0) / 1e6 for e in timeouts]
                to_v = [e.rto_us / 1000.0 for e in timeouts]
                ax.scatter(to_t, to_v, color="black", marker="x", s=40, zorder=5, label="TIMEOUT")

            ax.set_title(f"Delay = {d} ms, j = {d * 0.1:.1f} ms ({PROTO_LABELS[r['protocol']]}, W={r['window_size']})")
            ax.set_xlabel("Elapsed Time (s)")
            ax.set_ylabel("Time (ms)")
            ax.legend(loc="best", fontsize=8)

        for idx in range(len(selected_runs), nrows * ncols):
            axes[idx // ncols][idx % ncols].axis("off")

        fig2.tight_layout()
        rtt_plot = os.path.join(out_dir, "exp3_rtt_rto_tracking.png")
        fig2.savefig(rtt_plot)
        plt.close(fig2)

    return summary_plot, rtt_plot


def plot_exp4(runs: List[Dict[str, Any]], out_dir: str) -> Optional[str]:
    if not runs:
        return None

    imp_order = [
        ("corrupt5", "Corruption\n5%"),
        ("reorder5", "Reordering\n5%"),
        ("duplicate5", "Duplication\n5%"),
        ("ackloss5", "ACK Loss\n5%"),
    ]
    present_protos = [p for p in ("STOP_AND_WAIT", "GO_BACK_N", "SELECTIVE")
                      if any(r["protocol"] == p for r in runs)]
    if not present_protos:
        return None

    lookup: Dict[Tuple[str, str], Dict[str, Any]] = {}
    for r in runs:
        for imp_key, _ in imp_order:
            if r["_run_id"].endswith(f"_{imp_key}"):
                lookup[(r["protocol"], imp_key)] = r

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.8))
    x_positions = list(range(len(imp_order)))
    n_p = len(present_protos)
    width = 0.72 / n_p

    for p_idx, proto in enumerate(present_protos):
        offsets = [x + (p_idx - (n_p - 1) / 2.0) * width for x in x_positions]
        gps = []
        ohs = []
        matches = []
        for imp_key, _ in imp_order:
            r = lookup.get((proto, imp_key))
            gps.append(r["goodput_kbps"] if r else 0.0)
            ohs.append((r["retransmission_overhead"] * 100.0) if r else 0.0)
            matches.append(r["integrity_match"] if r else False)

        bars1 = ax1.bar(offsets, gps, width=width * 0.92, color=PROTO_COLORS[proto], label=PROTO_LABELS[proto])
        ax2.bar(offsets, ohs, width=width * 0.92, color=PROTO_COLORS[proto], label=PROTO_LABELS[proto])

        for bar, ok in zip(bars1, matches):
            h = bar.get_height()
            if h > 0:
                ax1.text(bar.get_x() + bar.get_width() / 2.0, h,
                         "OK" if ok else "FAIL", ha="center", va="bottom",
                         fontsize=7, fontweight="bold",
                         color="#1b5e20" if ok else "#b71c1c")

    labels = [lbl for _, lbl in imp_order]
    for ax in (ax1, ax2):
        ax.set_xticks(x_positions)
        ax.set_xticklabels(labels)
        ax.legend(loc="best")

    ax1.set_ylabel("Goodput (kbit/s)")
    ax1.set_title("Exp 4: Goodput & SHA-256 Integrity Under Impairments")

    ax2.set_ylabel("Retransmission Overhead (%)")
    ax2.set_title("Exp 4: Retransmission Overhead Under Impairments")

    fig.tight_layout()
    out_path = os.path.join(out_dir, "exp4_impairments.png")
    fig.savefig(out_path)
    plt.close(fig)
    return out_path


def plot_exp5(runs: List[Dict[str, Any]], out_dir: str) -> Optional[str]:
    if not runs:
        return None

    groups: Dict[Tuple[str, int], List[Dict[str, Any]]] = {}
    for r in runs:
        groups.setdefault((r["protocol"], int(r["k"])), []).append(r)

    ordered_keys = sorted(groups.keys(), key=lambda pk: (0 if pk[0] == "GO_BACK_N" else 1, pk[1]))
    if not ordered_keys:
        return None

    fig, axes = plt.subplots(len(ordered_keys), 1, figsize=(11, 2.2 * len(ordered_keys) + 0.8), squeeze=False)
    for row_idx, (proto, k_val) in enumerate(ordered_keys):
        ax = axes[row_idx][0]
        gruns = sorted(groups[(proto, k_val)], key=lambda r: r["window_size"])
        ws = [int(r["window_size"]) for r in gruns]
        ok_flags = [bool(r["integrity_match"]) for r in gruns]
        w_limit = (2 ** k_val - 1) if proto == "GO_BACK_N" else (2 ** (k_val - 1))

        for w, ok in zip(ws, ok_flags):
            color = "#2ca02c" if ok else "#d62728"
            rect = mpatches.FancyBboxPatch(
                (w - 0.4, 0.15), 0.8, 0.7,
                boxstyle="round,pad=0.04", facecolor=color, edgecolor="black", linewidth=0.8
            )
            ax.add_patch(rect)
            ax.text(w, 0.5, "OK" if ok else "FAIL", ha="center", va="center",
                    color="white", fontweight="bold", fontsize=8.5)

        ax.axvline(w_limit + 0.5, color="black", linestyle="--", linewidth=1.8)
        ax.text(w_limit + 0.55, 0.92, f"Theoretical max W={w_limit}",
                va="top", ha="left" if w_limit + 0.5 < max(ws) else "right",
                fontsize=8.5, fontweight="bold")

        ax.set_xlim(min(ws) - 0.7, max(ws) + 1.5)
        ax.set_ylim(0, 1.05)
        ax.set_xticks(ws)
        ax.set_yticks([])
        ax.set_title(f"{PROTO_LABELS.get(proto, proto)}, k={k_val} (2^k={2**k_val}, theoretical limit W={w_limit})",
                     loc="left", fontsize=10)
        if row_idx == len(ordered_keys) - 1:
            ax.set_xlabel("Window Size W")

    pass_patch = mpatches.Patch(color="#2ca02c", label="SHA-256 MATCH (Intact)")
    fail_patch = mpatches.Patch(color="#d62728", label="SHA-256 MISMATCH (Corrupted)")
    fig.legend(handles=[pass_patch, fail_patch], loc="upper right", ncol=2)
    fig.suptitle("Exp 5: Sequence-Number Space Window Limits", fontsize=12, y=0.995)
    fig.tight_layout(rect=[0, 0, 1, 0.95])

    out_path = os.path.join(out_dir, "exp5_seqnum_limits.png")
    fig.savefig(out_path)
    plt.close(fig)
    return out_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze run metrics and plot all experiments.")
    parser.add_argument("runs_dir", help="Root runs directory (containing exp1..exp5 subdirs)")
    parser.add_argument("--out-dir", default=None, help="Output directory for plots and tables")
    args = parser.parse_args()

    runs_dir = os.path.abspath(args.runs_dir)
    out_dir = os.path.abspath(args.out_dir) if args.out_dir else os.path.join(runs_dir, "plots")
    os.makedirs(out_dir, exist_ok=True)

    setup_style()
    grouped = load_all_runs(runs_dir)
    write_summary_tables(grouped, out_dir)

    generated = [
        os.path.join(out_dir, "results_summary.csv"),
        os.path.join(out_dir, "results_summary.md"),
    ]
    for p in [
        plot_exp1(grouped["exp1"], out_dir),
        plot_exp2(grouped["exp2"], out_dir),
        *plot_exp3(grouped["exp3"], out_dir),
        plot_exp4(grouped["exp4"], out_dir),
        plot_exp5(grouped["exp5"], out_dir),
    ]:
        if p:
            generated.append(p)

    print(f"Generated {len(generated)} analysis outputs in {out_dir}:")
    for p in generated:
        print(f"  - {p}")


if __name__ == "__main__":
    main()

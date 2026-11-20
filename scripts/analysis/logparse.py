#!/usr/bin/env python3
"""Log and scenario parsers for BLG337E Project 1."""

import csv
from dataclasses import dataclass, field
from typing import Dict, List


@dataclass
class EndpointEvent:
    time_us: int
    event: str
    seq: int
    bytes: int
    is_retransmission: int
    rtt_us: int
    srtt_us: int
    rto_us: int


@dataclass
class EmulatorEvent:
    time_us: int
    event: str
    dir: str
    pkt_idx: int
    pkt_type: str
    seq: int
    bytes: int
    detail: str
    detail_map: Dict[str, str] = field(default_factory=dict)


def _normalize_endpoint_event(name: str) -> str:
    if name == "AKC_STALE":
        return "ACK_STALE"
    return name


def parse_endpoint_log(path: str) -> List[EndpointEvent]:
    events: List[EndpointEvent] = []
    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            events.append(
                EndpointEvent(
                    time_us=int(row["time_us"]),
                    event=_normalize_endpoint_event(row["event"].strip()),
                    seq=int(row["seq"]),
                    bytes=int(row["bytes"]),
                    is_retransmission=int(row["is_retransmission"]),
                    rtt_us=int(row["rtt_us"]),
                    srtt_us=int(row["srtt_us"]),
                    rto_us=int(row["rto_us"]),
                )
            )
    return events


def _parse_detail(detail: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    if not detail:
        return out
    for part in detail.split(";"):
        part = part.strip()
        if "=" in part:
            k, v = part.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def parse_emulator_log(path: str) -> List[EmulatorEvent]:
    events: List[EmulatorEvent] = []
    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            detail = (row.get("detail") or "").strip()
            events.append(
                EmulatorEvent(
                    time_us=int(row["time_us"]),
                    event=row["event"].strip(),
                    dir=row["dir"].strip(),
                    pkt_idx=int(row["pkt_idx"]),
                    pkt_type=row["pkt_type"].strip(),
                    seq=int(row["seq"]),
                    bytes=int(row["bytes"]),
                    detail=detail,
                    detail_map=_parse_detail(detail),
                )
            )
    return events


def parse_scenario(path: str) -> Dict[str, float]:
    params: Dict[str, float] = {
        "link_rate_bps": 1000000.0,
        "queue_capacity": 128.0,
        "prop_delay_ms": 20.0,
        "jitter_ms": 0.0,
        "data_loss": 0.0,
        "ack_loss": 0.0,
        "corrupt": 0.0,
        "duplicate": 0.0,
        "reorder": 0.0,
        "reorder_timeout_ms": 50.0,
    }
    with open(path, "r") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line or line.startswith("action"):
                continue
            line = line.replace("=", " ")
            parts = line.split()
            if len(parts) == 2:
                key, val = parts[0], parts[1]
                try:
                    params[key] = float(val)
                except ValueError:
                    pass
    return params

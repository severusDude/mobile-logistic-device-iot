#!/usr/bin/env python3
"""Analyze ESP32 manual-run serial logs.

Reads JSONL log.v1 events and legacy human serial lines. Handles simulator output
where multiple serial fragments are concatenated onto one physical line.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


JSON_MARKER = '{"log_type":"iot_device"'


@dataclass
class Analysis:
    files: list[str] = field(default_factory=list)
    json_events: list[dict[str, Any]] = field(default_factory=list)
    legacy_events: list[dict[str, Any]] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    @property
    def events(self) -> list[dict[str, Any]]:
        return self.json_events + self.legacy_events


def read_inputs(paths: list[str]) -> tuple[list[str], str]:
    if not paths:
        paths = ["-"]

    labels: list[str] = []
    chunks: list[str] = []
    for raw in paths:
        if raw == "-":
            labels.append("stdin")
            chunks.append(sys.stdin.read())
            continue
        path = Path(raw)
        labels.append(str(path))
        chunks.append(path.read_text(encoding="utf-8", errors="replace"))
    return labels, "\n".join(chunks)


def extract_json_events(text: str, warnings: list[str]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    pos = 0
    while True:
        start = text.find(JSON_MARKER, pos)
        if start < 0:
            break
        depth = 0
        in_string = False
        escaped = False
        end = None
        for idx in range(start, len(text)):
            ch = text[idx]
            if escaped:
                escaped = False
                continue
            if ch == "\\" and in_string:
                escaped = True
                continue
            if ch == '"':
                in_string = not in_string
                continue
            if in_string:
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    end = idx + 1
                    break
        if end is None:
            warnings.append(f"Incomplete JSON event near offset {start}.")
            break
        raw = text[start:end]
        try:
            data = json.loads(raw)
        except json.JSONDecodeError as exc:
            warnings.append(f"Bad JSON event near offset {start}: {exc}")
        else:
            if data.get("log_type") == "iot_device":
                data.setdefault("_source", "jsonl")
                events.append(data)
        pos = end
    return events


LEGACY_PATTERNS: list[tuple[str, re.Pattern[str], Any]] = [
    ("setup_step", re.compile(r"\[MOBILE\] Setup complete"), lambda m: {"step": "setup_complete"}),
    ("setup_step", re.compile(r"\[GPS\] UART2 initialised"), lambda m: {"step": "gps_uart_ready"}),
    ("setup_step", re.compile(r"\[RFID\] PN532 found\. Chip: (?P<chip>0x[0-9A-Fa-f]+)"), lambda m: {"step": "rfid_pn532", "status": "ready", "chip": m.group("chip")}),
    ("setup_step", re.compile(r"\[RFID\] ERROR: (?P<message>PN532 not found[^[]*)"), lambda m: {"level": "ERROR", "step": "rfid_pn532", "status": "not_found", "message": m.group("message").strip()}),
    ("wifi_status", re.compile(r"\[WiFi\] Connected\. IP: (?P<ip>[0-9.]+)"), lambda m: {"status": "connected", "ip": m.group("ip")}),
    ("wifi_status", re.compile(r"\[WiFi\] Connection failed"), lambda m: {"level": "WARN", "status": "failed"}),
    ("mqtt_status", re.compile(r"\[MQTT\] Connected"), lambda m: {"status": "connected"}),
    ("mqtt_status", re.compile(r"\[MQTT\] Subscribed to (?P<topic>\S+)"), lambda m: {"status": "subscribed", "topic": m.group("topic")}),
    ("mqtt_status", re.compile(r"\[MQTT\] Failed\. rc=(?P<rc>-?\d+)"), lambda m: {"level": "WARN", "status": "failed", "rc": int(m.group("rc"))}),
    ("heartbeat", re.compile(r"\[HB\] Heartbeat published\. Uptime: (?P<uptime>\d+)s"), lambda m: {"uptime_sec": int(m.group("uptime")), "published": True}),
    ("telemetry", re.compile(r"\[TEL\] #(?P<seq>\d+) lat=(?P<lat>-?\d+(?:\.\d+)?) lng=(?P<lng>-?\d+(?:\.\d+)?) spd=(?P<speed>-?\d+(?:\.\d+)?)km/h fix=(?P<fix>YES|NO)"), lambda m: {"telemetry_seq": int(m.group("seq")), "lat": float(m.group("lat")), "lng": float(m.group("lng")), "speed_kmh": float(m.group("speed")), "gps_fix": m.group("fix") == "YES", "published": True}),
    ("telemetry", re.compile(r"\[TEL\] Buffered"), lambda m: {"published": False}),
    ("scan_publish", re.compile(r"\[SCAN\] Published: epc=(?P<epc>[^\s\[]+) ctx=(?P<context>[A-Za-z_]+)"), lambda m: {"epc": m.group("epc"), "scan_context": m.group("context"), "published": True}),
    ("scan_publish", re.compile(r"\[SCAN\] Buffered"), lambda m: {"published": False}),
    ("rfid_scan", re.compile(r"\[RFID\] Tag scanned: (?P<epc>[^\s\[]+) \| uid=(?P<uid>[0-9A-Fa-f]+) \| ctx=(?P<context>[A-Za-z_]+) \| active=(?P<active>\d+) \| scans_today=(?P<scans>\d+)"), lambda m: {"epc": m.group("epc"), "uid": m.group("uid"), "scan_context": m.group("context"), "active_package_count": int(m.group("active")), "scans_today": int(m.group("scans"))}),
    ("rfid_cooldown", re.compile(r"\[RFID\] Tag (?P<epc>[^\s\[]+) in cooldown - suppressed"), lambda m: {"epc": m.group("epc"), "suppressed": True}),
    ("scenario_result", re.compile(r"\[SCENARIO\] EPC=(?P<epc>[^\s\[]+)"), lambda m: {"field": "epc", "epc": m.group("epc")}),
    ("scenario_result", re.compile(r"\[SCENARIO\] CTX=(?P<context>[A-Za-z_]+)"), lambda m: {"field": "context", "scan_context": m.group("context")}),
    ("scenario_result", re.compile(r"\[SCENARIO\] ACTIVE=(?P<active>\d+)"), lambda m: {"field": "active", "active_package_count": int(m.group("active"))}),
    ("scenario_command", re.compile(r"\[SCENARIO\] Next RFID scan context armed: (?P<context>[A-Za-z_]+)"), lambda m: {"action": "armed", "scan_context": m.group("context")}),
    ("scenario_command", re.compile(r"\[SCENARIO\] Scenario state reset"), lambda m: {"action": "reset"}),
    ("buffer_event", re.compile(r"\[BUF\] Buffered event\. Buffer size: (?P<size>\d+)"), lambda m: {"action": "buffered", "size": int(m.group("size"))}),
    ("buffer_event", re.compile(r"\[BUF\] Buffer full"), lambda m: {"level": "WARN", "action": "drop_oldest"}),
]


def parse_legacy_events(text: str) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for event, pattern, factory in LEGACY_PATTERNS:
        for match in pattern.finditer(text):
            data = factory(match)
            data.setdefault("level", "INFO")
            data["event"] = event
            data["_source"] = "legacy"
            data["_offset"] = match.start()
            events.append(data)
    events.sort(key=lambda item: item.get("_offset", 0))
    return events


def uniq(values: Iterable[Any]) -> list[Any]:
    seen = set()
    out = []
    for value in values:
        if value in (None, "") or value in seen:
            continue
        seen.add(value)
        out.append(value)
    return out


def count(events: list[dict[str, Any]], name: str) -> int:
    return sum(1 for event in events if event.get("event") == name)


def flag(events: list[dict[str, Any]], name: str, **criteria: Any) -> bool:
    for event in events:
        if event.get("event") != name:
            continue
        if all(event.get(key) == value for key, value in criteria.items()):
            return True
    return False


def render_markdown(analysis: Analysis) -> str:
    events = analysis.events
    telemetry = [e for e in events if e.get("event") == "telemetry"]
    scans = [e for e in events if e.get("event") in ("rfid_scan", "scan_publish")]
    cooldowns = [e for e in events if e.get("event") == "rfid_cooldown"]
    scenario = [e for e in events if e.get("event") in ("scenario_command", "scenario_result")]
    warning_events = [e for e in events if e.get("level") in ("WARN", "ERROR")]

    wifi_ok = flag(events, "wifi_status", status="connected")
    mqtt_ok = flag(events, "mqtt_status", status="connected")
    heartbeat_ok = count(events, "heartbeat") > 0
    setup_ok = any(e.get("event") == "setup_step" and e.get("step") == "setup_complete" for e in events)
    gps_ok = any(e.get("event") == "setup_step" and e.get("step") == "gps_uart_ready" for e in events)
    rfid_ok = any(e.get("event") == "setup_step" and e.get("step") == "rfid_pn532" and e.get("status") == "ready" for e in events)

    lines = ["# Log Analysis", ""]
    lines.append("## Files")
    for item in analysis.files:
        lines.append(f"- `{item}`")
    lines.extend(["", "## Summary"])
    lines.append(f"- JSONL events: {len(analysis.json_events)}")
    lines.append(f"- Legacy events: {len(analysis.legacy_events)}")
    lines.append(f"- Total parsed events: {len(events)}")
    lines.append(f"- WiFi connected: {'yes' if wifi_ok else 'no'}")
    lines.append(f"- MQTT connected: {'yes' if mqtt_ok else 'no'}")
    lines.append(f"- Heartbeats: {count(events, 'heartbeat')}")
    lines.append(f"- Telemetry events: {len(telemetry)}")
    lines.append(f"- Scan/cooldown events: {len(scans)}/{len(cooldowns)}")

    lines.extend(["", "## Connectivity"])
    lines.append(f"- Setup complete: {'yes' if setup_ok else 'no'}")
    lines.append(f"- GPS UART init: {'yes' if gps_ok else 'no'}")
    lines.append(f"- RFID PN532 ready: {'yes' if rfid_ok else 'no'}")
    lines.append(f"- WiFi connected: {'yes' if wifi_ok else 'no'}")
    lines.append(f"- MQTT connected: {'yes' if mqtt_ok else 'no'}")

    lines.extend(["", "## Telemetry"])
    if telemetry:
        seqs = [e.get("telemetry_seq") for e in telemetry if isinstance(e.get("telemetry_seq"), int)]
        fixes = sum(1 for e in telemetry if e.get("gps_fix") is True)
        if seqs:
            lines.append(f"- Sequence range: {min(seqs)}..{max(seqs)}")
        lines.append(f"- GPS fix events: {fixes}/{len(telemetry)}")
        last = telemetry[-1]
        if "lat" in last and "lng" in last:
            lines.append(f"- Last position: {last.get('lat')}, {last.get('lng')}")
    else:
        lines.append("- None parsed.")

    lines.extend(["", "## RFID / Scans"])
    epcs = uniq(e.get("epc") for e in scans + cooldowns)
    contexts = uniq(e.get("scan_context") for e in scans + scenario)
    lines.append(f"- EPCs: {', '.join(epcs) if epcs else 'none'}")
    lines.append(f"- Contexts: {', '.join(contexts) if contexts else 'none'}")
    lines.append(f"- Published scans: {sum(1 for e in scans if e.get('published') is True)}")
    lines.append(f"- Buffered scans: {sum(1 for e in scans if e.get('published') is False)}")
    lines.append(f"- Cooldown suppressions: {len(cooldowns)}")

    lines.extend(["", "## Scenario Evidence"])
    if scenario:
        for item in scenario[-12:]:
            detail = []
            for key in ("action", "epc", "scan_context", "active_package_count", "command", "message"):
                if key in item:
                    detail.append(f"{key}={item[key]}")
            lines.append(f"- {item.get('event')}: {', '.join(detail) if detail else item.get('field', 'seen')}")
    else:
        lines.append("- None parsed.")

    lines.extend(["", "## Warnings / Errors"])
    if warning_events or analysis.warnings:
        for item in warning_events[:20]:
            detail = item.get("message") or item.get("status") or item.get("action") or item.get("error") or ""
            lines.append(f"- {item.get('level')}: {item.get('event')} {detail}".rstrip())
        for warning in analysis.warnings:
            lines.append(f"- PARSER: {warning}")
    else:
        lines.append("- None parsed.")

    missing = []
    checks = [
        ("setup_complete", setup_ok),
        ("gps_uart_ready", gps_ok),
        ("rfid_pn532_ready", rfid_ok),
        ("wifi_connected", wifi_ok),
        ("mqtt_connected", mqtt_ok),
        ("heartbeat", heartbeat_ok),
        ("telemetry", bool(telemetry)),
    ]
    if scenario and not scans:
        checks.append(("scan_after_scenario_command", False))
    for name, ok in checks:
        if not ok:
            missing.append(name)

    lines.extend(["", "## Missing Expected Milestones"])
    if missing:
        for name in missing:
            lines.append(f"- {name}")
    else:
        lines.append("- None.")

    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Analyze IoT device serial logs.")
    parser.add_argument("paths", nargs="*", help="Log file paths, or '-' for stdin.")
    args = parser.parse_args(argv)

    labels, text = read_inputs(args.paths)
    analysis = Analysis(files=labels)
    analysis.json_events = extract_json_events(text, analysis.warnings)
    analysis.legacy_events = parse_legacy_events(text)
    sys.stdout.write(render_markdown(analysis))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

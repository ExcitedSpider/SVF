#!/usr/bin/env python3
"""Run `ae -ae-sparsity=semi-sparse` on every case in cases.csv and summarise the alarms.

Usage:
    run_ae.py OUT_DIR [--bitcode-dir DIR] [--ae PATH] [--timeout SEC] [--only CASE ...]

The bitcode directory (default: bitcode/ next to this script) must contain
the files named in the `bitcode` column of cases.csv; each one is checked
against its SHA-256 before it is analysed. For every case the script writes
OUT_DIR/<case>.log (full ae output) and prints one Markdown table row. A machine-readable summary is written to OUT_DIR/results.json.

Alarms are collected from both the inline events ae prints while it runs and
the final report section, so a run that crashes still reports the alarms it
emitted before the crash.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import subprocess
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ANSI = re.compile(r"\x1b\[[0-9;]*m")
LOC = r'"ln": (\d+), "cl": \d+, "fl": "([^"]+)"'
EVENT = re.compile(r"(Full Overflow|Partial Overflow|\w*NullPtrDereference)\s*:.*?" + LOC)
FINAL_OVERFLOW = re.compile(r"#+Buffer Overflow \(\d+ found\)#+(.*)", re.S)
STAT = re.compile(r"^(Analyzed_Func_Num|Func_Num|Func_Coverage_Percent)\s+(\S+)", re.M)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def site(line: str, file: str) -> str:
    return f"{Path(file).name}:{line}"


def classify(returncode: int, text: str) -> str:
    if returncode == 0:
        return "finished"
    if "Assertion" in text and "failed" in text:
        return "assertion failure"
    if returncode in (-11, 139):
        return "segfault"
    if returncode in (-9, 137):
        return "killed (timeout or memory)"
    return f"exit {returncode}"


def run_case(row: dict[str, str], bitcode: Path, out_dir: Path, ae: str, timeout: int) -> dict:
    log = out_dir / f"{row['case']}.log"
    command = [ae, "-ae-sparsity=semi-sparse", f"-ae-fun-entry={row['entry']}",
               "-overflow", "-null-deref", str(bitcode)]
    started = time.monotonic()
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
        timer = threading.Timer(timeout, process.kill)
        timer.start()
        # wait4 reports the resource usage of this child alone.
        _, status, usage = os.wait4(process.pid, 0)
        timer.cancel()
    wall = time.monotonic() - started
    returncode = os.waitstatus_to_exitcode(status)
    peak_mib = usage.ru_maxrss / 1024

    text = ANSI.sub("", log.read_text(errors="replace"))
    overflow, null_deref = set(), set()
    for kind, line, file in EVENT.findall(text):
        (overflow if "Overflow" in kind else null_deref).add(site(line, file))
    final = FINAL_OVERFLOW.search(text)
    if final:
        overflow |= {site(line, file) for line, file in re.findall(LOC, final.group(1))}

    known = row["known_bad_lines"].split(";")
    return {
        "case": row["case"],
        "entry": row["entry"],
        "outcome": classify(returncode, text),
        "wall_sec": round(wall, 1),
        "peak_rss_mib": round(peak_mib),
        "overflow_lines": sorted(overflow),
        "null_deref_lines": sorted(null_deref),
        "known_bad_lines": known,
        "known_hit": [k for k in known if k in overflow or k in null_deref],
        "stats": dict(STAT.findall(text)),
        "last_lines": text.splitlines()[-3:] if returncode else [],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--bitcode-dir", type=Path, default=HERE / "bitcode",
                        help="directory holding the cases.csv inputs (default: bitcode/)")
    parser.add_argument("--ae", default="ae", help="ae executable (default: ae on PATH)")
    parser.add_argument("--timeout", type=int, default=1800, help="per-case seconds")
    parser.add_argument("--only", nargs="*", help="run only these case names")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    with (HERE / "cases.csv").open(newline="") as stream:
        rows = [r for r in csv.DictReader(stream) if not args.only or r["case"] in args.only]

    print("| Case | Entry | Outcome | Wall (s) | Peak RSS (MiB) | Overflow lines | Null-deref lines | Known bad line hit |")
    print("|---|---|---|---:|---:|---:|---:|---|")
    results = []
    for row in rows:
        bitcode = args.bitcode_dir / row["bitcode"]
        if not bitcode.is_file():
            print(f"| {row['case']} | {row['entry']} | missing input `{row['bitcode']}` | | | | | |")
            continue
        if sha256(bitcode) != row["sha256"]:
            print(f"| {row['case']} | {row['entry']} | SHA-256 mismatch for `{row['bitcode']}` | | | | | |")
            continue
        result = run_case(row, bitcode, args.out_dir, args.ae, args.timeout)
        results.append(result)
        print(f"| {result['case']} | {result['entry']} | {result['outcome']} | {result['wall_sec']} | "
              f"{result['peak_rss_mib']} | {len(result['overflow_lines'])} | "
              f"{len(result['null_deref_lines'])} | {', '.join(result['known_hit']) or 'none'} |",
              flush=True)
    (args.out_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

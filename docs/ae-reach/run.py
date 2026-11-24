#!/usr/bin/env python3
"""Run the canonical AE evaluation inside the LLVM 22 container."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import random
import re
import resource
import subprocess
import tempfile
import time


MODES = ("dense", "semi-sparse", "sparse")
KIT = Path("/e")
CLANG = "/opt/llvm/bin/clang"
OPT = "/opt/llvm/bin/opt"
BASE = "a8a0f5181748059c2df2281e5e4ada63ccda3832"
VIOLATION = re.compile(rb"^VIOLATION line=(-?\d+)$", re.MULTILINE)


def load(path):
    return json.loads(path.read_text())


def save(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2) + "\n")


def execute(command, cwd=None, env=None, seconds=120):
    start = time.monotonic()
    try:
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, timeout=seconds)
        return result.returncode, result.stdout, result.stderr, time.monotonic() - start
    except subprocess.TimeoutExpired:
        return 124, b"", b"", time.monotonic() - start


def status(code):
    if code == 0:
        return "ok"
    if code == 124:
        return "timeout"
    return "crash" if code < 0 else "error"


def prepare(job, work):
    task, source, case = job
    directory = work / ("cases" if case else "svcomp") / task
    directory.mkdir(parents=True, exist_ok=True)
    ir = directory / "input.ll"
    code, _, err, _ = execute([CLANG, "-g", "-S", "-c", "-emit-llvm", "-Xclang",
                               "-disable-O0-optnone", "-fno-discard-value-names",
                               "-I/e/include", str(source), "-o", str(ir)])
    if code == 0:
        code, _, err, _ = execute([OPT, "-S", "-p=mem2reg", str(ir), "-o", str(ir)])
    return job, directory, ir, err.decode(errors="replace") if code else None


def run_engine(prepared, mode, args):
    (task, _, case), directory, ir, failure = prepared
    stock = [args.stock, "-ae-sparsity=" + mode, "-stat=false", str(ir)]
    stock_status = status(execute(stock, cwd=directory)[0]) if failure is None else "error"
    report = args.output / "reports" / ("cases" if case else "svcomp") / task / (mode + ".json")
    native = directory / (mode + ".json")
    native.unlink(missing_ok=True)
    command = [args.ae, "-ae-reach=" + str(native), *stock[1:]]
    if failure is None:
        code, _, err, seconds = execute(command, cwd=directory)
        state = status(code)
        failure = err.decode(errors="replace")[-4000:]
        if state == "ok" and not native.is_file():
            state, failure = "error", "ae did not write a report"
    else:
        state, seconds = "error", 0.0
    if state == "ok":
        try:
            result = load(native)
        except (ValueError, OSError) as exc:
            state, failure = "error", str(exc)
    if state != "ok":
        result = dict(schema_version=1, tool="ae-reach", tool_commit=args.commit,
                      svf_commit=BASE, mode=mode, command=command, status=state,
                      seconds=seconds, sites=[], verdict="error", diagnostic=failure)
    result.update(task=task, suite="cases" if case else "svcomp", stock_status=stock_status)
    return report, result


def candidates(ir, spot_check):
    rng = random.Random(451)
    boundary = [0, 1, -1, -(2**31), 2**31 - 1, -(2**63), 2**63 - 1]
    text = ir.read_text()
    instructions = "\n".join(line.split(", !", 1)[0] for line in text.splitlines()
                             if line.startswith("  ") and not line.startswith("  #"))
    constants = {int(n) for n in re.findall(r"(?:\bi(?:8|16|32|64) |, )(-?\d+)\b", instructions)}
    neighbors = {n + delta for n in constants for delta in (-1, 0, 1)
                 if -(2**63) <= n + delta < 2**63}
    yield []
    for number in dict.fromkeys(boundary + sorted(neighbors, key=lambda n: (abs(n), n))[:80]):
        yield [number] * 1024
    for _ in range(220 if spot_check else 40):
        yield [rng.randint(-1000, 1000) for _ in range(1024)]


def witness_line(code, stderr):
    matches = VIOLATION.findall(stderr)
    return int(matches[0]) if code == 99 and len(matches) == 1 else None


def trim_witness(values, witness, binary, directory, env, line):
    low, high = 0, len(values)
    while low < high:
        middle = (low + high) // 2
        witness.write_text("".join(f"{value}\n" for value in values[:middle]))
        code, _, error, _ = execute([str(binary)], cwd=directory, env=env, seconds=0.12)
        if witness_line(code, error) == line:
            high = middle
        else:
            low = middle + 1
    result = values[:high]
    witness.write_text("".join(f"{value}\n" for value in result))
    return result


def replay(prepared, reports):
    (task, source, case), directory, ir, compile_failure = prepared
    if compile_failure is not None or not any(r["status"] == "ok" for _, r in reports):
        return
    known_lines = {site["location"]["line"] for _, r in reports for site in r["sites"]}
    if not known_lines:
        return
    safe_lines = {site["location"]["line"] for _, r in reports
                  for site in r["sites"] if site["verdict"] == "safe"}
    binary = directory / "replay"
    code, _, _, _ = execute([CLANG, "-O0", "-Wno-everything", "-I/e/include", str(source),
                             "/e/runtime/verifier_rt.c", "-o", str(binary), "-lm"])
    if code != 0:
        return
    witness = directory / "witness.txt"
    env = dict(os.environ, VERIFIER_WITNESS=str(witness))
    tmp = directory / "replay-tmp"
    tmp.mkdir(exist_ok=True)
    confirmations = {}
    checks = 0
    violations = 0
    # On SV-COMP, line -1 cannot identify a particular site if several exist.
    # Also require the only __assert_fail calls to be in reach_error itself.
    if not case:
        lines = ir.read_text().splitlines()
        function = ""
        for line in lines:
            if line.startswith("define "):
                function = line
            if re.search(r"\bcall .*@__assert_fail\(", line) and "@reach_error(" not in function:
                return
        if len(known_lines) != 1 or max(len(r["sites"]) for _, r in reports) != 1:
            if not safe_lines:
                return
    for values in candidates(ir, bool(safe_lines)):
        witness.write_text("".join(f"{value}\n" for value in values))
        code, _, stderr, _ = execute([str(binary)], cwd=directory, env=env, seconds=0.12)
        checks += 1
        line = witness_line(code, stderr)
        if line is None or (case and line not in known_lines) or (not case and line != -1):
            continue
        if line not in confirmations:
            values = trim_witness(values, witness, binary, directory, env, line)
            with tempfile.TemporaryDirectory(dir=tmp) as temp:
                code, _, stderr, _ = execute(
                    ["bash", "/e/tools/replay.sh", str(source), str(witness)],
                    cwd=directory, env=dict(env, TMPDIR=temp), seconds=25)
            if witness_line(code, stderr) != line:
                continue
            confirmations[line] = {"witness": values, "replay": {"exit_code": code, "line": line}}
            if line in safe_lines or (line == -1 and safe_lines):
                violations += 1
        if not safe_lines and (not case or len(confirmations) == len(known_lines)):
            break
    for _, result in reports:
        if result["status"] != "ok":
            continue
        result["replay_audit"] = {"checks": checks, "counterexamples": violations}
        for site in result["sites"]:
            line = site["location"]["line"] if case else -1
            if line in confirmations and (case or len(result["sites"]) == 1):
                site.update(confirmations[line], verdict="unsafe", reason="confirmed-concrete-violation")
            elif line in confirmations and site["verdict"] == "safe":
                site.update(verdict="unknown", reason="ambiguous-concrete-violation")
        if any(site["verdict"] == "unsafe" for site in result["sites"]):
            result["verdict"] = "unsafe"
        elif all(site["verdict"] == "safe" for site in result["sites"]) and result["sites"]:
            result["verdict"] = "safe"
        else:
            result["verdict"] = "unknown"


def verify(report):
    assert report["schema_version"] == 1 and report["tool"] == "ae-reach"
    assert all(isinstance(report[key], str) for key in ("tool_commit", "svf_commit", "task"))
    assert report["suite"] in {"cases", "svcomp"} and report["mode"] in MODES
    assert all(isinstance(arg, str) for arg in report["command"]) and report["command"]
    assert report["status"] in {"ok", "crash", "timeout", "error"}
    assert report["seconds"] >= 0
    if report["status"] != "ok":
        assert not report["sites"] and report["verdict"] == "error"
        return
    assert report["verdict"] in {"safe", "unsafe", "unknown"}
    for site in report["sites"]:
        assert site["kind"] in {"assert", "eq", "unreachable", "reach_error"}
        assert site["verdict"] in {"safe", "unsafe", "unknown"} and site["reason"]
        assert all(isinstance(site["location"][key], str) for key in ("file", "function"))
        assert all(isinstance(site["location"][key], int) for key in ("line", "column"))
        assert all(isinstance(line, int) for line in site["depends_on"])
        assert all(isinstance(fact, str) for fact in site["coverage"])
        keys = {"condition", "left", "right"} if site["kind"] == "eq" else (
            {"condition"} if site["kind"] == "assert" else set())
        assert set(site["abstract_values"]) == keys
        assert all(value is None or isinstance(value, str) for value in site["abstract_values"].values())
        if site["verdict"] == "unsafe":
            assert all(isinstance(value, int) for value in site["witness"])
            assert site["replay"] == {"exit_code": 99, "line": (
                site["location"]["line"] if report["suite"] == "cases" else -1)}


def convert(output, truth):
    with (output / "sites.jsonl").open("w") as sites, (output / "tasks.jsonl").open("w") as tasks:
        for path in sorted((output / "reports").rglob("*.json")):
            report = load(path)
            verify(report)
            task, mode = report["task"], report["mode"]
            rows = {row["location"]["line"]: row for row in report["sites"]}
            if report["suite"] == "cases":
                for expected in truth[task]:
                    line = expected["line"]
                    row = rows.get(line, {})
                    verdict = row.get("verdict", "error" if report["status"] != "ok" else "unknown")
                    print(json.dumps(dict(task=task, mode=mode, line=line, verdict=verdict,
                                          reason=row.get("reason", "missing-site" if report["status"] == "ok"
                                                         else report["status"]),
                                          witness=row.get("witness"),
                                          depends_on=row.get("depends_on", []))), file=sites)
            else:
                verdict = report["verdict"]
                witness = next((row["witness"] for row in report["sites"] if "witness" in row), None)
                reason = ",".join(sorted({row["reason"] for row in report["sites"]})) or (
                    "no-discovered-sites" if report["status"] == "ok" else report["status"])
                print(json.dumps(dict(task=task, mode=mode, status=report["status"],
                                      verdict=verdict, witness=witness, seconds=report["seconds"],
                                      sites=len(report["sites"]), reason=reason)), file=tasks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ae")
    parser.add_argument("--stock")
    parser.add_argument("--commit")
    parser.add_argument("--work", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--convert-only", action="store_true")
    parser.add_argument("--cases-only", action="store_true")
    args = parser.parse_args()
    truth = load(KIT / "cases/truth.json")
    args.output = args.output.resolve()
    if not args.convert_only:
        if not all((args.ae, args.stock, args.commit, args.work)):
            parser.error("--ae, --stock, --commit, and --work are required for evaluation")
        args.work = args.work.resolve()
        limit = 4 * 1024**3
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        os.environ.update(AE_REACH_TOOL_COMMIT=args.commit, AE_REACH_SVF_COMMIT=BASE)
        jobs = [(path.stem, path, True) for path in sorted((KIT / "cases").glob("*.c"))]
        if not args.cases_only:
            jobs += [(task["id"], KIT / "svcomp/src" / task["input"], False)
                     for task in load(KIT / "svcomp/tasks.json")]

        def process(job):
            prepared = prepare(job, args.work)
            reports = [run_engine(prepared, mode, args) for mode in MODES]
            replay(prepared, reports)
            for path, report in reports:
                verify(report)
                save(path, report)
            print(job[0], " ".join(report["verdict"] for _, report in reports), flush=True)

        with ThreadPoolExecutor(max_workers=8) as pool:
            for _ in pool.map(process, jobs):
                pass
        (args.output / "README").write_text(f"SVF baseline {BASE}\nNative tool commit {args.commit}\n")
    convert(args.output, truth)


if __name__ == "__main__":
    main()

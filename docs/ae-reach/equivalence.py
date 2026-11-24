#!/usr/bin/env python3
"""Compare exit code, stdout and stderr with the reachability option disabled."""

from concurrent.futures import ThreadPoolExecutor
import argparse
import json
from pathlib import Path
import resource
import subprocess
import tempfile

from run import MODES, save


def capture(command, cwd):
    try:
        result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=120)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as timeout:
        return 124, timeout.stdout or b"", timeout.stderr or b""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock")
    parser.add_argument("native")
    parser.add_argument("build")
    parser.add_argument("suite", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_AS, (4 * 1024**3, 4 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    cases = json.loads(subprocess.check_output(
        ["ctest", "--test-dir", args.build, "-R", "ae_", "--show-only=json-v1"]))["tests"]
    flags = {}
    for test in cases:
        command = test["command"]
        if Path(command[0]).name != "ae":
            continue
        ir = next(Path(arg).resolve() for arg in command if arg.endswith((".bc", ".bc.gz", ".ll")))
        options = tuple(arg for arg in command[1:] if arg.startswith("-")
                        and not arg.startswith(("-ae-sparsity=", "-stat=")))
        flags.setdefault(ir, set()).add(options)
    for ir in args.suite.rglob("*.bc"):
        flags.setdefault(ir.resolve(), {()})
    runs = [(ir, mode, opts) for ir, arguments in sorted(flags.items())
            for mode in MODES for opts in sorted(arguments)]

    def compare(run):
        ir, mode, opts = run
        command = ["-ae-sparsity=" + mode, "-stat=false", *opts, str(ir)]
        with tempfile.TemporaryDirectory(prefix="ae-equivalence-") as scratch:
            left = capture([args.stock, *command], cwd=scratch)
            right = capture([args.native, *command], cwd=scratch)
        if left != right:
            def decoded(result):
                return [result[0], *(data.decode(errors="backslashreplace")[:12000] for data in result[1:])]
            return {"args": command, "stock": decoded(left), "native": decoded(right)}
        return None

    with ThreadPoolExecutor(max_workers=8) as pool:
        mismatches = [row for row in pool.map(compare, runs) if row is not None]
    save(args.output, {"inputs": len(flags), "invocations": len(runs), "mismatches": mismatches})
    print(f"{len(runs)} pairs, {len(mismatches)} differences")


if __name__ == "__main__":
    main()

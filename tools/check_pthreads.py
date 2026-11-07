#!/usr/bin/env python3
"""Execute the preserved M0 pthread WASM smoke in Node and optionally Chromium.

SPDX-License-Identifier: Apache-2.0
This checks the host toolchain, not translated ARM pthread semantics (R17).
"""
from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import json
from pathlib import Path
import re
import subprocess
import threading
import time


def check_output(lines: list[str], exit_code: int) -> dict:
    """Validate actual worker output, atomic count, and program termination."""
    workers = [int(match[1]) for line in lines
               if (match := re.fullmatch(r"hello from pthread (\d+)", line.strip()))]
    totals = [line.strip() for line in lines if line.strip().startswith("counter=")]
    if exit_code != 0:
        raise RuntimeError(f"pthread program exited with {exit_code}")
    if sorted(workers) != [0, 1, 2, 3]:
        raise RuntimeError(f"expected exactly four workers 0..3, got {workers}")
    if totals != ["counter=400000 OK"]:
        raise RuntimeError(f"unexpected atomic counter output: {totals}")
    return {"passed": True, "workers": workers, "counter": 400000, "exit_code": exit_code}


def run(command: list[str], timeout: int = 180) -> dict:
    start = time.perf_counter()
    result = subprocess.run(command, text=True, capture_output=True, timeout=timeout)
    evidence = {"command": command, "elapsed_seconds": time.perf_counter() - start,
                "stdout": result.stdout, "stderr": result.stderr, "exit_code": result.returncode}
    return evidence


def chromium(directory: Path, timeout_seconds: int) -> dict:
    from playwright.sync_api import sync_playwright

    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

        def end_headers(self):
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            self.send_header("Cross-Origin-Resource-Policy", "same-origin")
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(directory)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    start = time.perf_counter()
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            try:
                page = browser.new_page()
                page.goto(f"http://127.0.0.1:{server.server_port}/probe.html")
                page.wait_for_function("window.__pthreadProbe && (window.__pthreadProbe.exitCode !== null || window.__pthreadProbe.aborted !== null)", timeout=timeout_seconds * 1000)
                state = page.evaluate("window.__pthreadProbe")
                if not page.evaluate("crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'"):
                    raise RuntimeError("Chromium page is not cross-origin isolated with SharedArrayBuffer")
                if state["aborted"] is not None:
                    raise RuntimeError(f"Chromium WASM aborted: {state['aborted']}")
                return {**check_output(state["stdout"], state["exitCode"]),
                        "elapsed_seconds": time.perf_counter() - start, "stdout": state["stdout"],
                        "stderr": state["stderr"], "cross_origin_isolated": True,
                        "browser_version": browser.version}
            finally:
                browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emcc", required=True, help="Emscripten compiler command; set EMSDK_PYTHON/EM_CONFIG as needed")
    parser.add_argument("--node", default="node")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--out", type=Path, default=Path("build/m0-pthreads"))
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / "tests/m0/hello_pthread.c"
    directory = args.out.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / "hello_pthread.js"
    report = {"fixture": str(source), "fixture_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "scope": "M0 Emscripten pthread toolchain smoke; not ARM recompilation or guest pthread compatibility",
              "backends": {}, "passed": False}
    try:
        command = [args.emcc, "-O2", "-pthread", "-sPROXY_TO_PTHREAD=1", "-sPTHREAD_POOL_SIZE=5",
                   "-sEXIT_RUNTIME=1", "-sASSERTIONS=1",
                   "-sINCOMING_MODULE_JS_API=['print','printErr','onAbort','onExit']",
                   str(source), "-o", str(output)]
        report["compile"] = run(command)
        if report["compile"]["exit_code"] != 0:
            raise RuntimeError("Emscripten pthread compilation failed; see compile stdout/stderr")
        report["artifacts"] = {path.name: {"bytes": path.stat().st_size,
                                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                               for path in (output, output.with_suffix(".wasm"))}
        node = run([args.node, str(output)], timeout=args.timeout)
        report["backends"]["node"] = node
        node.update(check_output(node["stdout"].splitlines(), node["exit_code"]))
        if args.browser:
            (directory / "probe.html").write_text("""<!doctype html><meta charset="utf-8"><title>ReARM M0 pthread probe</title>
<script>
window.__pthreadProbe = {stdout: [], stderr: [], exitCode: null, aborted: null};
var Module = {
  print: text => window.__pthreadProbe.stdout.push(String(text)),
  printErr: text => window.__pthreadProbe.stderr.push(String(text)),
  onExit: code => { window.__pthreadProbe.exitCode = code; },
  onAbort: reason => { window.__pthreadProbe.aborted = String(reason); }
};
</script><script src="hello_pthread.js"></script>
""", encoding="utf-8")
            report["backends"]["chromium"] = chromium(directory, args.timeout)
        else:
            report["backends"]["chromium"] = {"passed": False, "status": "not_requested"}
        report["passed"] = True
        print("M0 pthread Node" + (" and Chromium" if args.browser else "") + ": counter=400000 OK")
        return 0
    except Exception as error:
        report["error"] = str(error)
        print(f"M0 pthread check failed: {error}")
        return 1
    finally:
        (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())

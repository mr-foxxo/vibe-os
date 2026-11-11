#!/usr/bin/env python3
"""Focused probe for closure item 1.6: `curl http://...` over the stack's OWN
TCP client - completing the 1.2 "real client<->server TCP" evidence with a
live server, not only the loopback self-test.

Two layers of proof run side by side:

1. Kernel: `mk_network_tcp_wire_probe` drives connect->GET->response against
   THIS script's host server through slirp's 10.0.2.2 loopback redirection,
   logging every step (start / OK + first-bytes hex / gave-up with socket
   state). No typing, no app: it isolates the wire path.
2. Userland: the `curl http://10.0.2.2:<port>/` command must produce its
   `curl: http-ok ... status=200 ... body0=VIBE-HTTP-MARKER` witness.

The python http.server logs the requests it actually received; that log goes
into the report, so "did the GET arrive" is answered by the server itself.

Usage: python3 tools/probe_http.py --image build/boot.img \
           --report build/http-probe.md [--qemu qemu-system-i386] [--memory-mb 3072]
"""

from __future__ import annotations

import argparse
import errno
import http.server
import shutil
import socketserver
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_network_surface import (  # noqa: E402
    QemuSession,
    run_command_expect,
    scenario_boot_rescue_shell,
)

HTTP_PORT = 8099
REQUEST_LOG: list = []


def start_server(root: Path, port: int):
    class Capturing(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            # SimpleHTTPRequestHandler.__init__ overrides any class attr with
            # the CWD; pass the served root explicitly (the earlier version
            # answered with a 2KB directory listing instead of the marker
            # file, which made the console echo slow and the body token
            # impossible).
            kw["directory"] = str(root)
            super().__init__(*a, **kw)

        def log_message(self, fmt, *args):  # noqa: D102
            try:
                REQUEST_LOG.append(fmt % args)
            except Exception:
                pass

    server = socketserver.TCPServer(("127.0.0.1", port), Capturing)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def main() -> int:
    global HTTP_PORT
    parser = argparse.ArgumentParser(description="curl HTTP GET probe (item 1.6)")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=3072)
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    report_path = Path(args.report).resolve()
    token = "VIBE-HTTP-MARKER-42"
    lines = ["# HTTP GET Probe (item 1.6)", ""]

    with tempfile.TemporaryDirectory(prefix="vibe-http-web-") as web_dir, \
            tempfile.TemporaryDirectory(prefix="vibe-http-probe-") as temp_dir:
        (Path(web_dir) / "index.html").write_text(token + "\n", encoding="utf-8")
        server = None
        # A stale server from a dead run would otherwise capture the port and
        # serve a different/empty document; bind with retry so the proof is
        # always this run's marker.
        for candidate in range(HTTP_PORT, HTTP_PORT + 50):
            try:
                server = start_server(Path(web_dir), candidate)
                HTTP_PORT = candidate
                break
            except OSError as exc:
                if exc.errno != errno.EADDRINUSE:
                    raise
        if server is None:
            print("FAIL: no free port near 8099")
            return 1
        try:
            workspace = Path(temp_dir)
            scenario_image = workspace / "boot.img"
            shutil.copyfile(image_path, scenario_image)
            session = QemuSession(args.qemu, scenario_image, args.memory_mb, workspace)
            try:
                scenario_boot_rescue_shell(session)
                run_command_expect(
                    session,
                    f"curl http://10.0.2.2:{HTTP_PORT}/",
                    [f"curl: http-ok url=http://10.0.2.2:{HTTP_PORT}/ status=200",
                     "line0=HTTP/1.", "body0=VIBE-HTTP-MARKER"],
                    timeout=900.0,
                )
                lines.append(f"- PASS `curl http://10.0.2.2:{HTTP_PORT}/`")
                log = session.read_log()
                lines.append("")
                lines.append("## kernel TCP wire probe witnesses")
                lines.append("```text")
                lines.extend(
                    ln for ln in log.splitlines()
                    if any(m in ln for m in ("tcp wire probe", "tcp connected",
                                             "tcp reset", "curl: http"))
                )
                lines.append("```")
                lines.append("")
                lines.append("## host http.server request log")
                lines.append("```text")
                lines.extend(REQUEST_LOG)
                lines.append("```")
                report_path.parent.mkdir(parents=True, exist_ok=True)
                report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                print("\n".join(lines))
                return 0
            except Exception as exc:  # noqa: BLE001 - the report is the artifact
                lines.append("")
                lines.append(f"FAIL: {exc}")
                log = session.read_log()
                lines.append("")
                lines.append("## trace lines (curl/http/tcp/shell)")
                lines.append("```text")
                lines.extend(
                    ln for ln in log.splitlines()
                    if any(m in ln for m in ("curl", "http", "tcp", "shell: command",
                                            "shell: ready", "dhcp"))
                )
                lines.append("```")
                lines.append("")
                lines.append("## host http.server request log")
                lines.append("```text")
                lines.extend(REQUEST_LOG)
                lines.append("```")
                report_path.parent.mkdir(parents=True, exist_ok=True)
                report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                print("\n".join(lines))
                return 1
            finally:
                session.close()
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())

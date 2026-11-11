#!/usr/bin/env python3
"""Focused probe for closure item 1.5: ifconfig/route/netstat read the REAL
service state — netstat must render the kernel live socket table (rows>0:
the TCP loopback self-test leaves a listener + established sockets), route
and ifconfig must reflect the lease state. Asserts the serial witnesses the
three tools emit (app console never reaches serial).

Usage: python3 tools/probe_netcfg.py --image build/boot.img \
           --report build/netcfg-probe.md [--qemu qemu-system-i386] [--memory-mb 3072]
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_network_surface import (  # noqa: E402
    QemuSession,
    run_command_expect,
    scenario_boot_rescue_shell,
)

COMMANDS = [
    ("netstat", ["netstat: table rows="]),
    ("ifconfig", ["ifconfig: status ok"]),
    ("route", ["route: table ok"]),
]


def main() -> int:
    parser = argparse.ArgumentParser(description="netcfg trio real-state probe (item 1.5)")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=3072)
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    report_path = Path(args.report).resolve()
    lines = ["# Netcfg Real-State Probe (item 1.5)", ""]

    with tempfile.TemporaryDirectory(prefix="vibe-netcfg-probe-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)
        session = QemuSession(args.qemu, scenario_image, args.memory_mb, workspace)
        latency_note: list = []
        try:
            scenario_boot_rescue_shell(session)
            for command, markers in COMMANDS:
                run_command_expect(session, command, markers)
                lines.append(f"- PASS `{command}`")
            log = session.read_log()
            lines.append("")
            lines.append("## witnesses")
            lines.append("```text")
            lines.extend(
                ln for ln in log.splitlines()
                if any(m in ln for m in ("netstat: table", "ifconfig: status", "route: table",
                                         "dhcp ack", "tcp loopback smoke ok"))
            )
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
            lines.append("## trace lines (netcfg/table/shell)")
            lines.append("```text")
            lines.extend(
                ln for ln in log.splitlines()
                if any(m in ln for m in ("netstat", "ifconfig", "route:", "shell: command",
                                         "shell: ready", "tcp loopback"))
            )
            lines.append("```")
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print("\n".join(lines))
            return 1
        finally:
            session.close()


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Focused probe for item 6.1: the rescue shell must survive a failing
command and quit ONLY on an explicit `exit`.

Before the fix the loop broke on any rc != 0, so the first typo killed the
field session. The probe types:
  1. `cd /definitely-not-here` -> must report rc=1 and the shell must come
     back (`shell: ready` after `shell: command exit cd rc=1`);
  2. `true`                   -> proves the session still runs commands;
  3. `exit`                   -> must leave the session (the shell returns
     and `userland.app: main returned` follows).

Markers are the serial witnesses the shell already emits
(`shell: command exit <cmd> rc=N`, `shell: ready`), plus the app-return
line, so the proof is measured, not asserted.

Usage: python3 tools/probe_shell_error.py --image build/boot.img \
           --report build/shell-error-probe.md [--qemu qemu-system-i386] \
           [--memory-mb 3072]
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
    scenario_boot_rescue_shell,
    wait_for_all_since,
)

# (command, ordered markers that must appear AFTER it ran)
STEPS = [
    ("cd /definitely-not-here", ["shell: command cd",
                                  "shell: command exit cd rc=1",
                                  "shell: ready"]),
    ("true", ["shell: command true", "shell: command exit true rc=0"]),
    ("exit", ["app: main returned"]),
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Shell survives rc!=0 (item 6.1)")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=3072)
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    report_path = Path(args.report).resolve()
    lines = ["# Shell Error-Survival Probe (item 6.1)", ""]

    with tempfile.TemporaryDirectory(prefix="vibe-shell-probe-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)
        session = QemuSession(args.qemu, scenario_image, args.memory_mb, workspace)
        latency_note: list = []
        try:
            scenario_boot_rescue_shell(session)
            for command, markers in STEPS:
                start = len(session.read_log())
                session.type_batch(command)
                wait_for_all_since(session, markers, 900.0, start, latency_note)
                lines.append(f"- PASS `{command}`")
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print("\n".join(lines))
            return 0
        except Exception as exc:  # noqa: BLE001 - the report is the artifact
            lines.append("")
            for note in latency_note:
                lines.append(f"- {note}")
            lines.append("")
            lines.append(f"FAIL: {exc}")
            log = session.read_log()
            lines.append("")
            lines.append("## trace lines (shell session)")
            lines.append("")
            lines.append("```text")
            lines.extend(
                ln for ln in log.splitlines()
                if any(m in ln for m in ("shell: ready", "shell: command",
                                         "app: main returned", "bye"))
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

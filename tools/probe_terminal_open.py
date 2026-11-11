#!/usr/bin/env python3
"""Focused probe (KNOWN_BUGS #22 investigation, 11/11): does ctrl-x (key 24)
on the desktop session actually open a terminal window?

The audio gates stall at exactly this point: `desktop: key 24` is traced, then
NO further desktop window debug event (open-new/alloc-ok/open-fail/...) ever
appears, even though the kernel census/sched-score witnesses keep printing.
terminal_init_ksh (external "BSD shell implementation") runs pipe() + fork()
in the DESKTOP task before any of those prints, so the probe answers: is the
desktop hung inside pipe/fork, and is the hang audio-backend related (AC97
profile) or universal (default/none profile)?

Usage: python3 tools/probe_terminal_open.py --image build/boot.img \
           --report build/terminal-open-probe.md [--profile ac97|plain]
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_audio_stack import QemuMonitorSession  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="terminal-open probe (bug #22)")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=3072)
    parser.add_argument("--profile", choices=["ac97", "plain", "hda"], default="ac97")
    args = parser.parse_args()

    devices: list[str] = []
    expected_backend = "compat-ac97"
    if args.profile == "ac97":
        devices = ["AC97"]
    elif args.profile == "hda":
        devices = ["intel-hda", "hda-duplex"]
        expected_backend = "compat-azalia"

    lines = [f"# Terminal-open probe ({args.profile})", ""]
    with tempfile.TemporaryDirectory(prefix="vibe-term-probe-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(Path(args.image).resolve(), scenario_image)
        session = QemuMonitorSession(args.qemu, scenario_image, args.memory_mb,
                                     workspace, None, devices, [])
        try:
            session.wait_for_all(["desktop: session ready"], timeout=240.0)
            lines.append("- session ready: reached")

            def offset() -> int:
                return len(session.read_log())

            base = offset()
            time.sleep(2.0)
            growth_idle = offset() - base
            lines.append(f"- idle serial growth over 2 s before ctrl-x: {growth_idle} bytes")

            session.hmp("sendkey ctrl-x")
            last = offset()
            quiet = 0
            grew_at: list[float] = []
            deadline = time.time() + 60.0
            while time.time() < deadline:
                time.sleep(1.0)
                now = offset()
                if now > last:
                    quiet = 0
                    last = now
                    grew_at.append(round(60.0 - (deadline - time.time()), 1))
                else:
                    quiet += 1
                    if quiet >= 30:
                        break
            log = session.read_log()[base:]
            lines.append(f"- bytes after ctrl-x: {last - base}")
            lines.append(f"- growth windows (s since send): {grew_at[:20]}")
            lines.append(f"- open-new present: {'desktop: open-new' in log}")
            for probe in ("alloc-ok", " open-fail", "alloc-no-slot", "alloc-bad-type",
                          "terminal: ", "ksh", "startup sound", f"backend={expected_backend}"):
                lines.append(f"- contains {probe!r}: {probe in log}")
            lines.append("")
            lines.append("## Serial after ctrl-x")
            lines.append("```text")
            lines.append(log[-6000:])
            lines.append("```")
            Path(args.report).write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"terminal-open probe: done ({args.profile})")
            return 0
        except Exception as exc:  # noqa: BLE001
            lines.append(f"- probe error: {exc}")
            Path(args.report).write_text("\n".join(lines) + "\n", encoding="utf-8")
            return 1
        finally:
            session.close()


if __name__ == "__main__":
    sys.exit(main())

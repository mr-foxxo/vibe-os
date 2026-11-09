#!/usr/bin/env python3
import argparse
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List


BOOT_MARKERS = ["userland.app: shell start", "shell: ready"]
QEMU_OPTS = [
    "-machine", "pc",
    "-cpu", "core2duo",
    "-smp", "2,sockets=1,cores=2,threads=1,maxcpus=2",
    "-vga", "std",
    "-rtc", "base=localtime",
    "-usb",
]


class QemuSession:
    def __init__(self, qemu_binary: str, image_path: Path, memory_mb: int, workspace: Path):
        self.serial_log = workspace / "serial.log"
        self.monitor_socket = workspace / "monitor.sock"
        self.qemu_binary = qemu_binary
        self.image_path = image_path
        self.memory_mb = memory_mb
        self.proc = None
        self._mon = None
        self.key_latencies = []
        self._spawn()
        self._wait_for_monitor()

    def _spawn(self) -> None:
        self.proc = subprocess.Popen(
            [
                self.qemu_binary,
                *QEMU_OPTS,
                "-m", str(self.memory_mb),
                "-drive", f"format=raw,file={self.image_path}",
                "-netdev", "user,id=net0",
                "-device", "virtio-net-pci,netdev=net0",
                "-boot", "c",
                "-display", "none",
                "-serial", f"file:{self.serial_log}",
                "-monitor", f"unix:{self.monitor_socket},server,nowait",
                "-no-reboot",
                "-no-shutdown",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def restart(self) -> None:
        """Cold-restart the guest: with -no-reboot, HMP system_reset would exit
        the process, and the stage2 boot menu is the only place the rescue
        selection can be made. Relaunch a fresh QEMU and truncate the serial
        log so the caller's marker checks only observe the new boot."""
        self.close()
        try:
            self.serial_log.unlink()
        except FileNotFoundError:
            pass
        self._spawn()
        self._wait_for_monitor()

    def _wait_for_monitor(self) -> None:
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if self.monitor_socket.exists():
                return
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU exited before monitor socket was created")
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for QEMU monitor socket")

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.hmp("quit", timeout=0.5)
            except Exception:
                pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5.0)
        self._close_mon()

    def _close_mon(self) -> None:
        if self._mon is not None:
            try:
                self._mon.close()
            except OSError:
                pass
            self._mon = None

    def _mon_sock(self) -> socket.socket:
        if self._mon is None:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect(str(self.monitor_socket))
            self._mon = s
            self._drain_mon(0.5)
        return self._mon

    def _drain_mon(self, timeout: float) -> None:
        if self._mon is None:
            return
        s = self._mon
        s.settimeout(timeout)
        try:
            while True:
                b = s.recv(4096)
                if not b:
                    break
                if b"(qemu)" in b:
                    break
        except socket.timeout:
            pass
        finally:
            s.settimeout(2.0)

    def type_batch(self, text: str, ret: bool = True) -> None:
        """Items 0.15/0.16 pacing (03/11). 19_ns38: with a fresh connection
        + a `(qemu)`-prompt wait per character, each key cost seconds of
        wall (main loop starved by the two TCG vCPU threads on a 2-core
        host) — command 2 was still typing ~35 min into its window although
        command 1 PASSED end-to-end. The first fix here — one persistent
        connection, pipelined at 50 ms/keystroke — turned out UNSAFE:
        19_ns39 froze the entire run at guest tick ~48000 (timer ticks stop,
        serial silent, monitor unresponsive for 25 min) when the typing
        outran the guest's drain. So typing is now event-paced: send one
        key, wait for the serial to GROW (the shell echoes every keystroke),
        cap 10 s; no growth for 5 s raises with the key named — a freeze
        must be reported as a freeze, not as a blind window timeout. The
        completion assertion is unchanged: `shell: command exit <cmd> rc=0`
        in the serial."""
        s = self._mon_sock()

        def type_one(key: str) -> None:
            before = self.serial_log.stat().st_size if self.serial_log.exists() else 0
            t_send = time.time()
            s.sendall(f"sendkey {key}\n".encode("utf-8"))
            deadline = time.time() + 60.0
            quiet = 0
            while time.time() < deadline:
                try:
                    after = self.serial_log.stat().st_size
                except OSError:
                    after = 0
                if after > before:
                    # item 0.16 probe: monotonic gap from sendkey to the
                    # first new serial byte this key produced. This is the
                    # END-TO-END monitor->device-model->guest-echo latency
                    # (main-loop starvation dominates it on a busy host),
                    # measured WITHOUT touching the kernel.
                    self.key_latencies.append(round(time.time() - t_send, 2))
                    return
                time.sleep(0.1)
                quiet += 1
                if quiet >= 300:
                    raise RuntimeError(
                        f"serial frozen sending {key!r}: no guest output "
                        "for 30 s (freeze, not slowness)")
            # window ended without growth: record the cap so the report
            # shows the stall instead of losing the datapoint.
            self.key_latencies.append(">=60")

        for ch in text:
            if ch == " ":
                key = "spc"
            elif ch == "/":
                key = "slash"
            elif ch == "-":
                key = "minus"
            elif ch == ".":
                key = "dot"
            elif ch == ":":
                key = "shift-semicolon"
            else:
                key = ch
            type_one(key)
        if ret:
            type_one("ret")
        self._drain_mon(0.5)

    def hmp(self, command: str, timeout: float = 1.0) -> str:
        chunks: List[bytes] = []
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(timeout)
            client.connect(str(self.monitor_socket))
            try:
                while True:
                    chunk = client.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    if b"(qemu)" in chunk:
                        break
            except socket.timeout:
                pass

            client.sendall(command.encode("utf-8") + b"\n")
            if command.strip() == "quit":
                return ""

            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    chunk = client.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                chunks.append(chunk)
                if b"(qemu)" in chunk:
                    break
        return b"".join(chunks).decode("utf-8", errors="replace")

    def read_log(self) -> str:
        if not self.serial_log.exists():
            return ""
        return self.serial_log.read_text(encoding="utf-8", errors="replace")

    def wait_for_all(self, markers: List[str], timeout: float) -> None:
        deadline = time.time() + timeout
        pending = list(markers)
        while time.time() < deadline:
            log = self.read_log()
            pending = [marker for marker in pending if marker not in log]
            if not pending:
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))

    def type_text(self, text: str, pause: float = 0.06) -> None:
        for ch in text:
            if ch == " ":
                key = "spc"
            elif ch == "/":
                key = "slash"
            elif ch == "-":
                key = "minus"
            elif ch == ".":
                key = "dot"
            elif ch == ":":
                self.hmp("sendkey shift-semicolon")
                time.sleep(pause)
                continue
            else:
                key = ch
            self.hmp(f"sendkey {key}")
            time.sleep(pause)

    def type_text_slow(self, text: str) -> None:
        """Bug #19 item 0.12 harness pacing (01/11). Gate run 19_ns10
        proved the kernel path lossless — every key QEMU delivered reached
        read_line 1:1, zero drops/timeouts/unexpected replies — but QEMU's
        PS/2 queue silently discarded 6 of the 15 characters typed at
        60 ms while the 2-core TCG host was still saturated from boot:
        `netmgrd status` arrived as `netmgrd `+Enter (argc=1). Type at a
        pace the guest can drain and let each monitor round-trip settle.
        `type_text` keeps its fast default for the boot-menu pulses."""
        self.type_text(text, pause=0.25)
        time.sleep(0.3)

    def send_key(self, key: str, pause: float = 0.1) -> None:
        self.hmp(f"sendkey {key}")
        time.sleep(pause)


def wait_for_all_since(session: QemuSession, markers: List[str], timeout: float,
                       start_offset: int, latency_note: List[str]) -> None:
    deadline = time.time() + timeout
    pending = list(markers)
    while time.time() < deadline:
        log = session.read_log()[start_offset:]
        pending = [marker for marker in pending if marker not in log]
        if not pending:
            return
        if session.proc.poll() is not None:
            break
        time.sleep(0.05)
    # Item 0.16: on timeout, append the per-key latency line to the report.
    # sendkey->serial-byte is end-to-end monitor+device-model+guest latency;
    # the kernel's own wake->dispatch probes proved <=6 ticks, so any large
    # number here is environment (main-loop starvation), not kernel.
    summary = ", ".join(str(v) for v in session.key_latencies[-24:]) if session.key_latencies else "no-keys"
    latency_note.append(
        "key-latency (s, last24) when window expired: " + summary +
        f" | markers still pending: {', '.join(pending)}")
    raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))


def scenario_boot_rescue_shell(session: QemuSession, attempts: int = 4, window_s: float = 25.0) -> None:
    """Drive the stage2 boot menu to RESCUE SHELL with closed-loop feedback.

    The old selector fired `s,s,ret` three times on blind fixed delays and
    then waited 60s for markers that could never arrive if all attempts
    landed outside the menu window (the flake that made this harness — and
    audio/abi-compat — unreliable as gates). Now: keep pulsing the selection
    until the KERNEL confirms the choice in the serial log (`boot mode:
    rescue shell`); if the boot instead confirms a WRONG mode (`boot mode:
    safe mode`, or the autoboot desktop launching), cold-restart QEMU for a
    fresh menu window and try again. Each burst is `s,s,<ret>` because the
    menu maps the S-key scancode to move-down and RESCUE is two rows below
    the default entry (see boot/stage2.asm classify path).
    """
    for attempt in range(attempts):
        log = session.read_log()
        if "boot mode: rescue shell" in log and "shell: ready" in log:
            return
        deadline = time.time() + window_s
        while time.time() < deadline:
            session.send_key("s", pause=0.12)
            session.send_key("s", pause=0.12)
            session.send_key("ret", pause=0.25)
            log = session.read_log()
            if "boot mode: rescue shell" in log:
                break
            if ("boot mode: safe mode" in log or
                    "desktop.app: launch startx" in log or
                    "host: argv startx" in log):
                break  # wrong entry confirmed; restart for a fresh menu
            time.sleep(1.0)
        log = session.read_log()
        if "boot mode: rescue shell" in log:
            session.wait_for_all(["shell: ready"], timeout=240.0)
            # Bug #19 gate hardening (01/11): settle before typing. The
            # selector's own s,s,<ret> bursts are still draining through
            # the PS/2 queue when `shell: ready` appears (TCG pacing);
            # the shell's type-ahead flush eats them, but giving it a
            # quiet 3s wall window means the first typed command starts
            # on a clean queue instead of racing in-flight menu bytes.
            time.sleep(3.0)
            return
        session.restart()
    raise RuntimeError(
        "Timed out selecting rescue shell: 'boot mode: rescue shell' never "
        f"appeared after {attempts} menu attempts (with cold restarts)")


def run_command_expect(session: QemuSession, command: str, markers: List[str], timeout: float = 2700.0) -> None:
    """Typed command + COMPLETION gate.

    Two bugs fixed here. (1) The old gate waited for `shell: ready`, which
    the shell prints at the TOP of every loop iteration — before it even
    reads the line — so a command that never returned still passed as soon
    as its echo marker appeared. (2) The shell EXITS its loop on any rc!=0
    (userland/modules/shell.c), so a failing command silently ends all later
    markers. The gate now requires the shell's own exit marker
    (`shell: command exit <cmd> rc=0`), which proves the command actually
    ran to completion with success, and only then the prompt return.
    900s (01/11, gate runs 19_ns20/21): the kernel key path is now provably
    lossless — `key-cons`/`rk` traces showed every delivered character
    reaching read_line within ≤1 tick, in order, and `s,t,a,t` of `status`
    arriving for the first time — but on this 2-core TCG host the QEMU
    monitor round-trip of each sendkey can take 10-30 s wall while the
    guest is busy (init supervision poll + TCG), so typing 15 chars
    legitimately needs minutes. Bigger patience, same hard assertion:
    the completion marker `shell: command exit <cmd> rc=0` is unchanged.
    1800s (19_ns37): command 1 PASSED end-to-end (`PASS netmgrd status`) —
    the window was eaten by the remaining commands' typing wall, not by a
    kernel stall. 2700s (0.15, 03/11): one honest window per command for
    the full 22-command session; the assertion itself is untouched.
    """
    start = len(session.read_log())
    command_marker = command.split(" ", 1)[0]
    # 0.15 (03/11): pipelined batch typing on one persistent monitor
    # connection (19_ns38: per-char connect+prompt-wait cost seconds of
    # wall per keystroke — command 2 was still typing ~35 min into its
    # window; command 1 itself PASSED end-to-end).
    session.type_batch(command)
    wait_for_all_since(session,
                       [f"shell: command {command_marker}",
                        f"shell: command exit {command_marker} rc=0",
                        "shell: ready",
                        *markers],
                       timeout=timeout,
                       start_offset=start,
                       latency_note=getattr(session, "latency_note", []))


def validate_network_surface(qemu_binary: str, image_path: Path, memory_mb: int, report_path: Path) -> bool:
    commands = [
        ("netmgrd status", ["backend:", "datapath executor:", "socket rx capacity:"]),
        ("netmgrd export-state /runtime/netmgrd-surface.txt", ["state exported to /runtime/netmgrd-surface.txt"]),
        ("netmgrd monitor-events 3", ["event[0]: type=", "event[1]: type=", "event[2]: type="]),
        ("netmgrd dhcp", ["dhcp", "dns"]),
        ("netmgrd dns", ["dns "]),
        ("netctl status", ["packet path:", "event stream:", "socket rx capacity:"]),
        ("netctl events 3", ["event[0]: type=", "event[1]: type=", "event[2]: type="]),
        ("netctl ifconfig", ["state:", "ip:"]),
        ("netctl route", ["default via"]),
        ("netctl dhcp", ["connected", "dns:"]),
        ("netctl dns", ["dns "]),
        ("ifconfig", ["status:"]),
        ("route", ["Routing tables"]),
        ("netstat", ["netstat:", "sockets:", "backend:"]),
        ("ping localhost", ["1 packets transmitted", "1 packets received"]),
        ("ping 10.0.2.2", ["bytes from 10.0.2.2", "1 packets transmitted"]),
        ("host localhost", ["localhost", "127.0.0.1"]),
        ("dig localhost", ["localhost", "127.0.0.1"]),
        ("curl file:///runtime/netmgrd-surface.txt", ["manager=", "lease_state="]),
        ("curl file:///runtime/netmgrd-status.txt", ["state="]),
        ("ftp example.com", ["ftp transport unavailable"]),
        ("netctl socket-smoke", ["socket smoke", "ping"]),
    ]
    lines: List[str] = ["# Network Surface Validation", ""]
    latency_note: List[str] = []

    with tempfile.TemporaryDirectory(prefix="vibe-network-surface-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)
        session = QemuSession(qemu_binary, scenario_image, memory_mb, workspace)
        session.latency_note = latency_note
        try:
            scenario_boot_rescue_shell(session)
            for command, markers in commands:
                run_command_expect(session, command, markers)
                lines.append(f"- PASS `{command}`")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return True
        except Exception as exc:
            lines.append("")
            if latency_note:
                lines.append("## item 0.16 key latency at window-expiry")
                lines.append("")
                lines.extend(f"- {ln}" for ln in latency_note)
                lines.append("")
            lines.append(f"FAIL: {exc}")
            lines.append("")
            full = session.read_log()
            # Bug #19 (01/11): the last-12k tail alone lost the early
            # witnesses. The `rk` poll trace (what read_line actually
            # receives per poll) prints during the first seconds of typing,
            # and census/diag spam evicts it from the tail before the
            # timeout fires. Extract the campaign's trace lines explicitly
            # so every FAIL report is a complete measurement, then keep the
            # tail for raw context.
            trace_markers = (
                "rk ", "key-cons", "wake-stale", "tmo-stale", "evt-pub", "evt-rec", "request timeout",
                "unexpected reply", "input-diag", "service: request svc=",
                "shell: flushed", "shell: ready", "shell: command",
                "sched-watch",
            )
            trace_lines = [ln for ln in full.splitlines()
                           if any(m in ln for m in trace_markers)]
            lines.append("## trace lines (poll values, IPC anomalies, shell state)")
            lines.append("")
            lines.append("```")
            lines.extend(trace_lines[-180:])
            lines.append("```")
            lines.append("")
            lines.append("```")
            lines.append(full[-12000:])
            lines.append("```")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return False
        finally:
            session.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate VibeOS network command surface under QEMU")
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=256)
    args = parser.parse_args()

    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    ok = validate_network_surface(args.qemu, Path(args.image), args.memory_mb, report_path)
    if not ok:
        print(f"network-surface: validation failed; see {report_path}", file=sys.stderr)
        return 1
    print(f"network-surface: validation passed; see {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

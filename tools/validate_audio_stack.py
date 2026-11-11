#!/usr/bin/env python3
import argparse
import json
import re
import shutil
import os
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

BASE_WIDTH = 800
BASE_HEIGHT = 600
TERMINAL_FOCUS_POINT = (210, 170)


@dataclass
class AudioScenarioResult:
    passed: bool
    log: str
    missing_markers: List[str]
    error: str = ""


class QemuMonitorSession:
    def __init__(self,
                 qemu_binary: str,
                 image_path: Path,
                 memory_mb: int,
                 workspace: Path,
                 machine: Optional[str],
                 audio_devices: List[str],
                 extra_qemu_args: List[str]):
        self.serial_log = workspace / "serial.log"
        self.monitor_socket = workspace / "monitor.sock"
        self.qmp_socket = workspace / "qmp.sock"
        self._mon: Optional[socket.socket] = None
        self.key_latencies: List = []
        qemu_cmd = [
            qemu_binary,
            "-m",
            str(memory_mb),
            "-drive",
            f"format=raw,file={image_path}",
            "-netdev",
            "user,id=net0",
            "-device",
            "virtio-net-pci,netdev=net0",
            "-boot",
            "c",
            "-display",
            "none",
            "-serial",
            f"file:{self.serial_log}",
            "-monitor",
            f"unix:{self.monitor_socket},server,nowait",
            "-qmp",
            f"unix:{self.qmp_socket},server,nowait",
        ]
        if machine:
            qemu_cmd.extend(["-machine", machine])
        qemu_cmd.extend(extra_qemu_args)
        for device in audio_devices:
            qemu_cmd.extend(["-device", device])
        qemu_cmd.extend(
            [
                "-no-reboot",
                "-no-shutdown",
            ]
        )
        self.proc = subprocess.Popen(
            qemu_cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.mouse_x = 0
        self.mouse_y = 0
        self.mouse_position_known = False
        self._wait_for_monitor()
        self._wait_for_qmp()

    def _wait_for_monitor(self) -> None:
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if self.monitor_socket.exists():
                return
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU exited before monitor socket was created")
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for QEMU monitor socket")

    def _wait_for_qmp(self) -> None:
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if self.qmp_socket.exists():
                return
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU exited before QMP socket was created")
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for QMP socket")

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

    def _mon_sock(self) -> socket.socket:
        if self._mon is None:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect(str(self.monitor_socket))
            self._mon = s
            self._drain_mon(0.5)
        return self._mon

    def type_batch(self, text: str, ret: bool = True) -> None:
        """Items 0.15/0.16 pacing ported from validate_network_surface.py
        (19_ns40). The old per-character fresh-HMP-connection + `(qemu)`
        prompt wait cost seconds of wall per key on this 2-core TCG host,
        which is why every audio gate died waiting for
        `shell: command audiosvc` (11/11 A/B: identical on clean HEAD —
        KNOWN_BUGS #22 left the audio host unlaunched; with the host back,
        the gates still needed paced typing). One persistent connection,
        one key at a time, paced on serial GROWTH (the shell echoes every
        keystroke); no growth for 30 s raises named-key freeze."""
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
                    self.key_latencies.append(round(time.time() - t_send, 2))
                    return
                time.sleep(0.1)
                quiet += 1
                if quiet >= 300:
                    raise RuntimeError(
                        f"serial frozen sending {key!r}: no guest output "
                        "for 30 s (freeze, not slowness)")
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

    def close(self) -> None:
        if self._mon is not None:
            try:
                self._mon.close()
            except OSError:
                pass
            self._mon = None
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

    def _recv_qmp_message_once(self, client: socket.socket, timeout: float) -> dict:
        buffer = ""
        deadline = time.time() + timeout

        while time.time() < deadline:
            client.settimeout(max(0.05, deadline - time.time()))
            chunk = client.recv(65536)
            if not chunk:
                break
            buffer += chunk.decode("utf-8", errors="replace")

            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                if not line.strip():
                    continue
                message = json.loads(line)
                if "return" in message or "error" in message:
                    return message

        return {}

    def qmp_once(self, payload: dict, timeout: float = 1.0) -> dict:
        last_error: Optional[Exception] = None
        deadline = time.time() + max(1.0, timeout * 3.0)

        while time.time() < deadline:
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                    client.settimeout(timeout)
                    client.connect(str(self.qmp_socket))
                    greeting = client.recv(4096)
                    if b"QMP" not in greeting:
                        raise RuntimeError("Unexpected QMP greeting")

                    client.sendall(json.dumps({"execute": "qmp_capabilities"}).encode("utf-8") + b"\n")
                    response = self._recv_qmp_message_once(client, timeout)
                    if "return" not in response:
                        raise RuntimeError("QMP capability handshake failed")

                    client.sendall(json.dumps(payload).encode("utf-8") + b"\n")
                    return self._recv_qmp_message_once(client, timeout)
            except (BrokenPipeError,
                    ConnectionRefusedError,
                    ConnectionResetError,
                    OSError,
                    json.JSONDecodeError) as exc:
                last_error = exc
                if self.proc.poll() is not None:
                    raise
                time.sleep(0.05)

        if last_error is not None:
            raise last_error
        return {}

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

    def wait_for_log(self, marker: str, timeout: float) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if marker in self.read_log():
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError(f"Timed out waiting for marker: {marker}")

    def wait_for_all(self, markers: List[str], timeout: float) -> None:
        deadline = time.time() + timeout
        pending = list(markers)
        while time.time() < deadline and pending:
            log = self.read_log()
            pending = [marker for marker in pending if marker not in log]
            if not pending:
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))

    def boot_mode_size(self) -> Optional[Tuple[int, int]]:
        log = self.read_log()
        matches = re.findall(r"video: boot init backend=.* mode=(\d+)x(\d+)x\d+", log)
        if not matches:
            return None
        width_text, height_text = matches[-1]
        return (int(width_text, 10), int(height_text, 10))

    def send_key(self, key: str, pause: float = 0.08) -> None:
        parts = key.split("-")
        base_key = parts[-1]
        modifier_keys = parts[:-1]
        events = []

        for modifier in modifier_keys:
            events.append(
                {
                    "type": "key",
                    "data": {
                        "down": True,
                        "key": {"type": "qcode", "data": modifier},
                    },
                }
            )
        events.append(
            {
                "type": "key",
                "data": {
                    "down": True,
                    "key": {"type": "qcode", "data": base_key},
                },
            }
        )
        events.append(
            {
                "type": "key",
                "data": {
                    "down": False,
                    "key": {"type": "qcode", "data": base_key},
                },
            }
        )
        for modifier in reversed(modifier_keys):
            events.append(
                {
                    "type": "key",
                    "data": {
                        "down": False,
                        "key": {"type": "qcode", "data": modifier},
                    },
                }
            )

        response = self.qmp_once(
            {
                "execute": "input-send-event",
                "arguments": {
                    "head": 0,
                    "events": events,
                },
            }
        )
        if "error" in response:
            raise RuntimeError(f"QMP key send failed for {key}: {response['error']}")
        time.sleep(pause)

    def type_text(self, text: str, pause: float = 0.08) -> None:
        key_map = {
            " ": "spc",
            "\n": "ret",
            "-": "minus",
            ".": "dot",
            "/": "slash",
            "_": "shift-minus",
        }
        for ch in text:
            self.send_key(key_map.get(ch, ch.lower()), pause=pause)

    def reset_mouse_to_center(self) -> None:
        mode = self.boot_mode_size()

        if not mode:
            self.mouse_x = 0
            self.mouse_y = 0
            self.mouse_position_known = False
            return

        width, height = mode
        target_x = width // 2
        target_y = height // 2

        if self.mouse_position_known:
            self.move_mouse_to(target_x, target_y, pause=0.02)

        self.mouse_x = target_x
        self.mouse_y = target_y
        self.mouse_position_known = True

    def qmp_mouse_rel(self, dx: int, dy: int) -> None:
        raw_dx = dx // 2
        raw_dy = dy // 2
        if dx != 0 and raw_dx == 0:
            raw_dx = 1 if dx > 0 else -1
        if dy != 0 and raw_dy == 0:
            raw_dy = 1 if dy > 0 else -1
        response = self.qmp_once(
            {
                "execute": "input-send-event",
                "arguments": {
                    "head": 0,
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": raw_dx}},
                        {"type": "rel", "data": {"axis": "y", "value": raw_dy}},
                    ],
                },
            }
        )
        if "error" in response:
            raise RuntimeError(f"QMP mouse move failed: {response['error']}")

    def move_mouse_to(self, x: int, y: int, pause: float = 0.04) -> None:
        dx = x - self.mouse_x
        dy = y - self.mouse_y

        while dx != 0 or dy != 0:
            step_x = max(-40, min(40, dx))
            step_y = max(-40, min(40, dy))
            self.qmp_mouse_rel(step_x, step_y)
            self.mouse_x += step_x
            self.mouse_y += step_y
            dx -= step_x
            dy -= step_y
            time.sleep(pause)
        self.mouse_position_known = True

    def left_click(self, pause: float = 0.08) -> None:
        response = self.qmp_once(
            {
                "execute": "input-send-event",
                "arguments": {
                    "head": 0,
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": 1}},
                        {"type": "rel", "data": {"axis": "y", "value": 0}},
                        {"type": "btn", "data": {"button": "left", "down": True}},
                    ],
                },
            }
        )
        if "error" in response:
            raise RuntimeError(f"QMP mouse press failed: {response['error']}")
        self.mouse_x += 1
        time.sleep(pause)
        response = self.qmp_once(
            {
                "execute": "input-send-event",
                "arguments": {
                    "head": 0,
                    "events": [
                        {"type": "btn", "data": {"button": "left", "down": False}},
                    ],
                },
            }
        )
        if "error" in response:
            raise RuntimeError(f"QMP mouse release failed: {response['error']}")
        time.sleep(pause)
        self.qmp_mouse_rel(-1, 0)
        self.mouse_x -= 1
        time.sleep(pause)


def log_contains(session: QemuMonitorSession, marker: str, start_offset: int = 0) -> bool:
    log = session.read_log()
    if start_offset > 0:
        log = log[start_offset:]
    return marker in log


def wait_for_all_since(session: QemuMonitorSession,
                       markers: List[str],
                       timeout: float,
                       start_offset: int = 0) -> None:
    deadline = time.time() + timeout
    pending = list(markers)
    while time.time() < deadline and pending:
        log = session.read_log()
        if start_offset > 0:
            log = log[start_offset:]
        pending = [marker for marker in pending if marker not in log]
        if not pending:
            return
        if session.proc.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))


def wait_for_any_marker_since(session: QemuMonitorSession,
                              markers: List[str],
                              timeout: float,
                              start_offset: int = 0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        log = session.read_log()
        if start_offset > 0:
            log = log[start_offset:]
        if any(marker in log for marker in markers):
            return
        if session.proc.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError("Timed out waiting for markers: " + ", ".join(markers))


def wait_for_any_marker(session: QemuMonitorSession, markers: List[str], timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        log = session.read_log()
        if any(marker in log for marker in markers):
            return
        if session.proc.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError("Timed out waiting for any marker: " + " | ".join(markers))


def wait_for_terminal_command_done(session: QemuMonitorSession,
                                   command: str,
                                   timeout: float = 12.0) -> None:
    done_marker = f"terminal: command done {command}"
    if log_contains(session, done_marker):
        return
    try:
        session.wait_for_log(done_marker, timeout=timeout)
    except RuntimeError:
        if log_contains(session, f"terminal: command start {command}") or \
           log_contains(session, f"shell: command {command}"):
            raise RuntimeError(f"Timed out waiting for marker: {done_marker}")
        raise


def wait_for_terminal_command_done_since(session: QemuMonitorSession,
                                         command: str,
                                         start_offset: int,
                                         timeout: float = 12.0) -> None:
    done_marker = f"terminal: command done {command}"
    if log_contains(session, done_marker, start_offset):
        return
    try:
        wait_for_all_since(session, [done_marker], timeout=timeout, start_offset=start_offset)
    except RuntimeError:
        if log_contains(session, f"terminal: command start {command}", start_offset) or \
           log_contains(session, f"shell: command {command}", start_offset):
            raise RuntimeError(f"Timed out waiting for marker: {done_marker}")
        raise


def run_command(session: QemuMonitorSession,
                command: str,
                timeout: float = 6.0,
                pause: float = 0.12,
                marker: Optional[str] = None) -> int:
    command_marker = marker
    start_offset = len(session.read_log())
    if command_marker is None:
        command_marker = command.split(" ", 1)[0] if command else ""
    session.type_batch(command)
    if command_marker:
        wait_for_all_since(session,
                           [f"shell: command {command_marker}"],
                           timeout=max(timeout, 25.0),
                           start_offset=start_offset)
    return start_offset


def focus_terminal(session: QemuMonitorSession,
                   timeout: float = 4.0,
                   expect_open: bool = False) -> None:
    start_offset = len(session.read_log())
    session.send_key("ctrl-x", pause=0.2)
    if expect_open:
        wait_for_any_marker_since(session,
                                  ["desktop: key 24", "desktop: open-new w=0 t=1 i=0"],
                                  timeout=timeout,
                                  start_offset=start_offset)
        return
    try:
        wait_for_any_marker_since(session,
                                  ["desktop: key 24", "desktop: open-new"],
                                  timeout=min(timeout, 1.5),
                                  start_offset=start_offset)
    except RuntimeError:
        time.sleep(0.4)


def scaled_point(session: QemuMonitorSession, point: Tuple[int, int]) -> Tuple[int, int]:
    mode = session.boot_mode_size()
    if not mode:
        return point
    width, height = mode
    return (
        (point[0] * width) // BASE_WIDTH,
        (point[1] * height) // BASE_HEIGHT,
    )


def focus_existing_terminal(session: QemuMonitorSession, pause: float = 0.12) -> None:
    _ = session
    time.sleep(max(0.6, pause))


def append_missing_alias_groups(log: str,
                                missing: List[str],
                                alias_groups: List[Tuple[str, List[str]]]) -> List[str]:
    for label, markers in alias_groups:
        if not any(marker in log for marker in markers):
            missing.append(label)
    return missing


def run_audio_capture_smoke(qemu_binary: str,
                            image_path: Path,
                            memory_mb: int,
                            machine: Optional[str],
                            audio_devices: List[str],
                            extra_qemu_args: List[str],
                            expected_backend: str,
                            require_capture: bool,
                            record_ms: int,
                            verify_capture_playback: bool,
                            verify_playback_path: str,
                            require_path_programmed: bool,
                            require_hardware_diag: bool,
                            require_desktop_startup_sound: bool,
                            require_boot_startup_sound: bool,
                            require_capture_smoke: bool = False) -> AudioScenarioResult:
    backend_markers = [
        f"audiosvc: backend={expected_backend}",
        f"soundctl: backend={expected_backend}",
    ]
    alias_groups: List[Tuple[str, List[str]]] = [
        ("startx-launch", ["desktop.app: launch startx", "host: startx start", "host: desktop session launched"]),
        (f"backend={expected_backend}", backend_markers),
        ("status: transport=", ["audiosvc: transport=", "soundctl: transport="]),
        ("status: codecready-quirk=", ["audiosvc: codecready-quirk=", "soundctl: codecready-quirk="]),
        ("status: multichannel=", ["audiosvc: multichannel=", "soundctl: multichannel="]),
        ("status: control-owner=audiosvc", ["audiosvc: control-owner=audiosvc", "soundctl: control-owner=audiosvc"]),
        ("status: backend-executor=kernel", ["audiosvc: backend-executor=kernel", "soundctl: backend-executor=kernel"]),
        ("status: ui-progress=decoupled", ["audiosvc: ui-progress=decoupled", "soundctl: ui-progress=decoupled"]),
    ]
    if require_capture_smoke:
        # Boot-witness-only deterministic gate (KNOWN_BUGS #24: the post-ksh
        # desktop terminal no longer emits the busybox-shell typed-command
        # witnesses these gates were written against; capture proof must not
        # depend on that broken channel).
        required_markers = [
            "desktop: session start",
            "audiosvc: online",
            f"audio: deferred launch backend={expected_backend}",
            "audio: hda capture smoke bytes=",
        ]
        if expected_backend == "compat-azalia":
            required_markers.append("capture path usable")
    else:
        required_markers = [
            "desktop: session start",
            "audiosvc: export ok",
            "desktop: open-new w=0 t=1 i=0",
        ]
    if require_capture:
        required_markers.extend(
            [
                "soundctl: record begin",
                "soundctl: record ok",
                "soundctl: capture-ready=",
                "soundctl: capture-dma=",
                "service: storage request type=17",
            ]
        )
        if verify_capture_playback:
            required_markers.extend(
                [
                    "soundctl: play begin",
                    "soundctl: play ok",
                ]
            )
    elif verify_playback_path:
        required_markers.extend(
            [
                "soundctl: play begin",
                "soundctl: play ok",
            ]
        )
    if require_path_programmed:
        alias_groups.append(("status: path-programmed=1",
                             ["audiosvc: path-programmed=1", "soundctl: path-programmed=1"]))
    if require_hardware_diag:
        alias_groups.extend(
            [
                ("status: pci=", ["audiosvc: pci=", "soundctl: pci="]),
                ("status: codec=", ["audiosvc: codec=", "soundctl: codec="]),
                ("status: route=", ["audiosvc: route=", "soundctl: route="]),
            ]
        )
    if require_desktop_startup_sound:
        required_markers.extend(
            [
                "desktop: startup sound begin",
                "audio: begin desktop-session",
                "audio: started desktop-session",
                "audio: done desktop-session",
                "desktop: startup sound returned",
            ]
        )
    if require_boot_startup_sound:
        required_markers.extend(
            [
                "audio: begin boot",
                "audio: started boot",
                "audio: done boot",
                "init: boot sound returned",
            ]
        )

    with tempfile.TemporaryDirectory(prefix="vibe-audio-smoke-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)

        session = QemuMonitorSession(qemu_binary,
                                     scenario_image,
                                     memory_mb,
                                     workspace,
                                     machine,
                                     audio_devices,
                                     extra_qemu_args)
        try:
            if require_boot_startup_sound:
                session.wait_for_all(
                    [
                        "audio: begin boot",
                        "audio: started boot",
                        "audio: done boot",
                        "init: boot sound returned",
                    ],
                    timeout=20.0,
                )
            # Boot-window hardening (same lesson as the network-surface gate):
            # on the slow 2-core TCG host a full desktop boot measured 105s to
            # `desktop: session start` (2026-11-11, AC97 profile) while the
            # kernel itself was healthy - a 45s wall was an environment budget,
            # not an assertion. The marker set is unchanged; only the window is
            # configurable now (env VIBE_AUDIO_BOOT_TIMEOUT, default 180s).
            boot_wait = float(os.environ.get("VIBE_AUDIO_BOOT_TIMEOUT", "180"))
            wait_for_any_marker(session,
                                ["desktop.app: launch startx", "host: startx start", "host: desktop session launched"],
                                timeout=boot_wait)
            session.wait_for_all(["desktop: session start"], timeout=boot_wait)
            if require_desktop_startup_sound:
                session.wait_for_all(
                    [
                        "desktop: startup sound begin",
                        "audio: begin desktop-session",
                        "audio: started desktop-session",
                        "audio: done desktop-session",
                        "desktop: startup sound returned",
                    ],
                    timeout=20.0,
                )
            time.sleep(1.0)
            if require_capture_smoke:
                log = session.read_log()
                missing = [m for m in required_markers if m not in log]
                mm = re.search(
                    r"audio: hda capture smoke bytes=(\d+) blocks=(\d+) xruns=(\d+)",
                    log)
                if mm:
                    got, blocks, xruns = (int(g) for g in mm.groups())
                    if got < 256:
                        missing.append(f"capture smoke bytes={got}<256")
                    if blocks < 2:
                        missing.append(f"capture smoke blocks={blocks}<2")
                    if xruns != 0:
                        missing.append(f"capture smoke xruns={xruns}")
                else:
                    missing.append("capture smoke witness not found")
                return AudioScenarioResult(not missing, log, missing, "")
            focus_terminal(session, timeout=12.0, expect_open=True)
            time.sleep(1.0)
            focus_existing_terminal(session)
            command_offset = run_command(session,
                                         "audiosvc export-state /runtime/audio-validation.state",
                                         timeout=8.0)
            wait_for_all_since(session,
                               ["shell: command audiosvc", "audiosvc: export ok"],
                               timeout=15.0,
                               start_offset=command_offset)
            wait_for_any_marker_since(session, backend_markers, timeout=15.0, start_offset=command_offset)
            wait_for_any_marker_since(session,
                                      ["audiosvc: transport=", "soundctl: transport="],
                                      timeout=15.0,
                                      start_offset=command_offset)
            wait_for_any_marker_since(session,
                                      ["audiosvc: codecready-quirk=", "soundctl: codecready-quirk="],
                                      timeout=15.0,
                                      start_offset=command_offset)
            wait_for_any_marker_since(session,
                                      ["audiosvc: multichannel=", "soundctl: multichannel="],
                                      timeout=15.0,
                                      start_offset=command_offset)
            wait_for_any_marker_since(session,
                                      ["audiosvc: control-owner=audiosvc", "soundctl: control-owner=audiosvc"],
                                      timeout=15.0,
                                      start_offset=command_offset)
            wait_for_any_marker_since(session,
                                      ["audiosvc: backend-executor=kernel", "soundctl: backend-executor=kernel"],
                                      timeout=15.0,
                                      start_offset=command_offset)
            wait_for_any_marker_since(session,
                                      ["audiosvc: ui-progress=decoupled", "soundctl: ui-progress=decoupled"],
                                      timeout=15.0,
                                      start_offset=command_offset)
            if require_hardware_diag:
                wait_for_any_marker_since(session,
                                          ["audiosvc: pci=", "soundctl: pci="],
                                          timeout=15.0,
                                          start_offset=command_offset)
                wait_for_any_marker_since(session,
                                          ["audiosvc: codec=", "soundctl: codec="],
                                          timeout=15.0,
                                          start_offset=command_offset)
                wait_for_any_marker_since(session,
                                          ["audiosvc: route=", "soundctl: route="],
                                          timeout=15.0,
                                          start_offset=command_offset)
            wait_for_terminal_command_done_since(session, "audiosvc", command_offset, timeout=12.0)
            if require_capture:
                focus_existing_terminal(session)
                command_offset = run_command(session,
                                             f"soundctl record {record_ms} /capture.wav",
                                             timeout=8.0)
                wait_for_all_since(session,
                                   ["soundctl: record begin", "soundctl: record ok"],
                                   timeout=35.0,
                                   start_offset=command_offset)
                wait_for_terminal_command_done_since(session, "soundctl", command_offset, timeout=20.0)
                if verify_capture_playback:
                    focus_existing_terminal(session)
                    command_offset = run_command(session, "soundctl play /capture.wav", timeout=8.0)
                    wait_for_all_since(session,
                                       ["soundctl: play begin", "soundctl: play ok"],
                                       timeout=20.0,
                                       start_offset=command_offset)
                    wait_for_terminal_command_done_since(session, "soundctl", command_offset, timeout=20.0)
            elif verify_playback_path:
                focus_existing_terminal(session)
                command_offset = run_command(session,
                                             f"soundctl play {verify_playback_path}",
                                             timeout=8.0)
                wait_for_all_since(session,
                                   ["soundctl: play begin", "soundctl: play ok"],
                                   timeout=25.0,
                                   start_offset=command_offset)
                wait_for_terminal_command_done_since(session, "soundctl", command_offset, timeout=20.0)
            if require_path_programmed:
                focus_existing_terminal(session)
                command_offset = run_command(session,
                                             "audiosvc export-state /runtime/audio-validation.state",
                                             timeout=8.0)
                wait_for_any_marker_since(session,
                                          ["audiosvc: path-programmed=1", "soundctl: path-programmed=1"],
                                          timeout=15.0,
                                          start_offset=command_offset)
                wait_for_terminal_command_done_since(session, "audiosvc", command_offset, timeout=12.0)
            log = session.read_log()
        except Exception as exc:
            log = session.read_log()
            missing = [marker for marker in required_markers if marker not in log]
            missing = append_missing_alias_groups(log, missing, alias_groups)
            return AudioScenarioResult(False, log, missing, str(exc))
        finally:
            session.close()

    missing = [marker for marker in required_markers if marker not in log]
    missing = append_missing_alias_groups(log, missing, alias_groups)
    return AudioScenarioResult(not missing, log, missing, "")


def write_report(report_path: Path, result: AudioScenarioResult) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Audio Stack Validation Report",
        "",
        f"- Result: {'PASS' if result.passed else 'FAIL'}",
    ]
    if result.error:
        lines.append(f"- Error: {result.error}")
    if result.missing_markers:
        lines.append("- Missing markers: " + ", ".join(result.missing_markers))
    lines.extend(
        [
            "",
            "## Serial Log",
            "",
            "```text",
            result.log.rstrip(),
            "```",
            "",
        ]
    )
    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate VibeOS audio scenarios under QEMU")
    parser.add_argument("--image", required=True, help="path to boot.img")
    parser.add_argument("--report", required=True, help="path to markdown report")
    parser.add_argument("--qemu", default="qemu-system-i386", help="QEMU binary")
    parser.add_argument("--memory-mb", type=int, default=3072, help="guest memory in MiB")
    parser.add_argument("--machine", default="", help="optional QEMU machine type")
    parser.add_argument("--audio-device",
                        action="append",
                        dest="audio_devices",
                        help="QEMU -device entry for audio emulation; may be passed multiple times")
    parser.add_argument("--no-default-audio-device",
                        action="store_true",
                        help="do not inject the default AC97 device when no --audio-device is provided")
    parser.add_argument("--qemu-arg",
                        action="append",
                        default=[],
                        help="extra raw QEMU argument appended before -device entries; may be passed multiple times")
    parser.add_argument("--expect-backend",
                        default="compat-ac97",
                        help="expected backend marker reported by audiosvc")
    parser.add_argument("--skip-capture",
                        action="store_true",
                        help="skip soundctl record validation and only verify desktop/audiosvc boot path")
    parser.add_argument("--record-ms",
                        type=int,
                        default=250,
                        help="duration in ms for soundctl record when capture validation is enabled")
    parser.add_argument("--verify-capture-playback",
                        action="store_true",
                        help="after recording /capture.wav, run soundctl play /capture.wav and require playback markers")
    parser.add_argument("--verify-playback-path",
                        default="",
                        help="run soundctl play <path> after soundctl status and require playback markers")
    parser.add_argument("--require-path-programmed",
                        action="store_true",
                        help="after playback, require audiosvc/soundctl to report path-programmed=1")
    parser.add_argument("--require-hardware-diag",
                        action="store_true",
                        help="require audiosvc/soundctl to emit PCI/codec/route hardware markers")
    parser.add_argument("--require-desktop-startup-sound",
                        action="store_true",
                        help="require the delayed desktop startup WAV path to run after desktop session start")
    parser.add_argument("--require-boot-startup-sound",
                        action="store_true",
                        help="require the boot WAV path to run during bootstrap before startx")
    parser.add_argument("--require-capture-smoke",
                        action="store_true",
                        help="instead of typed capture, assert the boot-time azalia "
                             "capture self-test witness (bytes>0, xruns==0); no typing "
                             "required (KNOWN_BUGS #24)")
    args = parser.parse_args()

    audio_devices = args.audio_devices if args.audio_devices else ([] if args.no_default_audio_device else ["AC97"])
    machine = args.machine if args.machine else None
    result = run_audio_capture_smoke(args.qemu,
                                     Path(args.image),
                                     args.memory_mb,
                                     machine,
                                     audio_devices,
                                     args.qemu_arg,
                                     args.expect_backend,
                                     not args.skip_capture,
                                     args.record_ms,
                                     args.verify_capture_playback,
                                     args.verify_playback_path,
                                     args.require_path_programmed,
                                     args.require_hardware_diag,
                                     args.require_desktop_startup_sound,
                                     args.require_boot_startup_sound,
                                     args.require_capture_smoke)
    report_path = Path(args.report)
    write_report(report_path, result)
    if not result.passed:
        print(f"audio-stack: validation failed; see {report_path}", file=sys.stderr)
        return 1

    print(f"audio-stack: validation passed; see {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

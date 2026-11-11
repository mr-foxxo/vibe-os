#!/usr/bin/env python3
import argparse
import json
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Tuple


BOOT_MARKER = "userland.app: shell start"
SHELL_READY_MARKER = "shell: ready"
DESKTOP_READY_MARKER = "desktop: session ready"
AUTODESKTOP_BOOT_MARKERS = [
    "desktop.app: launch startx",
    "desktop: session start",
    DESKTOP_READY_MARKER,
]
QEMU_VALIDATION_COMMON_OPTS = [
    "-machine",
    "pc",
    "-cpu",
    "core2duo",
    "-smp",
    "2,sockets=1,cores=2,threads=1,maxcpus=2",
    "-vga",
    "std",
    "-rtc",
    "base=localtime",
    "-usb",
]
DESKTOP_CENTER_X = 320
DESKTOP_CENTER_Y = 240
FILES_ICON_CENTER = (572, 63)
TRASH_ICON_CENTER = (572, 255)
SAFE_WALLPAPER_POINT = (32, 120)
BASE_WIDTH = 640
BASE_HEIGHT = 480
TASKBAR_HEIGHT = 22
START_MENU_HEIGHT = 404
START_MENU_WIDTH = 336
START_MENU_LIST_PANEL_HEIGHT = START_MENU_HEIGHT - 146
START_MENU_LIST_VIEW_HEIGHT = START_MENU_LIST_PANEL_HEIGHT - 8
START_MENU_ENTRY_STEP = 36


@dataclass
class Scenario:
    name: str
    description: str
    command: Optional[str]
    must_have: List[str]
    boot_markers: Optional[List[str]] = None
    command_marker: Optional[str] = None
    boot_action: Optional[Callable[["QemuSession"], None]] = None
    action: Optional[Callable[["QemuSession"], None]] = None


@dataclass
class ScenarioResult:
    scenario: Scenario
    passed: bool
    log: str
    missing_markers: List[str]
    error: Optional[str] = None


def parse_mode_text(mode_text: str) -> Optional[str]:
    parts = mode_text.lower().split("x")
    if len(parts) != 2:
        return None
    try:
        width = int(parts[0], 10)
        height = int(parts[1], 10)
    except ValueError:
        return None
    if width <= 0 or height <= 0:
        return None
    return f"{width}x{height}"


class QemuSession:
    def __init__(self, qemu_binary: str, image_path: Path, memory_mb: int, workspace: Path):
        self.workspace = workspace
        self.serial_log = workspace / "s.log"
        self.monitor_socket = workspace / "m.sock"
        self.qmp_socket = workspace / "qmp.sock"
        self.qemu_binary = qemu_binary
        self.image_path = image_path
        self.memory_mb = memory_mb
        self._spawn()
        self.mouse_x = 0
        self.mouse_y = 0
        self.mouse_position_known = False
        self.qmp_client: Optional[socket.socket] = None
        self.qmp_buffer = ""
        self._wait_for_monitor()
        self._wait_for_qmp()

    def _spawn(self) -> None:
        self.proc = subprocess.Popen(
            [
                self.qemu_binary,
                *QEMU_VALIDATION_COMMON_OPTS,
                "-m",
                str(self.memory_mb),
                "-drive",
                f"format=raw,file={self.image_path}",
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
                "-no-reboot",
                "-no-shutdown",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def restart(self) -> None:
        """Cold-restart: -no-reboot means HMP system_reset would EXIT the
        process, and the stage2 boot menu (the only place the rescue
        selection can be made) needs a fresh guest. Relaunches QEMU, resets
        the host-side mouse model, and truncates the serial log so marker
        checks only observe the new boot."""
        self.close()
        try:
            self.serial_log.unlink()
        except FileNotFoundError:
            pass
        self._spawn()
        self.mouse_x = 0
        self.mouse_y = 0
        self.mouse_position_known = False
        self.qmp_client = None
        self.qmp_buffer = ""
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

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.qmp({"execute": "quit"}, timeout=0.5)
            except Exception:
                pass
        self._disconnect_qmp()
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)

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

    def hmp(self, command: str, timeout: float = 1.0) -> str:
        last_error: Optional[Exception] = None
        deadline = time.time() + max(1.0, timeout * 3.0)

        while time.time() < deadline:
            chunks: List[bytes] = []
            try:
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
                        return b"".join(chunks).decode("utf-8", errors="replace")

                    response_deadline = time.time() + timeout
                    while time.time() < response_deadline:
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
            except (BrokenPipeError, ConnectionRefusedError, ConnectionResetError, OSError) as exc:
                last_error = exc
                if self.proc.poll() is not None:
                    raise
                time.sleep(0.05)

        if last_error is not None:
            raise last_error
        return ""

    def screendump(self, path: Path, timeout: float = 2.0) -> None:
        if path.exists():
            path.unlink()
        response = self.hmp(f"screendump {path}", timeout=timeout)
        if not path.is_file():
            raise RuntimeError(f"QEMU screendump failed: {response.strip() or 'no output'}")

    def _connect_qmp(self) -> None:
        self._disconnect_qmp()
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.settimeout(1.0)
        client.connect(str(self.qmp_socket))
        greeting = client.recv(4096)
        if b"QMP" not in greeting:
            client.close()
            raise RuntimeError("Unexpected QMP greeting")
        self.qmp_client = client
        self.qmp_buffer = ""
        response = self.qmp({"execute": "qmp_capabilities"})
        if "return" not in response:
            raise RuntimeError("QMP capability handshake failed")

    def _disconnect_qmp(self) -> None:
        if self.qmp_client is not None:
            try:
                self.qmp_client.close()
            except Exception:
                pass
        self.qmp_client = None
        self.qmp_buffer = ""

    def _recv_qmp_message(self, timeout: float) -> dict:
        client = self.qmp_client
        if client is None:
            raise RuntimeError("QMP client is not connected")

        deadline = time.time() + timeout
        while time.time() < deadline:
            if "\n" in self.qmp_buffer:
                line, self.qmp_buffer = self.qmp_buffer.split("\n", 1)
                if not line.strip():
                    continue
                return json.loads(line)

            client.settimeout(max(0.05, deadline - time.time()))
            chunk = client.recv(65536)
            if not chunk:
                break
            self.qmp_buffer += chunk.decode("utf-8", errors="replace")

        return {}

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
            except (BrokenPipeError, ConnectionRefusedError, ConnectionResetError, OSError, json.JSONDecodeError) as exc:
                last_error = exc
                if self.proc.poll() is not None:
                    raise
                time.sleep(0.05)

        if last_error is not None:
            raise last_error
        return {}

    def qmp(self, payload: dict, timeout: float = 1.0) -> dict:
        last_error: Optional[Exception] = None

        for attempt in range(2):
            try:
                if self.qmp_client is None:
                    self._connect_qmp()

                client = self.qmp_client
                if client is None:
                    raise RuntimeError("QMP client is not connected")

                client.settimeout(timeout)
                client.sendall(json.dumps(payload).encode("utf-8") + b"\n")

                deadline = time.time() + timeout
                while time.time() < deadline:
                    message = self._recv_qmp_message(deadline - time.time())
                    if not message:
                        break
                    if "return" in message or "error" in message:
                        return message
                return {}
            except (BrokenPipeError, ConnectionResetError, OSError, json.JSONDecodeError) as exc:
                last_error = exc
                self._disconnect_qmp()
                if attempt == 0:
                    continue
                raise

        if last_error is not None:
            raise last_error
        return {}

    def qmp_mouse_rel(self, dx: int, dy: int) -> None:
        raw_dx = dx // 2
        raw_dy = dy // 2
        if dx != 0 and raw_dx == 0:
            raw_dx = 1 if dx > 0 else -1
        if dy != 0 and raw_dy == 0:
            raw_dy = 1 if dy > 0 else -1
        response = self.qmp(
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

    def send_key(self, key: str, pause: float = 0.08) -> None:
        parts = key.split("-")
        base_key = parts[-1]
        modifier_keys = parts[:-1]
        events = []

        self._disconnect_qmp()

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

    def send_shortcut(self, key: str, pause: float = 0.08) -> None:
        self.hmp(f"sendkey {key}")
        time.sleep(pause)

    def type_text(self, text: str, pause: float = 0.08, use_hmp: bool = False) -> None:
        key_map = {
            " ": "spc",
            "\n": "ret",
            "-": "minus",
            ".": "dot",
            "/": "slash",
            "\\": "backslash",
            "_": "shift-minus",
        }
        for ch in text:
            key = key_map.get(ch, ch.lower())
            if use_hmp:
                self.send_shortcut(key, pause=pause)
            else:
                self.send_key(key, pause=pause)

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
        response = self.qmp(
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
        response = self.qmp(
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

    def mouse_wheel(self, delta: int, pause: float = 0.08) -> None:
        if delta == 0:
            return

        button = "wheel-up" if delta > 0 else "wheel-down"
        for _ in range(abs(delta)):
            response = self.qmp(
                {
                    "execute": "input-send-event",
                    "arguments": {
                        "head": 0,
                        "events": [
                            {"type": "btn", "data": {"button": button, "down": True}},
                            {"type": "btn", "data": {"button": button, "down": False}},
                        ],
                    },
                }
            )
            if "error" in response:
                raise RuntimeError(f"QMP mouse wheel failed: {response['error']}")
            time.sleep(pause)


def scaled_point(session: QemuSession, point: Tuple[int, int]) -> Tuple[int, int]:
    mode = session.boot_mode_size()
    if not mode:
        return point
    width, height = mode
    return (
        (point[0] * width) // BASE_WIDTH,
        (point[1] * height) // BASE_HEIGHT,
    )


def files_icon_center(session: QemuSession) -> Tuple[int, int]:
    mode = session.boot_mode_size()
    if not mode:
        return FILES_ICON_CENTER
    width, _ = mode
    return (width - 68, 63)


def trash_icon_center(session: QemuSession) -> Tuple[int, int]:
    mode = session.boot_mode_size()
    if not mode:
        return TRASH_ICON_CENTER
    width, _ = mode
    return (width - 68, 255)


def start_button_center(session: QemuSession) -> Tuple[int, int]:
    mode = session.boot_mode_size()
    if not mode:
        return (35, 469)
    _, height = mode
    return (35, height - 11)


def start_menu_terminal_center(session: QemuSession) -> Tuple[int, int, int]:
    return start_menu_list_entry_center(session, 0)


def start_menu_visible_count() -> int:
    return max(1, START_MENU_LIST_VIEW_HEIGHT // START_MENU_ENTRY_STEP)


def start_menu_list_entry_center(session: QemuSession, slot: int) -> Tuple[int, int, int]:
    mode = session.boot_mode_size()
    visible_count = start_menu_visible_count()
    scroll_steps = 0
    visible_slot = slot

    if visible_slot >= visible_count:
        scroll_steps = visible_slot - (visible_count - 1)
        visible_slot = visible_count - 1

    if not mode:
        return (110, 282 + (visible_slot * START_MENU_ENTRY_STEP), scroll_steps)
    _, height = mode
    menu_y = height - TASKBAR_HEIGHT - START_MENU_HEIGHT
    return (110, menu_y + 108 + (visible_slot * START_MENU_ENTRY_STEP), scroll_steps)


def start_menu_input_restart_center(session: QemuSession) -> Tuple[int, int, int]:
    return start_menu_list_entry_center(session, 5)


def start_menu_audio_restart_center(session: QemuSession) -> Tuple[int, int, int]:
    return start_menu_list_entry_center(session, 6)


def start_menu_video_restart_center(session: QemuSession) -> Tuple[int, int, int]:
    return start_menu_list_entry_center(session, 7)


def start_menu_network_restart_center(session: QemuSession) -> Tuple[int, int, int]:
    return start_menu_list_entry_center(session, 8)


def start_menu_spawn_clock_center(session: QemuSession) -> Tuple[int, int, int]:
    return start_menu_list_entry_center(session, 9)


def safe_wallpaper_point(session: QemuSession) -> Tuple[int, int]:
    return scaled_point(session, SAFE_WALLPAPER_POINT)


def load_ppm_rgb(path: Path) -> Tuple[int, int, bytes]:
    with path.open("rb") as handle:
        magic = handle.readline().strip()
        if magic != b"P6":
            raise RuntimeError(f"unsupported screenshot format: {magic!r}")

        line = handle.readline()
        while line.startswith(b"#"):
            line = handle.readline()
        width, height = map(int, line.split())

        max_value = int(handle.readline().strip())
        if max_value != 255:
            raise RuntimeError(f"unsupported screenshot max value: {max_value}")

        data = handle.read()

    expected_bytes = width * height * 3
    if len(data) != expected_bytes:
        raise RuntimeError(
            f"short screenshot payload: expected {expected_bytes} bytes, got {len(data)}"
        )
    return width, height, data


def ppm_pixel(data: bytes, width: int, x: int, y: int) -> Tuple[int, int, int]:
    base = ((y * width) + x) * 3
    return (data[base], data[base + 1], data[base + 2])


def desktop_assert_visual_proof(session: QemuSession) -> None:
    screenshot_path = session.workspace / "desktop-visual-proof.ppm"
    session.screendump(screenshot_path)
    width, height, data = load_ppm_rgb(screenshot_path)

    samples = {
        "wallpaper": ppm_pixel(data, width, *safe_wallpaper_point(session)),
        "start_button": ppm_pixel(data, width, *start_button_center(session)),
        "files_icon": ppm_pixel(data, width, *files_icon_center(session)),
        "trash_icon": ppm_pixel(data, width, *trash_icon_center(session)),
    }

    non_black_pixels = 0
    unique_colors = set()
    for index in range(0, len(data), 3):
        color = (data[index], data[index + 1], data[index + 2])
        if color != (0, 0, 0):
            non_black_pixels += 1
        unique_colors.add(color)

    pixel_count = width * height
    non_black_ratio = non_black_pixels / pixel_count if pixel_count else 0.0
    distinct_sample_colors = len(set(samples.values()))

    if samples["wallpaper"] == (0, 0, 0):
        raise RuntimeError(f"desktop visual proof failed: wallpaper sample is black {samples}")
    if samples["start_button"] == (0, 0, 0):
        raise RuntimeError(f"desktop visual proof failed: start button sample is black {samples}")
    if non_black_ratio < 0.25:
        raise RuntimeError(
            f"desktop visual proof failed: non-black ratio too low ({non_black_ratio:.3f})"
        )
    if len(unique_colors) < 4:
        raise RuntimeError(
            f"desktop visual proof failed: unique color count too low ({len(unique_colors)})"
        )
    if distinct_sample_colors < 2:
        raise RuntimeError(
            "desktop visual proof failed: desktop UI samples collapsed to one color "
            f"({samples})"
        )


def log_contains(session: QemuSession, marker: str, start_offset: int = 0) -> bool:
    log = session.read_log()
    if start_offset > 0:
        log = log[start_offset:]
    return marker in log


def wait_for_all_since(session: QemuSession,
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


def click_until_log(session: QemuSession,
                    point_fn: Callable[[QemuSession], Tuple[int, int]],
                    marker: str,
                    attempts: int = 4,
                    settle: float = 0.4,
                    timeout_per_attempt: float = 4.0) -> None:
    if log_contains(session, marker):
        return

    last_error: Optional[Exception] = None
    for attempt in range(attempts):
        time.sleep(settle if attempt == 0 else 0.25)
        try:
            session.reset_mouse_to_center()
            x, y = point_fn(session)
            session.move_mouse_to(x, y, pause=0.03)
            session.left_click(pause=0.08)
        except Exception as exc:
            last_error = exc
            if session.proc.poll() is not None:
                raise
            time.sleep(0.1)
            continue

        deadline = time.time() + timeout_per_attempt
        while time.time() < deadline:
            if log_contains(session, marker):
                return
            if session.proc.poll() is not None:
                break
            time.sleep(0.05)

    if last_error is not None and session.proc.poll() is not None:
        raise last_error
    raise RuntimeError(f"Timed out waiting for marker: {marker}")


def send_shortcut_until_log(session: QemuSession,
                            key: str,
                            marker: str,
                            attempts: int = 3,
                            settle: float = 0.5,
                            timeout_per_attempt: float = 4.0) -> None:
    if log_contains(session, marker):
        return

    last_error: Optional[Exception] = None
    for attempt in range(attempts):
        time.sleep(settle if attempt == 0 else 0.25)
        for sender in (session.send_shortcut, session.send_key):
            try:
                sender(key, pause=0.2)
            except Exception as exc:
                last_error = exc
                if session.proc.poll() is not None:
                    raise
                time.sleep(0.1)
                continue

            deadline = time.time() + timeout_per_attempt
            while time.time() < deadline:
                if log_contains(session, marker):
                    return
                if session.proc.poll() is not None:
                    break
                time.sleep(0.05)

    if last_error is not None and session.proc.poll() is not None:
        raise last_error
    raise RuntimeError(f"Timed out waiting for marker: {marker}")


def open_start_menu_entry_until_log(session: QemuSession,
                                    point_fn: Callable[[QemuSession], Tuple[int, int, int]],
                                    marker: str,
                                    start_offset: int = 0,
                                    attempts: int = 4,
                                    settle: float = 0.4,
                                    timeout_per_attempt: float = 4.0) -> None:
    if log_contains(session, marker, start_offset):
        return

    last_error: Optional[Exception] = None
    for attempt in range(attempts):
        time.sleep(settle if attempt == 0 else 0.25)
        try:
            session.reset_mouse_to_center()
            x, y = safe_wallpaper_point(session)
            session.move_mouse_to(x, y, pause=0.03)
            session.left_click(pause=0.08)

            session.reset_mouse_to_center()
            x, y = start_button_center(session)
            session.move_mouse_to(x, y, pause=0.03)
            session.left_click(pause=0.08)

            session.reset_mouse_to_center()
            x, y, scroll_steps = point_fn(session)
            session.move_mouse_to(x, y, pause=0.03)
            if scroll_steps > 0:
                session.mouse_wheel(-scroll_steps, pause=0.06)
                time.sleep(0.08)
                session.move_mouse_to(x, y, pause=0.03)
            session.left_click(pause=0.08)
        except Exception as exc:
            last_error = exc
            if session.proc.poll() is not None:
                raise
            time.sleep(0.1)
            continue

        deadline = time.time() + timeout_per_attempt
        while time.time() < deadline:
            if log_contains(session, marker, start_offset):
                return
            if session.proc.poll() is not None:
                break
            time.sleep(0.05)

    if last_error is not None and session.proc.poll() is not None:
        raise last_error
    raise RuntimeError(f"Timed out waiting for marker: {marker}")


def wait_for_terminal_command_done(session: QemuSession, command: str, timeout: float = 12.0) -> None:
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


def wait_for_terminal_command_exit(session: QemuSession,
                                   command: str,
                                   rc: int = 0,
                                   timeout: float = 12.0) -> None:
    exit_marker = f"terminal: command exit {command} rc={rc}"
    if log_contains(session, exit_marker):
        return
    try:
        session.wait_for_log(exit_marker, timeout=timeout)
    except RuntimeError:
        if log_contains(session, f"terminal: command start {command}") or \
           log_contains(session, f"shell: command {command}"):
            raise RuntimeError(f"Timed out waiting for marker: {exit_marker}")
        raise


def run_terminal_command_expect(session: QemuSession,
                                command: str,
                                markers: List[str],
                                timeout: float = 16.0,
                                rc: int = 0,
                                pause: float = 0.12) -> None:
    start_offset = len(session.read_log())
    command_name = command.split(" ", 1)[0] if command else ""

    run_command(session, command, timeout=max(8.0, timeout / 2.0), marker="", pause=pause)
    wait_for_all_since(session,
                       [
                           f"shell: command {command_name}",
                           f"terminal: command exit {command_name} rc={rc}",
                           f"terminal: command done {command_name}",
                           *markers,
                       ],
                       timeout=timeout,
                       start_offset=start_offset)


def scenario_boot_rescue_shell(session: QemuSession, attempts: int = 4, window_s: float = 25.0) -> None:
    """Closed-loop rescue selection (see validate_network_surface.py): pulse
    s,s,<ret> until the kernel confirms `boot mode: rescue shell`; cold
    restart on a wrong-mode confirmation; never blind-timing out."""
    for attempt in range(attempts):
        if log_contains(session, "boot mode: rescue shell") and log_contains(session, SHELL_READY_MARKER):
            return
        deadline = time.time() + window_s
        while time.time() < deadline:
            session.send_shortcut("s", pause=0.12)
            session.send_shortcut("s", pause=0.12)
            session.send_shortcut("ret", pause=0.25)
            if log_contains(session, "boot mode: rescue shell"):
                break
            if log_contains(session, "boot mode: safe mode") or \
               log_contains(session, "desktop.app: launch startx") or \
               log_contains(session, "host: argv startx"):
                break
            time.sleep(1.0)
        if log_contains(session, "boot mode: rescue shell"):
            session.wait_for_all([SHELL_READY_MARKER], timeout=240.0)
            return
        session.restart()
    raise RuntimeError(
        "Timed out selecting rescue shell: 'boot mode: rescue shell' never "
        f"appeared after {attempts} menu attempts (with cold restarts)")


def scenario_startx(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch startx", DESKTOP_READY_MARKER], timeout=90.0)
    time.sleep(1.0)
    session.send_key("ctrl-y", pause=0.2)
    session.wait_for_all(
        [
            "desktop: key 25",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
        ],
        timeout=12.0,
    )


def run_command(session: QemuSession, command: str, timeout: float = 6.0, pause: float = 0.12,
                marker: Optional[str] = None, use_hmp: bool = False) -> None:
    command_marker = marker
    if command_marker is None:
        command_marker = command.split(" ", 1)[0] if command else ""
    session.type_text(command, pause=pause, use_hmp=use_hmp)
    if use_hmp:
        time.sleep(0.10)
        session.send_shortcut("ret", pause=0.20)
    else:
        session.send_key("ret", pause=0.15)
    if command_marker:
        session.wait_for_log(f"shell: command {command_marker}", timeout=timeout)


def scenario_terminal_runtime(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch terminal", "desktop: open-new w=0 t=1 i=0"], timeout=20.0)
    time.sleep(1.0)
    run_command(session, "cc /hello.c", timeout=8.0, marker="")
    session.wait_for_all(
        [
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
        ],
        timeout=12.0,
    )


def scenario_phase_e_writeback_shell(session: QemuSession) -> None:
    touch_path = "/tmp/phasee.tmp"
    touch_start = len(session.read_log())
    run_command(session, f"touch {touch_path}", timeout=10.0, pause=0.12)
    wait_for_all_since(session,
                       ["shell: command touch", "shell: ready"],
                       timeout=4.0,
                       start_offset=touch_start)
    session.wait_for_log("fs: writeback start", timeout=12.0)
    run_command(session, "cc /hello.c", timeout=10.0, marker="", pause=0.12)
    session.wait_for_all(
        [
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
            "fs: writeback done",
        ],
        timeout=20.0,
    )


def scenario_phase_e_terminal_cc(session: QemuSession) -> None:
    scenario_startx(session)
    time.sleep(1.0)
    run_command(session, "cc /hello.c", timeout=8.0, marker="")
    session.wait_for_all(
        [
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
            "terminal: command done cc",
        ],
        timeout=16.0,
    )


def scenario_phase_e_terminal_grep(session: QemuSession) -> None:
    scenario_startx(session)
    time.sleep(1.0)
    run_command(session, "/compat/bin/grep print /hello.c", timeout=8.0, marker="")
    session.wait_for_all(
        [
            "busybox: external ok grep",
            "grep: match ok",
            "terminal: command done /compat/bin/grep",
        ],
        timeout=16.0,
    )


def scenario_phase_e_terminal_writeback(session: QemuSession) -> None:
    touch_path = f"/tmp/phase-e-writeback-{time.time_ns()}"
    scenario_startx(session)
    time.sleep(1.0)
    run_command(session, f"touch {touch_path}", timeout=6.0, marker="")
    wait_for_terminal_command_done(session, "touch", timeout=8.0)
    session.wait_for_log("fs: writeback start", timeout=12.0)
    run_command(session, "cc /hello.c", timeout=8.0, marker="")
    session.wait_for_all(
        [
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
            "fs: writeback done",
            "terminal: command done cc",
        ],
        timeout=20.0,
    )


def scenario_terminal_vidmodes(session: QemuSession) -> None:
    scenario_startx(session)
    time.sleep(1.0)
    run_command(session, "vidmodes", timeout=8.0)
    session.wait_for_all(
        [
            "vidmodes: begin",
            "vidmodes: caps mode_count=",
            "video: runtime modeset source=",
            "video: handoff stage=runtime source=",
            "vidmodes: try 1024x768",
            "vidmodes: mode verify ok 1024x768",
            "vidmodes: mode ok 1024x768",
            "vidmodes: restore ok ",
            "vidmodes: summary ok=",
        ],
        timeout=40.0,
    )
    time.sleep(1.0)
    desktop_assert_visual_proof(session)


def scenario_network_terminal_surface(session: QemuSession) -> None:
    scenario_startx(session)
    time.sleep(1.0)

    commands = [
        ("ifconfig", ["ifconfig: status ok"]),
        ("route", ["route: table ok"]),
        ("netstat", ["netstat: telemetry ok"]),
        ("ping localhost", ["ping: loopback-ok target=localhost"]),
        ("host localhost", ["host: local-ok query=localhost"]),
        ("dig localhost", ["dig: local-ok query=localhost"]),
        ("curl file:///runtime/netmgrd-status.txt", ["curl: file-ok target=/runtime/netmgrd-status.txt"]),
        ("ftp example.com", ["ftp: transport unavailable"]),
    ]

    for command, markers in commands:
        run_terminal_command_expect(session, command, markers, timeout=18.0)

    run_terminal_command_expect(session,
                                "spawn clock",
                                ["spawn: launched pid=", "host: argv clock"],
                                timeout=16.0)


def scenario_bsd_utils_terminal_surface(session: QemuSession) -> None:
    scenario_startx(session)
    try:
        session.wait_for_log("audio: done desktop-session", timeout=12.0)
    except RuntimeError:
        time.sleep(4.5)
    else:
        time.sleep(1.5)

    commands = [
        ("uname -a", [], 0),
        ("pwd", [], 0),
        ("printf abi-smoke\\n", [], 0),
        ("true", [], 0),
    ]

    for command, markers, rc in commands:
        run_terminal_command_expect(session, command, markers, timeout=45.0, rc=rc, pause=0.18)


def scenario_editor_compat(session: QemuSession, command_name: str, text: str) -> None:
    scenario_startx(session)
    time.sleep(1.0)
    run_command(session, command_name, timeout=8.0, marker="")
    session.wait_for_all(
        [
            f"shell: command {command_name}",
            f"terminal: command exit {command_name} rc=0",
            f"terminal: command done {command_name}",
            "desktop: open-editor",
        ],
        timeout=20.0,
    )
    time.sleep(1.0)
    session.type_text(text, pause=0.08)
    session.send_key("ctrl-s", pause=0.2)
    session.wait_for_log("editor: save ok path=/docs/nota", timeout=12.0)


def scenario_vi_compat_editor(session: QemuSession) -> None:
    scenario_editor_compat(session, "vi", "vi smoke")


def scenario_mg_compat_editor(session: QemuSession) -> None:
    scenario_editor_compat(session, "mg", "mg smoke")


def scenario_desktop_visual_proof(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch startx", DESKTOP_READY_MARKER], timeout=45.0)
    session.wait_for_log("video: backend refresh backend=fast_lfb", timeout=20.0)
    session.wait_for_log("video: shadow backbuffer enabled bytes=", timeout=20.0)
    session.wait_for_log("desktop: visual ready", timeout=20.0)
    time.sleep(1.0)
    desktop_assert_visual_proof(session)


def scenario_open_terminal_combo(session: QemuSession, timeout: float = 45.0) -> None:
    _ = timeout
    scenario_startx(session)


def scenario_open_terminal(session: QemuSession, timeout: float = 45.0) -> None:
    session.wait_for_all(["desktop.app: launch startx", DESKTOP_READY_MARKER], timeout=timeout)
    open_start_menu_entry_until_log(session,
                                    start_menu_terminal_center,
                                    "desktop: open-new w=0 t=1 i=0",
                                    attempts=6,
                                    settle=0.8,
                                    timeout_per_attempt=6.0)


def scenario_doom(session: QemuSession) -> None:
    scenario_open_terminal(session)
    run_command(session, "doom", timeout=8.0, marker="")
    session.wait_for_all(["desktop: open-new w=1 t=16 i=0"], timeout=20.0)
    time.sleep(0.8)
    session.send_key("ret", pause=0.15)
    session.wait_for_all(
        [
            "doom: key enter",
            "fs: asset file /DOOM/DOOM.WAD",
            "doom: port run begin",
        ],
        timeout=20.0,
    )


def scenario_craft(session: QemuSession) -> None:
    scenario_open_terminal(session)
    run_command(session, "craft", timeout=8.0, marker="")
    session.wait_for_all(["desktop: open-new w=1 t=17 i=0"], timeout=20.0)
    session.wait_for_all(
        [
            "fs: asset file /textures/texture.png",
            "fs: asset file /textures/font.png",
            "fs: asset file /textures/sky.png",
            "fs: asset file /textures/sign.png",
            "craft: start",
            "craft: after textures",
            "craft: session ready",
            "craft: first frame rc=",
        ],
        timeout=25.0,
    )


def scenario_service_restart_shortcut(session: QemuSession,
                                      shortcut: str,
                                      key_marker: str,
                                      kill_marker: str,
                                      restart_marker: str,
                                      recovery_markers: Optional[List[str]] = None) -> None:
    session.wait_for_all(["desktop.app: launch startx", DESKTOP_READY_MARKER], timeout=45.0)
    time.sleep(1.0)
    session.send_key(shortcut, pause=0.2)

    markers = [
        key_marker,
        "desktop: open-new w=0 t=3 i=0",
        "desktop: open-new w=1 t=1 i=0",
        "shell: command kill",
        "terminal: command start kill",
        kill_marker,
        restart_marker,
        "kill: terminated pid=",
        "terminal: command done kill",
        "desktop: restart smoke followup",
        "shell: command spawn",
        "terminal: command start spawn",
        "spawn: launched pid=",
        "terminal: command done spawn",
        "host: argv clock",
    ]
    if recovery_markers:
        markers.extend(recovery_markers)
    session.wait_for_all(markers, timeout=20.0)


def scenario_service_restart_menu_click(session: QemuSession,
                                        restart_point_fn: Callable[[QemuSession], Tuple[int, int, int]],
                                        kill_marker: str,
                                        restart_marker: str,
                                        recovery_markers: Optional[List[str]] = None) -> None:
    session.wait_for_all(["desktop.app: launch startx", DESKTOP_READY_MARKER], timeout=45.0)
    open_start_menu_entry_until_log(session,
                                    restart_point_fn,
                                    "shell: command kill",
                                    attempts=6,
                                    settle=0.8,
                                    timeout_per_attempt=6.0)

    markers = [
        "shell: command kill",
        "terminal: command start kill",
        kill_marker,
        restart_marker,
        "kill: terminated pid=",
        "terminal: command done kill",
        "desktop: restart smoke followup",
        "shell: command spawn",
        "terminal: command done spawn",
    ]
    if recovery_markers:
        markers.extend(recovery_markers)
    session.wait_for_all(markers, timeout=20.0)


def scenario_input_restart(session: QemuSession) -> None:
    scenario_service_restart_shortcut(session,
                                      shortcut="ctrl-k",
                                      key_marker="desktop: key 11",
                                      kill_marker="kill: request target=input",
                                      restart_marker="service: restarting type=5",
                                      recovery_markers=["desktop: input reset"])


def scenario_audio_restart(session: QemuSession) -> None:
    scenario_service_restart_shortcut(session,
                                      shortcut="ctrl-u",
                                      key_marker="desktop: key 21",
                                      kill_marker="kill: request target=audio",
                                      restart_marker="service: restarting type=8")


def scenario_video_restart(session: QemuSession) -> None:
    scenario_service_restart_shortcut(session,
                                      shortcut="ctrl-v",
                                      key_marker="desktop: key 22",
                                      kill_marker="kill: request target=video",
                                      restart_marker="service: restarting type=4")


def scenario_network_restart(session: QemuSession) -> None:
    scenario_service_restart_shortcut(session,
                                      shortcut="ctrl-w",
                                      key_marker="desktop: key 23",
                                      kill_marker="kill: request target=network",
                                      restart_marker="service: restarting type=7")


def scenario_input_restart_mouse(session: QemuSession) -> None:
    scenario_service_restart_menu_click(session,
                                        restart_point_fn=start_menu_input_restart_center,
                                        kill_marker="kill: request target=input",
                                        restart_marker="service: restarting type=5",
                                        recovery_markers=["desktop: input reset"])


def scenario_audio_restart_mouse(session: QemuSession) -> None:
    scenario_service_restart_menu_click(session,
                                        restart_point_fn=start_menu_audio_restart_center,
                                        kill_marker="kill: request target=audio",
                                        restart_marker="service: restarting type=8")


def scenario_video_restart_mouse(session: QemuSession) -> None:
    scenario_service_restart_menu_click(session,
                                        restart_point_fn=start_menu_video_restart_center,
                                        kill_marker="kill: request target=video",
                                        restart_marker="service: restarting type=4")


def scenario_network_restart_mouse(session: QemuSession) -> None:
    scenario_service_restart_menu_click(session,
                                        restart_point_fn=start_menu_network_restart_center,
                                        kill_marker="kill: request target=network",
                                        restart_marker="service: restarting type=7")


def scenario_spawn_clock_shell(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch startx", DESKTOP_READY_MARKER], timeout=45.0)
    time.sleep(1.0)
    session.send_key("ctrl-l", pause=0.2)
    session.wait_for_all(
        [
            "desktop: key 12",
            "desktop: open-new w=0 t=1 i=0",
            "shell: command spawn",
            "spawn: launched pid=",
            "host: app start",
        ],
        timeout=12.0,
    )


SCENARIOS = [
    Scenario(
        name="startx-desktop",
        description="Shell -> startx.app -> desktop session -> single smoke shortcut opens files + terminal",
        command="startx",
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
        ],
        action=scenario_startx,
    ),
    Scenario(
        name="startx-autoboot-desktop",
        description="Userland autostart reaches desktop session and opens files + terminal via single smoke shortcut",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_startx,
    ),
    Scenario(
        name="terminal-app",
        description="Dedicated terminal launcher app",
        command="terminal",
        must_have=["desktop.app: launch terminal", "desktop: open-new w=0 t=1 i=0"],
    ),
    Scenario(
        name="clock-app",
        description="Dedicated clock launcher app",
        command="clock",
        must_have=["desktop.app: launch clock", "desktop: open-new w=0 t=2 i=0"],
    ),
    Scenario(
        name="filemanager-app",
        description="Dedicated filemanager launcher app",
        command="filemanager",
        must_have=["desktop.app: launch filemanager", "desktop: open-new w=0 t=3 i=0"],
    ),
    Scenario(
        name="editor-app",
        description="Dedicated editor launcher app",
        command="editor",
        must_have=["desktop.app: launch editor", "desktop: open-new w=0 t=4 i=0"],
    ),
    Scenario(
        name="taskmgr-app",
        description="Dedicated task manager launcher app",
        command="taskmgr",
        must_have=["desktop.app: launch taskmgr", "desktop: open-new w=0 t=5 i=0"],
    ),
    Scenario(
        name="calculator-app",
        description="Dedicated calculator launcher app",
        command="calculator",
        must_have=["desktop.app: launch calculator", "desktop: open-new w=0 t=6 i=0"],
    ),
    Scenario(
        name="sketchpad-app",
        description="Dedicated sketchpad launcher app",
        command="sketchpad",
        must_have=["desktop.app: launch sketchpad", "desktop: open-new w=0 t=7 i=0"],
    ),
    Scenario(
        name="personalize-app",
        description="Dedicated personalize launcher app",
        command="personalize",
        must_have=["desktop.app: launch personalize", "desktop: open-new w=0 t=18 i=0"],
    ),
    Scenario(
        name="edit-app",
        description="Dedicated edit launcher app",
        command="edit",
        must_have=["desktop.app: launch edit", "desktop: open-editor w=0 t=4 i=0"],
    ),
    Scenario(
        name="nano-app",
        description="Dedicated nano launcher app",
        command="nano",
        must_have=["desktop.app: launch nano", "desktop: open-editor w=0 t=4 i=0"],
    ),
    Scenario(
        name="terminal-runtime-apps",
        description="Graphical terminal executes runtime alias cc against /hello.c",
        command="terminal",
        must_have=[
            "desktop.app: launch terminal",
            "desktop: open-new w=0 t=1 i=0",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
        ],
        action=scenario_terminal_runtime,
    ),
    Scenario(
        name="cc-alias-shell",
        description="Text shell resolves cc as sectorc.app and compiles /hello.c",
        command="cc /hello.c",
        command_marker="cc",
        must_have=[
            "shell: command cc",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
        ],
        boot_markers=["boot mode: rescue shell", SHELL_READY_MARKER],
        boot_action=scenario_boot_rescue_shell,
    ),
    Scenario(
        name="java-explicit-path",
        description="Text shell executes java through /bin/java",
        command="/bin/java -version",
        command_marker="/bin/java",
        must_have=[
            "shell: command /bin/java",
            "busybox: external ok java",
            "java: version ok",
        ],
        boot_markers=["boot mode: rescue shell", SHELL_READY_MARKER],
        boot_action=scenario_boot_rescue_shell,
    ),
    Scenario(
        name="grep-explicit-path",
        description="Text shell executes grep through /compat/bin/grep against /hello.c",
        command="/compat/bin/grep print /hello.c",
        command_marker="/compat/bin/grep",
        must_have=[
            "shell: command /compat/bin/grep",
            "busybox: external ok grep",
            "grep: match ok",
        ],
        boot_markers=["boot mode: rescue shell", SHELL_READY_MARKER],
        boot_action=scenario_boot_rescue_shell,
    ),
    Scenario(
        name="rescue-shell-boot",
        description="Stage2 rescue-shell path reaches the shell host and accepts input without relying on desktop launch",
        command=None,
        must_have=[
            "boot mode: rescue shell",
            SHELL_READY_MARKER,
        ],
        boot_markers=["boot mode: rescue shell", SHELL_READY_MARKER],
        boot_action=scenario_boot_rescue_shell,
    ),
    Scenario(
        name="phase-e-writeback-shell",
        description="Text shell keeps launching runtime reads while filesystem writeback advances in the background",
        command=None,
        must_have=[
            "shell: command touch",
            "fs: writeback start",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
            "fs: writeback done",
        ],
        boot_markers=["boot mode: rescue shell", SHELL_READY_MARKER],
        boot_action=scenario_boot_rescue_shell,
        action=scenario_phase_e_writeback_shell,
    ),
    Scenario(
        name="phase-e-terminal-cc",
        description="Desktop stable-shortcut path opens the terminal and runs the native cc alias through sectorc.app",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
            "terminal: command done cc",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_phase_e_terminal_cc,
    ),
    Scenario(
        name="phase-e-terminal-grep",
        description="Desktop stable-shortcut path resolves /compat/bin/grep through the native executable alias boundary",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
            "busybox: external ok grep",
            "grep: match ok",
            "terminal: command done /compat/bin/grep",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_phase_e_terminal_grep,
    ),
    Scenario(
        name="phase-e-terminal-writeback",
        description="Desktop stable-shortcut path keeps terminal runtime reads working while filesystem writeback drains in the background",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
            "terminal: command done touch",
            "fs: writeback start",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
            "fs: writeback done",
            "terminal: command done cc",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_phase_e_terminal_writeback,
    ),
    Scenario(
        name="network-terminal-surface",
        description="Desktop stable-shortcut path runs the network CLI surface inside the Terminal app and confirms the graphical userspace stays alive by launching clock afterwards",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
            "ifconfig: status ok",
            "route: table ok",
            "netstat: telemetry ok",
            "ping: loopback-ok target=localhost",
            "host: local-ok query=localhost",
            "dig: local-ok query=localhost",
            "curl: file-ok target=/runtime/netmgrd-status.txt",
            "ftp: transport unavailable",
            "terminal: command done ftp",
            "spawn: launched pid=",
            "host: argv clock",
            "terminal: command done spawn",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_network_terminal_surface,
    ),
    Scenario(
        name="bsd-utils-terminal-surface",
        description="Desktop stable-shortcut path runs representative BSD utilities through the generic terminal/external-app boundary and checks their exit status markers",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
            "terminal: command exit uname rc=0",
            "terminal: command exit pwd rc=0",
            "terminal: command exit printf rc=0",
            "terminal: command exit true rc=0",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_bsd_utils_terminal_surface,
    ),
    Scenario(
        name="vi-compat-editor",
        description="Desktop terminal launches the compat vi alias, reaches the editor path, accepts typing, and saves a document",
        command=None,
        must_have=[
            "shell: command vi",
            "terminal: command done vibefetch",
            "terminal: command exit vi rc=0",
            "terminal: command done vi",
            "desktop: open-editor",
            "editor: save ok path=/docs/nota",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_vi_compat_editor,
    ),
    Scenario(
        name="mg-compat-editor",
        description="Desktop terminal launches the compat mg alias, reaches the nano-style editor path, accepts typing, and saves a document",
        command=None,
        must_have=[
            "shell: command mg",
            "terminal: command done vibefetch",
            "terminal: command exit mg rc=0",
            "terminal: command done mg",
            "desktop: open-editor",
            "editor: save ok path=/docs/nota",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_mg_compat_editor,
    ),
    Scenario(
        name="doom-assets-app",
        description="Desktop autostart opens a terminal, launches DOOM and reaches the real WAD runtime",
        command=None,
        must_have=[
            "desktop: open-new w=0 t=1 i=0",
            "desktop: open-new w=1 t=16 i=0",
            "doom: key enter",
            "fs: asset file /DOOM/DOOM.WAD",
            "doom: port run begin",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_doom,
    ),
    Scenario(
        name="craft-assets-app",
        description="Desktop autostart opens a terminal, launches Craft and reaches real texture loads and first frame",
        command=None,
        must_have=[
            "desktop: open-new w=0 t=1 i=0",
            "desktop: open-new w=1 t=17 i=0",
            "fs: asset file /textures/texture.png",
            "fs: asset file /textures/font.png",
            "fs: asset file /textures/sky.png",
            "fs: asset file /textures/sign.png",
            "craft: after textures",
            "craft: session ready",
            "craft: first frame rc=",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_craft,
    ),
    Scenario(
        name="vidmodes-shell",
        description="Desktop autostart uses the stable desktop shortcut path, then exercises runtime video mode switching through vidmodes",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "terminal: command done vibefetch",
            "shell: command vidmodes",
            "vidmodes: begin",
            "vidmodes: caps mode_count=",
            "video: runtime modeset source=",
            "video: handoff stage=runtime source=",
            "vidmodes: try 1024x768",
            "video: shadow backbuffer enabled bytes=",
            "video: runtime mode backend=fast_lfb",
            "vidmodes: mode verify ok 1024x768",
            "vidmodes: mode ok 1024x768",
            "vidmodes: restore ok ",
            "vidmodes: summary ok=",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_terminal_vidmodes,
    ),
    Scenario(
        name="desktop-visual-proof",
        description="Desktop autostart reaches visual-ready on fast_lfb and produces a visible QEMU screenshot with distinct desktop UI samples",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "video: backend refresh backend=fast_lfb",
            "video: shadow backbuffer enabled bytes=",
            "desktop: visual ready",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_desktop_visual_proof,
    ),
    Scenario(
        name="input-restart-desktop",
        description="Desktop autostart uses a single shortcut to restart input, waits for the async recovery followup, then launches clock in a detached app context",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: key 11",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "shell: command kill",
            "kill: request target=input",
            "kill: terminated pid=",
            "service: restarting type=5",
            "terminal: command done kill",
            "desktop: input reset",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "spawn: launched pid=",
            "terminal: command done spawn",
            "host: argv clock",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_input_restart,
    ),
    Scenario(
        name="audio-restart-desktop",
        description="Desktop autostart uses a single shortcut to restart audiosvc, waits for the async recovery followup, then launches clock in a detached app context",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: key 21",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "shell: command kill",
            "kill: request target=audio",
            "kill: terminated pid=",
            "service: restarting type=8",
            "terminal: command done kill",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "spawn: launched pid=",
            "terminal: command done spawn",
            "host: argv clock",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_audio_restart,
    ),
    Scenario(
        name="input-restart-mouse-desktop",
        description="Desktop autostart clicks the start-menu restart entry for inputsvc and confirms the async recovery followup still runs under pointer-driven control",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "shell: command kill",
            "kill: request target=input",
            "kill: terminated pid=",
            "service: restarting type=5",
            "terminal: command done kill",
            "desktop: input reset",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "terminal: command done spawn",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_input_restart_mouse,
    ),
    Scenario(
        name="audio-restart-mouse-desktop",
        description="Desktop autostart clicks the start-menu restart entry for audiosvc and confirms the async recovery followup still runs under pointer-driven control",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "shell: command kill",
            "kill: request target=audio",
            "kill: terminated pid=",
            "service: restarting type=8",
            "terminal: command done kill",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "terminal: command done spawn",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_audio_restart_mouse,
    ),
    Scenario(
        name="network-restart-desktop",
        description="Desktop autostart uses a single shortcut to restart network, waits for the async recovery followup, then launches clock in a detached app context",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: key 23",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "shell: command kill",
            "kill: request target=network",
            "kill: terminated pid=",
            "service: restarting type=7",
            "terminal: command done kill",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "spawn: launched pid=",
            "terminal: command done spawn",
            "host: argv clock",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_network_restart,
    ),
    Scenario(
        name="network-restart-mouse-desktop",
        description="Desktop autostart clicks the start-menu restart entry for network and confirms the async recovery followup still runs under pointer-driven control",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "shell: command kill",
            "kill: request target=network",
            "kill: terminated pid=",
            "service: restarting type=7",
            "terminal: command done kill",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "terminal: command done spawn",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_network_restart_mouse,
    ),
    Scenario(
        name="video-restart-desktop",
        description="Desktop autostart uses a single shortcut to restart videosvc, waits for the async recovery followup, then launches clock in a detached app context",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: key 22",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
            "shell: command kill",
            "kill: request target=video",
            "kill: terminated pid=",
            "service: restarting type=4",
            "terminal: command done kill",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "spawn: launched pid=",
            "terminal: command done spawn",
            "host: argv clock",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_video_restart,
    ),
    Scenario(
        name="video-restart-mouse-desktop",
        description="Desktop autostart clicks the start-menu restart entry for videosvc and confirms the async recovery followup still runs under pointer-driven control",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "shell: command kill",
            "kill: request target=video",
            "kill: terminated pid=",
            "service: restarting type=4",
            "terminal: command done kill",
            "desktop: restart smoke followup",
            "shell: command spawn",
            "terminal: command done spawn",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_video_restart_mouse,
    ),
    Scenario(
        name="spawn-clock-shell",
        description="Desktop shortcut opens a terminal and launches the modular clock AppFS app in its own detached task context",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: key 12",
            "desktop: open-new w=0 t=1 i=0",
            "shell: command spawn",
            "spawn: launched pid=",
            "host: app start",
        ],
        boot_markers=[
            *AUTODESKTOP_BOOT_MARKERS,
        ],
        action=scenario_spawn_clock_shell,
    ),
]


def run_scenario_once(qemu_binary: str, image_path: Path, memory_mb: int, scenario: Scenario) -> ScenarioResult:
    with tempfile.TemporaryDirectory(prefix="vmod-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)

        session = QemuSession(qemu_binary, scenario_image, memory_mb, workspace)
        error: Optional[str] = None
        log = ""
        try:
            if scenario.boot_action:
                scenario.boot_action(session)
            session.wait_for_all(scenario.boot_markers or [BOOT_MARKER, SHELL_READY_MARKER], timeout=60.0)
            rescue_shell = bool(scenario.boot_markers and "boot mode: rescue shell" in scenario.boot_markers)
            if rescue_shell:
                time.sleep(0.6)
            if scenario.command:
                command_timeout = 6.0
                if rescue_shell:
                    command_timeout = max(10.0, 2.0 + (0.20 * (len(scenario.command) + 1)))
                run_command(session,
                            scenario.command,
                            timeout=command_timeout,
                            pause=0.12,
                            marker=scenario.command_marker,
                            use_hmp=False)
            if scenario.action:
                scenario.action(session)
            else:
                session.wait_for_all(scenario.must_have, timeout=16.0)
        except Exception as exc:
            error = str(exc)
        finally:
            session.close()
            time.sleep(0.1)
            log = session.read_log()

    missing = [marker for marker in scenario.must_have if marker not in log]
    return ScenarioResult(
        scenario=scenario,
        passed=(not missing and error is None),
        log=log,
        missing_markers=missing,
        error=error,
    )


def run_scenario(qemu_binary: str, image_path: Path, memory_mb: int, scenario: Scenario) -> ScenarioResult:
    result = run_scenario_once(qemu_binary, image_path, memory_mb, scenario)

    # Some QEMU boots still occasionally die before the scenario itself
    # begins (for example an early SMP/AP bring-up fault). Retry once only
    # when the first attempt produced no scenario-specific evidence at all.
    if result.passed:
        return result
    if any(marker in result.log for marker in scenario.must_have):
        return result
    return run_scenario_once(qemu_binary, image_path, memory_mb, scenario)


def write_report(report_path: Path, results: List[ScenarioResult]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Modular Apps Validation Report",
        "",
        "## QEMU Interactive Matrix",
        "",
        "| Scenario | Result | Notes |",
        "| --- | --- | --- |",
    ]

    for result in results:
        notes = result.scenario.description
        if result.missing_markers:
            notes = "missing: " + ", ".join(result.missing_markers)
        if result.error:
            notes = (notes + " | " if notes else "") + result.error
        lines.append(f"| {result.scenario.name} | {'PASS' if result.passed else 'FAIL'} | {notes} |")

    lines.extend(["", "## Marker Summary", ""])
    for result in results:
        lines.append(f"### {result.scenario.name}")
        if result.passed:
            lines.append("- required markers observed")
        else:
            lines.append("- missing markers: " + ", ".join(result.missing_markers))
        if result.error:
            lines.append("- error: " + result.error)
        observed = [marker for marker in result.scenario.must_have if marker in result.log]
        if observed:
            lines.append("- observed markers: " + ", ".join(observed))
        preview = [line for line in result.log.strip().splitlines() if line][-20:]
        if preview:
            lines.extend(["", "```text", *preview, "```"])
        lines.append("")

    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate modular desktop/launcher apps in headless QEMU.")
    parser.add_argument("--image", required=True, help="boot image to validate")
    parser.add_argument("--report", required=True, help="markdown report output path")
    parser.add_argument("--qemu", default="qemu-system-i386", help="QEMU binary")
    parser.add_argument("--memory-mb", type=int, default=3072, help="guest RAM in MB")
    parser.add_argument("--scenario", action="append",
                        help="run only the named scenario (can be passed multiple times)")
    parser.add_argument("--expect-boot-mode",
                        help="require a boot-time video init marker for the given WxH mode, e.g. 800x600")
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    report_path = Path(args.report).resolve()
    if not image_path.is_file():
        print(f"error: boot image not found: {image_path}", file=sys.stderr)
        return 1

    qemu_binary = shutil.which(args.qemu) or shutil.which("qemu-system-x86_64")
    if qemu_binary is None:
        print("error: no QEMU binary found", file=sys.stderr)
        return 1

    selected_scenarios = SCENARIOS
    if args.scenario:
        selected_names = set(args.scenario)
        selected_scenarios = [scenario for scenario in SCENARIOS if scenario.name in selected_names]
        missing_names = sorted(selected_names - {scenario.name for scenario in selected_scenarios})
        if missing_names:
            print("error: unknown scenario(s): " + ", ".join(missing_names), file=sys.stderr)
            return 1

    expected_boot_mode = None
    if args.expect_boot_mode:
        expected_boot_mode = parse_mode_text(args.expect_boot_mode)
        if expected_boot_mode is None:
            print("error: invalid --expect-boot-mode value, expected WxH", file=sys.stderr)
            return 1
        selected_scenarios = [
            Scenario(
                name=f"{scenario.name}-boot-{expected_boot_mode}",
                description=f"{scenario.description}; boot should initialize in {expected_boot_mode}",
                command=scenario.command,
                must_have=[*scenario.must_have, "video: boot init backend=", f"mode={expected_boot_mode}x8"],
                boot_markers=(scenario.boot_markers or [BOOT_MARKER, SHELL_READY_MARKER]),
                command_marker=scenario.command_marker,
                action=scenario.action,
            )
            for scenario in selected_scenarios
        ]

    results = [run_scenario(qemu_binary, image_path, args.memory_mb, scenario) for scenario in selected_scenarios]
    write_report(report_path, results)

    failed = [result for result in results if not result.passed]
    if failed:
        print(f"modular-apps: {len(failed)} scenario(s) failed; see {report_path}", file=sys.stderr)
        return 1

    print(f"modular-apps: validation passed; report written to {report_path}")
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    sys.exit(main())

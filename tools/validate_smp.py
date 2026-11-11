#!/usr/bin/env python3
import argparse
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List


@dataclass
class Scenario:
    name: str
    description: str
    qemu_args: List[str]
    must_have: List[str]
    toggle_smp: bool = False


@dataclass
class ScenarioResult:
    scenario: Scenario
    passed: bool
    log: str
    missing_markers: List[str]
    error: str = ""


class QemuSession:
    def __init__(self, qemu_binary: str, image_path: Path, memory_mb: int, workspace: Path, qemu_args: List[str]):
        self.serial_log = workspace / "serial.log"
        self.monitor_socket = workspace / "monitor.sock"
        cmd = [
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
            "-no-reboot",
            "-no-shutdown",
        ] + qemu_args
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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

    def send_key(self, key: str, pause: float = 0.10) -> None:
        self.hmp(f"sendkey {key}")
        time.sleep(pause)

    def read_log(self) -> str:
        if not self.serial_log.exists():
            return ""
        return self.serial_log.read_text(encoding="utf-8", errors="replace")

    def wait_for_any(self, markers: List[str], timeout: float) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            log = self.read_log()
            if any(marker in log for marker in markers):
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for any marker: " + " | ".join(markers))

    def wait_for_markers(self, markers: List[str], timeout: float) -> None:
        """Poll the serial log until every marker appears. Never raises:
        slow TCG hosts still get the full log read once the deadline hits."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            log = self.read_log()
            if all(marker in log for marker in markers):
                return
            if "kernel_panic:" in log:
                return
            if self.proc.poll() is not None:
                return
            time.sleep(0.5)


def run_scenario(qemu_binary: str, image_path: Path, memory_mb: int, scenario: Scenario, timeout_seconds: float) -> ScenarioResult:
    with tempfile.TemporaryDirectory(prefix=f"vibe-smp-{scenario.name}-") as temp_dir:
        scenario_image = Path(temp_dir) / "boot.img"
        shutil.copyfile(image_path, scenario_image)

        try:
            session = QemuSession(qemu_binary, scenario_image, memory_mb, Path(temp_dir), scenario.qemu_args)
            if scenario.toggle_smp:
                time.sleep(2.0)
                session.send_key("m")
                session.send_key("ret")
            session.wait_for_markers(scenario.must_have, timeout=timeout_seconds)
            time.sleep(1.0)
            log = session.read_log()
            session.close()
        except Exception as exc:
            log = ""
            try:
                log = session.read_log()  # type: ignore[name-defined]
                session.close()  # type: ignore[name-defined]
            except Exception:
                pass
            missing = [marker for marker in scenario.must_have if marker not in log]
            return ScenarioResult(scenario=scenario, passed=False, log=log, missing_markers=missing, error=str(exc))

    missing = [marker for marker in scenario.must_have if marker not in log]
    return ScenarioResult(scenario=scenario, passed=not missing, log=log, missing_markers=missing)


def write_report(report_path: Path, results: List[ScenarioResult]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# SMP Validation Report", ""]
    for result in results:
        status = "PASS" if result.passed else "FAIL"
        tail = result.log.strip()[-6000:] if result.log.strip() else "(empty log)"
        lines.extend(
            [
                f"## {result.scenario.name}: {status}",
                "",
                result.scenario.description,
                "",
                f"- Required markers: {', '.join(result.scenario.must_have)}",
                f"- Missing markers: {', '.join(result.missing_markers) if result.missing_markers else '(none)'}",
                f"- Error: {result.error if result.error else '(none)'}",
                "",
                "```text",
                tail,
                "```",
                "",
            ]
        )
    report_path.write_text("\n".join(lines), encoding="utf-8")


def build_scenarios() -> List[Scenario]:
    common = ["init: entered builtin entry"]
    return [
        Scenario(
            name="fallback-no-toggle",
            description="Validates clean BSP-only fallback when multiprocessor topology exists but the SMP experiment stays disabled.",
            qemu_args=["-smp", "2"],
            must_have=common + [
                "smp: skipped (experimental flag off)",
            ],
        ),
        Scenario(
            name="smp2-experimental",
            description="Enables experimental SMP in stage2 and validates dual-core bring-up in QEMU.",
            qemu_args=["-smp", "2"],
            must_have=common + [
                "smp: online cpus=2/2",
                "smp: ap lapic timer armed",
                "smp: scheduler enabled cpus=2",
                "userland.app: shell start",
            ],
            toggle_smp=True,
        ),
        Scenario(
            name="smp4-experimental",
            description="Enables experimental SMP in stage2 and validates four-core bring-up in QEMU.",
            qemu_args=["-smp", "4"],
            must_have=common + [
                "smp: online cpus=4/4",
                "smp: scheduler enabled cpus=4",
                "userland.app: shell start",
            ],
            toggle_smp=True,
        ),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate VibeOS SMP fallback and multiprocessor bring-up")
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--memory-mb", type=int, default=3072)
    parser.add_argument("--timeout", type=float, default=240.0, help="per-scenario marker wait in seconds (TCG hosts need more)")
    args = parser.parse_args()

    image_path = Path(args.image)
    report_path = Path(args.report)
    qemu_binary = shutil.which(args.qemu) or shutil.which("qemu-system-x86_64")

    if qemu_binary is None:
        print("error: no QEMU binary found", file=sys.stderr)
        return 1
    if not image_path.is_file():
        print(f"error: image not found: {image_path}", file=sys.stderr)
        return 1

    results = [run_scenario(qemu_binary, image_path, args.memory_mb, scenario, args.timeout) for scenario in build_scenarios()]
    write_report(report_path, results)
    return 0 if all(result.passed for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())

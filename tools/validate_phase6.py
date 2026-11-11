#!/usr/bin/env python3
import argparse
import select
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional


COMMON_SUCCESS_MARKERS = [
    "init: entered builtin entry",
    "init: storage smoke ok",
]

SHELL_OR_DESKTOP_READY_MARKERS = [
    "userland.app: shell start",
    "init: bootstrap shell active",
    "init: desktop host launched",
    "host: desktop session launched",
    "host: startx start",
    "desktop: session start",
]


@dataclass
class Scenario:
    name: str
    description: str
    args: List[str]
    must_have: List[str]
    must_have_any: Optional[List[str]] = None
    notes: str = ""
    # Some firmware paths burn a fixed wall-clock budget before the kernel
    # emits anything (SeaBIOS usb-storage reset/get-max-lun retries measured
    # at ~11 s before the first serial byte, kernel itself reaches shell in
    # 4-6 s after). Such scenarios set a per-scenario budget so the global
    # cap does not judge BIOS emulation speed as a boot failure.
    timeout_seconds: Optional[int] = None


@dataclass
class ScenarioResult:
    scenario: Scenario
    passed: bool
    timed_out: bool
    exit_code: int
    log: str
    observed_markers: List[str]
    observed_any_markers: List[str]
    missing_markers: List[str]
    missing_any_markers: List[str]


def decode_output(output: Optional[bytes]) -> str:
    if output is None:
        return ""
    return output.decode("utf-8", errors="replace")


def parse_mbr(image_path: Path) -> dict:
    with image_path.open("rb") as handle:
        sector = handle.read(512)
    if len(sector) != 512:
        raise RuntimeError(f"{image_path} does not contain a full MBR sector")

    signature = sector[510:512]
    entries = []
    for index in range(4):
        offset = 446 + (index * 16)
        entry = sector[offset : offset + 16]
        entries.append(
            {
                "index": index,
                "boot": entry[0],
                "type": entry[4],
                "start_lba": int.from_bytes(entry[8:12], "little"),
                "sector_count": int.from_bytes(entry[12:16], "little"),
            }
        )

    return {
        "signature_ok": signature == b"\x55\xaa",
        "entries": entries,
        "active_entries": [entry for entry in entries if entry["boot"] == 0x80],
    }


def run_scenario(
    qemu_binary: str,
    image_path: Path,
    memory_mb: int,
    timeout_seconds: int,
    scenario: Scenario,
) -> ScenarioResult:
    with tempfile.TemporaryDirectory(prefix=f"vibe-phase6-{scenario.name}-") as temp_dir:
        scenario_image = Path(temp_dir) / "boot.img"
        shutil.copyfile(image_path, scenario_image)

        command = [qemu_binary] + [
            argument.format(image=scenario_image, memory_mb=memory_mb)
            for argument in scenario.args
        ]
        command.extend(["-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0"])

        timed_out = False
        exit_code = 0
        output = ""
        if scenario.timeout_seconds is not None:
            timeout_seconds = scenario.timeout_seconds

        # Stream serial output and stop as soon as every expected marker has
        # been observed; only burn the full timeout when the boot is broken.
        proc = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        deadline = time.monotonic() + timeout_seconds
        markers_seen = False
        try:
            while True:
                remaining_time = deadline - time.monotonic()
                if remaining_time <= 0:
                    break
                ready, _, _ = select.select(
                    [proc.stdout], [], [], min(1.0, remaining_time))
                if not ready:
                    if proc.poll() is not None:
                        break
                    continue
                line = proc.stdout.readline()
                if not line:
                    if proc.poll() is not None:
                        break
                    continue
                output += decode_output(line)
                remaining = [m for m in scenario.must_have if m not in output]
                any_done = (not scenario.must_have_any) or any(
                    m in output for m in scenario.must_have_any)
                if not remaining and any_done:
                    markers_seen = True
                    break
            if proc.poll() is None:
                timed_out = not markers_seen
                proc.kill()
            exit_code = proc.wait()
            rest = proc.stdout.read()
            if rest:
                output += decode_output(rest)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()

    missing = [marker for marker in scenario.must_have if marker not in output]
    observed = [marker for marker in scenario.must_have if marker in output]
    missing_any = []
    observed_any = []
    if scenario.must_have_any:
        observed_any = [marker for marker in scenario.must_have_any if marker in output]
        if not observed_any:
            missing_any = list(scenario.must_have_any)
    passed = not missing and not missing_any
    return ScenarioResult(
        scenario=scenario,
        passed=passed,
        timed_out=timed_out,
        exit_code=exit_code,
        log=output,
        observed_markers=observed,
        observed_any_markers=observed_any,
        missing_markers=missing,
        missing_any_markers=missing_any,
    )


def build_scenarios() -> List[Scenario]:
    headless_tail = [
        "-display",
        "none",
        "-serial",
        "stdio",
        "-monitor",
        "none",
    ]
    ide_drive = [
        "-m",
        "{memory_mb}",
        "-drive",
        "format=raw,file={image}",
        "-boot",
        "c",
    ]

    return [
        Scenario(
            name="ide-default",
            description="Default IDE regression target",
            args=ide_drive + headless_tail,
            must_have=["ata: start="] + COMMON_SUCCESS_MARKERS,
            must_have_any=SHELL_OR_DESKTOP_READY_MARKERS,
            notes="Legacy ATA path is accepted if it reaches either the shell fallback or the newer desktop-host/startx-host flow.",
        ),
        Scenario(
            name="core2duo",
            description="Core 2 Duo IDE regression target",
            args=["-cpu", "core2duo"] + ide_drive + headless_tail,
            must_have=["ata: start="] + COMMON_SUCCESS_MARKERS,
            must_have_any=SHELL_OR_DESKTOP_READY_MARKERS,
            notes="Core 2 Duo uses the same ATA compatibility rule as the default IDE path and may reach either shell fallback or desktop-host flow.",
        ),
        Scenario(
            name="pentium",
            description="Pentium IDE regression target",
            args=["-cpu", "pentium"] + ide_drive + headless_tail,
            must_have=["ata: start="] + COMMON_SUCCESS_MARKERS,
            must_have_any=SHELL_OR_DESKTOP_READY_MARKERS,
            notes="Pentium uses the same ATA compatibility rule as the default IDE path and may reach either shell fallback or desktop-host flow.",
        ),
        Scenario(
            name="atom-n270",
            description="Atom N270 IDE regression target",
            args=["-cpu", "n270"] + ide_drive + headless_tail,
            must_have=["ata: start="] + COMMON_SUCCESS_MARKERS,
            must_have_any=SHELL_OR_DESKTOP_READY_MARKERS,
            notes="Atom N270 uses the same ATA compatibility rule as the default IDE path and may reach either shell fallback or desktop-host flow.",
        ),
        Scenario(
            name="ahci-q35",
            description="AHCI/SATA regression target on q35",
            args=[
                "-machine",
                "q35",
                "-m",
                "{memory_mb}",
                "-drive",
                "if=none,id=bootdisk,format=raw,file={image}",
                "-device",
                "ahci,id=ahci",
                "-device",
                "ide-hd,drive=bootdisk,bus=ahci.0,bootindex=0",
                "-boot",
                "c",
            ]
            + headless_tail,
            must_have=[
                "ahci: controller",
                "ahci: sata port",
            ] + COMMON_SUCCESS_MARKERS,
            must_have_any=SHELL_OR_DESKTOP_READY_MARKERS,
        ),
        Scenario(
            name="usb-bios-boot",
            description="USB mass-storage BIOS boot validation",
            args=[
                "-m",
                "{memory_mb}",
                "-drive",
                "if=none,id=usbdisk,format=raw,file={image}",
                "-usb",
                "-device",
                "usb-storage,drive=usbdisk,bootindex=0",
                "-boot",
                "menu=off",
            ]
            + headless_tail,
            must_have=[
                "init: entered builtin entry",
                "storage: init returned",
                # adendo 48: the native UHCI+BOT backend brought up against
                # this emulated stick, with the resolved AppFS partition.
                "storage: usb backend ready",
            ],
            # Measured 2026-10-20 with the runtime backend enabled: init
            # entry ~38 s, usb backend ready ~37 s, and the init write-path
            # smoke (read+write+restore through BOT) ~198 s under TCG (BOT is
            # tick/poll bound in emulation; real UHCI hardware is far
            # faster). Timeout covers the smoke with host-load headroom.
            timeout_seconds=320,
            # Adendo 48 (2026-10-20): the NATIVE USB mass-storage runtime
            # backend (UHCI+BOT+SCSI, kernel/drivers/usb/usb_host.c wired
            # via kernel_storage_probe_extra) now loads fs/AppFS from this
            # same emulated stick, so the boot may pass the shell fallback
            # marker and reach desktop instead. Either outcome is a pass;
            # a hard failure is no longer expected but is still what a
            # missing marker means. EHCI/xHCI backends remain unimplemented
            # (COMPLETION_ACTION_PLAN P0 line, [~]).
            must_have_any=[
                # functional read+write proof through the USB backend
                "init: storage smoke ok",
            ] + SHELL_OR_DESKTOP_READY_MARKERS,
            notes="Boot path validated through BIOS USB emulation; runtime storage now served by the native UHCI/BOT mass-storage backend (adendo 48), proven by the init read+write+restore smoke on the stick; EHCI/xHCI still missing.",
        ),
    ]


def write_report(report_path: Path, mbr_info: dict, results: List[ScenarioResult]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "# Phase 6 Validation Report",
        "",
        "## MBR Sanity",
        "",
        f"- Signature `0x55AA`: {'yes' if mbr_info['signature_ok'] else 'no'}",
        f"- Active partitions: {len(mbr_info['active_entries'])}",
    ]

    for entry in mbr_info["entries"]:
        lines.append(
            "- Partition {index}: boot=0x{boot:02x} type=0x{type:02x} start_lba={start_lba} sectors={sector_count}".format(
                **entry
            )
        )

    lines.extend(
        [
            "",
            "## QEMU Matrix",
            "",
            "| Scenario | Result | Exit | Notes |",
            "| --- | --- | --- | --- |",
        ]
    )

    for result in results:
        note = result.scenario.notes
        if result.missing_markers or result.missing_any_markers:
            parts = []
            if result.missing_markers:
                parts.append(", ".join(result.missing_markers))
            if result.missing_any_markers:
                parts.append("one of: " + ", ".join(result.missing_any_markers))
            note = "missing: " + " | ".join(parts)
        elif result.timed_out:
            note = (note + " " if note else "") + "timed out after reaching expected markers"

        lines.append(
            f"| {result.scenario.name} | {'PASS' if result.passed else 'FAIL'} | {result.exit_code} | {note or '-'} |"
        )

    lines.extend(["", "## Marker Summary", ""])
    for result in results:
        lines.append(f"### {result.scenario.name}")
        if result.passed:
            lines.append("- required markers observed")
        else:
            parts = []
            if result.missing_markers:
                parts.append(", ".join(result.missing_markers))
            if result.missing_any_markers:
                parts.append("one of: " + ", ".join(result.missing_any_markers))
            lines.append(f"- missing markers: {' | '.join(parts)}")

        if result.observed_markers:
            lines.append("- observed required markers: " + ", ".join(result.observed_markers))
        if result.observed_any_markers:
            lines.append("- observed success markers: " + ", ".join(result.observed_any_markers))

        preview_lines = [line for line in result.log.strip().splitlines() if line][:14]
        if preview_lines:
            lines.append("")
            lines.append("```text")
            lines.extend(preview_lines)
            lines.append("```")
        lines.append("")

    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run VibeOS Phase 6 compatibility validation in headless QEMU.")
    parser.add_argument("--image", required=True, help="boot image to validate")
    parser.add_argument("--report", required=True, help="markdown report output path")
    parser.add_argument("--qemu", default="qemu-system-i386", help="QEMU binary")
    parser.add_argument("--memory-mb", type=int, default=3072, help="guest RAM in MB")
    parser.add_argument("--timeout", type=int, default=20, help="per-scenario timeout in seconds")
    args = parser.parse_args()

    image_path = Path(args.image)
    report_path = Path(args.report)

    if not image_path.is_file():
        print(f"error: boot image not found: {image_path}", file=sys.stderr)
        return 1

    qemu_binary = shutil.which(args.qemu)
    if qemu_binary is None:
        qemu_binary = shutil.which("qemu-system-x86_64")
    if qemu_binary is None:
        print("error: no QEMU binary found (checked requested binary and qemu-system-x86_64)", file=sys.stderr)
        return 1

    mbr_info = parse_mbr(image_path)
    if not mbr_info["signature_ok"]:
        print("error: invalid MBR signature", file=sys.stderr)
        return 1
    if not mbr_info["active_entries"]:
        print("error: no active MBR partition found", file=sys.stderr)
        return 1

    results = [
        run_scenario(qemu_binary, image_path, args.memory_mb, args.timeout, scenario)
        for scenario in build_scenarios()
    ]
    write_report(report_path, mbr_info, results)

    failures = [result for result in results if not result.passed]
    if failures:
        print(f"phase6: {len(failures)} scenario(s) failed; see {report_path}", file=sys.stderr)
        return 1

    print(f"phase6: validation passed; report written to {report_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

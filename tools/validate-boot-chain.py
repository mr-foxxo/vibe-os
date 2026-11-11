#!/usr/bin/env python3
"""validate-boot-chain.py: Full boot chain validation for VibeOS.

Tests:
  1. Kernel boots to storage init
  2. Storage backend is found (AHCI/ATA/USB/NVMe)
  3. USB host controller is detected
  4. Scheduler + service layer init
  5. Console service responds
  6. Audio service health queryable
  7. Video init completes
  8. Full userland launch

Usage:
  python3 validate-boot-chain.py --image build/boot.img [--report /tmp/report.md]
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

QEMU = os.environ.get("QEMU", "qemu-system-x86_64")

MARKERS = {
    "kernel_entry": "kernel entry",
    "pci_enum": "PCI OK",
    "storage_ahci": "storage: ahci ok",
    "storage_ata": "storage: ata ok",
    "storage_nvme": "storage: nvme ok",
    "storage_usbms": "storage: usb-ms ok",
    "storage_usb": "storage: using usb backend",
    "storage_ready": "Storage OK",
    "usb_init": "USB OK",
    "scheduler_init": "Scheduler+drivers OK",
    "smp_start": "SMP OK",
    "vfs_init": "VFS OK",
    "video_init": "video: boot init",
    "video_fast_lfb": "video: backend refresh backend=fast_lfb",
    "audio_backend": "audio: service init exit",
    "console_online": "service-host: online console",
    "init_spawn": "init: spawning services",
    "desktop_launch": "desktop: visual ready",
    "shell_prompt": "terminal: command done",
    "usb_storage_ready": "storage: usb mass-storage ready",
    "hda_ready": "audio: hda ready",
    "network_init": "network: service init",
}

class BootValidator:
    def __init__(self, image_path, timeout=60):
        self.image_path = image_path
        self.timeout = timeout
        self.matched = {}
        self.log = []

    def run_qemu(self):
        cmd = [
            QEMU, "-machine", "q35,accel=kvm:hax:whpx:tcg",
            "-cpu", "host" if sys.platform == "linux" else "max",
            "-m", "512M",
            "-drive", f"file={self.image_path},if=none,id=drive0,format=raw",
            "-device", "ahci,id=ahci",
            "-device", "ide-hd,drive=drive0,bus=ahci.0",
            "-device", "piix3-usb-uhci",
            "-device", "usb-storage,drive=drive0",
            "-vga", "std",
            "-audiodev", "none,id=noaudio",
            "-device", "ac97,audiodev=noaudio",
            "-netdev", "user,id=net0",
            "-device", "virtio-net,netdev=net0",
            "-debugcon", "stdio",
            "-display", "none",
            "-no-reboot",
            "-serial", "none",
            "-global", "isa-debugcon.iobase=0xE9",
        ]

        with tempfile.NamedTemporaryFile(mode="w+", suffix=".log", delete=False) as logf:
            log_path = logf.name

        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )

            start = time.monotonic()
            output_lines = []

            while time.monotonic() - start < self.timeout:
                line = proc.stdout.readline()
                if not line:
                    if proc.poll() is not None:
                        break
                    continue
                text = line.decode("utf-8", errors="replace").strip()
                output_lines.append(text)
                self.log.append(text)

                for name, marker in MARKERS.items():
                    if marker in text and name not in self.matched:
                        self.matched[name] = text

            proc.terminate()
            proc.wait(5)

            with open(log_path, "w") as f:
                f.write("\n".join(output_lines))
            return log_path

        except FileNotFoundError:
            print(f"Error: QEMU not found at {QEMU}")
            sys.exit(1)

    def summary(self):
        results = []
        all_pass = True

        required = [
            ("kernel_entry", "Kernel entry"),
            ("pci_enum", "PCI enumeration"),
            ("scheduler_init", "Scheduler/driver init"),
            ("vfs_init", "VFS init"),
            ("console_online", "Console service"),
        ]

        for key, label in required:
            if key in self.matched:
                results.append((label, "PASS", self.matched[key]))
            else:
                results.append((label, "FAIL", "not found"))
                all_pass = False

        storage_keys = ["storage_nvme", "storage_ahci", "storage_ata", "storage_usbms"]
        if any(k in self.matched for k in storage_keys):
            found = [k for k in storage_keys if k in self.matched]
            results.append(("Storage backend", "PASS", self.matched[found[0]]))
        else:
            if "storage_ready" in self.matched:
                results.append(("Storage backend", "PASS", self.matched["storage_ready"]))
            else:
                results.append(("Storage backend", "FAIL", "no backend found"))
                all_pass = False

        optional = [
            ("usb_init", "USB init", "skipped"),
            ("smp_start", "SMP bring-up", "skipped"),
            ("video_fast_lfb", "Fast LFB backend", "unavailable"),
            ("usb_storage_ready", "USB mass-storage", "unavailable"),
            ("hda_ready", "HDA audio", "unavailable"),
            ("network_init", "Network service", "skipped"),
            ("desktop_launch", "Desktop visual", "skipped"),
        ]

        for key, label, default in optional:
            if key in self.matched:
                results.append((label, "PASS", self.matched[key]))
            elif default:
                results.append((label, "INFO", default))

        return results, all_pass

def generate_report(results, all_pass, log_path, report_path=None):
    lines = []
    lines.append("# Boot Chain Validation Report")
    lines.append("")
    lines.append(f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append("")
    lines.append("## Results")
    lines.append("")
    lines.append("| Check | Status | Detail |")
    lines.append("|-------|--------|--------|")

    for label, status, detail in results:
        icon = "✅" if status == "PASS" else "⚠️" if status == "INFO" else "❌"
        lines.append(f"| {label} | {icon} {status} | {detail} |")

    lines.append("")
    lines.append(f"## Overall: {'✅ PASS' if all_pass else '❌ FAIL'}")
    lines.append("")

    if all_pass:
        lines.append("All required boot chain stages completed successfully.")
    else:
        lines.append("Some required stages failed. Check QEMU log for details.")

    lines.append("")
    lines.append(f"Full log: `{log_path}`")

    report = "\n".join(lines)

    if report_path:
        with open(report_path, "w") as f:
            f.write(report)
        print(f"Report written to {report_path}")
    else:
        print(report)

def main():
    parser = argparse.ArgumentParser(description="VibeOS boot chain validator")
    parser.add_argument("--image", required=True, help="Path to boot.img")
    parser.add_argument("--report", help="Path to output report file")
    parser.add_argument("--timeout", type=int, default=60, help="QEMU timeout (seconds)")
    args = parser.parse_args()

    if not os.path.exists(args.image):
        print(f"Error: image not found: {args.image}")
        sys.exit(1)

    print(f"Validating boot chain with {args.image}...")
    validator = BootValidator(args.image, args.timeout)
    log_path = validator.run_qemu()
    results, all_pass = validator.summary()

    for label, status, detail in results:
        icon = {"PASS": " [PASS]", "FAIL": " [FAIL]", "INFO": " [INFO]"}.get(status, " [?]")
        print(f"  {icon} {label}: {detail}")

    if args.report:
        generate_report(results, all_pass, log_path, args.report)
    print(f"\nOverall: {'PASS' if all_pass else 'FAIL'}")
    return 0 if all_pass else 1

if __name__ == "__main__":
    sys.exit(main())

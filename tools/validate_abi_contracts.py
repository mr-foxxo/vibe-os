#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List


ROOT = Path(__file__).resolve().parent.parent

DOCS_TO_CHECK = [
    ROOT / "docs" / "abi-improvements.md",
    ROOT / "docs" / "abi-inventory.md",
    ROOT / "docs" / "abi-contracts.md",
]

COMPAT_REFERENCES = [
    "compat/sys/sys/exec_elf.h",
    "compat/sys/kern/kern_exec.c",
    "compat/sys/kern/kern_exit.c",
    "compat/sys/kern/kern_fork.c",
    "compat/sys/sys/stat.h",
    "compat/sys/sys/ioctl.h",
    "compat/sys/sys/ttycom.h",
    "compat/sys/sys/termios.h",
    "compat/sys/sys/socket.h",
    "compat/sys/net/if.h",
    "compat/sys/netinet/in.h",
    "compat/sys/sys/poll.h",
    "compat/sys/sys/select.h",
    "compat/sys/sys/signal.h",
    "compat/sys/sys/audioio.h",
    "compat/sys/dev/pci/pcireg.h",
    "compat/sys/dev/pci/pcivar.h",
]


STATIC_CHECK_SOURCE = r"""
#include <stddef.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/network.h>
#include <include/userland_api.h>
#include <lang/include/vibe_app_runtime.h>

_Static_assert(VIBE_APP_ABI_VERSION == 1u, "unexpected vibe app abi version");
_Static_assert(MK_LAUNCH_ABI_VERSION == 2u, "unexpected launch abi version");
_Static_assert(TASK_SNAPSHOT_ABI_VERSION == 5u, "unexpected snapshot abi version");
_Static_assert(VIBE_STAT_ABI_LEGACY == 1, "legacy stat abi marker changed");
_Static_assert(VIBE_STAT_ABI_COMPAT == 2, "compat stat abi marker changed");
_Static_assert(sizeof(struct vibe_app_header_legacy) == 40u, "legacy app header changed");
_Static_assert(sizeof(struct vibe_app_header) == 44u, "current app header changed");
_Static_assert(sizeof(struct stat) <= sizeof(struct stat_compat), "legacy stat should stay smaller");
_Static_assert(sizeof(struct sockaddr_storage) == 128u, "sockaddr_storage layout drifted");
_Static_assert(sizeof(struct mk_network_socket_list_entry) == 28u, "socket list entry layout drifted");
_Static_assert(sizeof(struct mk_network_socket_list_request) == 4u, "socket list request layout drifted");
_Static_assert(sizeof(struct winsize) == 8u, "winsize layout drifted");
_Static_assert(offsetof(struct mk_launch_descriptor, entry) > offsetof(struct mk_launch_descriptor, argv_data),
               "launch descriptor layout drifted");
_Static_assert(sizeof(((struct termios *)0)->c_cc) == NCCS, "termios c_cc size drifted");

int main(void) {
    return 0;
}
"""


def check_docs() -> List[str]:
    errors: List[str] = []
    aggregated = []

    for path in DOCS_TO_CHECK:
        if not path.exists():
            errors.append(f"missing required document: {path.relative_to(ROOT)}")
            continue
        aggregated.append(path.read_text(encoding="utf-8"))

    combined = "\n".join(aggregated)
    for ref in COMPAT_REFERENCES:
        ref_path = ROOT / ref
        if not ref_path.exists():
            errors.append(f"missing compat reference on disk: {ref}")
            continue
        if ref not in combined:
            errors.append(f"compat reference not documented in ABI docs: {ref}")

    return errors


def run_static_compile(cc: str) -> List[str]:
    errors: List[str] = []

    with tempfile.TemporaryDirectory(prefix="vibe-abi-contracts-") as temp_dir:
        source = Path(temp_dir) / "abi_contracts_check.c"
        output = Path(temp_dir) / "abi_contracts_check.o"
        source.write_text(STATIC_CHECK_SOURCE, encoding="utf-8")

        command = [
            cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Iheaders",
            "-I.",
            "-c",
            str(source),
            "-o",
            str(output),
        ]

        completed = subprocess.run(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            text=True,
        )
        if completed.returncode != 0:
            errors.append("static ABI compile failed:")
            errors.extend(completed.stdout.rstrip().splitlines())

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate documented VibeOS ABI contracts")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="C compiler to use for static ABI checks")
    args = parser.parse_args()

    errors = []
    errors.extend(check_docs())
    errors.extend(run_static_compile(args.cc))

    if errors:
        for line in errors:
            print(f"abi-contracts: {line}", file=sys.stderr)
        return 1

    print("abi-contracts: validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

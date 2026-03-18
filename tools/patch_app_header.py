#!/usr/bin/env python3
import argparse
import struct
import subprocess
import sys

HEADER_STRUCT = struct.Struct("<IHHIIII16s")
SYMBOLS = ("__app_load_start", "__app_image_end", "__app_bss_end", "vibe_app_entry")


def load_symbols(nm, elf_path):
    output = subprocess.check_output([nm, "-n", elf_path], text=True)
    values = {}
    for line in output.splitlines():
        parts = line.strip().split()
        if len(parts) != 3:
            continue
        address, _sym_type, name = parts
        if name in SYMBOLS:
            values[name] = int(address, 16)
    missing = [name for name in SYMBOLS if name not in values]
    if missing:
        raise RuntimeError(f"missing symbols: {', '.join(missing)}")
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", default="i686-elf-nm")
    parser.add_argument("--elf", required=True)
    parser.add_argument("--bin", required=True)
    args = parser.parse_args()

    values = load_symbols(args.nm, args.elf)
    load_start = values["__app_load_start"]
    image_end = values["__app_image_end"]
    bss_end = values["__app_bss_end"]
    entry = values["vibe_app_entry"]

    image_size = image_end - load_start
    memory_size = bss_end - load_start
    entry_offset = entry - load_start
    if image_size <= 0 or memory_size < image_size or entry_offset < 0:
        raise RuntimeError("invalid app layout symbols")

    with open(args.bin, "r+b") as app_bin:
        raw = bytearray(app_bin.read(HEADER_STRUCT.size))
        if len(raw) != HEADER_STRUCT.size:
            raise RuntimeError("app binary too small for header")

        fields = list(HEADER_STRUCT.unpack(raw))
        fields[3] = image_size
        fields[4] = memory_size
        fields[5] = entry_offset

        app_bin.seek(0)
        app_bin.write(HEADER_STRUCT.pack(*fields))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"patch_app_header.py: {exc}", file=sys.stderr)
        raise

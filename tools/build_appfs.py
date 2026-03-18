#!/usr/bin/env python3
import argparse
import math
import os
import struct
import sys

APPFS_MAGIC = 0x53465056
APPFS_VERSION = 1
ENTRY_MAX = 48
NAME_SIZE = 16
ENTRY_STRUCT = struct.Struct("<16sIIII")
DIR_HEADER = struct.Struct("<IIII")


def checksum_bytes(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def build_directory(entries):
    payload = bytearray(DIR_HEADER.size + (ENTRY_MAX * ENTRY_STRUCT.size))
    DIR_HEADER.pack_into(payload, 0, APPFS_MAGIC, APPFS_VERSION, len(entries), 0)
    offset = DIR_HEADER.size
    for name, lba_start, sector_count, image_size in entries:
        encoded = name.encode("ascii")
        if len(encoded) >= NAME_SIZE:
            raise ValueError(f"app name too long: {name}")
        ENTRY_STRUCT.pack_into(
            payload,
            offset,
            encoded + b"\0" * (NAME_SIZE - len(encoded)),
            lba_start,
            sector_count,
            image_size,
            0,
        )
        offset += ENTRY_STRUCT.size

    checksum = checksum_bytes(payload)
    DIR_HEADER.pack_into(payload, 0, APPFS_MAGIC, APPFS_VERSION, len(entries), checksum)
    return bytes(payload)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--directory-lba", type=int, required=True)
    parser.add_argument("--directory-sectors", type=int, required=True)
    parser.add_argument("--app-area-sectors", type=int, required=True)
    parser.add_argument("apps", nargs="*")
    args = parser.parse_args()

    if len(args.apps) > ENTRY_MAX:
        raise SystemExit(f"too many apps: {len(args.apps)} > {ENTRY_MAX}")

    directory_size = args.directory_sectors * 512
    data_lba = args.directory_lba + args.directory_sectors
    current_lba = data_lba
    used_app_sectors = 0
    entries = []

    with open(args.image, "r+b") as image:
        for app_path in args.apps:
            name = os.path.splitext(os.path.basename(app_path))[0]
            with open(app_path, "rb") as app_file:
                blob = app_file.read()
            sectors = int(math.ceil(len(blob) / 512.0))
            if used_app_sectors + sectors > args.app_area_sectors:
                raise SystemExit("app area overflow while packing external apps")

            image.seek(current_lba * 512)
            image.write(blob)
            image.write(b"\0" * ((sectors * 512) - len(blob)))
            entries.append((name, current_lba, sectors, len(blob)))
            current_lba += sectors
            used_app_sectors += sectors

        directory_blob = build_directory(entries)
        if len(directory_blob) > directory_size:
            raise SystemExit("app directory does not fit reserved sectors")
        image.seek(args.directory_lba * 512)
        image.write(directory_blob)
        image.write(b"\0" * (directory_size - len(directory_blob)))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"build_appfs.py: {exc}", file=sys.stderr)
        raise

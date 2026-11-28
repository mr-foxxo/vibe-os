#!/usr/bin/env python3
import argparse
import datetime
import os
import shutil
import struct
import subprocess
import tempfile

FAT32_EOC = 0x0FFFFFFF
SHORT_NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")


def run(cmd):
    subprocess.run(cmd, check=True)


def write_file(path, data, offset=0):
    with open(path, "r+b") as handle:
        handle.seek(offset)
        handle.write(data)


def normalize_destination(destination):
    normalized = destination.strip().replace("\\", "/")
    if not normalized:
        raise SystemExit("boot file destination cannot be empty")
    if not normalized.startswith("/"):
        normalized = "/" + normalized

    components = []
    for component in normalized.split("/"):
        if not component or component == ".":
            continue
        if component == "..":
            raise SystemExit(f"unsupported parent directory in boot file destination: {destination!r}")
        components.append(component)

    if not components:
        raise SystemExit("boot file destination must reference a file")
    return "/" + "/".join(components)


def ensure_mtools_path(part_path, destination, mmd_tool):
    mmd = shutil.which(mmd_tool) or mmd_tool
    normalized = normalize_destination(destination)

    components = [component for component in normalized.split("/")[:-1] if component]
    current = ""
    for component in components:
        current += "/" + component
        subprocess.run(
            [mmd, "-i", part_path, f"::{current}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def copy_boot_file(part_path, source_path, destination, mcopy_tool, mmd_tool):
    mcopy = shutil.which(mcopy_tool) or mcopy_tool
    normalized = normalize_destination(destination)

    ensure_mtools_path(part_path, normalized, mmd_tool)
    run([mcopy, "-i", part_path, "-o", source_path, f"::{normalized}"])


def encode_short_name(component):
    upper = component.upper()
    if upper in {".", ".."}:
        return (upper + (" " * (11 - len(upper)))).encode("ascii")

    stem, dot, ext = upper.partition(".")
    if dot and "." in ext:
        raise SystemExit(f"long or invalid FAT file name without mtools: {component!r}")
    if not stem or len(stem) > 8 or len(ext) > 3:
        raise SystemExit(f"long or invalid FAT file name without mtools: {component!r}")
    if any(char not in SHORT_NAME_CHARS for char in stem + ext):
        raise SystemExit(f"unsupported FAT short-name characters without mtools: {component!r}")
    return f"{stem:<8}{ext:<3}".encode("ascii")


def encode_dos_datetime(timestamp):
    if timestamp is None:
        dt = datetime.datetime.now()
    else:
        dt = datetime.datetime.fromtimestamp(timestamp)

    year = min(max(dt.year, 1980), 2107)
    date_value = ((year - 1980) << 9) | (dt.month << 5) | dt.day
    time_value = (dt.hour << 11) | (dt.minute << 5) | (dt.second // 2)
    return date_value, time_value


class FAT32Image:
    def __init__(self, handle):
        self.handle = handle
        self.handle.seek(0)
        boot_sector = self.handle.read(512)
        if len(boot_sector) != 512:
            raise SystemExit("failed to read FAT32 boot sector")

        self.bytes_per_sector = struct.unpack_from("<H", boot_sector, 11)[0]
        self.sectors_per_cluster = boot_sector[13]
        self.reserved_sectors = struct.unpack_from("<H", boot_sector, 14)[0]
        self.num_fats = boot_sector[16]
        total_sectors16 = struct.unpack_from("<H", boot_sector, 19)[0]
        total_sectors32 = struct.unpack_from("<I", boot_sector, 32)[0]
        fat_size16 = struct.unpack_from("<H", boot_sector, 22)[0]
        fat_size32 = struct.unpack_from("<I", boot_sector, 36)[0]
        self.root_cluster = struct.unpack_from("<I", boot_sector, 44)[0]

        self.total_sectors = total_sectors16 or total_sectors32
        self.fat_size = fat_size16 or fat_size32
        self.cluster_size = self.bytes_per_sector * self.sectors_per_cluster
        self.first_data_sector = self.reserved_sectors + (self.num_fats * self.fat_size)
        self.data_offset = self.first_data_sector * self.bytes_per_sector
        self.total_clusters = (self.total_sectors - self.first_data_sector) // self.sectors_per_cluster
        self.max_cluster = self.total_clusters + 1
        self.fat_offsets = [
            (self.reserved_sectors + (index * self.fat_size)) * self.bytes_per_sector
            for index in range(self.num_fats)
        ]
        self.next_free_cluster = 2

        if self.bytes_per_sector == 0 or self.sectors_per_cluster == 0:
            raise SystemExit("invalid FAT32 geometry")
        if self.root_cluster < 2:
            raise SystemExit("invalid FAT32 root directory cluster")

    def read_at(self, offset, size):
        self.handle.seek(offset)
        data = self.handle.read(size)
        if len(data) != size:
            raise SystemExit("short read from FAT32 image")
        return data

    def write_at(self, offset, data):
        self.handle.seek(offset)
        self.handle.write(data)

    def cluster_offset(self, cluster):
        if cluster < 2 or cluster > self.max_cluster:
            raise SystemExit(f"invalid FAT32 cluster {cluster}")
        return self.data_offset + ((cluster - 2) * self.cluster_size)

    def read_cluster(self, cluster):
        return self.read_at(self.cluster_offset(cluster), self.cluster_size)

    def write_cluster(self, cluster, data):
        if len(data) > self.cluster_size:
            raise SystemExit("cluster write exceeds FAT32 cluster size")
        payload = data
        if len(payload) < self.cluster_size:
            payload = payload + (b"\0" * (self.cluster_size - len(payload)))
        self.write_at(self.cluster_offset(cluster), payload)

    def read_fat_entry(self, cluster):
        entry = struct.unpack("<I", self.read_at(self.fat_offsets[0] + (cluster * 4), 4))[0]
        return entry & 0x0FFFFFFF

    def write_fat_entry(self, cluster, value):
        masked_value = value & 0x0FFFFFFF
        for fat_offset in self.fat_offsets:
            entry_offset = fat_offset + (cluster * 4)
            original = struct.unpack("<I", self.read_at(entry_offset, 4))[0]
            packed = (original & 0xF0000000) | masked_value
            self.write_at(entry_offset, struct.pack("<I", packed))

    def is_end_of_chain(self, value):
        return value >= 0x0FFFFFF8

    def get_cluster_chain(self, start_cluster):
        if start_cluster == 0:
            return []

        chain = []
        current = start_cluster
        seen = set()
        while True:
            if current in seen:
                raise SystemExit("loop detected in FAT32 cluster chain")
            seen.add(current)
            chain.append(current)
            next_cluster = self.read_fat_entry(current)
            if self.is_end_of_chain(next_cluster):
                return chain
            if next_cluster == 0:
                raise SystemExit("broken FAT32 cluster chain")
            current = next_cluster

    def allocate_cluster(self):
        for candidate in range(self.next_free_cluster, self.max_cluster + 1):
            if self.read_fat_entry(candidate) == 0:
                self.write_fat_entry(candidate, FAT32_EOC)
                self.write_cluster(candidate, b"")
                self.next_free_cluster = candidate + 1
                return candidate

        for candidate in range(2, self.next_free_cluster):
            if self.read_fat_entry(candidate) == 0:
                self.write_fat_entry(candidate, FAT32_EOC)
                self.write_cluster(candidate, b"")
                self.next_free_cluster = candidate + 1
                return candidate

        raise SystemExit("no free clusters left in FAT32 boot partition")

    def allocate_chain(self, cluster_count):
        if cluster_count <= 0:
            return 0, []

        chain = []
        for _ in range(cluster_count):
            cluster = self.allocate_cluster()
            if chain:
                self.write_fat_entry(chain[-1], cluster)
            chain.append(cluster)
        self.write_fat_entry(chain[-1], FAT32_EOC)
        return chain[0], chain

    def free_chain(self, start_cluster):
        if start_cluster < 2:
            return

        current = start_cluster
        seen = set()
        while True:
            if current in seen:
                raise SystemExit("loop detected while freeing FAT32 cluster chain")
            seen.add(current)
            next_cluster = self.read_fat_entry(current)
            self.write_fat_entry(current, 0)
            self.write_cluster(current, b"")
            if self.is_end_of_chain(next_cluster):
                return
            if next_cluster == 0:
                return
            current = next_cluster

    def entry_first_cluster(self, entry):
        high = struct.unpack_from("<H", entry, 20)[0]
        low = struct.unpack_from("<H", entry, 26)[0]
        return (high << 16) | low

    def make_entry(self, short_name, attributes, first_cluster, size, timestamp):
        entry = bytearray(32)
        entry[0:11] = short_name
        entry[11] = attributes
        date_value, time_value = encode_dos_datetime(timestamp)
        struct.pack_into("<H", entry, 14, time_value)
        struct.pack_into("<H", entry, 16, date_value)
        struct.pack_into("<H", entry, 18, date_value)
        struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", entry, 22, time_value)
        struct.pack_into("<H", entry, 24, date_value)
        struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
        struct.pack_into("<I", entry, 28, size)
        return bytes(entry)

    def lookup_entry(self, directory_cluster, short_name):
        free_slot = None
        directory_chain = self.get_cluster_chain(directory_cluster)

        for cluster in directory_chain:
            cluster_offset = self.cluster_offset(cluster)
            data = self.read_cluster(cluster)
            for index in range(0, self.cluster_size, 32):
                entry = data[index : index + 32]
                entry_offset = cluster_offset + index
                first_byte = entry[0]
                if first_byte == 0x00:
                    return None, free_slot if free_slot is not None else entry_offset
                if first_byte == 0xE5:
                    if free_slot is None:
                        free_slot = entry_offset
                    continue
                if entry[11] == 0x0F:
                    continue
                if entry[0:11] == short_name:
                    return entry, entry_offset

        if free_slot is not None:
            return None, free_slot

        last_cluster = directory_chain[-1]
        new_cluster = self.allocate_cluster()
        self.write_fat_entry(last_cluster, new_cluster)
        self.write_fat_entry(new_cluster, FAT32_EOC)
        return None, self.cluster_offset(new_cluster)

    def ensure_directory(self, parent_cluster, component):
        short_name = encode_short_name(component)
        existing_entry, entry_offset = self.lookup_entry(parent_cluster, short_name)

        if existing_entry is not None:
            if not (existing_entry[11] & 0x10):
                raise SystemExit(f"boot file path component is not a directory: {component!r}")
            return self.entry_first_cluster(existing_entry)

        new_cluster = self.allocate_cluster()
        dot_entry = self.make_entry(encode_short_name("."), 0x10, new_cluster, 0, None)
        dotdot_entry = self.make_entry(encode_short_name(".."), 0x10, parent_cluster, 0, None)
        directory_data = bytearray(self.cluster_size)
        directory_data[0:32] = dot_entry
        directory_data[32:64] = dotdot_entry
        self.write_cluster(new_cluster, directory_data)

        directory_entry = self.make_entry(short_name, 0x10, new_cluster, 0, None)
        self.write_at(entry_offset, directory_entry)
        return new_cluster

    def write_file_to_directory(self, directory_cluster, component, source_path):
        short_name = encode_short_name(component)
        existing_entry, entry_offset = self.lookup_entry(directory_cluster, short_name)

        if existing_entry is not None and (existing_entry[11] & 0x10):
            raise SystemExit(f"boot file path collides with directory: {component!r}")

        with open(source_path, "rb") as handle:
            payload = handle.read()

        first_cluster = 0
        if payload:
            cluster_count = (len(payload) + self.cluster_size - 1) // self.cluster_size
            first_cluster, chain = self.allocate_chain(cluster_count)
            for index, cluster in enumerate(chain):
                start = index * self.cluster_size
                end = start + self.cluster_size
                self.write_cluster(cluster, payload[start:end])

        if existing_entry is not None:
            old_cluster = self.entry_first_cluster(existing_entry)
            self.free_chain(old_cluster)

        timestamp = os.path.getmtime(source_path)
        file_entry = self.make_entry(short_name, 0x20, first_cluster, len(payload), timestamp)
        self.write_at(entry_offset, file_entry)


def copy_boot_file_without_mtools(part_path, source_path, destination):
    normalized = normalize_destination(destination)
    components = [component for component in normalized.strip("/").split("/") if component]
    with open(part_path, "r+b") as handle:
        image = FAT32Image(handle)
        directory_cluster = image.root_cluster
        for component in components[:-1]:
            directory_cluster = image.ensure_directory(directory_cluster, component)
        image.write_file_to_directory(directory_cluster, components[-1], source_path)


def mkfs_fat_command(args, part_path):
    tool = shutil.which(args.mkfs_fat) or args.mkfs_fat
    if os.path.basename(tool) == "newfs_msdos":
        return [
            tool,
            "-F",
            "32",
            "-v",
            "VIBEBOOT",
            "-S",
            "512",
            "-s",
            str(args.boot_partition_sectors),
            "-o",
            str(args.boot_partition_start_lba),
            "-r",
            str(args.boot_partition_reserved_sectors),
            part_path,
        ]

    return [
        tool,
        "-F",
        "32",
        "-n",
        "VIBEBOOT",
        "-R",
        str(args.boot_partition_reserved_sectors),
        "-h",
        str(args.boot_partition_start_lba),
        part_path,
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--mkfs-fat", default="mkfs.fat")
    parser.add_argument("--mcopy", default="mcopy")
    parser.add_argument("--mmd", default="mmd")
    parser.add_argument("--mbr", required=True)
    parser.add_argument("--vbr", required=True)
    parser.add_argument("--stage2", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--image-total-sectors", type=int, required=True)
    parser.add_argument("--boot-partition-start-lba", type=int, required=True)
    parser.add_argument("--boot-partition-sectors", type=int, required=True)
    parser.add_argument("--boot-partition-reserved-sectors", type=int, required=True)
    parser.add_argument("--boot-stage2-start-sector", type=int, required=True)
    parser.add_argument("--boot-kernel-start-sector", type=int, required=True)
    parser.add_argument("--data-partition-start-lba", type=int, default=0)
    parser.add_argument("--data-partition-sectors", type=int, default=0)
    parser.add_argument("--data-partition-image")
    parser.add_argument("--boot-file", action="append", default=[])
    args = parser.parse_args()
    has_mcopy = shutil.which(args.mcopy) is not None
    has_mmd = shutil.which(args.mmd) is not None
    use_mtools = has_mcopy and has_mmd

    image_size = args.image_total_sectors * 512
    with open(args.image, "wb") as image:
        image.truncate(image_size)

    with open(args.mbr, "rb") as handle:
        write_file(args.image, handle.read(), 0)

    with tempfile.TemporaryDirectory() as tmpdir:
        part_path = os.path.join(tmpdir, "bootpart.img")
        with open(part_path, "wb") as part:
            part.truncate(args.boot_partition_sectors * 512)

        run(mkfs_fat_command(args, part_path))

        with open(part_path, "r+b") as part, open(args.vbr, "rb") as vbr_handle:
            vbr = bytearray(part.read(512))
            custom = vbr_handle.read(512)
            # Keep the FAT32 BPB/EBPB fields, but replace the VBR entry jump
            # and boot code with our loader.
            vbr[0:3] = custom[0:3]
            vbr[90:510] = custom[90:510]
            vbr[510:512] = custom[510:512]
            part.seek(0)
            part.write(vbr)

        with open(args.stage2, "rb") as stage2_handle:
            stage2 = stage2_handle.read()
        stage2_sectors = (len(stage2) + 511) // 512
        if stage2_sectors + args.boot_stage2_start_sector > args.boot_kernel_start_sector:
            raise SystemExit("stage2 overlaps reserved kernel slot")

        with open(args.kernel, "rb") as kernel_handle:
            kernel = kernel_handle.read()
        kernel_sectors = (len(kernel) + 511) // 512
        if kernel_sectors + args.boot_kernel_start_sector > args.boot_partition_reserved_sectors:
            raise SystemExit("kernel does not fit inside FAT32 reserved sectors")

        with open(part_path, "r+b") as part:
            part.seek(args.boot_stage2_start_sector * 512)
            part.write(stage2)
            stage2_padding = (stage2_sectors * 512) - len(stage2)
            if stage2_padding > 0:
                part.write(b"\0" * stage2_padding)

            part.seek(args.boot_kernel_start_sector * 512)
            part.write(kernel)
            padding = (kernel_sectors * 512) - len(kernel)
            if padding > 0:
                part.write(b"\0" * padding)

        boot_files = list(args.boot_file)
        if not boot_files:
            boot_files.append(f"{args.kernel}::/KERNEL.BIN")

        for spec in boot_files:
            source_path, separator, destination = spec.partition("::")

            if separator == "" or not source_path or not destination:
                raise SystemExit(f"invalid --boot-file spec: {spec!r}")
            if use_mtools:
                copy_boot_file(part_path, source_path, destination, args.mcopy, args.mmd)
            else:
                copy_boot_file_without_mtools(part_path, source_path, destination)

        with open(part_path, "rb") as part:
            write_file(args.image, part.read(), args.boot_partition_start_lba * 512)

    if args.data_partition_image:
        if args.data_partition_start_lba <= 0 or args.data_partition_sectors <= 0:
            raise SystemExit("data partition geometry is required with --data-partition-image")

        with open(args.data_partition_image, "rb") as handle:
            data_partition = handle.read()
        max_size = args.data_partition_sectors * 512
        if len(data_partition) > max_size:
            raise SystemExit("data partition image does not fit inside the target partition")
        write_file(args.image, data_partition, args.data_partition_start_lba * 512)


if __name__ == "__main__":
    main()

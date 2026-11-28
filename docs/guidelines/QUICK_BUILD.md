# Vibe-OS Build Commands Quick Reference

## Main Build
```bash
make clean && make
```
Builds: Lean kernel (179K) + hello + js apps + boot.img

## Language Apps

### Build or Stage Individual Apps
```bash
make app-hello    # Builds and copies to build/bin/hello
make app-js       # Builds and copies to build/bin/js
make app-ruby     # Copies an existing build/lang/ruby.app
make app-python   # Copies an existing build/lang/python.app
```

### Build and Stage Language Apps
```bash
make apps
```
Builds Hello and the `js` command using the VibeOS app toolchain and copies them to `build/bin/`. Ruby, Python, Java, and Javac are copied when their `.app` files already exist. The optional glibc build is separate. Use `make run` to build and boot the image with its AppFS apps. The bundled `js`, `ruby`, and `python` commands report `ENOSYS` because their engines are still pending.

## Libraries

### Experimental glibc Build
```bash
make glibc-core                             # Attempt the core subset
make -f Build.glibc.mk glibc-full-build       # Attempt the full archive
make glibc-clean                            # Remove glibc build artifacts
```

The experimental glibc recipes can report success after compiler errors. See [known bug 26](../KNOWN_BUGS.md#x-26-make-apps-disparava-o-build-experimental-da-glibc-no-macos).

## Cleanup
```bash
make clean        # Remove all build artifacts
make apps-clean   # Remove staged apps and language app build artifacts
```

## Testing

### Run in QEMU
```bash
make run
```

Default `make run` profile now uses a T61-ish QEMU setup:
- `-cpu core2duo`
- `-smp 2,sockets=1,cores=2,threads=1,maxcpus=2`
- `-machine pc`

You can override it inline when needed:
```bash
make run QEMU_RUN_CPU=pentium
make run QEMU_RUN_SMP=1
make run QEMU_RUN_MACHINE=q35
```

### Run with Debug Serial Output
```bash
make run-debug
```

### Run Headless CPU Regression Targets
```bash
make run-headless-core2duo-debug
make run-headless-pentium-debug
make run-headless-atom-debug
```

### Run Headless AHCI Regression
```bash
make run-headless-ahci-debug
```

### Run Headless USB BIOS Boot Validation
```bash
make run-headless-usb-debug
```

### Run Full Phase 6 Validation Matrix
```bash
make validate-phase6
```
Writes: `build/phase6-validation.md`

### Build The Legacy Raw Data Image
```bash
make legacy-data-img
```
Builds: `build/data-partition.img` with the AppFS directory, app area, persistence area, and bundled raw assets in the same logical layout still used by the data partition.

### Low-Wear Real Hardware Loop
```bash
make build/stage2.bin build/boot.bin
python3 tools/patch_boot_sectors.py \
  --target /dev/sdX \
  --vbr build/boot.bin \
  --stage2 build/stage2.bin
```
Writes only the FAT32 VBR plus the reserved `stage2` slot on the existing disk. This is the preferred loop for BIOS/bootloader debugging on fragile IDE media because it avoids rebuilding and reflashing the whole disk image.
If you also changed `boot/mbr.asm`, add `--mbr build/mbr.bin`.

### Dry Run For The Low-Wear Patch
```bash
python3 tools/patch_boot_sectors.py \
  --target /dev/sdX \
  --vbr build/boot.bin \
  --stage2 build/stage2.bin \
  --dry-run
```
Prints how many sectors would be touched before writing anything.

### When To Use Full Image Rebuild
- Use `make build/boot.img` when the FAT32 contents changed, such as `KERNEL.BIN`, manifests, or bundled boot assets.
- Use the low-wear patch loop when only `boot/stage1.asm`, `boot/stage2.asm`, or BIOS-facing boot code changed.
- If you patch `stage2` without patching `boot.bin`, keep the stage2 sector count unchanged; otherwise stage1 may load the wrong amount of data.

### GDB Debug Mode (stops at boot)
```bash
make debug
# In another terminal:
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) continue
```

## File Structure After Build

```
vibe-os/
├── build/
│   ├── bin/          - Staged language apps from make apps
│   │   ├── hello
│   │   └── js
│   ├── boot.img      (1.4 MB) - Bootable image
│   ├── data-partition.img - Raw legacy data/AppFS volume
│   ├── kernel.bin    (179 KB) - Lean kernel
│   ├── kernel.elf    (217 KB) - Kernel ELF
│   ├── boot-policy.txt - USB boot/loading strategy manifest copied to FAT32 as BOOTPOL.TXT
│   ├── phase6-validation.md - Generated QEMU compatibility matrix report
│   ├── lang/userland.app - External boot shell app autostarted by init
│   └── userland-main.bin (optional legacy monolith)
└── ...
```

## Architecture

```
┌─────────────────┐
│ FAT32 boot part │ (MBR/VBR/stage2/kernel/manifest)
├─────────────────┤
│ Raw data part   │ (AppFS + persist + bundled assets)
├─────────────────┤
└─────────────────┘

At Runtime:
┌─────────────────┐
│  /bin/userland  │ (autostarted AppFS shell app)
│  /bin/hello     │
│  /bin/mkdir     │
│  /bin/js        │
│  /bin/ruby      │ (when built)
│  /bin/python    │ (when built)
└─────────────────┘
```

See [BUILD_LANGS.md](BUILD_LANGS.md) for language app targets and runtime support.

## Troubleshooting

### Permission Errors in build/
```bash
chmod -R 755 build 2>/dev/null
make clean
make
```

### QEMU Not Found
```bash
# macOS with Homebrew:
brew install qemu
```

### GCC Errors (i686-elf-gcc missing)
```bash
# macOS with Homebrew:
brew install i686-elf-gcc nasm
```

### Gentoo Without `dosfstools`
If `mkfs.fat` is unavailable but `mtools` is installed, use the bundled wrapper:

```bash
make MKFS_FAT_TOOL=./tools/mkfs-fat-mtools.sh
```

This keeps the Linux host toolchain path and formats the FAT32 boot partition via `mformat`, including the reserved-sector layout expected by the bootloader image.

## Performance Notes

- **Kernel compilation**: ~10 seconds
- **Full app build**: ~30 seconds
- **GlibC core**: ~5 minutes (810 functions, 18 KB)
- **GlibC full**: ~variable (7529 functions, 27 KB)
- **Target CPU**: i5 2600 (native 32-bit x86, not cross-compile)

## Key Files

- **Makefile** - Main build orchestration
- **Build.glibc.mk** - GlibC build system
- **lang/sdk/app_entry.c** - App entry point template
- **lang/sdk/app_runtime.c** - Syscall wrappers (malloc, stdio, etc.)
- **lang/apps/*/\*_main.c** - Language runtime implementations
- **headers/lang/vibe_*.h** - Vibe OS C library headers

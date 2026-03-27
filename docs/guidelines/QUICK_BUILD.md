# Vibe-OS Build Commands Quick Reference

## Main Build
```bash
make clean && make
```
Builds: Lean kernel (179K) + hello + js apps + boot.img

## Language Apps

### Copy Already Built Apps to /bin
```bash
make app-hello    # → /bin/hello (3.1K)
make app-js       # → /bin/js (3.4K)
make app-ruby     # → Shows how to build mruby first
make app-python   # → Shows how to build micropython first
```

### Copy All Built Apps at Once
```bash
make apps
```
Copies hello + js to /bin, and libglibc.a to /lib

## Libraries

### GlibC (Optional)
```bash
make glibc-core   # Build core glibc (810 functions, ~18KB)
make glibc        # Build full glibc (7529 functions, ~27KB)
make glibc-clean  # Remove glibc build artifacts
```

## Cleanup
```bash
make clean        # Remove all build artifacts
make apps-clean   # Remove /bin and /lib directories
```

## Testing

### Run in QEMU
```bash
make run
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
├── bin/
│   ├── hello         (3.1 KB) - Hello world executable
│   └── js            (3.4 KB) - JavaScript runtime
├── lib/
│   └── libglibc.a    (27 KB)  - Shared C library
├── build/
│   ├── boot.img      (1.4 MB) - Bootable image
│   ├── data-partition.img - Raw legacy data/AppFS volume
│   ├── kernel.bin    (179 KB) - Lean kernel
│   ├── kernel.elf    (217 KB) - Kernel ELF
│   ├── boot-policy.txt - USB boot/loading strategy manifest copied to FAT32 as BOOTPOLICY.TXT
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
├─────────────────┤
│  /lib/libglibc.a│ (27 KB shared)
└─────────────────┘
```

## For Future Builds

### Ruby (MRuby)
```bash
cd lang/vendor/mruby
./minirake
cd ../../../
make app-ruby
```

### Python (MicroPython)
```bash
cd lang/vendor/micropython
make
cd ../../../
make app-python
```

See [BUILD_LANGS.md](BUILD_LANGS.md) for detailed setup.

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

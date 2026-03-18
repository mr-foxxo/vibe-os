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
│   ├── kernel.bin    (179 KB) - Lean kernel
│   ├── kernel.elf    (217 KB) - Kernel ELF
│   └── userland-main.bin (167 KB)
└── ...
```

## Architecture

```
┌─────────────────┐
│   Boot Sector   │ (512 B)
├─────────────────┤
│   Kernel        │ (179 KB, no glibc)
├─────────────────┤
│   Apps Embedded │ (hello, js)
│   in AppFS      │
├─────────────────┤
│   Free Space    │
└─────────────────┘

At Runtime:
┌─────────────────┐
│  /bin/hello     │ (links libglibc.a)
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

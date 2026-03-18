# Building Language Runtimes for Vibe-OS

Vibe-OS now supports multiple language runtimes as standalone executables in `/bin`, sharing the system's libglibc.a.

## Available Languages

### ✅ Fully Working

#### Hello World
```bash
make app-hello
```
Produces: `/bin/hello` (3.1 KB)

#### JavaScript (QuickJS-ng)
```bash
make app-js
```
Produces: `/bin/js` (3.4 KB)

### 🔄 Requires Vendor Build

#### Ruby (MRuby)
**Requirements**: Complete mruby vendor build with headers

```bash
# 1. Build mruby vendor (one time)
cd lang/vendor/mruby
./minirake
cd ../../..

# 2. Compile ruby app
make app-ruby
```
Produces: `/bin/ruby`

#### Python (MicroPython)
**Requirements**: Complete micropython vendor build with headers

```bash
# 1. Build micropython vendor (one time)
cd lang/vendor/micropython
make -C mpy-cross  # Build cross-compiler if needed
make -C ports/bare-metal  # Bare-metal build
cd ../../..

# 2. Compile python app
make app-python
```
Produces: `/bin/python`

## Architecture

```
Kernel (179 KB, no glibc)
    ↓
Apps in /bin (standalone executables)
    ↓
Shared /lib/libglibc.a (27 KB)
```

Each app in `/bin`:
- Links libglibc.a statically
- Uses app_runtime.c for syscall wrappers
- Can be invoked from shell: `hello`, `js`, `ruby`, `python`

## Building Everything

```bash
# Kernel + all available apps
make clean && make

# Copy to /bin
cp build/lang/hello.app bin/hello
cp build/lang/js.app bin/js

# Copy shared glibc
cp build/libglibc-full.a lib/libglibc.a
```

## Using Apps from Shell

Once built, apps are available in the filesystem:

```bash
# Hello world
hello

# JavaScript REPL
js

# Ruby (when built)
ruby

# Python (when built)
python
```

## Build System

- **Makefile**: Main build orchestration
  - `make app-hello`, `make app-js`, etc.
  - `make apps` - copies all built apps to /bin
  - `make apps-clean` - removes /bin and /lib

- **Makefile Variables**:
  - `HELLO_APP_BIN`, `JS_APP_BIN`, etc. - app binary paths
  - `LANG_APP_BINS` - list for boot image

## Next Steps

1. Once mruby/micropython vendors are complete:
   ```bash
   make app-ruby && make app-python
   ```

2. Test in QEMU:
   ```bash
   make run
   ```

3. Invoke from shell:
   - Type `hello` and press Enter
   - Type `js` for REPL
   - etc.

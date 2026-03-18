# Apps Build for Vibe OS
# Compiles language runtimes as standalone executables in /bin
# Each linked against dynamic glibc.so (or static glibc.a)

APPS_DIR := lang/apps
BIN_DIR := bin
LIB_DIR := lib

# Target apps to build
APPS := js ruby python hello

# Glibc library (to be created)
GLIBC_SO := $(LIB_DIR)/libglibc.so
GLIBC_A := $(LIB_DIR)/libglibc.a

# App build rules
define build_app
$1_MAIN := $(APPS_DIR)/$1/$1_main.c
$1_BIN := $(BIN_DIR)/$1
$1_OBJS := $(BIN_DIR)/$1.o

$$(BIN_DIR)/$1: $1_MAIN
	@mkdir -p $(BIN_DIR)
	@echo "Building $$@..."
	gcc -m32 -Os -I. -Iheaders -Ilang/vendor/glibc/include \
		-static $$< -o $$@ 2>/dev/null || \
	echo "  (requires glibc headers)"

endef

$(foreach app,$(APPS),$(eval $(call build_app,$(app))))

# Dynamic glibc library (if glibc-full.a exists)
$(GLIBC_SO): build/libglibc-full.a
	@mkdir -p $(LIB_DIR)
	@echo "Creating dynamic glibc library..."
	@gcc -m32 -shared -fPIC $< -o $@ 2>/dev/null || \
	echo "  (requires libglibc-full.a from make -f Build.glibc.mk glibc-full-build)"

# Build all apps
apps: $(addprefix $(BIN_DIR)/,$(APPS))

# Clean
apps-clean:
	rm -rf $(BIN_DIR) $(LIB_DIR)

apps-list:
	@echo "Available apps: $(APPS)"
	@echo "Currently built: $(filter $(BIN_DIR)/*,$(wildcard $(BIN_DIR)/*) $(wildcard $(LIB_DIR)/*))"

.PHONY: apps apps-clean apps-list

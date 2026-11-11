# Build rules for BSD Korn shell (pdksh) on VibeOS

KSH_DIR := compat/bin/ksh

KSH_CFLAGS := -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie \
	-fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra \
	-Wno-pointer-sign -Wno-unused-parameter -Wno-unused-variable \
	-DHAVE_CONFIG_H -DEMACS -DSMALL \
	-I$(KSH_DIR) -Icompat/include -Iheaders -Ilang/include -I.

KSH_SRCS := \
	$(KSH_DIR)/alloc.c \
	$(KSH_DIR)/c_ksh.c \
	$(KSH_DIR)/c_sh.c \
	$(KSH_DIR)/c_test.c \
	$(KSH_DIR)/edit.c \
	$(KSH_DIR)/emacs.c \
	$(KSH_DIR)/eval.c \
	$(KSH_DIR)/exec.c \
	$(KSH_DIR)/expr.c \
	$(KSH_DIR)/history.c \
	$(KSH_DIR)/io.c \
	$(KSH_DIR)/jobs.c \
	$(KSH_DIR)/lex.c \
	$(KSH_DIR)/mail.c \
	$(KSH_DIR)/main.c \
	$(KSH_DIR)/misc.c \
	$(KSH_DIR)/path.c \
	$(KSH_DIR)/ps.c \
	$(KSH_DIR)/shf.c \
	$(KSH_DIR)/syn.c \
	$(KSH_DIR)/table.c \
	$(KSH_DIR)/trap.c \
	$(KSH_DIR)/tty.c \
	$(KSH_DIR)/var.c \
	$(KSH_DIR)/version.c \
	$(KSH_DIR)/vi.c \
	$(KSH_DIR)/vibeos_compat.c

KSH_OBJS := $(addprefix $(BUILD_DIR)/,$(patsubst %.c,%.o,$(KSH_SRCS)))

$(BUILD_DIR)/compat/bin/ksh/%.o: compat/bin/ksh/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KSH_CFLAGS) -c $< -o $@

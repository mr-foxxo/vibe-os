# Build rules for BSD UHCI USB host controller stack
#
# Add BSD include path before compat/sys so our custom sys/*.h 
# headers (from bsd/) take precedence over compat/sys/sys/ versions.
# compat/sys is needed for dev/usb/*.h BSD USB headers.

BSD_USB_INCDIR := kernel/drivers/usb/bsd
# Note: kernel/drivers/usb/bsd MUST come before -idirafter headers in KERNEL_CFLAGS
# The final search order is: -I. ... -Ibsd -Icompat/sys ... -idirafter headers
BSD_USB_CFLAGS := -I$(BSD_USB_INCDIR) -Icompat/sys -Wno-unused-parameter -Wno-sign-compare

# BSD USB source files (from compat/sys/dev/usb/)
BSD_USB_SRCS := \
	compat/sys/dev/usb/uhci.c \
	compat/sys/dev/usb/usb_mem.c

# VibeOS glue for BSD USB
BSD_USB_SRCS += \
	kernel/drivers/usb/bsd_usb_impl.c \
	kernel/drivers/usb/bsd_usb_pci.c

BSD_USB_OBJS := $(addprefix $(BUILD_DIR)/,$(patsubst %.c,%.o,$(BSD_USB_SRCS)))

# Override CFLAGS for BSD USB files to add compatibility headers
$(BUILD_DIR)/compat/sys/dev/usb/%.o: compat/sys/dev/usb/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $(BSD_USB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/drivers/usb/bsd_usb_impl.o: kernel/drivers/usb/bsd_usb_impl.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $(BSD_USB_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/drivers/usb/bsd_usb_pci.o: kernel/drivers/usb/bsd_usb_pci.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $(BSD_USB_CFLAGS) -c $< -o $@

#!/usr/bin/env bash
# Host-side runner (tvzinha): flash the vibeOS image onto the Alcor USB stick
# through QEMU USB passthrough, because the stale 6.18.52 host kernel cannot
# load usb-storage (in-place upgrade without reboot; docs/hw_validation.md
# "Flash recipe" / P0 campaign). Requires sudo (usbfs) and the installed
# 6.18.55 kernel for the guest. TCG-only host: expect ~4 min per 256 MiB.
set -euo pipefail
IMG="${1:-build/vibe-os-usb.img}"
VENDOR="058f" PRODUCT="6387"          # Alcor Micro AU6387 stick
GUEST_KERNEL="/boot/vmlinuz-linux-cachyos-lts"
MODSRC="/lib/modules"                 # host modules dir, shared via 9p
WORK="$(mktemp -d /tmp/vibe-flash.XXXX)"
trap 'rm -rf "$WORK"' EXIT

[ -r "$IMG" ] || { echo "ERR: image '$IMG' missing (run: make imb)"; exit 1; }
[ -r "$GUEST_KERNEL" ] || { echo "ERR: guest kernel $GUEST_KERNEL missing"; exit 1; }

# Fail closed: exactly one matching USB stick on the bus.
STICKS=$(lsusb | grep -ci "${VENDOR}:${PRODUCT}" || true)
[ "$STICKS" -eq 1 ] || { echo "ERR: expected 1 stick ${VENDOR}:${PRODUCT}, found $STICKS"; exit 1; }

BUSYBOX=$(command -v busybox || true)
[ -n "$BUSYBOX" ] || { echo "ERR: host busybox missing (pacman -S busybox)"; exit 1; }

mkdir -p "$WORK/initrd"/{bin,proc,sys,dev,kmods}
cp "$BUSYBOX" "$WORK/initrd/bin/busybox"
for app in sh mount umount dd cat grep readlink insmod unzip sleep poweroff cmp ls ln; do
    ln -sf busybox "$WORK/initrd/bin/$app"
done
cp tools/usb_flash_guest/init_guest.sh "$WORK/initrd/init"
chmod +x "$WORK/initrd/init" "$WORK/initrd/bin/busybox"
( cd "$WORK/initrd" && find . | cpio -o -H newc --quiet | gzip > "$WORK/initrd.img" )

# ~15 min hard cap for the whole guest session
timeout 900 sudo -E qemu-system-x86_64 \
    -m 512 -smp 1 -machine pc \
    -kernel "$GUEST_KERNEL" \
    -initrd "$WORK/initrd.img" \
    -append "console=ttyS0 rdinit=/init" \
    -drive "file=$IMG,if=virtio,readonly=on" \
    -device virtio-9p-pci,fsdev=mods,mount_tag=mods \
    -fsdev "local,id=mods,path=$MODSRC,security_model=mapped-xattr" \
    -device usb-ehci \
    -device "usb-host,vendorid=$VENDOR,productid=$PRODUCT" \
    -serial stdio -display none -monitor none

echo "runner finished: look for RESULT WRITE_OK above"

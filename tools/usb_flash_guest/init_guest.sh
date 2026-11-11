#!/bin/sh
# Guest-side flasher for the vibeOS install stick (P0 campaign).
# Runs inside QEMU as init (pid 1) with the HOST's 6.18.55 kernel, whose
# modules live on the host disk (9p-exported /lib/modules tree).
# Proven end-to-end 2026-10-13; see docs/hw_validation.md "Flash recipe".
set -x
PATH=/bin:/sbin mount -t proc proc /proc 2>/dev/null
PATH=/bin:/sbin mount -t sysfs sys /sys 2>/dev/null
PATH=/bin:/sbin mount -t devtmpfs dev /dev 2>/dev/null

PATH=/bin:/sbin mount -t 9p -o trans=virtio mods /kmods 2>/dev/null
KO=$(ls /kmods/*/kernel/drivers/usb/storage/usb-storage.ko.zst 2>/dev/null | head -1)
[ -n "$KO" ] || { echo "RESULT ERR usb-storage.ko not found"; poweroff -f; }
if ! grep -q usb-storage /proc/devices 2>/dev/null; then
    insmod "$KO" || { echo "RESULT ERR insmod usb-storage"; poweroff -f; }
fi

# Give the stick time to enumerate, then FAIL CLOSED unless sda is a USB disk
i=0
while [ ! -b /dev/sda ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i+1)); done
[ -b /dev/sda ] || { echo "RESULT ERR no /dev/sda"; poweroff -f; }
readlink -f /sys/block/sda/device | grep -q usb \
    || { echo "RESULT ERR sda not usb"; poweroff -f; }

# Image arrives as virtio disk vda (readonly); target is the USB stick sda.
dd if=/dev/vda of=/dev/sda bs=1M conv=fsync status=none \
    || { echo "RESULT ERR dd"; poweroff -f; }

# Byte-for-byte MBR compare (cheap integrity check of geometry + signature)
dd if=/dev/vda bs=512 count=1 status=none | cmp - /dev/sda \
    || { echo "RESULT ERR mbr compare"; poweroff -f; }

echo "RESULT WRITE_OK"
poweroff -f

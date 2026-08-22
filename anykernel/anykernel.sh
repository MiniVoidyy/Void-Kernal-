#!/usr/bin/env bash
## AnyKernel3 flashable zip configuration for Void Kernel exynos9810 builds.
## Devices: Galaxy S9 (starlte), S9+ (star2lte), Note9 (crownlte)
## This file replaces the stock AnyKernel3 anykernel.sh at packaging time.

kernel.string=Void Kernel exynos9810 | by Mini_Voidyy | 20 Governors

do.modules=0
do.cleanup=0
do.cleanuponabort=0

device.name1=starlte
device.name2=star2lte
device.name3=crownlte
supported.versions=
supported.vendorversions=

# Keep dm-verity and forceencrypt state untouched (Samsung friendly)
flags="KEEPVERITY KEEPFORCEENCRYPT"

# Boot partition of all three Exynos 9810 devices
BLOCK=/dev/block/bootdevice/by-name/BOOT;

is_slot_device=0;
ramdisk_compression=auto;

split_boot;
flash_boot;

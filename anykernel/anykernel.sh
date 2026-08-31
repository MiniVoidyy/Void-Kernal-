#!/usr/bin/env bash
## AnyKernel3 config for Void Kernel universal installer
## Minimal - just enough for ak3-core.sh split_boot/flash_boot to work.

kernel.string=Void Kernel exynos9810 | by Mini_Voidyy | 20 Governors

do.modules=0
do.cleanup=0
do.cleanuponabort=0

device.name1=starlte
device.name2=star2lte
device.name3=crownlte
supported.versions=
supported.vendorversions=

flags="KEEPVERITY KEEPFORCEENCRYPT"

BLOCK=/dev/block/bootdevice/by-name/BOOT;
is_slot_device=0;
ramdisk_compression=auto;

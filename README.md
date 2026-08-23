# Void Kernel - Exynos 9810 (Galaxy S9 / S9+ / Note 9)

**Void Kernel** by **Mini_Voidyy** - a custom kernel builder for the Samsung
Galaxy S9 (`starlte`), S9+ (`star2lte`) and Note 9 (`crownlte`) Exynos models.

## Base

| | |
|---|---|
| Source | [duhansysl/exynos9810-kernel](https://github.com/duhansysl/exynos9810-kernel) (DS-ACK) |
| Kernel | **4.9.337** (real, bootable base) |
| Governors | **20** (7 stock + 13 custom) |
| Root | KernelSU / KernelSU Next / SukiSU-Ultra (kprobe hooks, non-GKI) |

> **Why not 6.6?** A true 6.6 build for Exynos 9810 exists only as a bare
> mainline port (no display, modem, camera or touch drivers), so it cannot
> run Android on these devices. Void Kernel therefore builds on the fully
> working DS-ACK 4.9.337 tree.

## The 20 governors

Stock: `schedutil` `interactive` `ondemand` `conservative` `userspace`
`performance` `powersave`

Custom (added by this project, in `governors/`):
`darkness` `nightmare` `intellidemand` `alucard` `blu_active`
`intelliactive` `yankactive` `lionfish` `impulse` `wave`
`smartassV2` `smartmax` `smartmax_eps`

Screen-aware governors (`smartassV2`, `smartmax`, `smartmax_eps`) cap the
frequency while the display is off.

## Build matrix - 36 flashable zips

| Variant | SELinux mode | Root solutions |
|---|---|---|
| AOSP enforcing | enforcing | KSU / KSU Next / SukiSU |
| AOSP permissive | permissive | KSU / KSU Next / SukiSU |
| One UI enforcing | enforcing | KSU / KSU Next / SukiSU |
| One UI permissive | permissive | KSU / KSU Next / SukiSU |

Each combination builds all three devices (`starlte`, `star2lte`, `crownlte`)
via AnyKernel3 and keeps your original ramdisk.

Permissive variants append `androidboot.selinux=permissive` to the kernel
command line (works on Android 10+ ROMs). Verify after flashing with
`adb shell getenforce`.

## Flashing in TWRP

Every kernel zip is a standard **AnyKernel3** package - it flashes in TWRP
exactly like any other custom kernel:

1. Unlock bootloader + have TWRP installed for your device.
2. Download the zip for **your device** from Actions artifacts.
3. Move it to internal storage / SD card.
4. TWRP -> Install -> select `VoidKernel-4.9.337-<variant>-<root>-<device>.zip`.
5. Reboot.

Flags are Samsung-friendly: `KEEPVERITY KEEPFORCEENCRYPT`, original ramdisk
kept untouched, only the boot partition is written.

## Build with GitHub Actions

1. Push this repo to GitHub.
2. Run the **Build Void Kernel** workflow (*Actions -> Run workflow*).
   The workflow uses Linux 4.9-compatible root releases by default. You can
   pass `ksu_ref` to test another tag or branch for the selected root solution.
3. Download artifacts: one bundle per variant/root combination containing a
   zip per device, plus the **Void Kernel Manager** module.

Artifacts are named `VoidKernel-<variant>-<root>`; zips inside are
`VoidKernel-4.9.337-<variant>-<root>-<device>.zip`.

## Build locally (Linux/WSL2)

```sh
sudo apt install bc bison build-essential curl flex git unzip zip libssl-dev python-is-python3 gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi
scripts/build.sh --variant oneui-enforcing --root sukisu --devices starlte
# output: build/zips/VoidKernel-4.9.337-oneui-enforcing-sukisu-starlte.zip
```

Toolchain (Proton Clang) downloads automatically. Root defaults are KernelSU
`v0.9.5`, KernelSU Next `v3.2.0-legacy`, and SukiSU-Ultra `v2.0_beta`.
Override with `KERNEL_REPO`, `KERNEL_BRANCH`, `KSU_REF`, `TOOLCHAIN_DIR` env
vars.

## Void Kernel Manager module

Flashable in TWRP **or** via Magisk / KernelSU / KernelSU Next / SukiSU app.
Installs the `voidman` CLI:

```
su -c voidman status
su -c voidman govs                                  # list all 20
su -c voidman setgov big darkness
su -c voidman freq big max 2704000
su -c voidman profile gaming                        # battery|balanced|performance|gaming
su -c voidman save                                  # persist current settings across boots
su -c voidman selinux permissive                    # runtime switch (enforcing builds)
```

The saved profile is re-applied automatically at every boot by `service.sh`.

## Credits

- duhansysl / ananjaser1211 (Apollo) - DS-ACK base
- RestlessGoose - CrownTrail/EMS work
- tiann (KernelSU), rifsxd (KernelSU-Next), SukiSU-Ultra team
- osm0sis - AnyKernel3

GPL-2.0 applies to all kernel sources.

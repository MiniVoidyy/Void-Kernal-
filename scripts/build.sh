#!/usr/bin/env bash
# Void Kernel exynos9810 kernel builder
#
# Usage:
#   scripts/build.sh --variant <v> --root <r> [--devices "..."] [options]
#
#     --variant   aosp-enforcing | aosp-permissive | oneui-enforcing | oneui-permissive
#     --root      y | n
#     --devices   space separated: starlte star2lte crownlte (default: all)
#     --clean     remove previous kernel tree/build dirs first
#
# Environment overrides:
#   KERNEL_REPO    (default duhansysl/exynos9810-kernel)
#   KERNEL_BRANCH  (default: repo HEAD)
#   TOOLCHAIN_DIR  (pre-installed clang toolchain; skips proton download)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VARIANT="aosp-enforcing"
ROOT="y"
DEVICES="starlte star2lte crownlte"
CLEAN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --variant) VARIANT="$2"; shift 2 ;;
        --root) ROOT="$2"; shift 2 ;;
        --devices) DEVICES="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

KERNEL_REPO="${KERNEL_REPO:-https://github.com/duhansysl/exynos9810-kernel.git}"
KERNEL_BRANCH="${KERNEL_BRANCH:-}"
KERNEL_DIR="$PROJECT_ROOT/kernel"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$PROJECT_ROOT/toolchain/proton-clang}"

echo "==> Variant : $VARIANT"
echo "==> Root    : $ROOT"
echo "==> Devices : $DEVICES"

mkdir -p "$PROJECT_ROOT/toolchain"

if [ ! -d "$KERNEL_DIR/.git" ] || [ "$CLEAN" = 1 ]; then
    rm -rf "$KERNEL_DIR"
    if [ -n "$KERNEL_BRANCH" ]; then
        git clone --depth=1 -b "$KERNEL_BRANCH" "$KERNEL_REPO" "$KERNEL_DIR" --recursive --shallow-submodules
    else
        git clone --depth=1 "$KERNEL_REPO" "$KERNEL_DIR" --recursive --shallow-submodules
    fi
fi

"$SCRIPT_DIR/add-governors.sh" "$KERNEL_DIR"
KSU_CONFIG=""
if [ "$ROOT" = "y" ]; then
    KSU_CONFIG="ksu.config"
fi

# Samsung hardcodes -mcpu/-mtune=exynos-m3 (their out-of-tree toolchain);
# neither clang's integrated assembler nor modern GNU as knows that cpu.
# Retune to a supported ARMv8 big core - instruction set is unchanged
# (-march=armv8-a+crypto+crc stays).
sed -i 's/exynos-m3/cortex-a75/g' "$KERNEL_DIR/Makefile"

if [ ! -x "$TOOLCHAIN_DIR/bin/clang" ]; then
    echo "==> Downloading Proton Clang toolchain"
    curl -L --fail -o /tmp/proton-clang.zip \
        https://github.com/kdrag0n/proton-clang/archive/refs/heads/master.zip
    rm -rf "$TOOLCHAIN_DIR"
    unzip -q /tmp/proton-clang.zip -d "$PROJECT_ROOT/toolchain"
    mv "$PROJECT_ROOT/toolchain/proton-clang-master" "$TOOLCHAIN_DIR"
    rm -f /tmp/proton-clang.zip
    # Proton ships an ancient GNU ld/as that cannot link modern glibc
    # (SHT_RELR). Remove them so HOSTCC=gcc uses the system binutils;
    # target linking uses ld.lld and cross-prefixed tools regardless.
    rm -f "$TOOLCHAIN_DIR/bin/ld" "$TOOLCHAIN_DIR/bin/as"
    # This tree's vdso32 build invokes $(CROSS_COMPILE_ARM32)clang; no
    # such prefixed binary exists in any clang distribution, so alias it
    # (the prefixed ld comes from the distro binutils package).
    ln -sf clang "$TOOLCHAIN_DIR/bin/arm-linux-gnueabi-clang"
fi

export PATH="$TOOLCHAIN_DIR/bin:$PATH"
command -v clang >/dev/null 2>&1 || { echo "ERROR: clang not found after toolchain setup" >&2; exit 1; }
export ARCH=arm64
export KBUILD_BUILD_USER=void
export KBUILD_BUILD_HOST=exynos9810
if [ "$ROOT" = "y" ]; then
    export LOCALVERSION="-VOID-ksun-${VARIANT}"
else
    export LOCALVERSION="-VOID-${VARIANT}"
fi
# Silences "environment variable ANDROID_MAJOR_VERSION undefined" kconfig warning
export ANDROID_MAJOR_VERSION="${ANDROID_MAJOR_VERSION:-a13}"

CROSS_A64="aarch64-linux-gnu-"
CROSS_A32="arm-linux-gnueabi-"

ZIPS=""
mkdir -p "$PROJECT_ROOT/logs"

# One kernel serves every exynos9810 device: Image.gz-dtb embeds ALL enabled
# board DTBs ($(dtb-y)) and the bootloader picks its own. Per-device
# *_defconfig files in this tree are tiny overlays, NOT full configs -
# exynos9810_defconfig is the real Samsung base.
OUT_DIR="$PROJECT_ROOT/out"
[ -f "$KERNEL_DIR/arch/arm64/configs/exynos9810_defconfig" ] || {
    echo "ERROR: exynos9810_defconfig missing" >&2; exit 1;
}

echo "==> defconfig + variant merge"
make -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 exynos9810_defconfig $KSU_CONFIG
"$SCRIPT_DIR/apply-variant.sh" "$KERNEL_DIR" "$VARIANT" "$OUT_DIR"

echo "==> building (jobs=$(nproc))"
BUILD_LOG="$PROJECT_ROOT/logs/build.log"
if ! make -j"$(nproc)" -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 \
    CROSS_COMPILE="$CROSS_A64" \
    CC=clang \
    LD=ld.lld \
    AR=llvm-ar \
    NM=llvm-nm \
    STRIP=llvm-strip \
    OBJCOPY=llvm-objcopy \
    OBJDUMP=llvm-objdump \
    READELF=llvm-readelf \
    HOSTCC=gcc \
    HOSTAR=ar \
    Image.gz-dtb >"$BUILD_LOG" 2>&1; then
    echo "==> BUILD FAILED - last 80 lines of $BUILD_LOG:"
    tail -n 80 "$BUILD_LOG"
    exit 1
fi
echo "==> build OK"

for dev in $DEVICES; do
    ZIP_NAME="VoidKernel-4.9.337-${VARIANT}-${ROOT}-${dev}"
    "$SCRIPT_DIR/package.sh" "$KERNEL_DIR" "$OUT_DIR" "$dev" "$ZIP_NAME"
    ZIPS="$ZIPS $ZIP_NAME.zip"
done

echo ""
echo "==> DONE. Flashable zips:"
for z in $ZIPS; do
    echo "    $PROJECT_ROOT/build/zips/$z"
done

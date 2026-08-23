#!/usr/bin/env bash
# Void Kernel exynos9810 kernel builder
#
# Usage:
#   scripts/build.sh --variant <v> --root <r> [--devices "..."] [options]
#
#     --variant   aosp-enforcing | aosp-permissive | oneui-enforcing | oneui-permissive
#     --root      ksu | ksu-next | sukisu
#     --devices   space separated: starlte star2lte crownlte (default: all)
#     --clean     remove previous kernel tree/build dirs first
#
# Environment overrides:
#   KERNEL_REPO    (default duhansysl/exynos9810-kernel)
#   KERNEL_BRANCH  (default: repo HEAD)
#   KSU_REF        (tag/branch for the root solution repo)
#   TOOLCHAIN_DIR  (pre-installed clang toolchain; skips proton download)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VARIANT="aosp-enforcing"
ROOT="ksu"
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
        git clone --depth=1 -b "$KERNEL_BRANCH" "$KERNEL_REPO" "$KERNEL_DIR"
    else
        git clone --depth=1 "$KERNEL_REPO" "$KERNEL_DIR"
    fi
fi

"$SCRIPT_DIR/add-governors.sh" "$KERNEL_DIR"
"$SCRIPT_DIR/integrate-root.sh" "$ROOT" "$KERNEL_DIR"

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
fi

export PATH="$TOOLCHAIN_DIR/bin:$PATH"
command -v clang >/dev/null 2>&1 || { echo "ERROR: clang not found after toolchain setup" >&2; exit 1; }
export ARCH=arm64
export KBUILD_BUILD_USER=void
export KBUILD_BUILD_HOST=exynos9810
export LOCALVERSION="-VOID-${ROOT}-${VARIANT}"
# Silences "environment variable ANDROID_MAJOR_VERSION undefined" kconfig warning
export ANDROID_MAJOR_VERSION="${ANDROID_MAJOR_VERSION:-a13}"

CROSS_A64="aarch64-linux-gnu-"
CROSS_A32="arm-linux-gnueabi-"

ZIPS=""
mkdir -p "$PROJECT_ROOT/logs"
for dev in $DEVICES; do
    OUT_DIR="$PROJECT_ROOT/out/$dev"
    DEFCONFIG_FILE="$KERNEL_DIR/arch/arm64/configs/${dev}_defconfig"
    [ -f "$DEFCONFIG_FILE" ] || { echo "ERROR: ${dev}_defconfig missing" >&2; exit 1; }

    echo "==> [$dev] base defconfig + device/variant merge"
    make -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 exynos9810_defconfig
    "$SCRIPT_DIR/apply-variant.sh" "$KERNEL_DIR" "$VARIANT" "$OUT_DIR" "$DEFCONFIG_FILE"

    echo "==> [$dev] building (jobs=$(nproc))"
    BUILD_LOG="$PROJECT_ROOT/logs/${dev}.log"
    if ! make -j"$(nproc)" -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 \
        LLVM=1 \
        LLVM_IAS=1 \
        CROSS_COMPILE="$CROSS_A64" \
        CROSS_COMPILE_ARM32="$CROSS_A32" \
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
        echo "==> BUILD FAILED for $dev - last 80 lines of $BUILD_LOG:"
        tail -n 80 "$BUILD_LOG"
        exit 1
    fi
    echo "==> [$dev] build OK"

    ZIP_NAME="VoidKernel-4.9.337-${VARIANT}-${ROOT}-${dev}"
    "$SCRIPT_DIR/package.sh" "$KERNEL_DIR" "$OUT_DIR" "$dev" "$ZIP_NAME"
    ZIPS="$ZIPS $ZIP_NAME.zip"
done

echo ""
echo "==> DONE. Flashable zips:"
for z in $ZIPS; do
    echo "    $PROJECT_ROOT/build/zips/$z"
done

#!/usr/bin/env bash
# Builds a universal AnyKernel3 flashable zip for a single root solution.
# Contains 12 kernel images (4 variants x 3 devices) with auto-detection.
# The installer auto-detects device, ROM type, and SELinux mode.
#
# Usage: package-universal.sh <root> <kernel_clone_dir>
#   root: ksu | ksu-next | sukisu
#   kernel_clone_dir: temp dir where kernel source lives (cleaned per root)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ROOT="${1:?usage: package-universal.sh <root> <kernel_clone_dir>}"
KERNEL_SRC="${2:?usage: package-universal.sh <root> <kernel_clone_dir>}"

VARIANTS="aosp-enforcing aosp-permissive oneui-enforcing oneui-permissive"
DEVICES="starlte star2lte crownlte"

TOOLCHAIN_DIR="$PROJECT_ROOT/toolchain/proton-clang"
AK3_DIR="$PROJECT_ROOT/build/AnyKernel3"
OUT_BASE="$PROJECT_ROOT/build/universal-$ROOT"

mkdir -p "$PROJECT_ROOT/logs" "$PROJECT_ROOT/build/zips" "$OUT_BASE"

# --- Toolchain ---
if [ ! -x "$TOOLCHAIN_DIR/bin/clang" ]; then
    echo "==> Downloading Proton Clang toolchain"
    mkdir -p "$PROJECT_ROOT/toolchain"
    curl -L --fail -o /tmp/proton-clang.zip \
        https://github.com/kdrag0n/proton-clang/archive/refs/heads/master.zip
    rm -rf "$TOOLCHAIN_DIR"
    unzip -q /tmp/proton-clang.zip -d "$PROJECT_ROOT/toolchain"
    mv "$PROJECT_ROOT/toolchain/proton-clang-master" "$TOOLCHAIN_DIR"
    rm -f /tmp/proton-clang.zip
    rm -f "$TOOLCHAIN_DIR/bin/ld" "$TOOLCHAIN_DIR/bin/as"
    ln -sf clang "$TOOLCHAIN_DIR/bin/arm-linux-gnueabi-clang"
fi
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

# --- Clone kernel source fresh for this root ---
rm -rf "$KERNEL_SRC"
echo "==> Cloning kernel source for root=$ROOT"
git clone --depth=1 https://github.com/duhansysl/exynos9810-kernel.git "$KERNEL_SRC"

# --- Integrate root ---
"$SCRIPT_DIR/integrate-root.sh" "$ROOT" "$KERNEL_SRC"

# --- Retune CPU ---
sed -i 's/exynos-m3/cortex-a75/g' "$KERNEL_SRC/Makefile"

# --- Build all variant x device combos ---
TOTAL=0
FAILED=0
export ARCH=arm64
export KBUILD_BUILD_USER=void
export KBUILD_BUILD_HOST=exynos9810
export ANDROID_MAJOR_VERSION="${ANDROID_MAJOR_VERSION:-a13}"
CROSS_A64="aarch64-linux-gnu-"
CROSS_A32="arm-linux-gnueabi-"

for DEVICE in $DEVICES; do
    for VARIANT in $VARIANTS; do
        TOTAL=$((TOTAL + 1))
        DEST="$OUT_BASE/kernels/$DEVICE/$VARIANT"
        mkdir -p "$DEST"

        OUT_DIR="$PROJECT_ROOT/out-$ROOT-$DEVICE"
        rm -rf "$OUT_DIR"

        echo "==> [$TOTAL/12] $ROOT / $DEVICE / $VARIANT"

        # defconfig + overlay + variant
        make -C "$KERNEL_SRC" O="$OUT_DIR" ARCH=arm64 exynos9810_defconfig
        "$SCRIPT_DIR/apply-variant.sh" "$KERNEL_SRC" "$VARIANT" "$OUT_DIR" \
            "$KERNEL_SRC/arch/arm64/configs/${DEVICE}_defconfig"

        export LOCALVERSION="-VOID-${ROOT}-${VARIANT}-${DEVICE}"

        if ! make -j"$(nproc)" -C "$KERNEL_SRC" O="$OUT_DIR" ARCH=arm64 \
            LLVM=1 LLVM_IAS=1 \
            CROSS_COMPILE="$CROSS_A64" \
            CROSS_COMPILE_ARM32="$CROSS_A32" \
            CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm STRIP=llvm-strip \
            OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf \
            HOSTCC=gcc HOSTAR=ar \
            Image.gz-dtb >"$PROJECT_ROOT/logs/build-${ROOT}-${DEVICE}-${VARIANT}.log" 2>&1; then
            echo "  FAILED: $ROOT / $DEVICE / $VARIANT" >&2
            FAILED=$((FAILED + 1))
            continue
        fi

        cp -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb" "$DEST/"
        echo "  OK ($(du -h "$DEST/Image.gz-dtb" | cut -f1))"
    done
done

echo "==> Built $((TOTAL - FAILED))/$TOTAL variants ($FAILED failed)"

if [ "$FAILED" -gt 0 ]; then
    echo "WARNING: $FAILED builds failed, packaging what we have" >&2
fi

# --- Package AnyKernel3 zip ---
echo "==> Packaging universal zip for $ROOT..."

rm -rf "$AK3_DIR"
git clone --depth=1 https://github.com/osm0sis/AnyKernel3.git "$AK3_DIR"
rm -rf "$AK3_DIR/.git" "$AK3_DIR/README.md" "$AK3_DIR/.github"

# Our anykernel.sh (minimal config for ak3-core.sh)
cp -f "$PROJECT_ROOT/anykernel/anykernel.sh" "$AK3_DIR/anykernel.sh"

# Our auto-detecting update-binary replaces stock
cp -f "$PROJECT_ROOT/anykernel/update-binary" "$AK3_DIR/META-INF/com/google/android/update-binary"

# Bundle all kernel images
cp -r "$OUT_BASE/kernels" "$AK3_DIR/kernels"

# Build zip
ZIP_NAME="VoidKernel-${ROOT}-universal-4.9.337"
(cd "$AK3_DIR" && zip -r9 -q "$PROJECT_ROOT/build/zips/$ZIP_NAME.zip" .)

echo "==> Packed build/zips/$ZIP_NAME.zip ($(du -h "$PROJECT_ROOT/build/zips/$ZIP_NAME.zip" | cut -f1))"
echo "==> DONE ($ROOT)"

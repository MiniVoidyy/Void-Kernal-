#!/usr/bin/env bash
# Packages a built exynos9810 kernel into an AnyKernel3 flashable zip.
#
# Usage: package.sh <kernel_dir> <out_dir> <device> <zip_name>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

KERNEL_DIR="${1:?usage: package.sh <kernel_dir> <out_dir> <device> <zip_name>}"
BUILD_OUT="${2:?usage: package.sh <kernel_dir> <out_dir> <device> <zip_name>}"
DEVICE="${3:?usage: package.sh <kernel_dir> <out_dir> <device> <zip_name>}"
ZIP_NAME="${4:?usage: package.sh <kernel_dir> <out_dir> <device> <zip_name>}"

AK3_DIR="$PROJECT_ROOT/build/AnyKernel3"
BOOT_DIR="$BUILD_OUT/arch/arm64/boot"

rm -rf "$AK3_DIR"
git clone --depth=1 https://github.com/osm0sis/AnyKernel3.git "$AK3_DIR"
rm -rf "$AK3_DIR/.git" "$AK3_DIR/README.md" "$AK3_DIR/.github"

cp -f "$PROJECT_ROOT/anykernel/anykernel.sh" "$AK3_DIR/anykernel.sh"

if [ -f "$BOOT_DIR/Image.gz-dtb" ]; then
    cp -f "$BOOT_DIR/Image.gz-dtb" "$AK3_DIR/"
elif [ -f "$BOOT_DIR/Image.gz" ]; then
    cp -f "$BOOT_DIR/Image.gz" "$AK3_DIR/"
    if compgen -G "$BOOT_DIR/dts/*/*.dtb" > /dev/null; then
        mkdir -p "$AK3_DIR/dtb"
        find "$BOOT_DIR/dts" -name '*.dtb' -exec cp -f {} "$AK3_DIR/dtb/" \;
    fi
else
    echo "ERROR: no kernel image found in $BOOT_DIR" >&2
    exit 1
fi

mkdir -p "$PROJECT_ROOT/build/zips"
(cd "$AK3_DIR" && zip -r9 -q "$PROJECT_ROOT/build/zips/$ZIP_NAME.zip" .)

echo "==> Packed build/zips/$ZIP_NAME.zip ($DEVICE)"

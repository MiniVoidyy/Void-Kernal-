#!/usr/bin/env bash
# Applies a Void Kernel variant configuration (ROM x SELinux mode) on top of a
# freshly generated device defconfig.
#
# Usage: apply-variant.sh <kernel_dir> <variant> [out_dir]
#   variant: aosp-enforcing|aosp-permissive|oneui-enforcing|oneui-permissive
#   out_dir: kernel build output dir (default: <kernel_dir>/out)
set -euo pipefail

KERNEL_DIR="${1:?usage: apply-variant.sh <kernel_dir> <variant> [out_dir]}"
VARIANT="${2:?usage: apply-variant.sh <kernel_dir> <variant> [out_dir]}"
OUT_DIR="${3:-$KERNEL_DIR/out}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_FRAG="$SCRIPT_DIR/../configs/base.fragment"
VARIANT_FRAG="$SCRIPT_DIR/../configs/$VARIANT.config"

[ -f "$KERNEL_DIR/scripts/kconfig/merge_config.sh" ] || {
    echo "ERROR: merge_config.sh not found in $KERNEL_DIR" >&2; exit 1;
}
[ -f "$VARIANT_FRAG" ] || { echo "ERROR: unknown variant '$VARIANT'" >&2; exit 1; }
[ -f "$OUT_DIR/.config" ] || { echo "ERROR: no .config in $OUT_DIR - run defconfig first" >&2; exit 1; }

cd "$KERNEL_DIR"

# -O puts the merged result back into $OUT_DIR/.config (default would be $PWD/.config)
scripts/kconfig/merge_config.sh -m -O "$OUT_DIR" "$OUT_DIR/.config" "$BASE_FRAG" "$VARIANT_FRAG"
yes "" | make ARCH=arm64 O="$OUT_DIR" olddefconfig

echo "==> Variant '$VARIANT' applied:"
grep -E 'CONFIG_(CMDLINE=|CMDLINE_EXTEND|SECURITY_SELINUX_DEVELOP|KPROBES=)' "$OUT_DIR/.config" || true
echo "==> Governors enabled: $(grep -cE '^CONFIG_CPU_FREQ_GOV_.*=y' "$OUT_DIR/.config")"

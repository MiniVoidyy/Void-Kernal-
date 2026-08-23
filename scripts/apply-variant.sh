#!/usr/bin/env bash
# Applies a Void Kernel variant configuration (ROM x SELinux mode) on top of a
# freshly generated device defconfig.
#
# Usage: apply-variant.sh <kernel_dir> <variant> [out_dir] [device_fragment]
#   variant: aosp-enforcing|aosp-permissive|oneui-enforcing|oneui-permissive
#   out_dir: kernel build output dir (default: <kernel_dir>/out)
#   device_fragment: optional device-specific defconfig fragment
set -euo pipefail

KERNEL_DIR="${1:?usage: apply-variant.sh <kernel_dir> <variant> [out_dir]}"
VARIANT="${2:?usage: apply-variant.sh <kernel_dir> <variant> [out_dir]}"
OUT_DIR="${3:-$KERNEL_DIR/out}"
DEVICE_FRAG="${4:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_FRAG="$SCRIPT_DIR/../configs/base.fragment"
VARIANT_FRAG="$SCRIPT_DIR/../configs/$VARIANT.config"

[ -f "$KERNEL_DIR/scripts/kconfig/merge_config.sh" ] || {
    echo "ERROR: merge_config.sh not found in $KERNEL_DIR" >&2; exit 1;
}
[ -f "$VARIANT_FRAG" ] || { echo "ERROR: unknown variant '$VARIANT'" >&2; exit 1; }
[ -f "$OUT_DIR/.config" ] || { echo "ERROR: no .config in $OUT_DIR - run defconfig first" >&2; exit 1; }
[ -z "$DEVICE_FRAG" ] || [ -f "$DEVICE_FRAG" ] || {
    echo "ERROR: device fragment '$DEVICE_FRAG' not found" >&2; exit 1;
}

cd "$KERNEL_DIR"

# -O puts the merged result back into $OUT_DIR/.config (default would be $PWD/.config)
FRAGMENTS=("$OUT_DIR/.config")
[ -z "$DEVICE_FRAG" ] || FRAGMENTS+=("$DEVICE_FRAG")
FRAGMENTS+=("$BASE_FRAG" "$VARIANT_FRAG")
scripts/kconfig/merge_config.sh -m -O "$OUT_DIR" "${FRAGMENTS[@]}"
# olddefconfig resolves new symbols non-interactively (no 'yes' needed;
# piping yes into it dies with SIGPIPE under pipefail)
make ARCH=arm64 O="$OUT_DIR" olddefconfig

required_configs=(
    CONFIG_ARM64
    CONFIG_CPU_FREQ
    CONFIG_KPROBES
    CONFIG_KSU
    CONFIG_CPU_FREQ_GOV_PERFORMANCE
    CONFIG_CPU_FREQ_GOV_POWERSAVE
    CONFIG_CPU_FREQ_GOV_USERSPACE
    CONFIG_CPU_FREQ_GOV_ONDEMAND
    CONFIG_CPU_FREQ_GOV_CONSERVATIVE
    CONFIG_CPU_FREQ_GOV_INTERACTIVE
    CONFIG_CPU_FREQ_GOV_SCHEDUTIL
    CONFIG_CPU_FREQ_GOV_DARKNESS
    CONFIG_CPU_FREQ_GOV_NIGHTMARE
    CONFIG_CPU_FREQ_GOV_INTELLIDEMAND
    CONFIG_CPU_FREQ_GOV_ALUCARD
    CONFIG_CPU_FREQ_GOV_BLU_ACTIVE
    CONFIG_CPU_FREQ_GOV_INTELLIACTIVE
    CONFIG_CPU_FREQ_GOV_YANKACTIVE
    CONFIG_CPU_FREQ_GOV_LIONFISH
    CONFIG_CPU_FREQ_GOV_IMPULSE
    CONFIG_CPU_FREQ_GOV_WAVE
    CONFIG_CPU_FREQ_GOV_SMARTASS2
    CONFIG_CPU_FREQ_GOV_SMARTMAX
    CONFIG_CPU_FREQ_GOV_SMARTMAX_EPS
)
for config in "${required_configs[@]}"; do
    grep -qx "${config}=y" "$OUT_DIR/.config" || {
        echo "ERROR: required kernel option ${config}=y was not resolved" >&2
        exit 1
    }
done

echo "==> Variant '$VARIANT' applied:"
grep -E 'CONFIG_(CMDLINE=|CMDLINE_EXTEND|SECURITY_SELINUX_DEVELOP|KPROBES=)' "$OUT_DIR/.config" || true
echo "==> Governors enabled: $(grep -cE '^CONFIG_CPU_FREQ_GOV_(PERFORMANCE|POWERSAVE|USERSPACE|ONDEMAND|CONSERVATIVE|INTERACTIVE|SCHEDUTIL|DARKNESS|NIGHTMARE|INTELLIDEMAND|ALUCARD|BLU_ACTIVE|INTELLIACTIVE|YANKACTIVE|LIONFISH|IMPULSE|WAVE|SMARTASS2|SMARTMAX|SMARTMAX_EPS)=y$' "$OUT_DIR/.config")"

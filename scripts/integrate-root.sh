#!/usr/bin/env bash
# Integrates a root solution into an exynos9810 kernel tree.
# Usage: integrate-root.sh <ksu|ksu-next|sukisu> [kernel_dir]
#
#   ksu       -> tiann/KernelSU          into drivers/kernelsu
#   ksu-next  -> rifsxd/KernelSU-Next    into drivers/kernelsu_next
#   sukisu    -> SukiSU-Ultra/SukiSU     into drivers/sukisu
#
# All three expose the same CONFIG_KSU tristate from their own Kconfig,
# and hook syscalls via kprobes on non-GKI kernels (CONFIG_KPROBES=y is
# set by configs/base.fragment). Exactly one root solution is integrated
# per build.
set -euo pipefail

ROOT="${1:?usage: integrate-root.sh <ksu|ksu-next|sukisu> [kernel_dir]}"
KERNEL_DIR="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/kernel}"

DRIVERS_DIR="$KERNEL_DIR/drivers"
DRIVERS_MAKEFILE="$DRIVERS_DIR/Makefile"
DRIVERS_KCONFIG="$DRIVERS_DIR/Kconfig"

case "$ROOT" in
    ksu)
        REPO="https://github.com/tiann/KernelSU.git"
        DIR="kernelsu"
        ;;
    ksu-next)
        REPO="https://github.com/rifsxd/KernelSU-Next.git"
        DIR="kernelsu_next"
        ;;
    sukisu)
        REPO="https://github.com/SukiSU-Ultra/SukiSU-Ultra.git"
        DIR="sukisu"
        ;;
    *)
        echo "ERROR: unknown root solution '$ROOT' (expected ksu|ksu-next|sukisu)" >&2
        exit 1
        ;;
esac

KSU_REF="${KSU_REF:-}"

echo "==> Cloning $REPO -> drivers/$DIR"

need_clone=0
if [ ! -d "$DRIVERS_DIR/$DIR/.git" ]; then
    need_clone=1
elif [ -n "$KSU_REF" ]; then
    cur_ref="$(git -C "$DRIVERS_DIR/$DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")"
    [ "$cur_ref" != "$KSU_REF" ] && need_clone=1
fi

if [ "$need_clone" = 1 ]; then
    rm -rf "$DRIVERS_DIR/$DIR"
    if ! git clone --depth=1 ${KSU_REF:+--branch "$KSU_REF"} "$REPO" "$DRIVERS_DIR/$DIR"; then
        git clone --depth=1 "$REPO" "$DRIVERS_DIR/$DIR"
    fi
fi

if ! grep -Eq "^obj-.CONFIG_KSU.[[:space:]]*\+= $DIR/" "$DRIVERS_MAKEFILE"; then
    printf 'obj-$(CONFIG_KSU)\t+= %s/\n' "$DIR" >> "$DRIVERS_MAKEFILE"
fi

if ! grep -q "source \"$DIR/Kconfig\"" "$DRIVERS_KCONFIG"; then
    printf '\nsource "%s/Kconfig"\n' "$DIR" >> "$DRIVERS_KCONFIG"
fi

echo "==> Root solution '$ROOT' wired into drivers/{Makefile,Kconfig}"

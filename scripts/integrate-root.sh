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
        DEFAULT_REF="v1.0.5"
        ;;
    ksu-next)
        REPO="https://github.com/rifsxd/KernelSU-Next.git"
        DIR="kernelsu_next"
        DEFAULT_REF="v3.2.0-legacy"
        ;;
    sukisu)
        REPO="https://github.com/SukiSU-Ultra/SukiSU-Ultra.git"
        DIR="sukisu"
        DEFAULT_REF="v2.0_beta"
        ;;
    *)
        echo "ERROR: unknown root solution '$ROOT' (expected ksu|ksu-next|sukisu)" >&2
        exit 1
        ;;
esac

KSU_REF="${KSU_REF:-$DEFAULT_REF}"

echo "==> Cloning $REPO@$KSU_REF -> drivers/$DIR"

need_clone=0
if [ ! -d "$DRIVERS_DIR/$DIR/.git" ]; then
    need_clone=1
elif [ -n "$KSU_REF" ]; then
    cur_ref="$(git -C "$DRIVERS_DIR/$DIR" describe --tags --exact-match 2>/dev/null || git -C "$DRIVERS_DIR/$DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")"
    [ "$cur_ref" != "$KSU_REF" ] && need_clone=1
fi

if [ "$need_clone" = 1 ]; then
    rm -rf "$DRIVERS_DIR/$DIR"
    git clone --depth=1 --branch "$KSU_REF" "$REPO" "$DRIVERS_DIR/$DIR"
fi

# The root-solution repos ship the kbuild module inside a kernel/ subdir;
# wire kbuild to whichever layout this checkout actually provides.
REL_DIR="$DIR"
if [ ! -f "$DRIVERS_DIR/$DIR/Kconfig" ] && [ -f "$DRIVERS_DIR/$DIR/kernel/Kconfig" ]; then
    REL_DIR="$DIR/kernel"
fi
if [ ! -f "$DRIVERS_DIR/$REL_DIR/Kconfig" ]; then
    echo "ERROR: no Kconfig found under $DRIVERS_DIR/$DIR" >&2
    exit 1
fi

# Purge ALL stale/broken root-solution wiring (the kernel tree itself ships
# an old 'kernelsu' source line), then append correct lines.
sed -i '\#^[[:space:]]*obj-.*CONFIG_KSU.*[[:space:]]+=[[:space:]]*\(kernelsu\|kernelsu_next\|sukisu\)/#d' "$DRIVERS_MAKEFILE"
printf 'obj-$(CONFIG_KSU)\t+= %s/\n' "$REL_DIR" >> "$DRIVERS_MAKEFILE"

sed -i '\#source ".*/\(kernelsu\|kernelsu_next\|sukisu\)/#d' "$DRIVERS_KCONFIG"
printf '\nsource "drivers/%s/Kconfig"\n' "$REL_DIR" >> "$DRIVERS_KCONFIG"

root_make_entries="$(grep -Ec '^[[:space:]]*obj-.*CONFIG_KSU.*(kernelsu|kernelsu_next|sukisu)/' "$DRIVERS_MAKEFILE" || true)"
root_kconfig_entries="$(grep -Ec 'source ".*/(kernelsu|kernelsu_next|sukisu)/' "$DRIVERS_KCONFIG" || true)"
if [ "$root_make_entries" -ne 1 ] || [ "$root_kconfig_entries" -ne 1 ]; then
    echo "ERROR: expected one root-solution Makefile and Kconfig entry" >&2
    exit 1
fi

echo "==> Root solution '$ROOT' wired into drivers/{Makefile,Kconfig}"

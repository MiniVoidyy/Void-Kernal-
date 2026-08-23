#!/usr/bin/env bash
# Integrates a root solution into an exynos9810 kernel tree.
# Usage: integrate-root.sh <ksu|ksu-next|sukisu> [kernel_dir]
#
#   ksu       -> tiann/KernelSU          into drivers/kernelsu
#   ksu-next  -> rifsxd/KernelSU-Next    into drivers/kernelsu_next
#   sukisu    -> SukiSU-Ultra/SukiSU     into drivers/sukisu
#
# All three expose the same CONFIG_KSU tristate from their own Kconfig,
# and hook syscalls via kprobes/manual hooks on non-GKI kernels
# (CONFIG_KPROBES=y is set by configs/base.fragment). Each root is pinned
# to a release ref known to build against 4.9-era kernels; override with
# the KSU_REF env var. Exactly one root solution is integrated per build.
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
        DEFAULT_REF="v0.9.5"            # last pure-kprobes release; newer refs need GKI-era syscall headers
        ;;
    ksu-next)
        REPO="https://github.com/KernelSU-Next/KernelSU-Next.git"
        DIR="kernelsu_next"
        DEFAULT_REF="v3.2.0-legacy"     # -legacy branch targets pre-GKI (4.x) kernels
        ;;
    sukisu)
        REPO="https://github.com/SukiSU-Ultra/SukiSU-Ultra.git"
        DIR="sukisu"
        DEFAULT_REF="v2.0_beta"         # newest tag still using kprobes hooks; v3+ syscall-table
                                        # patching needs 4.17+ pt_regs-thunk tables, not 4.9 arm64
        ;;
    *)
        echo "ERROR: unknown root solution '$ROOT' (expected ksu|ksu-next|sukisu)" >&2
        exit 1
        ;;
esac

KSU_REF="${KSU_REF:-$DEFAULT_REF}"

echo "==> Cloning $REPO -> drivers/$DIR"

need_clone=0
if [ ! -d "$DRIVERS_DIR/$DIR/.git" ]; then
    need_clone=1
elif [ -n "$KSU_REF" ]; then
    # Tags yield detached HEADs, so compare commits rather than branch names.
    cur_commit="$(git -C "$DRIVERS_DIR/$DIR" rev-parse HEAD 2>/dev/null || echo "")"
    ref_commit="$(git -C "$DRIVERS_DIR/$DIR" rev-parse -q --verify "${KSU_REF}^{commit}" 2>/dev/null || echo "")"
    if [ -z "$ref_commit" ] || [ "$cur_commit" != "$ref_commit" ]; then
        need_clone=1
    fi
fi

if [ "$need_clone" = 1 ]; then
    rm -rf "$DRIVERS_DIR/$DIR"
    if ! git clone --depth=1 ${KSU_REF:+--branch "$KSU_REF"} "$REPO" "$DRIVERS_DIR/$DIR"; then
        git clone --depth=1 "$REPO" "$DRIVERS_DIR/$DIR"
    fi
fi

# Upstream bug in KernelSU-Next's <4.12 kvmalloc/kvfree shim: its kvfree
# replacement drops the const qualifier the real kvfree has, so const-qualified
# callers fail under Samsung's -Werror on 4.9. Cast explicitly instead.
COMPAT_H="$DRIVERS_DIR/$DIR/kernel/compat/kernel_compat.h"
if [ -f "$COMPAT_H" ]; then
    sed -i \
        -e 's/static inline void ksu_kvfree(void \*buf)/static inline void ksu_kvfree(const void *buf)/' \
        -e 's/vfree(buf);/vfree((void *)buf);/' \
        -e 's/kfree(buf);/kfree((void *)buf);/' \
        "$COMPAT_H"
fi

# 4.9 has no include/uapi/linux/sched/types.h (pre-uapi-split); the kernel-side
# <linux/sched/types.h> included just above already provides those declarations.
RULES_C="$DRIVERS_DIR/$DIR/kernel/selinux/rules.c"
if [ -f "$RULES_C" ]; then
    sed -i '\|^#include <uapi/linux/sched/types.h>|d' "$RULES_C"
fi

# Tree-level: the stock tree ships manual KernelSU hooks guarded ONLY by
# '#ifdef CONFIG_KSU' in kernel/{sys,reboot}.c, but roots integrated here run
# kprobes-only and never define ksu_handle_setresuid/ksu_handle_sys_reboot ->
# undefined symbols at link. Align with the tree's own pattern used in fs/*.c
# ('defined(CONFIG_KSU) && !defined(CONFIG_KPROBES)'), which compiles them out.
for f in "$KERNEL_DIR/kernel/sys.c" "$KERNEL_DIR/kernel/reboot.c"; do
    [ -f "$f" ] && sed -i \
        's|^#ifdef CONFIG_KSU$|#if defined(CONFIG_KSU) \&\& !defined(CONFIG_KPROBES)|' "$f"
done

# SukiSU-Ultra (<=v2.0_beta) uses MODULE_IMPORT_NS(), a >=5.x module macro;
# leave it undefined on 4.9 rather than inventing a stub.
SSU_KSU_C="$DRIVERS_DIR/$DIR/kernel/ksu.c"
[ -f "$SSU_KSU_C" ] && sed -i '\|^MODULE_IMPORT_NS(|d' "$SSU_KSU_C"

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
# NOTE: 'obj-$(CONFIG_KSU)' - the $() is two chars, hence ERE with escapes.
sed -Ei '\#^[[:space:]]*obj-\$\(CONFIG_KSU\)[[:space:]]*\+=[[:space:]]*(kernelsu|kernelsu_next|sukisu)#d' "$DRIVERS_MAKEFILE"
printf 'obj-$(CONFIG_KSU)\t+= %s/\n' "$REL_DIR" >> "$DRIVERS_MAKEFILE"

sed -i '\#source ".*/\(kernelsu\|kernelsu_next\|sukisu\)/#d' "$DRIVERS_KCONFIG"
printf '\nsource "drivers/%s/Kconfig"\n' "$REL_DIR" >> "$DRIVERS_KCONFIG"

# Physically remove unused root-solution dirs (the stock kernelsu/ dir has
# no kbuild Makefile and would break the build if anything still points at it)
for d in kernelsu kernelsu_next sukisu; do
    [ "$d" = "$DIR" ] || rm -rf "$DRIVERS_DIR/$d"
done

echo "==> Root solution '$ROOT' wired into drivers/{Makefile,Kconfig}"

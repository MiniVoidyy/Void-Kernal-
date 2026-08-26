#!/usr/bin/env bash
# Integrates a root solution into an exynos9810 kernel tree.
# Usage: integrate-root.sh <ksu|ksu-next|sukisu> [kernel_dir]
#
#   ksu       -> tiann/KernelSU          into drivers/kernelsu
#   ksu-next  -> KernelSU-Next           into drivers/kernelsu_next
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
        DEFAULT_REF="v2.0_beta"         # newest tag still using kprobes hooks
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
    # Tags yield detached HEADs, so compare commits rather than branch names.
    cur_commit="$(git -C "$DRIVERS_DIR/$DIR" rev-parse HEAD 2>/dev/null || echo "")"
    ref_commit="$(git -C "$DRIVERS_DIR/$DIR" rev-parse -q --verify "${KSU_REF}^{commit}" 2>/dev/null || echo "")"
    if [ -z "$ref_commit" ] || [ "$cur_commit" != "$ref_commit" ]; then
        need_clone=1
    fi
fi

if [ "$need_clone" = 1 ]; then
    rm -rf "$DRIVERS_DIR/$DIR"
    git clone --depth=1 --branch "$KSU_REF" "$REPO" "$DRIVERS_DIR/$DIR"
fi

# SukiSU's first release kept the newer SELinux policydb implementation.
# Use KernelSU's API-compatible 4.9 implementation; both expose the same
# sepolicy.h interface, so SukiSU's rules and features remain intact.
if [ "$ROOT" = "sukisu" ]; then
    for policy_file in sepolicy.c rules.c; do
        curl --fail --location --retry 3 \
            --output "$DRIVERS_DIR/$DIR/kernel/selinux/$policy_file" \
            "https://raw.githubusercontent.com/tiann/KernelSU/b766b98513b5a7eb33bc1c4a76b5702bf1288f07/kernel/selinux/$policy_file"
    done
fi

# These releases support Linux 4.9, but retain declarations and include paths
# from newer kernels. Normalize them to the headers available in this tree.
while IFS= read -r source_file; do
    sed -i \
        -e '/^[[:space:]]*MODULE_IMPORT_NS/d' \
        -e '/^#include <linux\/compiler_types\.h>$/d' \
        -e 's#<linux/input-event-codes.h>#<uapi/linux/input-event-codes.h>#' \
        -e 's#<linux/limits.h>#<uapi/linux/limits.h>#' \
        -e 's#<linux/sched/task.h>#<linux/sched.h>#' \
        -e 's#<linux/sched/task_stack.h>#<linux/sched.h>#' \
        -e 's#<uapi/linux/sched/types.h>#<uapi/linux/sched.h>#' \
        -e 's/security_add_hooks(ksu_hooks, ARRAY_SIZE(ksu_hooks), "ksu");/security_add_hooks(ksu_hooks, ARRAY_SIZE(ksu_hooks));/' \
        -e 's/selinux_state\.enforcing/selinux_enforcing/g' \
        -e 's/selinux_state\.disabled/selinux_disabled/g' \
        "$source_file"
done < <(find "$DRIVERS_DIR/$DIR/kernel" -type f \( -name '*.c' -o -name '*.h' \))

KSU_COMPAT_SOURCE="$DRIVERS_DIR/$DIR/kernel/compat/kernel_compat.c"
if [ -f "$KSU_COMPAT_SOURCE" ]; then
    sed -i 's/kvfree(p);/kvfree((void *)p);/' "$KSU_COMPAT_SOURCE"
fi

# The base tree's manual calls match KernelSU Next, but KernelSU and SukiSU do
# not provide those symbols and instead use their own kprobe/LSM hooks.
if [ "$ROOT" != "ksu-next" ]; then
    for hook_source in \
        "$KERNEL_DIR/kernel/sys.c" \
        "$KERNEL_DIR/kernel/reboot.c" \
        "$KERNEL_DIR/drivers/tty/pty.c"; do
        if [ -f "$hook_source" ]; then
            sed -i '/^#ifdef CONFIG_KSU$/,/^#endif$/d' "$hook_source"
        fi
    done
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

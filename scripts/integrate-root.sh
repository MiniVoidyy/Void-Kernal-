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

# Rewrite post-4.9 header includes to their 4.9 equivalents:
#   linux/compiler_types.h      (5.3+)  -> linux/compiler.h
#   linux/sched/{task,task_stack}.h (4.11+) -> linux/sched.h
#   crypto/sha2.h               (5.8+)  -> crypto/sha.h
grep -rlE 'include <(linux/compiler_types\.h|linux/sched/(task_stack|task)\.h|crypto/sha2\.h)>' \
    "$DRIVERS_DIR/$DIR/kernel" 2>/dev/null |
    xargs -r sed -i \
        -e 's|#include <linux/compiler_types\.h>|#include <linux/compiler.h>|' \
        -e 's|#include <linux/sched/task_stack\.h>|#include <linux/sched.h>|' \
        -e 's|#include <linux/sched/task\.h>|#include <linux/sched.h>|' \
        -e 's|#include <crypto/sha2\.h>|#include <crypto/sha.h>|'

# security_add_hooks() grew its 'lsm name' parameter in 5.1; 4.9 takes
# (hooks, count). SukiSU passes the name unconditionally.
grep -rl 'security_add_hooks' "$DRIVERS_DIR/$DIR/kernel" 2>/dev/null |
    xargs -r sed -i 's|security_add_hooks(\(.*\), ARRAY_SIZE(\1), "[^"]*")|security_add_hooks(\1, ARRAY_SIZE(\1))|'

# --- SukiSU selinux layer dropped tiann's pre-4.19 guards; restore them. ---
# avc.h exports 'extern int selinux_enforcing' when SELINUX_DEVELOP=y; the
# global 'struct policydb policydb' replaces selinux_state.policy before 4.19;
# the pre-6.4 AVC reset must not touch selinux_state either.
SSU_SELINUX_C="$DRIVERS_DIR/$DIR/kernel/selinux/selinux.c"
if [ -f "$SSU_SELINUX_C" ]; then
    sed -i 's|#include "objsec.h"|#include "objsec.h"\n#include "avc.h"|' "$SSU_SELINUX_C"
    sed -i \
        -e 's|selinux_state\.enforcing = enforce;|selinux_enforcing = enforce;|' \
        -e 's|if (selinux_state\.disabled) {|if (selinux_disabled) {|' \
        -e 's|return selinux_state\.enforcing;|return selinux_enforcing;|' \
        "$SSU_SELINUX_C"
fi

SSU_RULES_C="$DRIVERS_DIR/$DIR/kernel/selinux/rules.c"
if [ -f "$SSU_RULES_C" ]; then
    # ensure avc_ss_reset() prototype is visible on the pre-4.19 path
    sed -i 's|#include "ss/services.h"|#include "ss/services.h"\n#include "avc.h"|' "$SSU_RULES_C"
    # get_policydb(): selinux_state.policy is 4.19+/5.10+; use the global policydb before that
    perl -0777 -pi -e 's/static struct policydb \*get_policydb\(void\)\s*\{[^}]*?\}/static struct policydb *get_policydb(void)\n{\n#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)\n\tstruct selinux_policy *policy = rcu_dereference(selinux_state.policy);\n\treturn &policy->policydb;\n#else\n\treturn &policydb;\n#endif\n}/s' "$SSU_RULES_C"
    # reset_avc_cache(): same story for selinux_state.avc / status_update_policyload
    perl -0777 -pi -e 's/\tstruct selinux_avc \*avc = selinux_state\.avc;\n\tavc_ss_reset\(avc, 0\);\n\tselnl_notify_policyload\(0\);\n\tselinux_status_update_policyload\(&selinux_state, 0\);/\tavc_ss_reset(0);\n\tselnl_notify_policyload(0);\n\tselinux_status_update_policyload(0);/s' "$SSU_RULES_C"
fi

# --- SukiSU sepolicy.c: filename_trans/add_type use post-4.x-only APIs ---
# Wrap them in version guards and splice in pre-4.20/pre-5.1 implementations
# (filename_trans via plain struct + hashtab; add_type via flex_array),
# mirroring what KernelSU-Next's -legacy branch ships for old kernels.
SSU_SEPOLICY_C="$DRIVERS_DIR/$DIR/kernel/selinux/sepolicy.c"
if [ -f "$SSU_SEPOLICY_C" ] && [ "$DIR" = "sukisu" ]; then
    PATCH_TMP="$(mktemp -d)"
    sed -i 's/\r$//' "$SSU_SEPOLICY_C"

    cat > "$PATCH_TMP/fnt.old.c" <<'FNT_EOF'
static bool add_filename_trans(struct policydb *db, const char *s,
			       const char *t, const char *c, const char *d,
			       const char *o)
{
	struct type_datum *src, *tgt, *def;
	struct class_datum *cls;

	src = symtab_search(&db->p_types, s);
	if (src == NULL) {
		pr_warn("source type %s does not exist\n", s);
		return false;
	}
	tgt = symtab_search(&db->p_types, t);
	if (tgt == NULL) {
		pr_warn("target type %s does not exist\n", t);
		return false;
	}
	cls = symtab_search(&db->p_classes, c);
	if (cls == NULL) {
		pr_warn("class %s does not exist\n", c);
		return false;
	}
	def = symtab_search(&db->p_types, d);
	if (def == NULL) {
		pr_warn("default type %s does not exist\n", d);
		return false;
	}

	struct filename_trans key;
	key.ttype = tgt->value;
	key.tclass = cls->value;
	key.name = (char *)o;

	struct filename_trans_datum *trans = hashtab_search(db->filename_trans, &key);

	if (trans == NULL) {
		trans = (struct filename_trans_datum *)kcalloc(1, sizeof(*trans),
							       GFP_ATOMIC);
		if (!trans) {
			pr_err("add_filename_trans: Failed to alloc datum\n");
			return false;
		}
		struct filename_trans *new_key =
			(struct filename_trans *)kzalloc(sizeof(*new_key),
							 GFP_ATOMIC);
		if (!new_key) {
			pr_err("add_filename_trans: Failed to alloc new_key\n");
			return false;
		}
		*new_key = key;
		new_key->name = kstrdup(key.name, GFP_ATOMIC);
		trans->otype = def->value;
		hashtab_insert(db->filename_trans, new_key, trans);
	}

	return ebitmap_set_bit(&db->filename_trans_ttypes, src->value - 1, 1) == 0;
}
FNT_EOF

    cat > "$PATCH_TMP/at.flex.tail.c" <<'AT_EOF'
// flex_array is not extensible; create new bigger arrays instead
struct flex_array *new_type_attr_map_array =
	flex_array_alloc(sizeof(struct ebitmap), db->p_types.nprim,
			 GFP_KERNEL | __GFP_ZERO);
struct flex_array *new_type_val_to_struct =
	flex_array_alloc(sizeof(struct type_datum *), db->p_types.nprim,
			 GFP_KERNEL | __GFP_ZERO);
struct flex_array *new_val_to_name_types =
	flex_array_alloc(sizeof(char *), db->symtab[SYM_TYPES].nprim,
			 GFP_KERNEL | __GFP_ZERO);

if (!new_type_attr_map_array || !new_type_val_to_struct ||
    !new_val_to_name_types) {
	pr_err("add_type: flex_array_alloc failed\n");
	return false;
}

if (flex_array_prealloc(new_type_attr_map_array, 0, db->p_types.nprim,
			GFP_KERNEL | __GFP_ZERO)) {
	pr_err("add_type: prealloc type_attr_map_array failed\n");
	return false;
}
if (flex_array_prealloc(new_type_val_to_struct, 0, db->p_types.nprim,
			GFP_KERNEL | __GFP_ZERO)) {
	pr_err("add_type: prealloc type_val_to_struct failed\n");
	return false;
}
if (flex_array_prealloc(new_val_to_name_types, 0,
			db->symtab[SYM_TYPES].nprim,
			GFP_KERNEL | __GFP_ZERO)) {
	pr_err("add_type: prealloc val_to_name failed\n");
	return false;
}

int j;
void *old_elem;
for (j = 0; j < db->type_attr_map_array->total_nr_elements; j++) {
	old_elem = flex_array_get(db->type_attr_map_array, j);
	if (old_elem)
		flex_array_put(new_type_attr_map_array, j, old_elem,
			       GFP_KERNEL | __GFP_ZERO);
}
for (j = 0; j < db->type_val_to_struct_array->total_nr_elements; j++) {
	old_elem = flex_array_get_ptr(db->type_val_to_struct_array, j);
	if (old_elem)
		flex_array_put_ptr(new_type_val_to_struct, j, old_elem,
				   GFP_KERNEL | __GFP_ZERO);
}
for (j = 0; j < db->symtab[SYM_TYPES].nprim; j++) {
	old_elem =
		flex_array_get_ptr(db->sym_val_to_name[SYM_TYPES], j);
	if (old_elem)
		flex_array_put_ptr(new_val_to_name_types, j, old_elem,
				   GFP_KERNEL | __GFP_ZERO);
}

struct flex_array *old_fa;

old_fa = db->type_attr_map_array;
db->type_attr_map_array = new_type_attr_map_array;
if (old_fa)
	flex_array_free(old_fa);

ebitmap_init(flex_array_get(db->type_attr_map_array, value - 1));
ebitmap_set_bit(flex_array_get(db->type_attr_map_array, value - 1),
		value - 1, 1);

old_fa = db->type_val_to_struct_array;
db->type_val_to_struct_array = new_type_val_to_struct;
if (old_fa)
	flex_array_free(old_fa);
flex_array_put_ptr(db->type_val_to_struct_array, value - 1, type,
		   GFP_KERNEL | __GFP_ZERO);

old_fa = db->sym_val_to_name[SYM_TYPES];
db->sym_val_to_name[SYM_TYPES] = new_val_to_name_types;
if (old_fa)
	flex_array_free(old_fa);
flex_array_put_ptr(db->sym_val_to_name[SYM_TYPES], value - 1, key,
		   GFP_KERNEL | __GFP_ZERO);

int i;
for (i = 0; i < db->p_roles.nprim; ++i) {
	ebitmap_set_bit(&db->role_val_to_struct[i]->types, value - 1,
			1);
}

return true;
AT_EOF

    export FNT_OLD="$PATCH_TMP/fnt.old.c"
    export AT_FLEX="$PATCH_TMP/at.flex.tail.c"

    # wrap add_filename_trans definition (skip forward decls; validate body sig).
    # Tolerant of tab/space indentation. If the layout is unrecognized
    # (e.g. an already version-guarded upstream), skip instead of failing.
    perl -0777 -pi -e 'BEGIN{ local $/; open my $f,"<",$ENV{FNT_OLD} or die; $o=<$f>; close $f; }
      my $sig = "static bool add_filename_trans";
      my $pos = 0; my ($sp, $ep);
      while (($pos = index($_, $sig, $pos)) >= 0) {
          my $brace = index($_, "{", $pos);
          last if $brace < 0;
          my $semi = index($_, ";", $pos);
          if ($semi >= 0 && $semi < $brace) { $pos += length($sig); next; }
          my $bodychk = substr($_, $brace, 64);
          if ($bodychk =~ /\{\n[ \t]*struct type_datum \*src,[ \t]*\*tgt,[ \t]*\*def;/) {
              $sp = $pos;
              my $k = index($_, "\n}\n", $brace);
              $ep = $k + 3 if $k >= 0;
              last;
          }
          $pos += length($sig);
      }
      if (defined $sp) {
          my $fn = substr($_, $sp, $ep - $sp);
          substr($_, $sp, $ep - $sp) =
            "#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)\n" . $fn .
            "#else\n" . $o . "#endif\n";
      }' "$SSU_SEPOLICY_C"

    # wrap add_type 5.x-style tail; splice flex_array tail below 5.1
    # (keeps ONE function envelope: guards live inside the braces)
    perl -0777 -pi -e 'BEGIN{ local $/; open my $f,"<",$ENV{AT_FLEX} or die; $o=<$f>; close $f; }
      my $marker = "\tstruct ebitmap *new_type_attr_map_array =";
      my $i = index($_, $marker);
      die "add_type tail marker not found" if $i < 0;
      my $closer = "\n\treturn true;\n}";
      my $k = index($_, $closer, $i);
      die "add_type tail end not found" if $k < 0;
      my $tailend = $k + length($closer) - 1;   # exclude the closing brace
      my $tail = substr($_, $i, $tailend - $i);
      substr($_, $i, $tailend - $i) =
        "#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)\n" . $tail .
        "#else\n" . $o . "\n#endif\n";' "$SSU_SEPOLICY_C"

    rm -rf "$PATCH_TMP"
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

#!/system/bin/sh
SKIPUNZIP=0

ui_print "- Void Kernel Manager v1.1 by Mini_Voidyy"
ui_print "- Kernel: Void Kernel (exynos9810, 20 governors)"

mkdir -p /data/adb/void_kernel_manager
if [ ! -f /data/adb/void_kernel_manager/profile.conf ]; then
    cat > /data/adb/void_kernel_manager/profile.conf <<'EOF'
# Void Kernel Manager saved profile (applied at boot)
# Leave values empty to skip that setting.
LITTLE_GOVERNOR=
LITTLE_MIN=
LITTLE_MAX=
BIG_GOVERNOR=
BIG_MIN=
BIG_MAX=
EOF
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/system/bin/voidman" 0 0 0755

ui_print "- CLI installed as 'voidman' (run: su -c voidman help)"
ui_print "- Saved profile is applied automatically at boot."

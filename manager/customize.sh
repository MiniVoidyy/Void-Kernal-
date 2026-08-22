#!/system/bin/sh
SKIPUNZIP=0

ui_print "- Void Kernel Manager v1.0 by Mini_Voidyy"

mkdir -p /data/adb/void_kernel_manager
if [ ! -f /data/adb/void_kernel_manager/profile.conf ]; then
    cat > /data/adb/void_kernel_manager/profile.conf <<'EOF'
# Void Kernel Manager saved profile (applied at boot)
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
ui_print "- Profile is applied automatically at boot."

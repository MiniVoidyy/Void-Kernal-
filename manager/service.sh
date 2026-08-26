#!/system/bin/sh
# Void Kernel Manager - apply saved profile at boot (late_start service)
CONF=/data/adb/void_kernel_manager/profile.conf

apply_profile() {
    [ -f "$CONF" ] || return 0

    . "$CONF"

    LITTLE_POLICY=""
    BIG_POLICY=""
    BIG_MAX=0

    for cf in /sys/devices/system/cpu/cpu*/cpufreq; do
        [ -d "$cf" ] || continue
        P=$(readlink -f "$cf")
        case " $SEEN " in *" $P "*) continue ;; esac
        SEEN="$SEEN $P"
        MAXF=$(cat "$P/cpuinfo_max_freq" 2>/dev/null || echo 0)
        if [ -z "$LITTLE_POLICY" ]; then
            LITTLE_POLICY="$P"
        fi
        if [ "$MAXF" -gt "$BIG_MAX" ] 2>/dev/null; then
            BIG_MAX=$MAXF
            BIG_POLICY="$P"
        fi
    done

    [ -n "$BIG_POLICY" ] && [ "$BIG_POLICY" = "$LITTLE_POLICY" ] && BIG_POLICY=""

    for role in LITTLE BIG; do
        eval POLICY="\$${role}_POLICY"
        [ -n "$POLICY" ] || continue
        [ -d "$POLICY" ] || continue
        eval G="\$${role}_GOVERNOR"
        eval MN="\$${role}_MIN"
        eval MX="\$${role}_MAX"
        if [ -n "$G" ] && [ -e "$POLICY/scaling_governor" ]; then
            echo "$G" > "$POLICY/scaling_governor" 2>/dev/null
        fi
        if [ -n "$MN" ]; then
            echo "$MN" > "$POLICY/scaling_min_freq" 2>/dev/null
        fi
        if [ -n "$MX" ]; then
            echo "$MX" > "$POLICY/scaling_max_freq" 2>/dev/null
        fi
    done
}

{
    until [ "$(getprop sys.boot_completed)" = "1" ] && [ -d /sys/devices/system/cpu/cpu0/cpufreq ]; do
        sleep 5
    done
    sleep 30
    apply_profile
} &

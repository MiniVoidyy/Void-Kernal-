#!/usr/bin/env bash
# Installs the Void 20-governor pack into an exynos9810 kernel tree.
# Copies the 13 custom governor sources into drivers/cpufreq and wires
# them into drivers/cpufreq/{Makefile,Kconfig}. Idempotent.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="${1:-$SCRIPT_DIR/../kernel}"
GOVERNOR_SRC="$SCRIPT_DIR/../governors"

CPUFREQ_DIR="$KERNEL_DIR/drivers/cpufreq"
MAKEFILE="$CPUFREQ_DIR/Makefile"
KCONFIG="$CPUFREQ_DIR/Kconfig"

[ -f "$KERNEL_DIR/Makefile" ] || { echo "ERROR: $KERNEL_DIR is not a kernel tree" >&2; exit 1; }
[ -d "$CPUFREQ_DIR" ] || { echo "ERROR: $CPUFREQ_DIR not found" >&2; exit 1; }

GOVS="darkness nightmare intellidemand alucard blu_active intelliactive yankactive lionfish impulse wave smartass2 smartmax smartmax_eps"

for g in $GOVS; do
    [ -f "$GOVERNOR_SRC/cpufreq_$g.c" ] || { echo "ERROR: missing $GOVERNOR_SRC/cpufreq_$g.c" >&2; exit 1; }
    cp -f "$GOVERNOR_SRC/cpufreq_$g.c" "$CPUFREQ_DIR/"
done

for g in $GOVS; do
    if ! grep -q "cpufreq_$g\.o" "$MAKEFILE"; then
        sym=$(echo "$g" | tr '[:lower:]' '[:upper:]')
        printf 'obj-$(CONFIG_CPU_FREQ_GOV_%s)\t+= cpufreq_%s.o\n' "$sym" "$g" >> "$MAKEFILE"
    fi
done

if ! grep -q "CPU_FREQ_GOV_DARKNESS" "$KCONFIG"; then
    printf '\n' >> "$KCONFIG"
    cat "$GOVERNOR_SRC/Kconfig.void" >> "$KCONFIG"
fi

echo "Void governor pack installed: +13 custom (+7 stock = 20 total)"

#!/bin/sh
warn=0
say() { echo "$1"; }
bad() { echo "WARN: $1"; warn=1; }
os=$(uname -s)
say "os:        $(uname -sr)"
if [ "$os" = "Darwin" ]; then
    say "cpu:       $(sysctl -n machdep.cpu.brand_string)"
    say "cores:     $(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null) performance, $(sysctl -n hw.perflevel1.physicalcpu 2>/dev/null) efficiency"
    say "load:      $(sysctl -n vm.loadavg)"
    batt=$(pmset -g batt 2>/dev/null | head -1)
    say "power:     $batt"
    case "$batt" in *"Battery Power"*) bad "running on battery, frequency may be capped";; esac
    lpm=$(pmset -g 2>/dev/null | awk '/lowpowermode/ {print $2}')
    say "low power: ${lpm:-unknown}"
    [ "$lpm" = "1" ] && bad "low power mode is on"
    therm=$(pmset -g therm 2>/dev/null | tr '\n' ' ')
    say "thermal:   $therm"
    limit=$(echo "$therm" | sed -n 's/.*CPU_Speed_Limit[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p')
    if [ -n "$limit" ] && [ "$limit" -lt 100 ]; then bad "cpu speed limit is $limit, thermal or power throttling active"; fi
    say "malloc:    MallocNanoZone=${MallocNanoZone:-default}"
    say "isolation: not available on macOS, no core pinning, no fixed frequency, no isolcpus"
    load1=$(sysctl -n vm.loadavg | awk '{print $2}')
    awk -v l="$load1" 'BEGIN{exit !(l > 4.0)}' && bad "load average $load1 is high, other processes will compete"
else
    say "cpu:       $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
    gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
    say "governor:  ${gov:-unknown}"
    [ "$gov" = "performance" ] || bad "cpu governor is not performance"
    turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null)
    say "no_turbo:  ${turbo:-unknown}"
    [ "$turbo" = "1" ] || bad "turbo boost is not disabled"
    iso=$(cat /sys/devices/system/cpu/isolated 2>/dev/null)
    say "isolated:  ${iso:-none}"
    [ -n "$iso" ] || bad "no isolated cpus, use isolcpus or accept variance"
    aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)
    say "aslr:      ${aslr:-unknown}"
    grep -q -E 'constant_tsc' /proc/cpuinfo && grep -q -E 'nonstop_tsc' /proc/cpuinfo || bad "tsc is not invariant"
    say "load:      $(cut -d' ' -f1-3 /proc/loadavg)"
fi
if [ "$warn" = "1" ]; then
    echo "environment is not clean for benchmarking, results may exceed the spread gate"
    exit 1
fi
echo "environment ok"

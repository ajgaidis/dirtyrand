#!/bin/bash -e

# Pin BELOW the thermal ceiling, not at the 3.0GHz max non-turbo clock.
# 3.0GHz is the LEAST thermally-sustainable steady state on this chassis: a
# long all-core sweep heat-soaks the package past TjMax and the hardware
# throttles underneath the governor, regardless of the pin. A lower clock the
# chassis can hold for the whole sweep trades absolute speed (which we don't
# measure) for a steady clock (which is what makes the defense-vs-baseline
# ratio meaningful). Tune down further if run-pts.sh still reports throttling.
SUSTAIN_FREQ="2.0GHz"

main()
{
    #
    # NOTE: if possible, disable some of these options in the BIOS
    #

    # (1) ensure Intel Turbo-Boost is disabled
    if [[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 0 ]]; then
        echo "[!] Intel Turbo Boost is enabled."
        echo " |----> disabling..."
        echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
    fi
    echo "[+] Intel Turbo Boost is disabled."

    # (2) ensure SMT is enabled (per VPK's request)
    if [[ $(cat /sys/devices/system/cpu/smt/active) == 0 ]]; then
        echo "[!] SMT is disabled."
    else
        echo "[+] SMT is enabled."
    fi

    # (3) ensure Intel Speed Step is disabled in bios
    echo "[?] Ensure Intel Speed Step is disabled in BIOS."

    # (4) ensure max cstate is 0
    if [[ $(cat /sys/module/intel_idle/parameters/max_cstate) != 0 ]]; then
        echo "[WARNING] Max cstate is not 0."
        # This guy is set on the grub command line:
        #   processor.max_cstate=0 intel_idle.max_cstate=0
    else
        echo "[+] Max cstate is 0."
    fi

    # (5) disable ASLR
    #echo 0 | sudo tee /proc/sys/kernel/randomize_va_space > /dev/null
    #echo "[+] ASLR is disabled."

    # (6) set cpu frequency (pin min==max so it can't ramp)
    echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null
    sudo cpupower frequency-set -d "$SUSTAIN_FREQ" -u "$SUSTAIN_FREQ" > /dev/null
    echo "[+] Pinned CPU frequency to $SUSTAIN_FREQ."

    # (7) verify the clock actually HOLDS under load -- an idle reading proves
    #     nothing; the drift only appears once every core is busy.
    echo "[?] Verifying pinned frequency under all-core load..."
    for _ in $(seq "$(nproc)"); do timeout 8 bash -c 'while :; do :; done' & done
    sleep 5
    cur_mhz=$(( $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq) / 1000 ))
    wait
    want_mhz=$(awk -v f="$SUSTAIN_FREQ" 'BEGIN { sub(/GHz/,"",f); printf "%d", f*1000 }')
    if [[ $cur_mhz -lt $(( want_mhz - 100 )) ]]; then
        echo "[!] Under load the clock fell to ${cur_mhz}MHz (wanted ~${want_mhz}MHz)."
        echo " |----> the pin is NOT holding -- thermal/RAPL throttling. Lower SUSTAIN_FREQ."
    else
        echo "[+] Under-load frequency holds at ${cur_mhz}MHz (~${want_mhz}MHz target)."
    fi
}
main


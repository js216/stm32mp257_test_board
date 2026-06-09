#!/bin/sh
# smc_audit.sh - prove which secure-world (SMC) services this Linux image
# still uses on the STM32MP257F-DK lean image: PSCI (TF-A BL31), the OP-TEE
# driver itself, and the SCMI protocols carried over the OP-TEE transport.
# Read-only except: mounts debugfs if absent, and bounces cpu1
# offline/online once to prove live PSCI CPU_OFF/CPU_ON calls.

echo "=== SMC_AUDIT_BEGIN ==="

echo "--- 1. DTB secure-world declarations ---"
for p in /proc/device-tree/firmware/*; do
	n=$(basename "$p")
	c=$(tr '\0' ' ' < "$p/compatible" 2>/dev/null)
	m=$(tr '\0' ' ' < "$p/method" 2>/dev/null)
	echo "firmware/$n: compatible=$c method=$m"
done
echo "psci: compatible=$(tr '\0' ' ' < /proc/device-tree/psci/compatible 2>/dev/null) method=$(tr '\0' ' ' < /proc/device-tree/psci/method 2>/dev/null)"
echo "scmi protocol nodes declared: $(ls /proc/device-tree/firmware/scmi 2>/dev/null | grep protocol | tr '\n' ' ')"

echo "--- 2. PSCI runtime (TF-A BL31, not OP-TEE) ---"
dmesg | grep -i psci
echo "cpuidle driver: $(cat /sys/devices/system/cpu/cpuidle/current_driver 2>/dev/null)"
for s in /sys/devices/system/cpu/cpu0/cpuidle/state*; do
	[ -d "$s" ] || continue
	echo "idle $(basename "$s"): name=$(cat "$s"/name) desc=$(cat "$s"/desc) usage=$(cat "$s"/usage)"
done
echo "cpu1 hotplug bounce (live PSCI CPU_OFF/CPU_ON):"
echo 0 > /sys/devices/system/cpu/cpu1/online 2>/dev/null
sleep 1
off=$(cat /sys/devices/system/cpu/cpu1/online 2>/dev/null)
echo 1 > /sys/devices/system/cpu/cpu1/online 2>/dev/null
sleep 1
on=$(cat /sys/devices/system/cpu/cpu1/online 2>/dev/null)
echo "cpu1 off=$off back_on=$on"
if [ "$off" = "0" ] && [ "$on" = "1" ]; then HOTPLUG=OK; else HOTPLUG=FAIL; fi

echo "--- 3. OP-TEE runtime ---"
dmesg | grep -iE 'optee|smccc'
ls -l /dev/tee* 2>/dev/null
command -v tee-supplicant > /dev/null && echo "tee-supplicant: installed"
grep -i optee /proc/interrupts

echo "--- 4. SCMI runtime (transport = OP-TEE msg channel 0) ---"
dmesg | grep -i scmi
for d in /sys/bus/scmi_protocol/devices/*; do
	[ -e "$d" ] || continue
	echo "scmi dev $(basename "$d"): $(cat "$d"/modalias 2>/dev/null)"
done

echo "--- 5. regulators (SCMI voltd 0x17 -> OP-TEE -> STPMIC2 on I2C7) ---"
NSCMI=0
for r in /sys/class/regulator/regulator.*; do
	[ -d "$r" ] || continue
	link=$(readlink -f "$r" 2>/dev/null)
	case "$link" in *scmi*) src=scmi; NSCMI=$((NSCMI + 1));; *) src=other;; esac
	echo "$(basename "$r"): src=$src name=$(cat "$r"/name 2>/dev/null) state=$(cat "$r"/state 2>/dev/null) uV=$(cat "$r"/microvolts 2>/dev/null) users=$(cat "$r"/num_users 2>/dev/null)"
done

echo "--- 6. clocks (debugfs) ---"
mount -t debugfs none /sys/kernel/debug 2>/dev/null
if [ -r /sys/kernel/debug/clk/clk_summary ]; then
	SCMI_CLKS=$(grep -ci scmi /sys/kernel/debug/clk/clk_summary)
	echo "clk_summary scmi-named clocks: $SCMI_CLKS"
	grep -i scmi /sys/kernel/debug/clk/clk_summary | head -20
else
	SCMI_CLKS=na
	echo "debugfs clk_summary unavailable"
fi
if [ -r /sys/kernel/debug/regulator/regulator_summary ]; then
	echo "--- regulator_summary (consumers) ---"
	cat /sys/kernel/debug/regulator/regulator_summary
fi

echo "--- 7. watchdog + cpufreq (migrated off the secure world?) ---"
for w in /sys/class/watchdog/watchdog*; do
	[ -d "$w" ] || continue
	echo "$(basename "$w"): identity=$(cat "$w"/identity 2>/dev/null)"
done
echo "cpufreq scaling_driver: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null || echo none)"

echo "--- SUMMARY ---"
echo "PSCI_HOTPLUG=$HOTPLUG"
if [ -e /dev/tee0 ]; then echo "OPTEE_DEV=present"; else echo "OPTEE_DEV=absent"; fi
echo "SCMI_PROTO_DEVS=$(ls /sys/bus/scmi_protocol/devices 2>/dev/null | wc -l)"
echo "SCMI_REGULATORS=$NSCMI"
echo "SCMI_CLOCKS=$SCMI_CLKS"
echo "=== SMC_AUDIT_END ==="
exit 0

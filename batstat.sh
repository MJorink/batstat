#!/bin/sh

for bat in BAT0 BAT1; do
	dir="/sys/class/power_supply/$bat"
	[ -d "$dir" ] || continue

	start=$(cat "$dir/charge_start_threshold" 2>/dev/null)
	stop=$(cat "$dir/charge_stop_threshold" 2>/dev/null)

	echo
	echo "$bat:"
	awk -v start="${start:-0}" -v stop="${stop:-0}" -F= '
		/POWER_SUPPLY_CAPACITY=/          { capacity = $2 }
		/POWER_SUPPLY_ENERGY_FULL=/       { full = $2 }
		/POWER_SUPPLY_ENERGY_FULL_DESIGN=/{ design = $2 }
		/POWER_SUPPLY_CYCLE_COUNT=/       { cycles = $2 }
		END {
			o = "\033[0;33m"; r = "\033[0m"
			loss = design - full
			degradation = (design > 0) ? (loss * 100 / design) : 0
			printf o "POWER_SUPPLY_CAPACITY"           r "=%.0f%%\n",                        capacity+0
			printf o "POWER_SUPPLY_ENERGY_FULL_DESIGN" r "=%.2f Wh\n",                       design/1000000
			printf o "POWER_SUPPLY_ENERGY_FULL"        r "=%.2f Wh\n",                       full/1000000
			printf o "POWER_SUPPLY_ENERGY_LOSS"        r "=%.2f Wh (%.0f%% degradation)\n",  loss/1000000, degradation
			printf o "POWER_SUPPLY_CYCLE_COUNT"        r "=%d\n",                            cycles+0
			printf o "CHARGE_START_THRESHOLD"          r "=%d\n",                            start+0
			printf o "CHARGE_STOP_THRESHOLD"           r "=%d\n",                            stop+0
		}' "$dir/uevent"
	echo
done

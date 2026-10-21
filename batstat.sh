#!/bin/sh

usage() {
	echo "usage: ${0##*/} [-w [seconds]]"
	echo "  -w [seconds]  keep updating the stats (default interval: 2s)"
	exit "${1:-0}"
}

watch=
interval=2
while [ $# -gt 0 ]; do
	case $1 in
	-w|--watch)
		watch=1
		case $2 in
		''|*[!0-9.]*) ;;
		*) interval=$2; shift ;;
		esac
		;;
	-h|--help) usage ;;
	*) usage 1 >&2 ;;
	esac
	shift
done

show() {
	found=
	for dir in /sys/class/power_supply/*; do
		[ "$(cat "$dir/type" 2>/dev/null)" = Battery ] || continue
		found=1
		bat=${dir##*/}

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
	[ -n "$found" ] || echo "no batteries found" >&2
}

if [ -z "$watch" ]; then
	show
	exit
fi

trap 'exit 0' INT TERM
while :; do
	out=$(show 2>&1)
	printf '\033[H\033[2J%s\n' "$out"
	sleep "$interval"
done

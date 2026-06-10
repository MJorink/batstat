#!/bin/sh

get_values() {
        # Check if the battery directory actually exists before trying to read it
        if [ ! -d "/sys/class/power_supply/$bat" ]; then
                return 1
        fi

        capacity=$(grep 'POWER_SUPPLY_CAPACITY=' /sys/class/power_supply/$bat/uevent | cut -d= -f2)
        full=$(grep 'POWER_SUPPLY_ENERGY_FULL=' /sys/class/power_supply/$bat/uevent | cut -d= -f2)
        design=$(grep 'POWER_SUPPLY_ENERGY_FULL_DESIGN=' /sys/class/power_supply/$bat/uevent | cut -d= -f2)
        cycles=$(grep 'POWER_SUPPLY_CYCLE_COUNT=' /sys/class/power_supply/$bat/uevent | cut -d= -f2)
        
        # Fallback to 0 if variables are empty to prevent awk math errors
        : "${capacity:=0}"
        : "${full:=0}"
        : "${design:=0}"
        : "${cycles:=0}"
        
        loss=$(( design - full ))
        return 0
}

print_values () {
        awk -v capacity="$capacity" -v full="$full" -v design="$design" -v loss="$loss" -v cycles="$cycles" '
            BEGIN {
                o = "\033[0;33m"
                r = "\033[0m"
                
                # Protect against division by zero if design is 0
                degradation = (design > 0) ? (loss * 100 / design) : 0
                
                # Fixed: Changed % to %% at the end of CAPACITY
                printf o "POWER_SUPPLY_CAPACITY"            r "=%.0f%%\n",                          capacity
                printf o "POWER_SUPPLY_ENERGY_FULL_DESIGN"  r "=%.2f Wh\n",                         design/1000000
                printf o "POWER_SUPPLY_ENERGY_FULL"         r "=%.2f Wh\n",                         full/1000000
                printf o "POWER_SUPPLY_ENERGY_LOSS"         r "=%.2f Wh (%.0f%% degradation)\n",    loss/1000000, degradation
                printf o "POWER_SUPPLY_CYCLE_COUNT"         r "=%d\n",                              cycles
            }'
}

main () {
    echo
    for bat in BAT0 BAT1; do
        # Only print if get_values succeeds (battery exists)
        if get_values; then
                echo "$bat:"
                print_values
                echo
        fi
    done
}

main

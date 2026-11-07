#!/bin/bash
# PicoCalc panel throughput benchmark - Radxa Zero 3W
# Read-only. Measures what actually reaches the screen during gameplay.
# Zeek the Alchemist
#
# Run:  sudo bash ~/picocalc-bench.sh [label]
# Output appends to ~/picocalc-bench.txt

if [ "$EUID" -ne 0 ]; then echo "Run with sudo: sudo bash $0"; exit 1; fi
RUSER="${SUDO_USER:-dewey}"
USERHOME=$(getent passwd "$RUSER" | cut -d: -f6)
OUT="$USERHOME/picocalc-bench.txt"
LABEL="${1:-run}"
SPIDEV=/sys/bus/spi/devices/spi3.0
exec > >(tee -a "$OUT") 2>&1

echo
echo "===== BENCH: $LABEL  ($(date '+%F %T')) ====="
echo "overlays: $(grep user_overlays /boot/armbianEnv.txt)"
dmesg | grep -iE 'fe640000|dma.*spi|spi.*dma' | tail -4
BAD1=$(dmesg | grep -ci 'bad desc')

echo ">>> PLAY now (real gameplay, moving around). Measuring in 30 s for 10 s."
sleep 30
IRQ=$(grep -m1 'fe640000' /proc/interrupts | awk '{print $1}' | tr -d ':')
irqsum() { [ -n "$IRQ" ] && grep "^ *$IRQ:" /proc/interrupts | awk '{s=0; for(i=2;i<=NF;i++) if($i ~ /^[0-9]+$/) s+=$i; print s}'; }
B1=$(cat $SPIDEV/statistics/bytes); T1=$(cat $SPIDEV/statistics/transfers); I1=$(irqsum)
E1=$(cat $SPIDEV/statistics/errors); TO1=$(cat $SPIDEV/statistics/timedout)
top -b -d 10 -n 2 -w 150 > /tmp/pc-top.txt &
TOPPID=$!
sleep 10
B2=$(cat $SPIDEV/statistics/bytes); T2=$(cat $SPIDEV/statistics/transfers); I2=$(irqsum)
E2=$(cat $SPIDEV/statistics/errors); TO2=$(cat $SPIDEV/statistics/timedout)
wait $TOPPID
BAD2=$(dmesg | grep -ci 'bad desc')

BPS=$(( (B2 - B1) / 10 ))
echo "bytes/sec to panel:        $BPS  (99 MHz max is ~12375000)"
echo "full 320x320 frames/sec:   $(awk "BEGIN{printf \"%.1f\", $BPS/204800}")"
echo "transfers/sec:             $(( (T2 - T1) / 10 ))"
[ -n "$I1" ] && echo "SPI interrupts/sec:        $(( (I2 - I1) / 10 ))"
echo "SPI errors / timeouts:     $(( E2 - E1 )) / $(( TO2 - TO1 ))"
echo "new 'Bad Desc' messages:   $(( BAD2 - BAD1 ))  (total since boot: $BAD2)"
awk '/^top -/{n++} n==2' /tmp/pc-top.txt | sed -n '3p'
awk '/^top -/{n++} n==2' /tmp/pc-top.txt | sed -n '7,14p'
echo "last dma/spi kernel lines:"
dmesg | grep -iE 'bad desc|pl330|fe640000|mcbuf' | tail -5
echo "Done. Appended to $OUT"

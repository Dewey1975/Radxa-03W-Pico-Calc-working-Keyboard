#!/bin/bash
# PicoCalc speaker/headphone audio for the Radxa Zero 3W (Armbian vendor kernel).
# 1-bit delta-sigma audio on I2S1 SDO3 = header pin 32.
# Needs the jumper wires from ADDENDUM-audio.md. Run from this folder:
#   bash install.sh          (add --mpv to also install the mpv speaker boost)
# Backs up /boot/armbianEnv.txt as armbianEnv.txt.before-i2s.
set -e
cd "$(dirname "$0")"

echo "== Checking this system before changing anything"
SYM=/proc/device-tree/__symbols__
for s in i2s1_8ch i2s1m2_sdo3; do
	if [ ! -e "$SYM/$s" ]; then
		echo "STOP: this kernel's device tree has no '$s'. Nothing was changed."
		exit 1
	fi
done
echo "   ok"

echo "== Installing build tools (if needed)"
dpkg -s libasound2-dev >/dev/null 2>&1 || sudo apt-get install -y libasound2-dev
command -v dtc >/dev/null || sudo apt-get install -y device-tree-compiler

echo "== Building stand-in codec driver for kernel $(uname -r)"
make clean >/dev/null 2>&1 || true
make
sudo install -Dm644 picocalc_dsm_codec.ko "/lib/modules/$(uname -r)/extra/picocalc_dsm_codec.ko"
sudo depmod -a

echo "== Building the 1-bit converter (ALSA plugin)"
LIBDIR="/usr/lib/$(gcc -print-multiarch)/alsa-lib"
gcc -O2 -Wall -fPIC -DPIC -shared -o libasound_module_pcm_picocalc_dsm.so pcm_picocalc_dsm.c -lasound
sudo install -Dm644 libasound_module_pcm_picocalc_dsm.so "$LIBDIR/libasound_module_pcm_picocalc_dsm.so"
sed "s#@LIBDIR@#$LIBDIR#" alsa.conf.in > 60-picocalc-dsm.conf
sudo install -Dm644 60-picocalc-dsm.conf /etc/alsa/conf.d/60-picocalc-dsm.conf

echo "== PipeWire / WirePlumber config"
install -Dm644 pipewire.conf "$HOME/.config/pipewire/pipewire.conf.d/60-picocalc-dsm.conf"
install -Dm644 wireplumber.conf "$HOME/.config/wireplumber/wireplumber.conf.d/60-picocalc-dsm.conf"

if [ "$1" = "--mpv" ]; then
	echo "== mpv speaker boost (press n to toggle)"
	mkdir -p "$HOME/.config/mpv"
	touch "$HOME/.config/mpv/mpv.conf" "$HOME/.config/mpv/input.conf"
	sed -i "/@dialog/d" "$HOME/.config/mpv/mpv.conf" "$HOME/.config/mpv/input.conf"
	cat mpv/mpv.conf.add >> "$HOME/.config/mpv/mpv.conf"
	cat mpv/input.conf.add >> "$HOME/.config/mpv/input.conf"
fi

echo "== Device tree overlay"
dtc -@ -q -I dts -O dtb -o picocalc-i2s-dsm.dtbo picocalc-i2s-dsm.dts
sudo cp picocalc-i2s-dsm.dtbo /boot/overlay-user/

echo "== Updating /boot/armbianEnv.txt (backup: armbianEnv.txt.before-i2s)"
sudo cp /boot/armbianEnv.txt /boot/armbianEnv.txt.before-i2s
# Anything else driving pin 7/32 (old PWM overlays) comes out; I2S goes in.
sudo sed -i -E '/^user_overlays=/ { s/ rk3568-pwm14-m0//g; s/ rk3568-pwm15-m0//g; s/ picocalc-pwm-audio//g; / picocalc-i2s-dsm( |$)/! s/$/ picocalc-i2s-dsm/ }' /boot/armbianEnv.txt
grep '^user_overlays=' /boot/armbianEnv.txt

echo
echo "All done. Reboot with:  sudo reboot"
echo "Then test:  speaker-test -D picocalc -c 2 -r 48000 -t sine -f 440 -l 1"
echo "To undo the boot change:"
echo "  sudo cp /boot/armbianEnv.txt.before-i2s /boot/armbianEnv.txt && sudo reboot"

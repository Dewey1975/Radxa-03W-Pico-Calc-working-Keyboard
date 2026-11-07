# Addendum: Keep system updates from breaking your PicoCalc

Once the keyboard, audio and screen are working, one wrong click on a system update can undo all of it. This addendum locks the handful of packages that matter, so the rest of the system can still update safely.

Items are labeled **Verified** (seen working on the real hardware) or **Untested**.

## Test system

- Radxa Zero 3W, 2 GB RAM, in a ClockworkPi PicoCalc
- Michael Mayer / `ironat` ZeroCalc adapter
- Armbian, kernel `6.1.115-vendor-rk35xx`

## Why updates are a problem

Two drivers in this build are kernel modules, built for one exact kernel version:

- the patched keyboard driver, `picocalc_kbd.ko` (main README)
- the audio stand-in codec driver, `picocalc_dsm_codec.ko` ([audio addendum](ADDENDUM-audio.md))

If an update installs a new kernel, the PicoCalc boots into it, and neither module loads. You lose the keyboard and the sound until both are rebuilt for the new kernel. The bootloader and device-tree packages are also part of the boot chain that the display, keyboard and audio overlays depend on, so they get locked too.

## Lock the kernel and boot packages

Paste this over SSH:

```
dpkg -l | awk '/^ii/ && $2 ~ /^(linux-(image|dtb|headers|u-boot)|armbian-(bsp|firmware))/ {print $2}' | xargs -r sudo apt-mark hold
apt-mark showhold
```

The first line finds the installed kernel, device-tree, header, bootloader, firmware and Armbian board packages, and locks them. The second line lists what's locked.

**Verified:** on our unit it locked these six:

```
armbian-bsp-cli-radxa-zero3-vendor
armbian-firmware
linux-dtb-vendor-rk35xx
linux-headers-vendor-rk35xx
linux-image-vendor-rk35xx
linux-u-boot-radxa-zero3-vendor
```

Yours should look the same. If the list is empty, or has no `linux-image` line, stop: your image uses different package names. Run `dpkg -l | grep -E 'linux-|armbian-'` to see them.

## After locking

Everything else can update normally, through the desktop's update button or with:

```
sudo apt update && sudo apt full-upgrade
```

`apt` skips the locked packages and lists them as **"kept back"**. That's expected. It means the lock is working.

**Untested:** we locked the packages but have not yet run a full upgrade afterward on our unit. The lock itself is standard `apt` behavior.

## Unlocking

Only when you're ready to rebuild the keyboard and audio modules for a new kernel:

```
sudo apt-mark unhold armbian-bsp-cli-radxa-zero3-vendor armbian-firmware linux-dtb-vendor-rk35xx linux-headers-vendor-rk35xx linux-image-vendor-rk35xx linux-u-boot-radxa-zero3-vendor
```

Then follow the rebuild steps in the main README and the audio addendum for the new `uname -r`. Make a backup image of the SD card first.

— Zeek the Alchemist

# Addendum: Game speed on the 320x320 panel

This addendum follows the [desktop and display addendum](ADDENDUM-desktop-and-display.md) (do its section 10, compositor off, first). It covers what was actually holding games back on the PicoCalc screen, and the three changes that fixed the stutter: running the screen at its full 99 MHz, putting the panel's SPI bus in polling mode, and turning RetroArch's vsync off.

Items are labeled **Verified** (seen working on the real hardware), **Observed** (seen, cause is our best guess) or **Untested**.

## Test system

- Radxa Zero 3W, 2 GB RAM, in a ClockworkPi PicoCalc
- Michael Mayer / `ironat` ZeroCalc adapter
- Armbian, kernel `6.1.115-vendor-rk35xx`, XFCE on X11
- RetroPie with `lr-mgba`. Test games: GBA Doom (one of the heaviest GBA games) and Tomb Raider: Legend
- `spi3-nodma` already in place (desktop and display addendum, section 3)

## The short version

You need `dtc` (`sudo apt install device-tree-compiler` if `dtc --version` says it's missing).

**1.** Put [`screen/spi3-poll.dts`](screen/spi3-poll.dts) in your home folder on the PicoCalc (WinSCP works fine).

**2.** Over SSH, back up your boot config and make the full-speed panel overlay (details in Change 1):

```
sudo cp /boot/armbianEnv.txt /boot/armbianEnv.txt.bak-speed
dtc -q -I dtb -O dts /boot/overlay-user/ili9488-panel-mipi-dbi-spi.dtbo | sed 's/spi-max-frequency = <0x[0-9a-f]*>/spi-max-frequency = <0x5e69ec0>/' > ~/ili9488-fast.dts
sudo dtc -q -I dts -O dtb -o /boot/overlay-user/ili9488-fast.dtbo ~/ili9488-fast.dts
sudo sed -i 's/ili9488-panel-mipi-dbi-spi/ili9488-fast/' /boot/armbianEnv.txt
```

**3.** Add the polling overlay (Change 2) and check the boot line:

```
sudo armbian-add-overlay ~/spi3-poll.dts && grep user_overlays /boot/armbianEnv.txt
```

The line should include `ili9488-fast`, `spi3-nodma` and `spi3-poll`, plus your keyboard (and audio, if you have it) overlays. Then:

```
sudo poweroff
```

Do a full power-on, not a reboot, so the keyboard comes back.

**4.** Once it's back up, check both took:

```
od -An -tu4 --endian=big /proc/device-tree/spi@fe640000/panel@0/spi-max-frequency
dmesg | grep 'fe640000.spi: probed'
```

The first should print `99000000`. The second should say `poll=1`.

**5.** Do the RetroArch settings in Change 3 below.

**Result** (**Verified**): GBA Doom went from stuttery to "damn near okay", and Tomb Raider: Legend, which used to dip to about 28 fps, plays smoothly.

**To undo everything:** `sudo cp /boot/armbianEnv.txt.bak-speed /boot/armbianEnv.txt`, then power off and on.

## What was really slowing games down

A read-only diagnostic during gameplay showed (**Verified**):

- **16-bit color is already on.** The panel's init sequence sets `3a 55` (RGB565, 2 bytes per pixel), and colors look right. Nothing to gain there.
- **The SPI clock only has a few real speeds.** The SPI block runs from a 198 MHz source and divides by even numbers only, so the possible speeds are 99, 49.5, 33 MHz and so on. **Anything you ask for under 99 MHz really runs at 49.5.** The stock panel overlay asks for 70 MHz, so it runs at 49.5. Change 1 fixes that. At 99 MHz and 2 bytes per pixel, the wire can carry about 60 full 320x320 frames a second.
- **Even at 99 MHz, the screen only got about a quarter of that:** 2.99 MB/s during GBA Doom, about 14.6 full frames a second.
- **The real wall was interrupts.** With DMA off, the CPU refills the SPI controller's small buffer one interrupt at a time: **about 23,500 SPI interrupts a second.** The overhead between refills, not the clock, was the limit.

That also explains the bad feel. The on-screen fps counter shows how fast the *emulator* runs. The screen was only updating about half that often, and unevenly, so frames were dropped.

## Why not just turn DMA back on

**Verified:** removing `spi3-nodma` brings back the error flood, and the kernel log says why:

```
dma-pl330 fe530000.dma-controller: fill_queue:2267 Bad Desc(407884)
dma-pl330 fe530000.dma-controller: pl330_submit_req:1726 Try increasing mcbufsz (263/256)
```

The DMA engine builds a small program for each transfer, and it only has room for 256 bytes. The panel driver's big screen transfers need 263 to 279, so they get refused. `mcbufsz` is a fixed setting in the `pl330` DMA driver, not something an overlay can change, so this needs either a kernel change or a driver that splits transfers into smaller pieces. Neither is done yet (see "Not done yet"). Put `spi3-nodma` back afterwards.

## Change 1: run the screen at 99 MHz (`ili9488-fast` overlay)

L0lus's panel overlay, `ili9488-panel-mipi-dbi-spi`, asks for 70 MHz (`spi-max-frequency = <0x42c1d80>`). Because of the even-divider rule above, that lands on 49.5 MHz. The commands in step 2 of the short version make a copy named `ili9488-fast` with that one number changed to 99 MHz (`0x5e69ec0`), and switch the boot line over to the copy. Nothing else in the overlay changes, and L0lus's original file is left alone.

**Verified:** `ili9488-fast` has run on our unit since October with a normal picture and no errors. The commands above produce a file byte-for-byte identical to the one on our unit. We did not benchmark this change on its own, so we have no separate before-and-after number for it.

**To undo just this:** `sudo sed -i 's/ili9488-fast/ili9488-panel-mipi-dbi-spi/' /boot/armbianEnv.txt`, then power off and on.

## Change 2: SPI polling mode (`spi3-poll` overlay)

The Rockchip SPI driver in this vendor kernel has a polling mode (the boot log shows `poll=0` by default). In polling mode one CPU core keeps the SPI buffer topped up continuously instead of taking an interrupt every refill. It needs no DMA. The overlay is in [`screen/spi3-poll.dts`](screen/spi3-poll.dts); it only adds `rockchip,poll-only` to the panel's SPI bus.

Install steps are in the short version above. Keep `spi3-nodma`: the two work together.

`user_overlays` on our unit afterwards:

```
user_overlays=ili9488-fast picocalc-kbd-i2c3 spi3-nodma picocalc-i2s-dsm spi3-poll
```

(`picocalc-i2s-dsm` is the [audio](ADDENDUM-audio.md) overlay; leave it out if you haven't done audio.)

**Measured during GBA Doom** (**Verified**, with `screen/picocalc-bench.sh`):

| | Before (interrupts) | Polling |
|---|---|---|
| Data to screen | 2.99 MB/s | **3.94 MB/s** (+32%) |
| SPI interrupts | 23,492 a second | **0** |
| SPI errors / timeouts | 0 / 0 | 0 / 0 |

GBA's picture is 240x160, scaled to about 320x213 on the panel, so a frame is about 136 KB. 3.94 MB/s works out to roughly 29 frames a second actually reaching the screen, which matches what the emulator was producing. The fps counter barely moved (about 38), but the screen now keeps up with the game, and that's where the night-and-day difference in feel comes from.

**Cost** (**Observed**): Xorg sits around 69% of one core, mostly feeding the SPI. The RK3566 has four cores and plenty sat idle.

**To undo:**

```
sudo sed -i 's/ spi3-poll//' /boot/armbianEnv.txt && grep user_overlays /boot/armbianEnv.txt
```

Then power off and on.

## Change 3: RetroArch threaded video on, vsync off

This panel has no tear-sync (TE) signal, so vsync on only adds waiting. Threaded video lets the game keep its own speed. On our unit `video_vsync` had ended up back on `"true"`, and `video_threaded` was listed twice, so these commands clear out any old copies of both lines and write them once. Run as your normal user, **not** with `sudo`, so the file stays owned by you:

```
cp /opt/retropie/configs/all/retroarch.cfg ~/retroarch-all.cfg.bak-poll
sed -i '/^video_vsync =/d; /^video_threaded =/d' /opt/retropie/configs/all/retroarch.cfg
printf 'video_threaded = "true"\nvideo_vsync = "false"\n' >> /opt/retropie/configs/all/retroarch.cfg
grep -E '^video_(vsync|threaded)' /opt/retropie/configs/all/retroarch.cfg
```

It should show `video_threaded = "true"` and `video_vsync = "false"`, once each.

**To undo:** `cp ~/retroarch-all.cfg.bak-poll /opt/retropie/configs/all/retroarch.cfg`

**Verified:** with both changes, Tomb Raider: Legend gameplay is smooth, including the busy scenes that used to dip to about 28 fps.

## Measuring your own unit

[`screen/picocalc-bench.sh`](screen/picocalc-bench.sh) is read-only. Copy it to your home folder, get into real gameplay, then over SSH:

```
sudo bash ~/picocalc-bench.sh my-label
```

It waits 30 seconds (go play), measures for 10, and appends to `~/picocalc-bench.txt`: bytes per second to the panel, SPI interrupts, errors, new `Bad Desc` messages, and the top CPU users. Run it before and after a change and compare.

## Not done yet (Untested)

- **RetroArch is drawing in software.** Its `gl` driver runs on `llvmpipe` (OpenGL on the CPU), costing about 65% of a core. Getting it onto the GPU should free the most CPU.
- **DMA with smaller transfers.** Splitting each screen transfer so the DMA program stays under 256 bytes would free the core that polling keeps busy.
- Estimate only: together these might add another 30–50% in heavy GBA scenes. GBA's ceiling is 60 fps, and at some point the emulator itself becomes the limit on this chip.

## Credits

- L0lus, for the Radxa Zero 3 PicoCalc port and display work
- Michael Mayer / `ironat`, for the ZeroCalc adapter PCB
- ClockworkPi, for the PicoCalc hardware
- Radxa, for the Zero 3W platform

— Zeek the Alchemist

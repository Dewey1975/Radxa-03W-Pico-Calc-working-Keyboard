# Addendum: Game speed on the 320x320 panel (SPI polling mode)

This addendum follows section 10 of the [desktop and display addendum](ADDENDUM-desktop-and-display.md). It covers what was actually holding games back on the PicoCalc screen, and the two changes that fixed the stutter: putting the panel's SPI bus in polling mode, and turning RetroArch's vsync off.

Items are labeled **Verified** (seen working on the real hardware), **Observed** (seen, cause is our best guess) or **Untested**.

## Test system

- Radxa Zero 3W, 2 GB RAM, in a ClockworkPi PicoCalc
- Michael Mayer / `ironat` ZeroCalc adapter
- Armbian, kernel `6.1.115-vendor-rk35xx`, XFCE on X11
- RetroPie with `lr-mgba`. Test games: GBA Doom (one of the heaviest GBA games) and Tomb Raider: Legend
- `spi3-nodma` already in place (desktop and display addendum, section 3)

## The short version

Put [`screen/spi3-poll.dts`](screen/spi3-poll.dts) in your home folder on the PicoCalc (WinSCP works fine), then over SSH:

```
sudo cp /boot/armbianEnv.txt /boot/armbianEnv.txt.bak-nodma
sudo armbian-add-overlay ~/spi3-poll.dts && grep user_overlays /boot/armbianEnv.txt
sudo poweroff                                   # then a full power-on, so the keyboard comes back
```

After it's back up:

```
dmesg | grep 'fe640000.spi: probed'
```

It should say `poll=1`. Then set RetroArch's vsync off (see "Change 2" below).

**Result** (**Verified**): GBA Doom went from stuttery to "damn near okay", and Tomb Raider: Legend, which used to dip to about 28 fps, plays smoothly.

## What the old theory got wrong

Section 10 of the display addendum guessed that the panel takes 3 bytes per pixel and the clock was the ceiling. A read-only diagnostic during gameplay showed otherwise (**Verified**):

- **16-bit color is already on.** The panel's init sequence sets `3a 55` (RGB565, 2 bytes per pixel), and colors look right. Nothing to gain there.
- **The SPI clock is already maxed at 99 MHz.** The SPI block runs from a 198 MHz source and divides by even numbers only, so the possible speeds are 99, 49.5, 33 MHz and so on. **Anything you ask for under 99 MHz really runs at 49.5.** On our unit the panel overlay requests 99 MHz, and that's what it gets. At 99 MHz and 2 bytes per pixel, the wire can carry about 60 full 320x320 frames a second.
- **The screen only got about a quarter of that:** 2.99 MB/s during GBA Doom, about 14.6 full frames a second.
- **The real wall was interrupts.** With DMA off, the CPU refills the SPI controller's small buffer one interrupt at a time: **about 23,500 SPI interrupts a second.** The overhead between refills, not the clock, was the limit.

That also explains the bad feel. The on-screen fps counter shows how fast the *emulator* runs. The screen was only updating about half that often, and unevenly, so frames were dropped.

## Why not just turn DMA back on

**Verified:** removing `spi3-nodma` brings back the error flood, and the kernel log says why:

```
dma-pl330 fe530000.dma-controller: fill_queue:2267 Bad Desc(407884)
dma-pl330 fe530000.dma-controller: pl330_submit_req:1726 Try increasing mcbufsz (263/256)
```

The DMA engine builds a small program for each transfer, and it only has room for 256 bytes. The panel driver's big screen transfers need 263 to 279, so they get refused. `mcbufsz` is a fixed setting in the `pl330` DMA driver, not something an overlay can change, so this needs either a kernel change or a driver that splits transfers into smaller pieces. Neither is done yet (see "Not done yet"). Put `spi3-nodma` back afterwards.

## Change 1: SPI polling mode (`spi3-poll` overlay)

The Rockchip SPI driver in this vendor kernel has a polling mode (the boot log shows `poll=0` by default). In polling mode one CPU core keeps the SPI buffer topped up continuously instead of taking an interrupt every refill. It needs no DMA. The overlay is in [`screen/spi3-poll.dts`](screen/spi3-poll.dts); it only adds `rockchip,poll-only` to the panel's SPI bus.

Install steps are in "The short version" above. Keep `spi3-nodma`: the two work together.

`user_overlays` on our unit afterwards:

```
user_overlays=ili9488-fast picocalc-kbd-i2c3 spi3-nodma picocalc-i2s-dsm spi3-poll
```

(`ili9488-fast` is the name of our panel overlay, which requests 99 MHz. Keep whatever panel, keyboard and audio overlays your build uses and just add `spi3-poll` to the end.)

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

## Change 2: RetroArch vsync off

This panel has no tear-sync (TE) signal, so vsync on only adds waiting. On our unit `video_vsync` had ended up back on `"true"`, and `video_threaded` was listed twice. Run as your normal user, **not** with `sudo`, so the file stays owned by you:

```
cp /opt/retropie/configs/all/retroarch.cfg ~/retroarch-all.cfg.bak-poll
sed -i 's/^video_vsync = .*/video_vsync = "false"/' /opt/retropie/configs/all/retroarch.cfg
awk '!(/^video_threaded =/ && seen++)' /opt/retropie/configs/all/retroarch.cfg > /tmp/ra.cfg && cat /tmp/ra.cfg > /opt/retropie/configs/all/retroarch.cfg
grep -E '^video_(vsync|threaded|driver)' /opt/retropie/configs/all/retroarch.cfg
```

It should show `video_driver = "gl"`, `video_threaded = "true"` once, and `video_vsync = "false"`.

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

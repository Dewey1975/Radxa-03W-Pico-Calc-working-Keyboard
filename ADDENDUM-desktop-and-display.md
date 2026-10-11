# Addendum: Getting a usable desktop on the 320x320 PicoCalc panel (Radxa Zero 3W, Armbian)

This addendum picks up where the display and keyboard guide leaves off. By this point the panel shows a picture and the keyboard types. This page covers what happened next: the desktop crashed, the kernel log filled with DMA errors, a lighter desktop showed a black screen, and what finally worked.

Everything below is labeled **Verified** (we saw it work or fail on the real hardware), **Observed** (we saw it happen, but the cause is our best guess), or **Untested**. Please treat the "Observed" items as leads, not facts.

## Test system

- Radxa Zero 3W, 2 GB RAM, inside a ClockworkPi PicoCalc
- Armbian GNOME desktop image (the apt sources point at the Ubuntu `resolute` base)
- Display: `ili9488-panel-mipi-dbi-spi` overlay (L0lus's repo)
- Keyboard: `picocalc-kbd-i2c3` overlay plus the patched driver from the keyboard repo
- Starting `/boot/armbianEnv.txt` overlay line:
  ```
  user_overlays=ili9488-panel-mipi-dbi-spi picocalc-kbd-i2c3
  ```
- Final overlay line after this addendum:
  ```
  user_overlays=ili9488-panel-mipi-dbi-spi picocalc-kbd-i2c3 spi3-nodma
  ```

## Quick summary

| Problem | Result |
|---|---|
| GNOME crashes back to the login screen when you open the apps grid | **Verified**, reproducible. Not fixed. Likely a GNOME-vs-tiny-screen layout problem (**Observed**). |
| Kernel log flooded with `dma-pl330 ... Bad Desc` | **Verified** workaround: disable DMA on the panel's SPI bus (`spi3-nodma` overlay). Did **not** fix the GNOME crash. |
| i3 starts but the panel stays black | **Verified** fix: an Xorg config file pointing X at the panel device. |
| i3 is hard to use without memorizing keys | Subjective. Works, but minimal. |
| XFCE | **Verified** working on the panel with the Xorg config. |
| The Raspberry Pi "set 640x640 and let it shrink" trick | **Verified** it does not work with this setup (section 8). |
| Making the XFCE interface smaller | **Verified** workaround: lower the DPI (section 8). A wider canvas for apps is still unsolved. |
| GBA games (RetroArch/RetroPie) slow and not filling the screen | **Verified** partial fix: turn off the XFCE compositor, threaded video, frame skip (section 10). Panel speed limit remains. |

## 1. Check what the system sees first

These only read information and are safe to run:

```
ls /dev/fb* /dev/dri/
cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/drm/*/modes
cat /boot/armbianEnv.txt
```

On our machine the framebuffer reported `320,320`, and the panel's DRM connector offered a `320x320` mode. So the kernel side was already correct. If yours reports the same, the problem is in the desktop, not in the resolution settings.

The panel appears as its own DRM device. In the GNOME log it showed up as `/dev/dri/card1` (`panel-mipi-dbi`), with the Radxa's main video hardware as `/dev/dri/card0` (`rockchip`). Card numbers can change, so check yours before copying any config:

```
ls -l /dev/dri/by-path
sudo journalctl -b --no-pager | grep -i "panel-mipi-dbi"
```

## 2. GNOME on Wayland: crash when opening the apps grid

**Symptom:** click the six-dot apps button, the screen freezes for a while, then you land on the login screen.

**What the log showed** (**Verified**): `gnome-shell` printed repeated assertion failures from `clutter_actor_allocate` complaining about values that were "not a number" (`isnan`), plus many "needs an allocation" messages for `AppDisplay` and icon widgets, right before the shell shut down. There were **no** out-of-memory or segfault lines, so low RAM was not the cause on our 2 GB board.

**Our best guess** (**Observed**, not proven): GNOME's apps grid does its layout math against the available screen space, and 320x320 is far smaller than anything GNOME expects. We could not confirm this beyond the log evidence.

**Things that did not help:**

- Changing the text scale (`gsettings set org.gnome.desktop.interface text-scaling-factor 0.6`) made text smaller at first, but the crash still happened. We reset it afterward with `gsettings reset ...`, which returns it to the default. Note that "reset" may be bigger than whatever you had before, so check the value first with `gsettings get` if you care.
- GNOME on Wayland does not offer scaling below 100%, so the "set it to 640x640 and let the system shrink it" trick does not carry over from Raspberry Pi OS.
- Adding `WaylandEnable=false` to `/etc/gdm3/custom.conf` did nothing, because this image ships **only** a Wayland session (`/usr/share/xsessions` did not exist). That line may or may not be needed now that X11 sessions are installed. We did not test removing it.

**Practical advice:** if GNOME works for you and you simply avoid the apps grid, you can stay on it. Otherwise move to a lighter desktop (sections 4 and 5).

## 3. The `dma-pl330` error flood and the `spi3-nodma` workaround

**Symptom** (**Verified**): `dmesg` filled with lines like:

```
dma-pl330 fe530000.dma-controller: fill_queue:2267 Bad Desc(41568)
dma-pl330 fe530000.dma-controller: pl330_submit_req:1726 Try increasing mcbufsz (263/256)
```

**Which device causes it** (**Verified**): this showed the panel's SPI bus owns those DMA channels:

```
sudo cat /sys/kernel/debug/dmaengine/summary
```

```
dma0 (fe530000.dma-controller): number of channels: 32
 dma0chan26   | fe640000.spi:tx
 dma0chan27   | fe640000.spi:rx
```

`fe640000.spi` is `spi3`, the bus the panel uses.

**Workaround** (**Verified** that it removes the errors): add a tiny separate overlay that stops the SPI driver from finding its DMA channels, so it falls back to CPU transfers. This does not edit L0lus's overlay, the `picolcd.bin` file, or any setup script.

Create `spi3-nodma.dts`:

```
/dts-v1/;
/plugin/;

&spi3 {
    dma-names = "disabled-tx", "disabled-rx";
};
```

Install it and back up the boot settings first:

```
sudo cp /boot/armbianEnv.txt ~/armbianEnv.txt.bak
sudo armbian-add-overlay ~/spi3-nodma.dts
cat /boot/armbianEnv.txt
sudo reboot
```

After the reboot:

```
sudo dmesg | grep -c "Bad Desc"
```

We got `0`, and the SPI channels no longer appeared in the DMA summary.

**Important:** this did **not** stop the GNOME apps-grid crash, so those were two separate problems. We did not measure whether CPU-driven SPI is slower in practice (**Untested**).

**To undo:** remove `spi3-nodma` from the `user_overlays=` line in `/boot/armbianEnv.txt`, then reboot.

## 4. i3: black screen on X11, and the fix

Install:

```
sudo apt install -y i3 xserver-xorg xterm
```

i3 appears as a session option on the login screen (gear icon).

**Symptom** (**Verified**): log in to i3 and the panel stays black.

**What was going on:**

- `xrandr` (run over SSH with `DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority`) showed the panel as an output named `Unknown19-1`, connected, offering `320x320`, while X's canvas started at 1024x768 with the output not active.
- Turning the output on by hand made `xrandr` report `320x320+0+0` with a `*` next to the mode, but the panel stayed black.
- An `xterm` was running and i3 was managing windows, so X had a picture; it just wasn't reaching the panel.
- `xrandr --listproviders` showed two GPUs. Linking them with `xrandr --setprovideroutputsource 1 0` also did not help.

**What fixed it** (**Verified**): tell Xorg to use the panel device directly with a software frame buffer. Create `/etc/X11/xorg.conf.d/20-picocalc.conf`:

```
Section "Device"
    Identifier "PicoCalcPanel"
    Driver "modesetting"
    Option "kmsdev" "/dev/dri/card1"
    Option "ShadowFB" "true"
    Option "AccelMethod" "none"
EndSection
```

Replace `card1` with your panel's card if it differs (see section 1). Then restart the login screen and log in again:

```
sudo systemctl restart gdm3
```

After this, the panel showed i3's first-run screen. We changed these three options together, so we don't know which one matters most.

**To undo:** delete that one file and restart `gdm3`.

**Using i3:** it is a tiling desktop, so there are no icons, wallpaper, or app menu.

- Choose **Alt** as the main key at first run (the PicoCalc keyboard has Alt and, as far as we saw, no Windows key).
- **Alt + Enter**: open a terminal
- **Alt + D**: open the program launcher (needs `sudo apt install -y suckless-tools` for `dmenu`)
- **Alt + Shift + Q**: close the focused window
- **Alt + Shift + E**: exit i3

i3 works fine on the panel, but it is minimal and you have to learn keys. If that isn't for you, try section 5.

## 5. XFCE: a normal desktop on the panel

```
sudo apt install -y xfce4 xfce4-terminal
```

If a blue screen asks which display manager to use, choose **gdm3** (the one already working).

On the login screen, choose **Xfce Session**, **not** the Wayland variant. The Xorg config from section 4 only applies to X11, and the Wayland variant would likely give a black screen again (**Untested**, but consistent with everything above).

**Result** (**Verified**): XFCE shows a normal panel, menu, and clickable icons on the 320x320 screen, and the keyboard works. Making everything fit is covered in section 8. We had not yet tried a web browser or a full set of emulators on it.

## 6. Smaller things we hit

- **Unused Ethernet profile popup:** the Zero 3W has no Ethernet port, but a "Wired connection 1" profile existed and showed a "connection failed" popup. Disabling its autoconnect stopped it and did not affect Wi-Fi:
  ```
  sudo nmcli connection modify "Wired connection 1" connection.autoconnect no
  ```
- **Warm reboot vs. cold start:** the keyboard guide notes that a warm `sudo reboot` may not reset the PicoCalc keyboard controller. If the keys die after a change, do a clean `sudo poweroff`, cut all power, wait 20 seconds, and start again.
- **Don't run a full system upgrade without a plan:** the patched keyboard module is built for one kernel version, so a kernel update will break it until you rebuild the module for the new `uname -r`. Installing individual programs is fine.
- **Chrome:** Google does not ship Chrome for ARM Linux. Use Chromium or Firefox. Browsers are slow to start on 2 GB of RAM.
- **Keep the login manager:** if you remove GNOME later, keep `gdm3` or swap to a lighter login manager first, so you don't lose the ability to choose a session. We had **not** removed GNOME when this was written.

## 7. Suggested order if you are starting from scratch

1. Follow the display and keyboard guide until the panel shows a picture and the keys type.
2. Run the read-only checks in section 1 and note your panel's `card` number.
3. Add `spi3-nodma` (section 3) if `dmesg` is flooded with `Bad Desc`.
4. Skip GNOME's apps grid. Install XFCE (section 5).
5. Add `20-picocalc.conf` (section 4) **before** logging in to any X11 session, so you don't see a black screen.
6. Lower XFCE's DPI so the interface fits the panel (section 8).
7. Keep a backup image of the card once everything works.

## 8. Making things fit: the 640x640 trick does not work here, lowering DPI does

On a Raspberry Pi, setting a 640x640 resolution in `raspi-config` works because the Pi's firmware scales the picture down to the panel. Nothing equivalent exists in this setup.

**What we tried** (**Verified** that it fails):

```
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority xrandr --output Unknown19-1 --mode 320x320 --scale 2x2
```

It printed:

```
xrandr: screen cannot be larger than 320x320 (desired size 640x640)
```

With `20-picocalc.conf` (section 4), X draws straight onto the panel device, and that device only allows a screen up to 320x320. So `--scale` cannot make a larger canvas. We did not test removing `20-picocalc.conf` to see whether scaling would then work, because without it the panel stayed black.

**What worked** (**Verified**): lower XFCE's DPI so text, menus and the panel shrink.

```
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus xfconf-query -c xsettings -p /Xft/DPI -n -t int -s 60
```

XFCE saves this by itself. We confirmed the file contains the setting:

```
grep -i dpi ~/.config/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml
```

```
<property name="DPI" type="int" value="60"/>
```

Programs that are already open may not change until you reopen them. Change the number to taste. To go back to the default, run the same command with `-r` in place of `-n -t int -s 60`.

**What is still unsolved** (**Untested** ideas, not recommendations): this makes the interface smaller but gives programs the same 320-pixel-wide canvas. A browser may still feel cramped. Possible directions we have not tried are a Wayland compositor that allows scales below 1 (for example sway with `output scale 0.5`, rendering on the main GPU and copying to the panel), or a nested or virtual X server shrunk onto the panel. If you try one of these, please report what happens.

## 9. Hardware notes: cover glass, case back, and cooling

These came from a real build and are not tested on other hardware.

**Cover glass**

- The original glass has the PicoCalc logo and a light hole, so a plain replacement sheet will not have them.
- A cheaper approach is a sacrificial clear cover (thin acrylic or PET sheet) over the original glass. Use thin double-sided tape on the border only, and check that the case still closes.
- For acrylic: about 1 mm thick, cast (not extruded), cleaned with mild soap and water. Avoid ammonia cleaners. Acrylic can be laser cut cleanly. Polycarbonate melts and yellows under a laser, so it needs a saw or CNC.
- If a cut-to-size quote is expensive, ordering spare glass pieces from the PicoCalc seller can be cheaper.

**Case back**

- In our build the Radxa and adapter almost fit, but pressing the back down pushed the board slightly out of place, and it came loose when jiggled.
- Planned fix (**Untested** when written): a 3D-printed back about 3/8 inch (roughly 9.5 mm) deeper, or a spacer ring of the same height, with longer screws. Stretch only the depth in your CAD tool so the screw holes and port cutouts stay in place.
- Print in PETG, ASA or ABS, not PLA, because PLA softens at around 55 to 60 degrees C.
- A small post or foam pad inside the back can hold the board gently in place.

**Cooling**

- The board runs warm in the enclosure. We measured the SoC at 63 C and the GPU at 58 C once, which is below the throttle point (roughly 85 C on the RK3566), but that was not a heavy load.
- Plan: a thin stick-on heatsink on the chip, with a vent hole in the back directly over it and a small 5V fan (about 30 mm) mounted on the outside over the hole.
- The Zero 3W has no fan connector. A fan needs a 5V pin and a ground pin from the header, so check which pins your adapter leaves free.
- Check temperature under a heavy load before deciding how much cooling you need:
  ```
  for z in /sys/class/thermal/thermal_zone*; do echo "$(cat $z/type): $(( $(cat $z/temp) / 1000 ))C"; done
  ```

## 10. Games: RetroPie / RetroArch speed on the panel

RetroPie was already installed on this machine (installing it is not covered here). The GBA default core is `lr-mgba`. The test game was GBA Doom, which is one of the heaviest GBA games, plus Tomb Raider: Legend as a lighter comparison.

**Where the RetroPie settings live** (**Verified**):

- Global RetroArch settings: `/opt/retropie/configs/all/retroarch.cfg`
- Per-system settings: for example `/opt/retropie/configs/gba/retroarch.cfg`
- Core options: `/opt/retropie/configs/all/retroarch-core-options.cfg`
- Which core is the default for a system: `cat /opt/retropie/configs/gba/emulators.cfg`
- Edit these as your normal user, not with `sudo sed -i`, because that can change the file's owner and stop RetroArch from saving to it.

**What we found, in order:**

1. **Window not filling the screen, and slow** (**Verified** fix): turning off XFCE's window compositor made the game fill the screen and run noticeably faster. XFCE saves this setting by itself.
   ```
   DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus xfconf-query -c xfwm4 -p /general/use_compositing -n -t bool -s false
   ```
   To undo it, run the same command with `-s true`.
2. **Heat is not the problem** (**Verified**): while a game was running, the SoC read 66 C, the GPU 61 C, and all four cores sat at the 1.8 GHz maximum. No throttling. The case does feel warm to the touch.
3. **The emulator is not CPU-bound** (**Verified**): `top` during Doom showed RetroArch at only about 18% CPU, while a kernel worker (about 47%), Xorg (about 24%) and softirq time (about 21%) were busy. The game was waiting on the screen update path, not on emulation.
4. **Turning off RetroArch shaders** (`video_shader_enable = "false"`) made no noticeable difference (**Observed**).
5. **A smaller window did not help** (**Verified**): with `video_fullscreen = "false"` and `video_scale = "1.000000"`, plus `fps_show = "true"` for an on-screen counter, GBA Doom still ran at 19 to 30 fps. So the idea that fewer pixels would be faster was wrong in this setup.
6. **Threaded video helped** (**Verified**): adding these lines to the global `retroarch.cfg` let the game keep its own speed and drop frames the panel can't show. Typical rate went to 33 to 36 fps, with a dip to about 28 in the busiest scene, and it felt faster.
   ```
   printf 'video_threaded = "true"\nvideo_vsync = "false"\n' >> /opt/retropie/configs/all/retroarch.cfg
   ```
7. **mGBA frame skip** (**Observed**): adding `mgba_frameskip = "auto"` to the core options file (with RetroArch closed) raised the worst scene from about 28.5 to about 32.5 fps. The option name and value are from memory; if your core version differs, it is simply ignored.
   ```
   cp /opt/retropie/configs/all/retroarch-core-options.cfg ~/core-options.bak
   echo 'mgba_frameskip = "auto"' >> /opt/retropie/configs/all/retroarch-core-options.cfg
   ```

**Result:** GBA Doom is playable but still slower than real hardware. Tomb Raider: Legend ran well, with dips to about 28 fps in busy scenes, where the original console would also slow down.

**Our best explanation for the remaining limit** (**Untested**, treat as a lead): the ILI9488 panel takes 3 bytes per pixel over SPI. A full 320x320 frame at about 70 MHz tops out near 28 fps if every frame is sent as a full-screen update, which matches the 19 to 30 fps we saw before threading. If that is right, no emulator setting can raise the ceiling, and the only remaining levers are in the display driver.

**Not tried (riskier):** re-enabling DMA for the panel's SPI bus (it caused the `Bad Desc` error flood in section 3), or running the SPI bus at a higher clock. Back up `/boot/armbianEnv.txt` before trying either.

**To undo the RetroArch changes:** keep backups before you edit (`cp /opt/retropie/configs/all/retroarch.cfg ~/retroarch-all.cfg.bak`) and copy them back.

## Credits

- L0lus, for the Radxa Zero 3 PicoCalc port and display work
- wasdwasd0105, for the upstream `picocalc-pi-zero-2` project
- Michael Mayer / `ironat`, for the ZeroCalc adapter PCB
- ClockworkPi, for the PicoCalc hardware and keyboard firmware
- Radxa, for the Zero 3W platform
- ChatGPT and Claude, for troubleshooting help along the way

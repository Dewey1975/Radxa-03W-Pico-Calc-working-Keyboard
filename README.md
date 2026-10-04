# Radxa Zero 3W in a ClockworkPi PicoCalc

## Fully working display and keyboard with the Michael Mayer ZeroCalc adapter

> **Update:** after the display and keyboard work, you may hit a desktop crash, DMA errors, or a black screen on X11. See the [addendum on getting a usable desktop on the 320x320 panel](ADDENDUM-desktop-and-display.md).

This guide documents a working ClockworkPi PicoCalc build using:

- a Radxa Zero 3W (2 GB model in the verified build);
- the original 320 x 320 PicoCalc SPI display;
- the original PicoCalc keyboard controller at I2C address `0x1f`;
- the Michael Mayer / `ironat` ZeroCalc adapter PCB; and
- a patched version of L0lus's `picocalc_kbd` Linux input driver.

The keyboard was physically tested, typed successfully in Linux, and continued to work after the permanent overlay was enabled and the PicoCalc was completely powered off and restarted.

> **Important:** A normal `sudo reboot` is not a complete keyboard-controller reset. During troubleshooting, the PicoCalc keyboard MCU remained in a bad I2C state across warm reboots. Use a clean Linux shutdown followed by a complete physical power-off and cold start whenever this guide calls for a **cold power cycle**.

---

## What was actually wrong

There were two separate incompatibilities hiding behind the same dead-keyboard symptom.

### 1. The Michael Mayer adapter uses physical pins 3 and 5

L0lus's published Radxa wiring and keyboard overlay use:

- physical pin 27 for SDA;
- physical pin 28 for SCL;
- Radxa `i2c4`; and
- pin group `i2c4m0_xfer`.

The Michael Mayer ZeroCalc PCB does **not** route the keyboard to pins 27 and 28. Its KiCad PCB connects:

- PicoCalc `I2C1_SDA` to 40-pin header physical pin **3**;
- PicoCalc `I2C1_SCL` to 40-pin header physical pin **5**;
- header pins **27 and 28 are unconnected**.

On the Radxa Zero 3W, physical pins 3 and 5 are the `I2C3-M0` pin group. Therefore this adapter requires:

- Linux bus `/dev/i2c-3`;
- device-tree target `&i2c3`; and
- pinctrl group `&i2c3m0_xfer`.

### 2. L0lus's FIFO read transaction fails on this combination

The original driver reads the PicoCalc FIFO with:

```c
i2c_smbus_read_word_data(i2c_client, reg_addr)
```

That is one SMBus Read Word operation: it writes the register number, issues a repeated START, then reads two bytes.

On the verified Radxa/I2C3/PicoCalc combination, the keyboard acknowledged its address but the combined write/read transaction failed with:

```text
Error: Sending messages failed: No such device or address
```

The kernel driver reported the same failure as error `-6` (`ENXIO`):

```text
picocalc_kbd 3-001f: kbd_read_i2c_2u8 Could not read from register 0x09, error: -6
picocalc_kbd 3-001f: input_fw_read_fifo Could not read REG_FIF, Error: -6
```

The keyboard worked when the register-select write and two-byte read were sent as **two separate I2C transfers with a STOP between them**:

```bash
sudo i2ctransfer -y 3 w1@0x1f 0x09
sudo i2ctransfer -y 3 r2@0x1f
```

The verified empty-FIFO response was:

```text
0x00 0x00
```

The driver fix replaces the SMBus Read Word call with separate `i2c_master_send()` and `i2c_master_recv()` calls.

---

## Verified system

The final successful machine reported:

```text
Armbian v26.8.1 for Radxa ZERO 3
Linux 6.1.115-vendor-rk35xx
Ubuntu stable (resolute)
```

The display had already been proven working with:

```text
ili9488-panel-mipi-dbi-spi
```

The final `/boot/armbianEnv.txt` overlay line was:

```text
user_overlays=ili9488-panel-mipi-dbi-spi picocalc-kbd-i2c3
```

This guide records that exact successful configuration. Other kernels and distributions may work, but they were not part of this final verification.

---

## Exact source versions inspected

These commit IDs make it possible to return to the same upstream source state later.

| Project | Repository | Inspected commit |
|---|---|---|
| L0lus Radxa port and keyboard driver | <https://github.com/L0lus/PicoCalc-Radxa-zero-3> | `bdbe21ad5823f538fc08f3e503fea50cd8d220a9` |
| Michael Mayer / ironat ZeroCalc PCB | <https://github.com/ironat/ZeroCalcGerber> | `af07dd2bb5890c8ee40a681c36393938402b83c9` |
| Radxa overlays | <https://github.com/radxa-pkg/radxa-overlays> | `788153020eb3ae4dfd7bde412bdaaf97715f0757` |
| Official ClockworkPi PicoCalc source | <https://github.com/clockworkpi/PicoCalc> | `f91519806d4b2e0a62c4638a9f695cd5162c5479` |

The ClockworkPi firmware defines:

- slave address `0x1f`;
- FIFO register `REG_ID_FIF = 0x09`;
- a two-byte FIFO response containing key state and key code.

---

## Adapter identification and pin cross-reference

The adapter used by the successful build is the ZeroCalc PCB published in the `ironat/ZeroCalcGerber` repository. The source board is:

<https://github.com/ironat/ZeroCalcGerber/blob/main/PicoCalcPiZeroKiCAD/PicoCalcPiZero.kicad_pcb>

The important routes visible in that PCB source are:

| PicoCalc signal | Adapter 40-pin header | Radxa Zero 3W function used |
|---|---:|---|
| `VDD` / `VSYS` | Pin 2 or 4 | 5 V |
| `GND` | Pin 6 (other grounds also share GND) | Ground |
| `I2C1_SDA` | **Pin 3** | `I2C3-M0 SDA`, `GPIO1_A0` |
| `I2C1_SCL` | **Pin 5** | `I2C3-M0 SCL`, `GPIO1_A1` |
| `LCD_DC` | Pin 18 | `GPIO3_B2` |
| `SPI1_TX` | Pin 19 | `GPIO4_C3`, SPI3 MOSI M1 |
| `LCD_RST` | Pin 22 | `GPIO3_C1` |
| `SPI1_SCK` | Pin 23 | `GPIO4_C2`, SPI3 clock M1 |
| `SPI1_CS` | Pin 24 | `GPIO4_C6`, SPI3 CS0 M1 |
| Header pin 27 | **Unconnected on this adapter** | Do not use for this adapter's keyboard |
| Header pin 28 | **Unconnected on this adapter** | Do not use for this adapter's keyboard |

This is why copying L0lus's I2C4/pins-27-and-28 keyboard overlay does not address the keyboard on the Michael Mayer board.

---

## Files supplied with this guide

```text
.
├── README.md
├── overlays
│   ├── i2c3-header-test.dts
│   └── picocalc-kbd-i2c3.dts
├── patches
│   └── picocalc-kbd-stop-separated-read.patch
├── scripts
│   └── apply_driver_fix.py
└── docs
    └── verified-diagnostic-log.md
```

- `i2c3-header-test.dts` enables I2C3 on pins 3 and 5 without claiming `0x1f`. Use it for raw hardware testing.
- `picocalc-kbd-i2c3.dts` enables the same pins and creates the permanent `picocalc_kbd` device at `0x1f`.
- `apply_driver_fix.py` patches the upstream driver automatically and makes a backup. Nobody needs to search manually through the C source.
- The `.patch` file contains the same source change in normal Git patch format.

---

# Installation

## Read this before entering commands

1. Commands beginning with `sudo` change the operating system.
2. Do not install both the I2C3 test overlay and keyboard overlay at the same time.
3. Do not use L0lus's original `picocalc_kbd.dts` with the Michael Mayer adapter. It targets I2C4 and the wrong physical pins for this PCB.
4. Do not run the upstream `setup_picocalc.sh` blindly on an already working MIPI-DBI display installation. That script installs the upstream I2C4 keyboard overlay and the older `ili9488-fbtft.dts` display overlay.
5. A kernel module is built for one kernel version. After a kernel upgrade, rebuild and reinstall `picocalc_kbd.ko` for the new `uname -r`.
6. Keep another SSH session or recovery SD card available when changing boot overlays.

## Step 1: Confirm the kernel build directory exists

Run:

```bash
test -d "/lib/modules/$(uname -r)/build" && echo "Kernel build directory is present" || echo "STOP: kernel headers/build directory is missing"
```

Continue only if it prints:

```text
Kernel build directory is present
```

## Step 2: Install the ordinary build and I2C tools

```bash
sudo apt update
sudo apt install -y git build-essential device-tree-compiler i2c-tools python3
```

## Step 3: Download L0lus's source

```bash
cd "$HOME"
git clone https://github.com/L0lus/PicoCalc-Radxa-zero-3.git
cd "$HOME/PicoCalc-Radxa-zero-3"
```

To use the exact inspected version:

```bash
git checkout bdbe21ad5823f538fc08f3e503fea50cd8d220a9
```

If the directory already exists, do not clone over it. Enter it with:

```bash
cd "$HOME/PicoCalc-Radxa-zero-3"
```

## Step 4: Apply the FIFO-read fix

Copy this repository's `scripts/apply_driver_fix.py` file to the Radxa, then run:

```bash
python3 /path/to/apply_driver_fix.py "$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.c"
```

Expected result:

```text
Keyboard driver patched successfully.
Backup: /home/YOUR_NAME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.c.backup
```

The script refuses to modify the file if it cannot identify the expected upstream function.

### Manual source change, for reviewers

The resulting helper must be:

```c
static inline int kbd_read_i2c_2u8(struct i2c_client* i2c_client,
    uint8_t reg_addr, uint8_t* dst)
{
    int rc;

    rc = i2c_master_send(i2c_client, (const char *)&reg_addr, 1);
    if (rc != 1)
        return rc < 0 ? rc : -EIO;

    rc = i2c_master_recv(i2c_client, (char *)dst, 2);
    if (rc != 2)
        return rc < 0 ? rc : -EIO;

    return 0;
}
```

`i2c_master_send()` completes the register-select write. `i2c_master_recv()` then starts a separate two-byte read. Because these are separate Linux I2C transfers, a STOP separates them.

## Step 5: Build the patched module

```bash
make -C /lib/modules/$(uname -r)/build M="$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd" modules
```

The verified build printed a compiler-version warning and this BTF message:

```text
Skipping BTF generation ... due to unavailability of vmlinux
```

Those were warnings. The successful build produced:

```text
$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.ko
```

Confirm the file exists:

```bash
test -f "$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.ko" && echo "Module built"
```

## Step 6: Install the module

```bash
sudo mkdir -p "/lib/modules/$(uname -r)/extra"
sudo install -m 644 "$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.ko" "/lib/modules/$(uname -r)/extra/picocalc_kbd.ko"
sudo depmod
```

These commands install the module for the currently running kernel. They do not by themselves connect it to the keyboard.

## Step 7: Install the permanent I2C3 keyboard overlay

Copy `overlays/picocalc-kbd-i2c3.dts` from this repository to the Radxa. Then run:

```bash
sudo armbian-add-overlay /path/to/picocalc-kbd-i2c3.dts
```

Check `/boot/armbianEnv.txt`:

```bash
grep '^user_overlays=' /boot/armbianEnv.txt
```

For the verified MIPI-DBI display system, the final line was:

```text
user_overlays=ili9488-panel-mipi-dbi-spi picocalc-kbd-i2c3
```

Remove these incompatible keyboard entries if they are present:

```text
picocalc_kbd
i2c4-test
i2c3-header-test
```

Do not remove the known-working display overlay.

## Step 8: Perform a cold power cycle

First shut Linux down cleanly:

```bash
sudo poweroff
```

Then:

1. Wait for the Radxa to finish shutting down.
2. Turn the PicoCalc completely off.
3. Disconnect external USB-C power, if attached.
4. Wait at least 20 seconds.
5. Reconnect power.
6. Turn the PicoCalc on.

Do not substitute `sudo reboot` for this first verification. A warm reboot did not reliably reset the keyboard MCU in the verified build.

## Step 9: Test the keyboard

After the desktop appears, press keys in a terminal or text field on the PicoCalc itself.

The real acceptance test is simple: **the keys type characters**.

An input device appearing in Linux or an I2C address displaying `UU` is not enough by itself. Earlier, the unpatched driver created an input device and reserved `0x1f` even while every FIFO read was failing.

---

# Optional display installation notes

The successful display used L0lus's MIPI-DBI overlay, not the `ili9488-fbtft.dts` overlay selected by the upstream setup script.

From the L0lus repository:

```bash
sudo armbian-add-overlay "$HOME/PicoCalc-Radxa-zero-3/picocalc_display/dts/ili9488-panel-mipi-dbi-spi.dts"
sudo install -m 644 "$HOME/PicoCalc-Radxa-zero-3/picocalc_display/bin/picolcd.bin" /lib/firmware/picolcd.bin
```

The firmware file must be included in the initramfs. Create:

```text
/etc/initramfs-tools/hooks/include-picolcd-bin.sh
```

with:

```sh
#!/bin/sh
PREREQ=""

prereqs()
{
    echo "$PREREQ"
}

case "$1" in
prereqs)
    prereqs
    exit 0
    ;;
esac

. /usr/share/initramfs-tools/hook-functions
copy_exec /lib/firmware/picolcd.bin
```

Then run:

```bash
sudo chmod +x /etc/initramfs-tools/hooks/include-picolcd-bin.sh
sudo update-initramfs -u
```

The already-proven system used the overlay name:

```text
ili9488-panel-mipi-dbi-spi
```

Do not replace a working display configuration merely to match this section.

---

# Diagnostic procedure

Use this only when the permanent driver is not working.

## Diagnostic mode: enable the pins without claiming the keyboard

Install `overlays/i2c3-header-test.dts`:

```bash
sudo armbian-add-overlay /path/to/i2c3-header-test.dts
```

Ensure the `user_overlays` line contains `i2c3-header-test` and does not contain `picocalc-kbd-i2c3`. Then perform a cold power cycle.

### Scan I2C3

```bash
sudo i2cdetect -y 3
```

The verified raw scan contained:

```text
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 1f
20: -- -- UU -- -- -- -- -- -- -- -- -- -- -- -- --
```

Interpretation:

- `1f` is the unclaimed PicoCalc keyboard controller.
- `UU` at `0x22` is an existing Radxa internal I2C client. It did not prevent the successful keyboard build.

If `0x1f` is missing, do not install the keyboard driver yet. Check adapter seating, power, cold-reset state, and whether the active overlay really selects I2C3-M0.

### Test the transaction that works

Select FIFO register `0x09`:

```bash
sudo i2ctransfer -y 3 w1@0x1f 0x09
```

Success is a silent return to the shell prompt.

Read two bytes in a separate transfer:

```bash
sudo i2ctransfer -y 3 r2@0x1f
```

With an empty FIFO, the verified response was:

```text
0x00 0x00
```

Any two returned bytes prove the register transaction completed. Nonzero bytes may represent a queued key event.

### The transaction that failed

The following combined transfer reproduced the driver problem:

```bash
sudo i2ctransfer -y 3 w1@0x1f 0x09 r2
```

It returned:

```text
Error: Sending messages failed: No such device or address
```

Do not keep repeating a failing transfer. In testing, the keyboard stopped accepting later transactions until it received a complete cold power cycle.

---

# Live test without changing the boot overlay

This was the final test used before making the overlay permanent.

Start from diagnostic mode with `i2c3-header-test` active and `0x1f` visible.

Load the patched module:

```bash
sudo modprobe picocalc_kbd
```

Create the I2C client dynamically:

```bash
echo picocalc_kbd 0x1f | sudo tee /sys/bus/i2c/devices/i2c-3/new_device
```

Expected output:

```text
picocalc_kbd 0x1f
```

The keyboard should begin typing immediately. This dynamic client disappears at shutdown, which is why the permanent device-tree overlay is still required.

---

# Why earlier evidence was misleading

## `UU` at `0x1f` did not prove communication

When a device-tree node creates an I2C client, Linux reserves its address. `i2cdetect` then prints `UU`, meaning a kernel client owns the address. It does not prove that the physical device answered.

The L0lus driver had its firmware-detection probe commented out. It could register `/dev/input/event*` even when reads from the actual keyboard failed. Therefore all three of these could exist simultaneously:

- `UU` at `0x1f`;
- a loaded `picocalc_kbd` module;
- a Linux input event device;

while the hardware was still returning `ENXIO` on every FIFO read.

The decisive hardware test was a raw `1f` in diagnostic mode with no keyboard client claiming the address, followed by successful STOP-separated register access.

## Error `-6`

Linux error `-6` is `ENXIO`, displayed in user space as "No such device or address." In this case it did not mean `/dev/i2c-3` was missing. It meant the I2C transaction was not acknowledged successfully.

## Why changing from I2C4 to I2C3 was necessary

The adapter PCB, not a generic Raspberry Pi convention, determines which header pins carry the keyboard signals. The Michael Mayer PCB source explicitly connects its SDA and SCL nets to pins 3 and 5 and explicitly leaves pins 27 and 28 unconnected.

---

# Troubleshooting decision table

| Result | Meaning | Next action |
|---|---|---|
| `/dev/i2c-3` does not exist | I2C3 overlay did not load | Check `user_overlays`, overlay compilation, and cold-start again |
| Raw scan shows `1f` | Keyboard is powered and acknowledges on I2C3 | Test separate write and read |
| Raw scan does not show `1f` | No keyboard address acknowledgment | Check adapter seating, power, I2C3-M0 pin routing, and cold power cycle |
| Separate write and read return bytes | Electrical path and keyboard register protocol work | Build/install the patched driver |
| Combined transaction fails | Reproduces the original driver incompatibility | Use the patched STOP-separated read helper |
| `UU` appears at `1f` after driver overlay | Kernel owns the address | Test actual key input; `UU` alone is not proof |
| Event device exists but keys do nothing | Driver may have registered without successful reads | Check kernel log for `REG_FIF` and error `-6` |
| Works after cold start but not warm reboot | Keyboard MCU was not reset by warm reboot | Use `sudo poweroff`, cut power fully, and cold-start |
| Stops after a kernel update | Installed `.ko` belongs to the previous kernel | Rebuild and reinstall against the current `uname -r` |

---

# Recovery and rollback

## Restore the original source file

The patch helper creates:

```text
picocalc_kbd.c.backup
```

Restore it with:

```bash
cp "$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.c.backup" "$HOME/PicoCalc-Radxa-zero-3/picocalc_kbd/picocalc_kbd.c"
```

Then rebuild and reinstall if a complete rollback is desired.

## Return to raw diagnostic mode

In `/boot/armbianEnv.txt`, replace:

```text
picocalc-kbd-i2c3
```

with:

```text
i2c3-header-test
```

Keep the working display overlay unchanged. Shut down and cold power-cycle afterward.

## Temporarily unload the driver

```bash
sudo modprobe -r picocalc_kbd
```

If the device-tree node is still present, its address may remain reserved even with the module unloaded. Diagnostic mode avoids that ambiguity.

---

# Confirmed success criteria

The solution is considered working only when all of these are true:

- the PicoCalc display works;
- the permanent overlay line contains `picocalc-kbd-i2c3`;
- the patched module is installed under the current kernel's `/lib/modules/.../extra/` directory;
- the machine completes a full power-off and cold start;
- physical PicoCalc key presses type correctly in Linux.

This exact set of conditions was achieved on the documented machine.

---

# Credits

- ClockworkPi for the PicoCalc hardware, schematic, and keyboard firmware.
- Michael Mayer / `ironat` for the ZeroCalc adapter PCB files.
- L0lus for the Radxa Zero 3 PicoCalc port, display work, and Linux keyboard-driver base.
- Radxa for the Zero 3W platform and official I2C3-M0 overlay definition.
- Duane Freeman and Bob for isolating the adapter pin-routing difference, reproducing the I2C failure, proving the STOP-separated transaction, patching the driver, and validating the complete cold-start result.

## Final technical summary

```text
Adapter keyboard pins:  physical 3 (SDA), physical 5 (SCL)
Radxa controller:       I2C3-M0
Linux bus:              /dev/i2c-3
Keyboard address:       0x1f
FIFO register:          0x09
Broken operation:       one SMBus Read Word / combined repeated-START transfer
Working operation:      separate one-byte send, STOP, separate two-byte receive
Permanent overlay:      picocalc-kbd-i2c3
Required reset:         complete cold power cycle for first verification/recovery
```

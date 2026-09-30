# Movement-only Corne TPS65 test

The #102 user report was that the laptop touchpad could move the cursor but
could not click while the Corne was connected; clicks recovered on unplug.
This is consistent with a mouse button being held by the Corne, not proof.
The driver press-and-hold path sends a button-down and requires a later
sensor read to observe release. Read errors or no further RDY processing
can leave that hold pressed. #102 still enabled that gesture.

This variant disables tap, press-and-hold, two-finger tap, and scrolling in
the left TPS65 node. It pins a driver fix that honors those disabled settings
while processing reported gesture flags, including stale flags if sensor
setup fails. All gesture settings in this build are false, so the handler
cannot enter a button-press path. The device provides cursor movement only.
The right keyboard firmware is unchanged. No OLED/RGB/Studio/logging added.
This does not establish that continuous movement or RDY handling is fixed.

Driver revision: e0f5e66e61e9daf961122f2f0a81be6b51b0ac9c, based on
bec78d530d896d5244a26d7bb39cf3e3ed69e25c (streaming mode).
Regression tests execute the actual pinned runtime handler with simulated
I2C frames: every combination of 8-bit gesture flags with movement and idle,
read failures, reset indication, and enabled-gesture compatibility cases.

Flash corne_left_keyboard_trackpad_movement_only.uf2 on the LEFT. Keep the
#102 right firmware or flash corne_right_keyboard_minimal.uf2 from the ZIP.
No settings_reset is needed for this change. Disconnect both halves from
USB/Bluetooth before flashing to clear any old host button-down state.
The previous left-side TPS65 wiring remains the same.

## Previous inspection and minimal baseline

# Minimal Corne with TPS65 on the left

Firmware contains the normal Corne keyboard and a TPS65 connected directly
to the left/central. USB keyboard and mouse reports come from the left;
the right supplies its keys over the existing BLE split.

P0.09/P0.10 are explicitly configured as GPIO through the nRF UICR
property; this is required for their use as SDA/SCL and avoids relying on a
previous firmware or bootloader setting. It may cause one automatic reboot
when first applied.

No OLED, RGB/backlight, ZMK Studio, battery reporting, USB logging, custom
probe threads, pointer split transport, or nice-oled dependency. OLED/i2c0
and spi3 nodes are disabled. Deep sleep is disabled and the idle threshold
is maximized for this USB-powered test. The previous keyboard layers and
Alt-Tab macro remain; Studio unlock keys become transparent.

## Inspection of the other ChatGPT test (#100)

Repository: deSales327/corne-oled-zmk.
Branch: test/tps65-left-streaming.
Inspected commit: b7c589692c46ae5980a378768b0924d37a1cdc38.

That test moved the sensor from the right peripheral to the left central,
using P0.10/P0.09 and P1.04/P1.06, and changed the listener to the local sensor.
The #100 left overlay did not explicitly configure NFC pins as GPIO.
The right build lost its trackpad shield. It still built nice_oled on both
halves and Studio on the left. Its shared DTS also retained a pointer split
node even though the listener was local. It reverted orientation from the
#59 flip-y setting to switch-xy and removed RGB key bindings.

The driver changed from AYM1607 revision 27321f0232b50f0af31eb27ff97d539933467ea4
to deSales327 revision bec78d530d896d5244a26d7bb39cf3e3ed69e25c.
Comparing those revisions shows one functional change: IQS5XX_EVENT_MODE
changes from BIT(0) to 0, selecting streaming mode. Initialization and the
RDY rising-edge interrupt remain unchanged. This minimal branch preserves
that exact driver and sensor settings to avoid another simultaneous driver
experiment. Removing other features does not prove the short-lived pointer
problem is fixed; the hardware test still has to establish that.

## Files and wiring

- corne_left_keyboard_trackpad_minimal.uf2: LEFT, with TPS65.
- corne_right_keyboard_minimal.uf2: RIGHT, keyboard only.
- settings_reset.uf2: optional reset of persisted settings and bonds.

TPS65 connections on the left nice!nano v2:
J1.1 RDY -> D9/P1.06; J1.2 NRST -> D8/P1.04; J1.3 GND -> GND;
J1.4 VDDHI -> VCC 3.3 V; J1.5 SCL -> D10/P0.09;
J1.6 SDA -> D16/P0.10. Never use RAW for VDDHI.

Flash the corresponding UF2 on each half, leaving the TPS65 on the left.
Use the left USB for keyboard/mouse reports. Both halves need power; USB
can power both while batteries are absent. OLEDs and RGB stay off by design.
For a complete fresh-state test, settings_reset may be used on BOTH halves
before flashing the normal right and left firmware. This erases Bluetooth
pairings/settings; host pairings must then be removed/recreated if using BLE.
There is no automatic settings reset at boot.

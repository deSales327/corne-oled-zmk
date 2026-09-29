# Power settings recovery diagnostic

This branch restores the standard nice!nano v2 EXT_POWER driver, with a 500 ms
settling delay. A settings handler for exactly `ext_power/state/EXT_POWER`
ignores the previous boolean during load and saves `true` at commit. This avoids
an off pulse after OLED/TPS65 hardware initialization. Bluetooth bonds, Studio
settings and saved RGB preferences are not erased. RGB, deep sleep and display
blanking remain disabled for the USB diagnostic. OLEDs, keymap, Studio and TPS65
pinout are otherwise unchanged from the previous power test.

Flash `corne_left_power_recovery.uf2` to the LEFT first, without settings_reset.
Leave powered for 10 seconds, unplug USB for 10 seconds, reconnect and check OLED
and VCC. Repeat several cold boots. Test the right file only after validating the
left; the right side was reported working and does not need changing first.
`settings_reset.uf2` is included for recovery only, not for this test.

This addresses a plausible persistent EXT_POWER-off state. Compilation does not
prove hardware recovery. The previous direct-GPIO diagnostic disabled the
settings handler but did not repair older stored values; the #58/reset comparison
alone cannot establish which firmware originally saved the bad state. The new
version restores the stock power initialization as well as repairing that key.
An old RGB-off preference can still suppress LEDs when returning to RGB firmware.

The upstream general guide below is retained for reference. Its broad reset
advice does not apply to this diagnostic, which should first be tested without
resetting settings.

---
[![Build Status](../../actions/workflows/build.yml/badge.svg)](../../actions)
[![Download Firmware](https://img.shields.io/badge/Download-Firmware-blue?logo=github)](../../releases/latest)


# Corne Keyboard Guide
This guide is for flashing the Ergomech Corne Keyboard. The Corne is 6×3+3 keys column-staggered split keyboard, using Cherry or Choc switches.

# ErgoMech Corne Wireless
The Ergomech Corne Wireless uses a Nice!Nano microcontroller and runs the ZMK firmware. This guide will show you how to flash the ZMK firmware to the Nice!Nano microcontroller.

## Default keymap
The default keymap of this keyboard can be found here:
![Default Keymap](./keymap-drawer/corne.svg)

## Flashing the Corne
The ZMK cli tool would typically have you step through several questions to generate the necessary code to flash the firmware then upload it to a new repository on GitHub.
However, Ergomech has already done this for you. You can find the repository [here](https://github.com/ergomechstore/corne-oled-zmk). Assuming you already have a GitHub account,
you can fork the repository, and make modifications to the keymap files in the future. For now, the guide will continue with the assumption that you have forked the repository.

### Running the Workflow
The repository has a GitHub workflow that leverages the zmkfirmware/zmk repository to build the firmware. The workflow will build the firmware and upload it as an artifact to the repository.
The workflow is triggered on push, pull_request, and manually via workflow_dispatch. You can trigger the workflow manually by going to the Actions tab in your forked repository and selecting the workflow.

### Workflow Artifact
Once the workflow has completed, you can download the artifact from the Actions tab. The artifact will be a .zip file that contains the firmware. Extract the .zip file in your
local directory. The extracted files will include:
- `corne_right-nice_nano_v2-zmk.uf2`
- `corne_left-nice_nano_v2-zmk.uf2`
- `settings_reset-nice_nano_v2-zmk.uf2`

### Flashing the keymap and firmware
#### Steps to ensure successful flashing
- Keep in mind that the power switch on the wireless Ergomech Corne is only **one** of the ways that the keyboard can be powered. The other way is to plug in the USB-C cable.
When flashing one side of the keyboard, the other side must be off. 
- The keyboard must be in bootloader mode to flash the firmware. To enter the bootloader mode, press the "BOOT" button twice in quick succession. 
- If you are having trouble flashing, you can always flash the `settings_reset-nice_nano_v2-zmk.uf2` file first. This is a good way to make sure 
that the keyboard is in a known state before flashing the firmware. The `reset` flash can be visually confirmed by the screen on the Nice!Nano microcontroller 
not displaying anything after the flash is complete.

#### Flashing Order
There is no required order to flash the firmware. You can flash the left or right side first. Assuming that you are attempting to flash the sides with the correct
file (i.e. the right side with the `corne_right-nice_nano_v2-zmk.uf2` file), you may find it helpful to follow the following order:
1. Confirm both sides of the keyboard are off.
2. Flash the right side of the keyboard, unplug the USB-C cable, and set it aside.
3. Flash the left side of the keyboard, leaving it plugged in after.
4. Turn on the right side of the keyboard. You should see the screen on the Nice!Nano microcontroller light up and display a checkmark next to the wifi icon if the sides have connected.
5. Open you favorite text editor and test the keyboard.


#### Flashing the firmware
1. Connect the keyboard to your computer via USB-C cable.
2. Press the "BOOT" button twice in quick succession to enter bootloader mode.
3. The keyboard should appear as a USB drive on your computer.
4. Drag and drop the `uf2` file that coincides with the side of the keyboard you are flashing onto the USB drive that represents the keyboard.
5. The keyboard will automatically reboot and the new firmware will be flashed.

**Note:** Some operating systems may show a failure when the keyboard reboots, or the USB drive may disappear. This is normal and the keyboard should be flashed successfully.
The keyboard flashing has been confirmed to work successfully on Windows 10, and Linux. 

## Modifying the keymap

### ZMK Keymap
We recommend at least reviewing the [ZMK Keymap documentation](https://zmk.dev/docs/features/keymaps) to understand the structure of the keymap files. This
will help you understand the changes we are making to the generated files. While not required, most example keymaps attempt to show the layout of the keyboard
shown as a comment underneath the layer declaration.

### ZMK Firmware
ZMK does provide an online [keymap editor](https://nickcoutsos.github.io/keymap-editor) and you can use this to change the keymap, this repo is already setup for the use of this editor.

#### Modifying the keymap with the keymap editor

#### Modifying the keymap manually
The exact spacing doesn't matter, but keeping the indentation consistent can be helpful for reading your keymap files. If you indent each button it will be easier
to confirm the structure of the keymap. Take a look at the [default keymap](config/corne.keymap) to see how this was done. 

The Ergomech Corne has a 5 way switch on the right side keyboard. The location of the key presses on the 5 way switch are on the last line of the `bindings` section of each layer.
As long as the correct number of entries exist on that row, the 5 way switch will work. 

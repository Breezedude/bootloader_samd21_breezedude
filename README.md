# Breezedude UF2 Bootloader

This repository contains the UF2 bootloader for the **Breezedude** board (SAMD21G18A).
It is based on the [Adafruit fork](https://github.com/adafruit/uf2-samdx1) of the
[Microsoft uf2-samd21](https://github.com/Microsoft/uf2-samd21) bootloader, which in
turn is derived from Atmel's SAM-BA. The codebase has been heavily modified for the
Breezedude hardware and use case.

Original upstream supports USB CDC (serial) and USB MSC (mass storage) flashing via
the UF2 file format. This fork retains those capabilities and adds Breezedude-specific
board configuration, pin assignments, and build customizations.

## UF2

**UF2 (USB Flashing Format)** is a name of a file format, developed by Microsoft, that is particularly
suitable for flashing devices over MSC devices. The file consists
of 512 byte blocks, each of which is self-contained and independent
of others.

Each 512 byte block consist of (see `uf2format.h` for details):
* magic numbers at the beginning and at the end
* address where the data should be flashed
* size of data
* data (up to 476 bytes; for SAMD it's 256 bytes so it's easy to flash in one go)

Thus, it's really easy for the microcontroller to recognize a block of
a UF2 file is written and immediately write it to flash.

* **UF2 specification repo:** https://github.com/Microsoft/uf2

## Features

* USB CDC (Serial emulation) monitor mode compatible with Arduino
  (including XYZ commands) and BOSSA flashing tool
* USB MSC interface for writing UF2 files
* reading of the content of the flash as an UF2 file via USB MSC
* UART Serial (real serial wire) monitor mode (typically disabled due to space constraints)
* In-memory logging for debugging - use the `logs` target to extract the logs using `openocd`
* double-tap reset to stay in the bootloader mode
* automatic reset after UF2 file is written

## Board identification

Configuration files for the Breezedude board are in `boards/breezedude/board_config.h` and `board.mk`.
Build with `make BOARD=breezedude` (or set `BOARD=breezedude` in `Makefile.user` to make it the default).

The board configuration specifies the USB vendor/product name and ID,
as well as the volume label (main thing that the operating systems show).

There is also `BOARD_ID`, which is meant to be machine-readable and specific
to a given version of board hardware. The programming environment might use
this to suggest packages to be imported (i.e., a package for a particular
external flash chip, SD card etc.).

These configuration values can be read from `INFO_UF2.TXT` file.
Presence of this file can be tested to see if the board supports `UF2` flashing,
while the contents, particularly `Board-ID` field, can be used for feature detection.

The current flash contents of the board is exposed as `CURRENT.UF2` file.
This file includes the bootloader address space. The last word of bootloader
space points to the string holding the `INFO_UF2.TXT` file, so it can be parsed
by a programming environment to determine which board does the `.UF2` file comes from.

## Handover

When the user space application implements the USB MSC protocol, it's possible to
handover execution to the bootloader in the middle of MSC file transfer,
when the application detects that a UF2 block is written.

Details are being finalized.

## Bootloader update

The bootloader will never write to its own flash area directly.
However, the user code can write there.
Thus, to update the bootloader, one can ship a user-space program,
that contains the new version of the bootloader and copies it to the
appropriate place in flash.

Such a program is generated during build in files `update-bootloader*.uf2`.
If you're already running UF2 bootloader, the easiest way to update
it, is to just copy this file to the exposed MSD drive.

The build also generates `update-bootloader*.ino` with an equivalent Arduino
sketch. You can copy&paste it into Arduino IDE and upload it to the device.

## Full-flash wiper (service tool)

`update-WIPE-ALL-FLASH-*.uf2` reinstalls the bootloader **and erases the complete
rest of the flash**. It is meant for devices that are suspected to carry stale or
corrupt content somewhere in flash (e.g. a `SETTINGS.TXT` that cannot be
rewritten on any host OS). After it has run, the device is in exactly the state
of a factory-fresh, SWD-programmed board.

**This is destructive.** Everything above the bootloader is erased to `0xFF`:

* the application (`OTA_SLOT0`)
* the OTA staging bank (`OTA_SLOT1`)
* `OTA_SETTINGS` - the device configuration (station name, sensor type, LoRa keys, ...)
* `OTA_VERSIONS`
* the OTA A/B flag row

Firmware **and** `settings.txt` have to be written again afterwards. Make sure the
settings of the device are backed up before running this (copy `SETTINGS.TXT` off
the drive). The erased flag row is a valid state: the bootloader treats it as
"slot 0 active, nothing pending", and `VERSIONS.TXT`/`SETTINGS.TXT` fall back to
their default contents until they are written again.

Because it is destructive, the wiper is **not** built by `make all`:

```
make BOARD=breezedude wipe        # only the wiper
make BOARD=breezedude WIPER=1     # everything, including the wiper
build_bootloader.bat wipe         # Windows: normal build plus the wiper
```

Usage is the same as a normal bootloader update: drop the `.uf2` onto the
bootloader drive. The device reinstalls the bootloader, blinks the error LED
while erasing (a few seconds), and comes back up as an empty bootloader drive.

The wiper also unlocks all 16 NVM regions (`UR` command plus the region lock bits
in the fuse row) before erasing. A set region lock bit makes the NVM controller
silently reject every erase/write in that region, which looks exactly like "the
file cannot be written, on any host OS" - so this is cleared as well. After the
erase, the whole area is read back: if anything did not erase, the device stays
in the wiper with a fast blinking error LED instead of reporting success.

Implementation note (`src/selfmain_wipe.c`): the wiper runs from
`APP_START_ADDRESS`, so it erases itself. The erase loop, the `BOOTPROT` restore
and the final reset therefore live in a single `.ramfunc` function that is copied
to RAM at startup and must not call anything located in flash. The bootloader is
written and verified *before* the erase starts, so a device that loses power
mid-wipe still comes back with a working bootloader. Like the regular updater,
the file also contains the legacy bridge at `0x2000`, so it works on devices
still running the old 8KB bootloader.

## Fuses

### SAMD21

The SAMD21 supports a `BOOTPROT` fuse, which write-protects the flash area of
the bootloader. Changes to this fuse only take effect after device reset.

OpenOCD exposes `at91samd bootloader` command to set this fuse. **This command is buggy.**
It seems to reset both fuse words to `0xffffffff`, which prevents the device
from operating correctly (it seems to reboot very frequently).
In `scripts/fuses.tcl` there is an OpenOCD script
which correctly sets the fuse. It's invoked by `dbgtool.js fuses`. It can be also
used to reset the fuses to sane values - just look at the comment at the top.

The bootloader update programs (both the `.uf2` file and the Arduino sketch)
clear the `BOOTPROT` (i.e., set it to `0x7`) before trying to flash anything.
After flashing is done, they set `BOOTPROT` to 8 kilobyte bootloader size (i.e, `0x2`).

### SAMD51

The SAMD51s bootloader protection can be temporarily disabled through an NVM
command rather than a full erase and write of the AUX page. The boot protection
will be checked and set by the self updaters.

So, if you've used self-updaters but want to load it directly, then you'll need
to temporarily turn off the protection. In gdb the command is:

`set ((Nvmctrl  *)0x41004000UL)->CTRLB.reg = (0xA5 << 8) | 0x1a`

## Build

### Requirements

* `make` and an Unix environment
* `node`.js in path (optional)
* `arm-none-eabi-gcc` in the path (the one coming with Yotta will do just fine). You can get the latest version from ARM: https://developer.arm.com/open-source/gnu-toolchain/gnu-rm/downloads
* `openocd` - you can use the one coming with Arduino (after your install the M0 board support)

Atmel Studio is not supported.

You will need a board with `openocd` support.

Arduino Zero (or M0 Pro) will work just fine as it has an integrated USB EDBG
port. You need to connect both USB ports to your machine to debug - one is for
flashing and getting logs, the other is for the exposed MSC interface.

Otherwise, you can use other SAMD21 board and an external `openocd` compatible
debugger. IBDAP is cheap and seems to work just fine. Another option is to use
Raspberry Pi and native bit-banging.

`openocd` will flash 16k, meaning that on SAMD21 the beginning of user program (if any) will
be overwritten with `0xff`. This also means that after fresh flashing of bootloader
no double-tap reset is necessary, as the bootloader will not try to start application
at `0xffffffff`.

### Build commands

The default board is `breezedude`. Build it with:

```
make BOARD=breezedude
```

Or create a `Makefile.user` with `BOARD=breezedude` to set it as the permanent default.
The board name refers to the subdirectory under `boards/`.

There are various targets:
* `all` - just compile the board
* `burn` or `b` - compile and deploy to the board using openocd
* `logs` or `l` - shows logs
* `run` or `r` - burn, wait, and show logs

Typically, you will do:

```
make r
```

### Configuration

There is a number of configuration parameters at the top of `uf2.h` file.
Adjust them to your liking.

By default, you cannot enable all the features, as the bootloader would exceed
the 8k(SAMD21)/16k(SAMD51) allocated to it by Arduino etc. It will assert on startup that it's not bigger
than 8k(SAMD21)/16k(SAMD51). Also, the linker script will not allow it.

Three typical configurations are:

* HID, WebUSB, MSC, plus flash reading via FAT; UART and CDC disabled;
  logging optional; **recommended**
* USB CDC and MSC, plus flash reading via FAT; UART disabled;
  logging optional; this may have Windows driver problems
* USB CDC and MSC, no flash reading via FAT (or at least `index.htm` disabled); UART enabled;
  logging disabled; no handover; no HID;
  only this one if you need the UART support in bootloader for whatever reason

CDC and MSC together will work on Linux and Mac with no drivers.
On Windows, if you have drivers installed for the USB ID chosen,
then CDC might work and MSC will not work;
otherwise, if you have no drivers, MSC will work, and CDC will work on Windows 10 only.
Thus, it's best to set the USB ID to one for which there are no drivers.

The bootloader sits at 0x00000000, and the application starts at 0x00002000 (SAMD21) or 0x00004000 (SAMD51).

## License

See THIRD-PARTY-NOTICES.txt for the original SAM-BA bootloader license from Atmel and the
Adafruit/Microsoft upstream licenses.

The Breezedude-specific modifications are licensed under MIT.

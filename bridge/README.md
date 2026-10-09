# GlideKVM Bridge

The GlideKVM Bridge lets the main computer's keyboard and mouse work on an
iPad, an iPhone, or any other phone or tablet that takes a Bluetooth keyboard
and mouse, with nothing installed on it. It is a small ESP32-S3 board plugged
into the main computer's USB port. To each device it is an ordinary Bluetooth
keyboard and mouse; GlideKVM tells it what to type and where to move.

One board serves up to eight devices, paired once each. The mouse and keys go
to whichever one the pointer is on, as with any other screen on the desk.

## What you need

Any ESP32-S3 development board with a USB port, for example the Freenove
ESP32-S3-WROOM board. Boards with only Wi-Fi and classic Bluetooth (the plain
ESP32) and the ESP32-S2 (no Bluetooth) don't work.

## Installing its software

Once, before first use, and again when a new version comes out:

1. Download `glidekvm-bridge-esp32s3.bin`: it comes with GlideKVM's releases,
   and every build on GitHub Actions has it as the `glidekvm-bridge-firmware`
   artifact.
2. Plug the board into the computer. If Windows doesn't show a new COM port in
   Device Manager, install the driver for its USB chip (CH343 or CP210x,
   printed on the chip next to the USB port).
3. In Chrome or Edge, open the
   [ESP Tool web page](https://espressif.github.io/esptool-js/), click
   **Connect** and pick the board's port.
4. Set the flash address to `0x0`, choose the downloaded file and click
   **Program**. It takes about a minute.
5. Unplug the board and plug it back in.

If the page can't connect, hold the board's **BOOT** button while plugging it
in, then try again.

## Using it

1. On the main computer, open **Arrange screens** and click **Add a phone or
   tablet...**. GlideKVM finds the board (sharing pauses meanwhile).
2. Click **Pair a phone or tablet**. On the device, open **Settings** >
   **Bluetooth** and tap **GlideKVM Bridge**.
3. Close the window. The device is on the desk next to this computer: drag it
   to where it sits and click **Save**.

On an iPhone the pointer also needs **Settings** > **Accessibility** >
**Touch** > **AssistiveTouch** turned on. iPads show it on their own.

### Good to know

- **The pointer is placed, then followed.** A Bluetooth mouse only says how
  far it moved, so when the mouse arrives GlideKVM pushes the pointer into the
  top left corner and moves it to where it came in. The device speeds the
  pointer up as it likes, so the two can drift apart; if moving back to the
  main computer takes more travel than expected, lower the pointer's speed on
  the device (on an iPad, **Settings** > **Accessibility** > **Pointer
  Control**), or set a shortcut to move the mouse here and back.
- **Ctrl works as Command** on iPads and iPhones, so Ctrl+C and Ctrl+V copy
  and paste as on the main computer. Turn it off in the device's panel on
  Arrange screens.
- **The on-screen keyboard.** Phones and tablets hide theirs while a
  Bluetooth keyboard is connected. Turn on **Keep its on-screen keyboard** in
  the device's panel and the bridge lets go of it while the mouse is
  elsewhere; it comes back in about a second.
- **No shared clipboard** with devices on the bridge: a Bluetooth keyboard
  can't carry it. Android devices can use the
  [GlideKVM app](../android/README.md) instead, which can.

## For developers

`firmware/` is an ESP-IDF 5.4 project using the NimBLE Bluetooth stack:

```sh
cd bridge/firmware
idf.py set-target esp32s3 && idf.py build
idf.py -p PORT flash
```

The computer talks to the board in lines of text at 921600 baud, described in
[`firmware/main/commands.h`](firmware/main/commands.h), so a serial terminal
is enough to try it. The parts that don't need a board are tested with
`firmware/test/test_main.c`; `src/test/smoke/bridge_board.py` runs the server
against a pretend board.

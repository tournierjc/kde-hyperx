# kde-hyperx

KDE system tray battery monitor for the HyperX Cloud Flight wireless headset.

Shows a permanent tray icon with real-time battery percentage, charging state, and color-coded levels. Communicates directly with the headset dongle over HID using the reverse-engineered protocol from [HeadsetControl](https://github.com/Sapd/HeadsetControl) and [hyperx-cloud-flight](https://github.com/kondinskis/hyperx-cloud-flight).

## Supported devices

| Headset | VID | PID |
|---|---|---|
| Cloud Flight (old) | `0x0951` | `0x16C4` |
| Cloud Flight (new) | `0x0951` | `0x1723` |

## Dependencies

- Qt 6 (Core, Gui, Widgets)
- hidapi (hidraw backend)
- g++ with C++17 support

On Arch Linux:

```
pacman -S qt6-base hidapi
```

## Build

```
./build.sh
```

Or with CMake:

```
cmake -B build && cmake --build build
```

## Install

```
./install.sh
```

## Uninstall

```
./uninstall.sh
```

## Manual setup

### udev rules (required for non-root HID access)

```
sudo cp udev/99-hyperx-headset.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Run

```
./build/kde-hyperx
```

### Autostart with KDE

```
cp desktop/kde-hyperx-autostart.desktop ~/.config/autostart/
```

## How it works

A background thread polls the Cloud Flight dongle every 30 seconds via a 20-byte HID report (`0x21 0xFF 0x05 ...`). The response carries a big-endian voltage value in bytes 3–4 which is mapped to a percentage through a polynomial curve. Voltages above `0x100B` indicate charging. The headset also pushes 2-byte events for power on/off and mic mute which are handled in real time.

The tray icon is rendered dynamically with QPainter — green above 60%, amber 25–60%, red below 25%, and blue with a lightning bolt overlay when charging.

## License

MIT

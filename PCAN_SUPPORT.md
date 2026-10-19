# PCAN support for Windows

Modified from https://github.com/vedderb/vesc_tool at commit
`f1e15cb1f81203d087599e7fd98ea9c2471a19c6` on 2026-10-19.

Development fork: https://github.com/iotbala/vesc_tool
PCAN work branch: `feature/pcan-support`. The local `origin` remote points to this
fork; `upstream` points to `vedderb/vesc_tool` for tracking upstream changes.

This checkout enables desktop CAN support on Windows and uses Qt Serial Bus's
`peakcan` backend for PEAK PCAN-USB and PCAN-PCI adapters. It retains SocketCAN
for Linux. No serial bridge or firmware change is needed for controllers already
using the VESC CAN protocol.

## Connect

1. Install the [PEAK driver and PCAN-Basic runtime](https://www.peak-system.com/support/downloads/drivers/driver-packages/).
   The application's architecture and `PCANBasic.dll` must match (this build is x64).
2. Connect the adapter and controller CAN-H, CAN-L and reference ground as required
   by their hardware manuals. Terminate the bus with 120 ohms at each end.
3. Configure the controller for VESC CAN mode, note its CAN ID, and use the same
   bitrate in the application. The initial selection is 500 kbit/s.
4. Open **Connection > CAN bus**, select **PEAK PCAN**, and click **Refresh**.
   Qt names the first PCAN USB channel `usb0` (PCAN_USBBUS1), then `usb1`, etc.
5. Select the bitrate and click **Scan**. Select a discovered ID and click the
   connect icon, or enter a known ID from 0 through 253 directly.
6. Use the existing real-time values, configuration, and motor controls. The normal
   reconnect action restores the backend, interface, bitrate and controller ID.

Scanning releases the adapter after discovery. Click Connect to open a controller
session. The application reserves node ID 254 for its replies; controllers on the
bus must use different IDs. CAN FD adapters operate in classic CAN mode here.
Hardware motor operation still needs to be checked against the actual controller.

For SocketCAN, configure and bring up the network interface with `ip link` before
connecting. The bitrate selector is disabled because SocketCAN uses that interface
configuration.

## Build and deploy

Use Qt **5.15.x**, its matching x64 MSVC kit, and Visual Studio's C++ build tools.
Also install [LLVM](https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.8)
and put `clang-cl.exe` on PATH (or pass `-ClangCl`). The bundled ESP flashing C
library requires GNU attributes that the Microsoft C compiler does not support;
the build script uses clang-cl for C and MSVC for C++.
Install Qt Serial Bus, Serial Port, Bluetooth/Connectivity, Gamepad, SVG,
Declarative, Qt Tools (`windeployqt`), Quick Controls 1 and 2, Graphical Effects, and Qt 3D for the embedded
QML views. Qt 6 is not a drop-in replacement for this upstream project.

From PowerShell at the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_pcan_windows.ps1 -QtDir C:\Qt\5.15.2\msvc2019_64
```

The script also accepts `-VcVars64`, `-ClangCl`, `-BuildDir`, and `-PcanBasicDll` overrides.
With no QtDir argument, it uses `QTDIR` or this checkout's `build\qt\5.15.2\msvc2019_64`.
The deployed application is in **build\pcan\windows**. Keep the entire directory
together, including `canbus\qtpeakcanbus.dll`, `Qt5SerialBus.dll`, and the Qt runtime.
PCANBasic may be installed system-wide or supplied alongside the executable.

Windows enables `HAS_CANBUS` by default. `CONFIG+=no_canbus` disables it for builds
without Qt Serial Bus. On Linux use `CONFIG+=canbus`. Static Windows builds also
require Qt's static `qtpeakcanbus` plugin; dynamic Qt is the tested build path.
Firmware images are not bundled (`exclude_fw`); controller communication uses the
configuration definitions included in the upstream source.

## Tests

In a Qt/MSVC developer command prompt, with Qt's `bin` directory on PATH:

```bat
mkdir build\can-tests
cd build\can-tests
qmake ..\..\tests\cantransport\cantransport.pro -config release
nmake
release\tst_cantransport.exe -o results.txt,txt
type results.txt
```

Tests cover sender IDs, short commands, CAN forwarding, fragmentation across the
255-byte offset boundary, replies up to 10,000 bytes, CRC, truncated frames,
wrong recipients and lost fragments. On Windows they also load `peakcan` and
enumerate adapters without opening or transmitting on the bus.

The connection screen reports driver/plugin loading failures and empty adapter
lists. If no adapter appears, check the PEAK driver, matching DLL architecture,
and the `canbus` plugin directory, then Refresh. If no nodes appear, check power,
wiring, termination, CAN mode and bitrate.

Backend reference: [Qt 5.15 PeakCAN documentation](https://doc.qt.io/archives/qt-5.15/qtserialbus-peakcan-overview.html).

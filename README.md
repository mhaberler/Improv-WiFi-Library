# Improv WiFi Library

Improv is a free and open standard with ready-made SDKs that offer a great user experience to configure Wi-Fi on devices. More details [here](https://www.improv-wifi.com/).

This library provides an easier way to handle the Improv serial protocol in your firmware, allowing you to configure the WiFi via web browser as in the following [example](https://jnthas.github.io/improv-wifi-demo/).

Simplest use example:

```cpp
#include <ImprovWiFiLibrary.h>

ImprovWiFi improvSerial(&Serial);

void setup() {
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", "2.1.5", "My Device");
}

void loop() {
  improvSerial.handleSerial();
}
```

## Bluetooth Support (Optional)

You can also use Improv over Bluetooth (uses about 25kb memory). BLE support is **optional** and must be explicitly enabled.

This fork's `ImprovWiFiBLE` is built against the BLE library **bundled with the ESP32 Arduino core** (`framework-arduinoespressif32/libraries/BLE`, a unified Bluedroid/NimBLE wrapper). No external NimBLE-Arduino dependency is required.

### Enabling BLE Support

Add the build flag to your `platformio.ini`:

```ini
build_flags =
    -DIMPROV_WIFI_BLE_ENABLED
```

That's it — no `lib_deps` entry is needed for BLE.

**Note:** BLE works on any ESP32-family SoC with Bluetooth hardware (ESP32, ESP32-C3, ESP32-S3, ESP32-C6, ESP32-C5, ESP32-P4 — anything with `SOC_BLE_SUPPORTED` in the IDF). Chips without a BLE radio (e.g. ESP32-S2, ESP32-C2) are not supported.

### Optional callbacks

- `onImprovError(cb)` — connection / RPC errors.
- `onImprovConnected(cb)` — Wi-Fi join succeeded.
- `onImprovIdentify(cb)` — client asked the device to identify itself (e.g. blink an LED, beep). The capability is advertised by default.
- `setCustomConnectWiFi(cb)` — override the default Wi-Fi connect logic.

### Stopping BLE provisioning

`improvBLE.end()` stops advertising, ends the RPC worker task (after the RPC
in progress) and shuts down the BLE stack. Its heap goes back to the
application; see [Memory footprint](#memory-footprint). `setDeviceInfo()`
starts it again. Call `end()` from `loop()` or `setup()`, not from an Improv
callback.

`end(true)` also asks the core to release the Bluetooth controller memory, so
BLE cannot be restarted until reboot. On the ESP32-C6 this frees no
additional heap.

If the application uses BLE itself (for example scanning), use
`improvBLE.stop()` instead. It stops only Improv: advertising, the RPC worker
and, on Arduino 4, the Improv GATT service. The BLE stack and the
application's scanner keep running, and `setDeviceInfo()` starts Improv again.
On Arduino 3, `stop()` keeps the GATT service registered and reuses it on
restart. Removing a service there makes the library rebuild the GATT table at
the next client disconnect, which crashes while the application scans. See
[Improv next to an application BLE scanner](#improv-next-to-an-application-ble-scanner)
for the rule that applies to both calls.

### Example with BLE enabled:

```cpp
#include <ImprovWiFiBLE.h>

ImprovWiFiBLE improvBLE;

void onIdentify() {
  // Blink an LED, beep, etc.
}

void setup() {
  improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32,
                          "My-Device-9a4c2b", "2.1.5", "My Device");
  improvBLE.onImprovError(/* ... */);
  improvBLE.onImprovConnected(/* ... */);
  improvBLE.onImprovIdentify(onIdentify);    // optional
  improvBLE.setCustomConnectWiFi(/* ... */); // optional
}

void loop() {
}
```

### Configuring a BLE Device

Provisioning happens from a browser over Web Bluetooth. Use this
library's [provisioning page](https://mhaberler.github.io/Improv-WiFi-Library/)
(source in [web/](web/)): click the Bluetooth button, pick the advertised
device, choose a network from the scan list and enter the password. It shows
device info and scanned networks, which the client currently deployed at
[improv-wifi.com](https://www.improv-wifi.com/) does not yet support (there,
SSID must be typed). The same page has a USB serial button for the Serial
transport.

Browsers tested:

- **Chrome on macOS** — works out of the box.
- **Chrome on Android** — works out of the box.
- **iOS / iPadOS** — Safari and Chrome do **not** expose Web Bluetooth.
  Use the [Bluefy browser](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
  and open the provisioning page inside it.

### Using both Serial and BLE:

```cpp
#include <ImprovWiFiLibrary.h>
#ifdef IMPROV_WIFI_BLE_ENABLED
#include <ImprovWiFiBLE.h>
#endif

ImprovWiFi improvSerial(&Serial);
#ifdef IMPROV_WIFI_BLE_ENABLED
ImprovWiFiBLE improvBLE;
#endif

void setup() {
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", "2.1.5", "My Device");
  #ifdef IMPROV_WIFI_BLE_ENABLED
  improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", "2.1.5", "My Device");
  #endif
}

void loop() {
  improvSerial.handleSerial();
}
```

## Memory footprint

Measured with [examples/heap-footprint](examples/heap-footprint) on an
ESP32-C6 (M5Stack NanoC6) and an ESP32-S3 (M5Stack StampS3). Software:
Arduino-ESP32 3.3.12 (pioarduino 55.03.312), library 0.7.0,
`CORE_DEBUG_LEVEL=0`. The sketch starts Wi-Fi (STA, no credentials) and the
selected transports, then reports the heap while idle: provisioning
available, BLE advertising, no client connected. The baseline is the same
sketch without Improv. Free heap is `ESP.getFreeHeap()`, and it stayed stable
while idle. Flash is from the PlatformIO build summary.

| Variant | C6 free heap | C6 Δ heap | C6 Δ flash | S3 free heap | S3 Δ heap | S3 Δ flash |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline (Wi-Fi only) | 321,188 | — | — | 259,960 | — | — |
| Serial only | 320,900 | −288 B | +8.6 KiB | 259,688 | −272 B | +8.1 KiB |
| BLE only | 243,732 | −75.6 KiB | +385 KiB | 153,384 | −104.1 KiB | +229 KiB |
| Serial + BLE | 243,476 | −75.9 KiB | +392.5 KiB | 153,136 | −104.3 KiB | +235.8 KiB |

Free heap is in bytes. Δ is relative to the baseline.

- Almost all of the BLE cost is the Bluetooth stack (NimBLE) and its
  controller, not the Improv code.
- BLE adds an `improv_rpc` worker task (6 KiB stack) and a 2-entry command queue.
- Not measured: peaks while a client is connected or during a Wi-Fi scan.

### Getting the BLE heap back: `end()`

BLE is started (`setDeviceInfo()`), advertises for 3 s, then is stopped with
`end()`. The cycle runs 5 times, followed by a separate `end(true)` run.

| Phase | C6 free heap | S3 free heap |
| --- | ---: | ---: |
| Wi-Fi only build (no BLE code) | 321,188 | 259,960 |
| BLE stack linked, not started yet | 293,348 | 236,424 |
| BLE advertising | 243,476 | 153,140 |
| after `end()` (1st cycle) | 289,620 | 232,844 |
| after `end()` (5th cycle) | 275,432 | 218,656 |
| after `end(true)` | 289,620 | 234,756 |

- **Linked but not started:** as soon as the BLE stack is linked into the
  firmware, part of the heap is gone at boot, even if BLE is never started.
  That is about 27 KiB on the C6 and about 23 KiB on the S3. `end()` doesn't
  give it back.
- **`end()`:** returns almost everything that starting BLE takes, about
  45 of 49 KiB on the C6 and about 78 of 81 KiB on the S3.
- **Repeated cycles:** each start/`end()` cycle loses about 3.5 KiB, on both
  chips. The loss is in the Arduino BLE library's teardown, not in Improv
  code. BLE still works after 5 cycles (checked on the C6).
- **`end(true)`:** additionally releases about 1.9 KiB of controller memory on
  the S3, and nothing on the C6. BLE cannot be restarted afterwards.
- **Net cost of one provisioning window:** after a window started once and
  closed with `end()`, free heap stays below a firmware without BLE by about
  31 KiB on the C6 and about 26.5 KiB on the S3.

### Improv next to an application BLE scanner

The application already runs the BLE stack with a continuous passive scan,
and Improv is started on top. Measured on the ESP32-S3 with
[`scan.cpp`](examples/heap-footprint/src/scan.cpp) (Arduino 3.3.12) and
[`scan_v4.cpp`](examples/heap-footprint/src/scan_v4.cpp) (Arduino 4.0.0-RC1).

| Phase | S3 free heap, Arduino 3.3.12 | S3 free heap, Arduino 4.0.0-RC1 |
| --- | ---: | ---: |
| Wi-Fi only (BLE linked, not started) | 236,672 | 242,328 |
| application scanning | 163,372 | 152,332 |
| scanning + Improv advertising | 150,388 | 141,144 |
| Improv's own cost | 12.7 KiB | 10.9 KiB |

On the ESP32-C6, Improv costs about 13.9 KiB on top of a running scanner, on
both cores, with legacy or extended scanning.

**Start and stop Improv only while no scan is running.** Adding or removing
Improv's GATT service resets NimBLE's GATT table, which fails during a scan
with `assert failed: ble_svc_gap_init` and makes the device reboot in a loop.
`setDeviceInfo()` and `stop()` therefore refuse to run while a scan is active:
they log an error and leave Improv as it was. Pause the scan around the call
and resume it with your own parameters:

```cpp
scan->stop();                      // Arduino 4: scan.stop() / scan.stopExtended()
improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "fw", "1.0", "My Device");  // or improvBLE.stop()
scan->start(0, nullptr, false);    // Arduino 4: scan.start(0) / scan.startExtended(...)
```

- Improv's own cost is only 11 to 13 KiB, because the application already
  pays for the stack. Scanning keeps running alongside Improv, and the BLE
  tests pass while the device scans.
- **Extended (BLE 5) scanning:** Arduino 4's prebuilt NimBLE has extended
  advertising and scanning enabled (`CONFIG_BT_NIMBLE_EXT_ADV=y`,
  `CONFIG_BT_NIMBLE_EXT_SCAN=y`). An extended scan on the Coded and 1M PHYs
  (`scan.startExtended()`) works alongside Improv. Improv's own advertisement
  still reaches Improv clients, with the same heap figures. In Arduino 3.3.12,
  extended advertising is off, and the BLE library offers extended scanning
  only for Bluedroid, so the scan is legacy only.
- **Heap peak:** during a client session with Wi-Fi scan, the lowest free heap
  was about 16 KiB below idle (Arduino 3).
- **Stopping Improv:** `end()` shuts down the whole BLE stack, so it also
  stops the application's scanner. Use `stop()` instead. Measured with
  [`stop_test.cpp`](examples/heap-footprint/src/stop_test.cpp): five
  stop/restart cycles while the application scans, then the BLE tests pass,
  and a client disconnect afterwards doesn't crash the device. This holds on
  both chips and both cores.

| Application scanning | C6, Arduino 3.3.12 | C6, Arduino 4.0.0-RC1 | S3, Arduino 3.3.12 | S3, Arduino 4.0.0-RC1 |
| --- | ---: | ---: | ---: | ---: |
| heap used while Improv runs | 14.0 KiB | 14.0 KiB | 12.1 KiB | 11.9 KiB |
| returned by `stop()` | 7.9 KiB | 11.8 KiB | 7.9 KiB | 9.2 KiB |
| lost per stop/restart cycle | none | ~1.2 KiB | none | ~0.9 KiB |

## Documentation

- Serial transport: [ImprovWiFiLibrary](docs/ImprovWiFiLibrary.md)
- BLE transport: [ImprovWiFiBLE](docs/ImprovWiFiBLE.md)

## License

This open source code is licensed under the MIT license (see [LICENSE](LICENSE)
for details).
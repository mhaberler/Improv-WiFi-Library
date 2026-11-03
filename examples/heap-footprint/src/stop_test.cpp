// ImprovWiFiBLE::stop(): Improv-only stop while the application keeps the BLE
// stack and a scan running (paused around start and stop). Start/stop cycles,
// then Improv is left running.
#include <Arduino.h>
#include <WiFi.h>
#include <ImprovWiFiBLE.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 4
#include <BLE.h>
#else
#include <BLEDevice.h>
#endif

#ifndef CYCLES
#define CYCLES 5
#endif

ImprovWiFiBLE improvBLE;
volatile uint32_t seen = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 4
BLEScan scan;
static void bleBegin() {
    BLE.begin("Heap Test");
    scan = BLE.getScan();
    scan.onResult([](const BLEAdvertisedDevice &) { seen++; });
    scan.setActiveScan(false);
    scan.setInterval(100);
    scan.setWindow(99);
}
static void scanStart() { scan.start(0); }
static void scanStop() { scan.stop(); }
static bool scanning() { return scan.isScanning(); }
static void scanClear() { scan.clearResults(); }
#else
BLEScan *scan;
class Counter : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice) override { seen++; }
};
static void bleBegin() {
    BLEDevice::init("Heap Test");
    scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new Counter(), true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
}
static void scanStart() { scan->start(0, nullptr, false); }
static void scanStop() { scan->stop(); }
static bool scanning() { return scan->isScanning(); }
static void scanClear() { scan->clearResults(); }
#endif

static void report(const char *phase, int cycle) {
    static uint32_t last = 0;
    scanClear();
    delay(2000);
    uint32_t n = seen;
    Serial.printf("PHASE %-9s cycle=%d free=%u min=%u maxblock=%u adv_seen=%u scanning=%d "
                  "improv=%d\n",
                  phase, cycle, ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  n - last, scanning(), improvBLE.isAdvertising());
    last = n;
}

static void improvStart() {
    scanStop(); // setDeviceInfo() refuses to start while scanning
    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
    scanStart();
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    delay(6000);
    bleBegin();
    scanStart();
    report("scanning", 0);
    for (int i = 1; i <= CYCLES; i++) {
        improvStart();
        report("improv", i);
        improvBLE.stop(); // refused while scanning (would assert in NimBLE)
        Serial.printf("PHASE %-9s cycle=%d improv=%d (expect 1)\n", "guard", i,
                      improvBLE.isAdvertising());
        scanStop();
        improvBLE.stop();
        scanStart();
        report("stopped", i);
    }
    improvStart(); // left running for a functional check
    report("restarted", CYCLES + 1);
    Serial.println("DONE");
}

void loop() {}

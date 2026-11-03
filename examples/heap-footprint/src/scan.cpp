// Improv's own heap cost when the application already runs the BLE stack
// (continuous passive scan), and whether scanning keeps working alongside it.
#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <ImprovWiFiBLE.h>

ImprovWiFiBLE improvBLE;
BLEScan *scan;
volatile uint32_t seen = 0;

class Counter : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice) override { seen++; }
};

// clearResults() first: the scan result list grows with every new device
static void report(const char *phase) {
    static uint32_t last = 0;
    scan->clearResults();
    delay(500);
    uint32_t n = seen;
    Serial.printf("PHASE %-12s free=%u min=%u maxblock=%u adv_seen_since_last=%u\n", phase,
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), n - last);
    last = n;
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    delay(6000);
    Serial.printf("PHASE %-12s free=%u min=%u maxblock=%u\n", "wifi-only", ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

    BLEDevice::init("Heap Test");
    scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new Counter(), true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false); // continuous, non-blocking
    delay(5000);
    report("scanning");
    delay(5000);
    report("scanning");

    // While a scan is active, setDeviceInfo() refuses to start (creating the
    // GATT server would assert in NimBLE): isAdvertising() stays false.
    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
    Serial.printf("PHASE %-12s advertising=%d (expect 0)\n", "guard", improvBLE.isAdvertising());

    // Pause the scan around the start, then resume with your own parameters.
    scan->stop();
    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
    scan->start(0, nullptr, false);
    delay(5000);
    report("scan+improv");
    delay(5000);
    report("scan+improv");
    Serial.println("DONE");
}

void loop() {
    delay(5000);
    report("scan+improv");
}

// Arduino-ESP32 4.x: Improv next to an application BLE scan (legacy, or BLE5
// extended with -DEXT_SCAN). setDeviceInfo() must not run while scanning:
// it refuses (guard), so the scan is paused around it.
#include <Arduino.h>
#include <WiFi.h>
#include <BLE.h>
#include <ImprovWiFiBLE.h>

ImprovWiFiBLE improvBLE;
BLEScan scan;
volatile uint32_t seen = 0;

static void report(const char *phase) {
    static uint32_t last = 0;
    scan.clearResults();
    delay(500);
    uint32_t n = seen;
    Serial.printf("PHASE %-12s free=%u min=%u maxblock=%u adv_seen_since_last=%u scanning=%d\n",
                  phase, ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), n - last,
                  scan.isScanning());
    last = n;
}

static BTStatus startScan() {
#ifdef EXT_SCAN
    static BLEScan::ExtScanConfig coded, uncoded;
    coded.phy = BLEPhy::PHY_Coded;
    uncoded.phy = BLEPhy::PHY_1M;
    return scan.startExtended(0, &coded, &uncoded);
#else
    return scan.start(0);
#endif
}

static void stopScan() {
#ifdef EXT_SCAN
    scan.stopExtended();
#else
    scan.stop();
#endif
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    delay(6000);
    Serial.printf("PHASE %-12s free=%u min=%u maxblock=%u\n", "wifi-only", ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

    BTStatus st = BLE.begin("Heap Test");
    Serial.printf("PHASE %-12s %s\n", "ble-begin", st.toString());
    scan = BLE.getScan();
    scan.onResult([](const BLEAdvertisedDevice &) { seen++; });
    scan.setActiveScan(false);
    scan.setInterval(100);
    scan.setWindow(99);
    st = startScan();
    Serial.printf("PHASE %-12s %s\n", "scan", st.toString());
    delay(5000);
    report("scanning");
    delay(5000);
    report("scanning");

    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
    Serial.printf("PHASE %-12s advertising=%d (expect 0)\n", "guard", improvBLE.isAdvertising());

    stopScan();
    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
    st = startScan();
    Serial.printf("PHASE %-12s advertising=%d rescan=%s\n", "improv", improvBLE.isAdvertising(),
                  st.toString());
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

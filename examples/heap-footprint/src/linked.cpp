// Heap at boot with the BLE stack linked but never started, and whether
// Arduino's btMemRelease() gives anything back.
#include <Arduino.h>
#include <WiFi.h>
#include <ImprovWiFiBLE.h>
#include <esp32-hal-bt.h>

ImprovWiFiBLE improvBLE;
volatile bool never = false; // links the BLE stack without starting it

static void report(const char *phase) {
    delay(500);
    Serial.printf("PHASE %-12s free=%u maxblock=%u released=%d\n", phase, ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap(), btMemReleased(BT_MODE_BLE));
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    delay(6000);
    if (never) improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "x", "1", "x");
    report("before");
    bool ok = btMemRelease(BT_MODE_BLE);
    Serial.printf("PHASE btMemRelease ok=%d\n", ok);
    report("after");
    Serial.println("DONE");
}
void loop() {}

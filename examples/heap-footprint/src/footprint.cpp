// Heap footprint of the Improv transports (idle: provisioning available,
// BLE advertising, no client connected, no Wi-Fi credentials).
#include <Arduino.h>
#include <WiFi.h>
#include <ImprovTypes.h>
#ifdef USE_SERIAL
#include <ImprovWiFiLibrary.h>
ImprovWiFi improvSerial(&Serial);
#endif
#ifdef USE_BLE
#include <ImprovWiFiBLE.h>
ImprovWiFiBLE improvBLE;
#endif

#if defined(USE_SERIAL) && defined(USE_BLE)
#define VARIANT "serial+ble"
#elif defined(USE_SERIAL)
#define VARIANT "serial"
#elif defined(USE_BLE)
#define VARIANT "ble"
#else
#define VARIANT "baseline"
#endif

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA); // Wi-Fi stack up in every variant
#ifdef USE_SERIAL
    improvSerial.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
#endif
#ifdef USE_BLE
    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
#endif
}

void loop() {
#ifdef USE_SERIAL
    improvSerial.handleSerial();
#endif
    static uint32_t last = 0;
    if (millis() > 5000 && millis() - last >= 2000) {
        last = millis();
        Serial.printf("HEAP variant=%s free=%u min=%u maxblock=%u\n", VARIANT,
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }
}

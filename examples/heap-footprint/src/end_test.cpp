// ImprovWiFiBLE::end(): how much heap comes back, and does it leak over cycles.
#include <Arduino.h>
#include <WiFi.h>
#include <ImprovWiFiBLE.h>

ImprovWiFiBLE improvBLE;

static void report(const char *phase, int cycle) {
    delay(500); // let freed task stacks be reclaimed by the idle task
    Serial.printf("PHASE %-10s cycle=%d free=%u min=%u maxblock=%u\n", phase, cycle,
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
}

static void start() {
    improvBLE.setDeviceInfo(IMPROV_CHIP_FAMILY, "heap-test", "1.0", "Heap Test");
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    delay(6000); // USB serial re-enumeration after flashing
    report("before", 0);
    for (int i = 1; i <= CYCLES; i++) {
        start();
        delay(3000);
        report("running", i);
#ifdef RELEASE_MEMORY
        improvBLE.end(true);
#else
        improvBLE.end();
#endif
        report("ended", i);
    }
#ifndef RELEASE_MEMORY
    start(); // leave advertising for a functional check
    report("restarted", CYCLES + 1);
#endif
    Serial.println("DONE");
}

void loop() {}

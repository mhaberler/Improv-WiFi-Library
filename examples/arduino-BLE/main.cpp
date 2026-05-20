#include <WiFi.h>
#include <Esp.h>
#include <ImprovWiFiBLE.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "credstore.hpp"
#include "OneButton.h"

WiFiServer server(80);
ImprovWiFiBLE improvBLE;

char linebuf[80];
int charcount = 0;
void blink_led(int d, int times);
void handleHttpRequest();
void button_setup();
void button_loop();

void onImprovWiFiErrorCb(ImprovTypes::Error err) {
    server.stop();
    blink_led(2000, 3);
}

void onImprovWiFiConnectedCb(const char *ssid, const char *password) {
    // Save ssid and password here
    log_i("wifi connected %s %s", ssid, password);
    server.begin();
    blink_led(100, 3);
}

void onImprovWiFiIdentifyCb() {
    // Visible/audible identification of this device.
    log_i("identify received");
    blink_led(80, 10);
}

bool connectWifi(const char *ssid, const char *password) {
    log_i("wifi connecting:  %s %s", ssid, password);

    WiFi.begin(ssid, password);

    while (!improvBLE.isConnected()) {
        blink_led(500, 1);
    }
    saveWiFiCredentials( ssid, password);
    return true;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BLUE, OUTPUT);
    button_setup();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    String savedSSID, savedPASS;
    if (loadWiFiCredentials(savedSSID, savedPASS)) {
        log_w("loaded creds: %s %s ", savedSSID.c_str(), savedPASS.c_str());
        if (savedSSID == "") {
            Serial.println("No Wi-Fi credentials found in Preferences.");
            WiFi.begin();
        } else {
            WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
        }
    } else {
        log_i("No Wi-Fi credentials found in Preferences.");
        WiFi.begin();
    }

    improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", "2.1.5", "My Device");
    improvBLE.onImprovError(onImprovWiFiErrorCb);
    improvBLE.onImprovConnected(onImprovWiFiConnectedCb);
    improvBLE.onImprovIdentify(onImprovWiFiIdentifyCb);  // Optional
    improvBLE.setCustomConnectWiFi(connectWifi);  // Optional

    blink_led(100, 5);
}

void loop() {
    button_loop();

    if (improvBLE.isConnected()) {
        handleHttpRequest();
    }
}

void handleHttpRequest() {

    WiFiClient client = server.available();
    if (client) {
        blink_led(100, 1);
        memset(linebuf, 0, sizeof(linebuf));
        charcount = 0;
        // an http request ends with a blank line
        boolean currentLineIsBlank = true;
        while (client.connected()) {
            if (client.available()) {
                char c = client.read();
                // read char by char HTTP request
                linebuf[charcount] = c;
                if (charcount < sizeof(linebuf) - 1)
                    charcount++;

                if (c == '\n' && currentLineIsBlank) {
                    // send a standard http response header
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connection: close"); // the connection will be closed after completion of the response
                    client.println();
                    client.println("<!DOCTYPE HTML><html><body>");
                    client.println("<h1 id=\"welcome\">Welcome!</h1>");
                    client.println("<p>This is a simple webpage served by your ESP32</p>");
                    client.println("<h3>Chip Info</h3>");
                    client.println("<ul><li>Model:");
                    client.println(ESP.getChipModel());
                    client.println("</li><li>Cores: ");
                    client.println(ESP.getChipCores());
                    client.println("</li><li>Revision: ");
                    client.println(ESP.getChipRevision());
                    client.println("</li></ul>");
                    client.println("<h3>Network Info</h3>");
                    client.println("<ul><li>SSID: ");
                    client.println(WiFi.SSID());
                    client.println("</li><li>IP Address: ");
                    client.println(WiFi.localIP());
                    client.println("</li><li>MAC Address: ");
                    client.println(WiFi.macAddress());
                    client.println("</li></ul>");
                    client.println("<script>const params = new URLSearchParams(document.location.search);document.getElementById('welcome').innerHTML = 'Welcome' + (params.get(\"name\") ? ', ' + params.get('name') : '') + '!';</script>");
                    client.println("</body></html>");
                    break;
                }
            }
        }
        delay(1);
        client.stop();
    }
}

void blink_led(int d, int times) {
    for (int j = 0; j < times; j++) {
        digitalWrite(LED_BLUE, HIGH);
        delay(d);
        digitalWrite(LED_BLUE, LOW);
        delay(d);
    }
}

#if defined(BUTTON_PIN)

uint32_t numClicks;

OneButton button(BUTTON_PIN, true,
                 true); // Button pin, active low, pullup enabled

void singleClick() {
    log_i("singleClick() detected.");
    numClicks = 1;
}

void doubleClick() {
    log_i("doubleClick() detected.");
    numClicks = 2;
}

void multiClick() {
    int n = button.getNumberClicks();
    log_i("%d clicks detected.", n);
    if (n == 5) {
        digitalWrite(LED_BLUE, HIGH);

        clearWiFiCredentials();
        WiFi.disconnect(true, true);
        delay(100);
        ESP.restart();
    }
    numClicks = n;
}

#endif

void button_setup(void) {
#if defined(BUTTON_PIN)
    button.attachClick(singleClick);
    button.attachDoubleClick(doubleClick);
    button.attachMultiClick(multiClick);
#endif
}

void button_loop(void) {
#if defined(BUTTON_PIN)
    button.tick();
#endif
}

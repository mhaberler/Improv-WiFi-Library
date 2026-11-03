// Only compile BLE support if explicitly enabled
#ifdef IMPROV_WIFI_BLE_ENABLED

#include "ImprovWiFiBLE.h"

#include <algorithm>

#if defined(ARDUINO_ARCH_ESP8266)
    #include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32)
    #include <WiFi.h>
#endif

// BLE objects are raw pointers (3.x) or value handles (4.x)
template <class T> static T &deref(T &t) { return t; }
template <class T> static T &deref(T *t) { return *t; }

// ==== ctor/dtor ====
ImprovWiFiBLE::~ImprovWiFiBLE() {
    // Minimal cleanup; BLEDevice::deinit() can be called by app if desired
}

// ==== public API (matching ImprovWiFi) ====

void ImprovWiFiBLE::setDeviceInfo(ImprovTypes::ChipFamily chipFamily,
                                  const char *firmwareName,
                                  const char *firmwareVersion,
                                  const char *deviceName,
                                  const char *deviceUrl) {
    if (server_) return; // already initialized
    chip_ = chipFamily;
    firmware_name_ = firmwareName ? firmwareName : "";
    firmware_version_ = firmwareVersion ? firmwareVersion : "";
    device_name_ = deviceName ? deviceName : "";
    device_friendly_name_ = deviceName ? deviceName : "";
    device_url_ = deviceUrl ? deviceUrl : "";

    if (!rpc_queue_) {
        rpc_queue_ = xQueueCreate(2, sizeof(RpcPacket));
        if (!rpc_queue_ ||
            xTaskCreate(rpcTask, "improv_rpc", 6144, this, 1, &rpc_task_) != pdPASS) {
            log_e("failed to start Improv RPC task");
            return;
        }
    }

#ifdef IMPROV_BLE_V4
    BTStatus st = BLE.begin(device_name_);
    if (!st) {
        log_e("BLE.begin failed: %s", st.toString());
        return;
    }
    BLE.setPower(9);

    server_ = BLE.createServer();
    server_.advertiseOnDisconnect(false); // advertiseNow() restarts with current state
    server_.onDisconnect([this](const BLEServer &, const BLEConnInfo &, uint8_t) {
        rx_.clear();
        if (adv_) {
            advertiseNow();
        }
    });

    service_ = server_.createService(BLEUUID(SVC_UUID));

    ch_state_ = service_.createCharacteristic(
                    BLEUUID(CHAR_STATE_UUID), BLEProperty::Read | BLEProperty::Notify, BLEPermission::ReadOpen);
    ch_error_ = service_.createCharacteristic(
                    BLEUUID(CHAR_ERROR_UUID), BLEProperty::Read | BLEProperty::Notify, BLEPermission::ReadOpen);
    ch_rpc_cmd_ = service_.createCharacteristic(
                      BLEUUID(CHAR_RPC_CMD_UUID), BLEProperty::Write | BLEProperty::WriteNR, BLEPermission::WriteOpen);
    ch_rpc_res_ = service_.createCharacteristic(
                      BLEUUID(CHAR_RPC_RES_UUID), BLEProperty::Read | BLEProperty::Notify, BLEPermission::ReadOpen);
    ch_caps_ = service_.createCharacteristic(
                   BLEUUID(CHAR_CAPS_UUID), BLEProperty::Read, BLEPermission::ReadOpen);

    ch_rpc_cmd_.onWrite([this](const BLECharacteristic &c, const BLEConnInfo &conn) {
        peer_mtu_ = conn.getMTU();
        size_t len = 0;
        const uint8_t *v = c.getValue(&len);
        onRpcWrite(v, len);
    });

    ch_caps_.setValue(&caps_, 1);
    ch_error_.setValue(&error_, 1);
    ch_state_.setValue(&state_, 1);

    st = server_.start();
    if (!st) {
        log_e("BLE server start failed: %s", st.toString());
        return;
    }

    adv_ = BLE.getAdvertising();
    adv_.setAdvertisementData(buildAdvData(state_, caps_));
    if (!device_name_.isEmpty()) {
        BLEAdvertisementData scan;
        scan.setName(device_name_);
        adv_.setScanResponseData(scan);
    }
#else
    // Initialize BLE and set up the Improv service
    BLEDevice::init(device_name_.c_str());
#if defined(ESP_PWR_LVL_P9)
    BLEDevice::setPower(ESP_PWR_LVL_P9);
#endif

#if defined(CONFIG_NIMBLE_ENABLED)
    // Prefer a robust address type on ESP32
    BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_RANDOM_DEFAULT);
#endif

    server_ = BLEDevice::createServer();
    server_->setCallbacks(this);

    service_ = server_->createService(SVC_UUID);

    ch_state_ = service_->createCharacteristic(
                    CHAR_STATE_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    ch_error_ = service_->createCharacteristic(
                    CHAR_ERROR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    ch_rpc_cmd_ = service_->createCharacteristic(
                      CHAR_RPC_CMD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    ch_rpc_res_ = service_->createCharacteristic(
                      CHAR_RPC_RES_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    ch_caps_ =
        service_->createCharacteristic(CHAR_CAPS_UUID, BLECharacteristic::PROPERTY_READ);

    ch_rpc_cmd_->setCallbacks(this);

    // Seed characteristic values WITHOUT notify (no subscribers yet,
    // service not started — notify() would assert/null-deref).
    ch_caps_->setValue(&caps_, 1);
    ch_error_->setValue(&error_, 1);
    ch_state_->setValue(&state_, 1);

    service_->start();

    adv_ = BLEDevice::getAdvertising();

    // Build primary advertisement (UUID + Service Data + maybe short name)
    BLEAdvertisementData advData = buildAdvData(state_, caps_);
    adv_->setAdvertisementData(advData);

    // Optional: put the full name in the scan response (keeps primary ADV small)
    if (!device_name_.isEmpty()) {
        BLEAdvertisementData scan;
        scan.setName(device_name_.c_str());
        adv_->setScanResponseData(scan);
    }
#endif

    advertiseNow();
}

void ImprovWiFiBLE::setDeviceInfo(ImprovTypes::ChipFamily chipFamily,
                                  const char *firmwareName,
                                  const char *firmwareVersion,
                                  const char *deviceName) {
    setDeviceInfo(chipFamily, firmwareName, firmwareVersion, deviceName, nullptr);
}

void ImprovWiFiBLE::onImprovError(OnImprovError *errorCallback) {
    onImprovErrorCallback_ = errorCallback;
}

void ImprovWiFiBLE::onImprovConnected(OnImprovConnected *connectedCallback) {
    onImprovConnectedCallback_ = connectedCallback;
}

void ImprovWiFiBLE::onImprovIdentify(OnImprovIdentify *identifyCallback) {
    onImprovIdentifyCallback_ = identifyCallback;
}

void ImprovWiFiBLE::setCustomConnectWiFi(
    CustomConnectWiFi *connectWiFiCallBack) {
    customConnectWiFiCallback_ = connectWiFiCallBack;
}

bool ImprovWiFiBLE::tryConnectToWifi(const char *ssid, const char *password) {
    // Defaults match simple behavior of the serial variant
    return tryConnectToWifi(ssid, password, /*delayMs*/ 500, /*maxAttempts*/ 20);
}

bool ImprovWiFiBLE::tryConnectToWifi(const char *ssid, const char *password,
                                     uint32_t delayMs, uint8_t maxAttempts) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    WiFi.begin(ssid, password);

    for (uint8_t i = 0; i < maxAttempts; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        delay(delayMs);
    }
    return (WiFi.status() == WL_CONNECTED);
}

void ImprovWiFiBLE::end(bool releaseMemory) {
    if (rpc_task_ && xTaskGetCurrentTaskHandle() == rpc_task_) {
        log_e("end() called from an Improv callback; call it from loop()");
        return;
    }
    if (adv_) {
        deref(adv_).stop();
    }
    // Stop the worker: it finishes the RPC in progress, then exits.
    if (rpc_task_) {
        static RpcPacket stop;
        stop.len = RPC_STOP;
        xQueueSend(rpc_queue_, &stop, portMAX_DELAY);
        while (rpc_task_) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (server_) {
#ifdef IMPROV_BLE_V4
        BLE.end(releaseMemory);
        server_ = BLEServer();
        service_ = BLEService();
        ch_state_ = BLECharacteristic();
        ch_error_ = BLECharacteristic();
        ch_rpc_cmd_ = BLECharacteristic();
        ch_rpc_res_ = BLECharacteristic();
        ch_caps_ = BLECharacteristic();
        adv_ = BLEAdvertising();
#else
        BLEDevice::deinit(releaseMemory);
        server_ = nullptr;
        service_ = nullptr;
        ch_state_ = ch_error_ = ch_rpc_cmd_ = ch_rpc_res_ = ch_caps_ = nullptr;
        adv_ = nullptr;
#endif
    }
    if (rpc_queue_) {
        vQueueDelete(rpc_queue_);
        rpc_queue_ = nullptr;
    }
    std::vector<uint8_t>().swap(rx_);
    state_ = STATE_AUTHORIZED;
    error_ = ERR_NONE;
}

bool ImprovWiFiBLE::isAdvertising() {
    return (server_ && service_ && adv_); // espressif Nimble in Arduino lacks isAdvertising()
}

bool ImprovWiFiBLE::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

#ifndef IMPROV_BLE_V4
// ==== BLE callbacks ====

void ImprovWiFiBLE::onDisconnect(BLEServer *) {
    rx_.clear();
    if (adv_) {
        advertiseNow();
    }
}

void ImprovWiFiBLE::onWrite(BLECharacteristic *c) {
    if (c != ch_rpc_cmd_)
        return;
    const String v = c->getValue();
    onRpcWrite(reinterpret_cast<const uint8_t *>(v.c_str()), v.length());
}
#endif

void ImprovWiFiBLE::onRpcWrite(const uint8_t *data, size_t len) {
    // a stale partial command is dropped after 1 s of silence
    if (!rx_.empty() && millis() - rx_last_ms_ > 1000) {
        rx_.clear();
    }
    rx_last_ms_ = millis();
    if (data && len) {
        rx_.insert(rx_.end(), data, data + len);
    }

    if (rx_.size() < 3) {
        if (rx_.empty()) {
            reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        }
        return; // wait for more
    }
    const size_t expected = rx_[1] + 3; // cmd + len + payload + checksum
    if (rx_.size() < expected) {
        return; // wait for more
    }
    // size mismatch -> bad packet, reported by handleRpc
    static RpcPacket pkt; // only touched from the BLE callback context
    pkt.len = std::min(rx_.size(), sizeof(pkt.data));
    memcpy(pkt.data, rx_.data(), pkt.len);
    if (rx_.size() > sizeof(pkt.data)) {
        pkt.len = 0; // can't be valid (len byte max 255)
    }
    rx_.clear();
    if (!rpc_queue_ || xQueueSend(rpc_queue_, &pkt, 0) != pdTRUE) {
        log_w("RPC dropped, worker busy");
    }
}

void ImprovWiFiBLE::rpcTask(void *arg) {
    auto *self = static_cast<ImprovWiFiBLE *>(arg);
    RpcPacket pkt;
    for (;;) {
        if (xQueueReceive(self->rpc_queue_, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (pkt.len == RPC_STOP) {
            self->rpc_task_ = nullptr; // signals end() that we're done
            vTaskDelete(nullptr);
        }
        if (pkt.len < 3) {
            self->reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
            continue;
        }
        self->handleRpc(pkt.data, pkt.len);
    }
}

// ==== internal helpers / RPCs ====

void ImprovWiFiBLE::handleRpc(const uint8_t *data, size_t len) {
    const uint8_t cmd = data[0];
    const uint8_t declared_len = data[1];

    if (declared_len + 3 != len) {
        reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        return;
    }

    const uint8_t cs = data[len - 1];
    if (checksumLSB(data, len - 1) != cs) {
        reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        return;
    }

    switch (cmd) {
        case 0x01: // Send Wi-Fi
            rpcSendWifi(&data[2], declared_len);
            break;
        case 0x02: // Identify
            rpcIdentify();
            break;
        case 0x03: // Device info
            rpcDeviceInfo();
            break;
        case 0x04: // Scan Wi-Fi networks
            rpcScanNetworks();
            break;
        default:
            reportError(ERR_UNKNOWN_CMD, ImprovTypes::Error::ERROR_INVALID_RPC);
            break;
    }
}

void ImprovWiFiBLE::rpcSendWifi(const uint8_t *p, size_t n) {
    if (n < 2) {
        reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        return;
    }

    // Parse SSID
    const uint8_t ssid_len = p[0];
    if (1 + ssid_len > n) {
        reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        return;
    }
    String ssid;
    ssid.reserve(ssid_len);
    for (uint8_t i = 0; i < ssid_len; i++)
        ssid += (char)p[1 + i];

    // Parse password
    size_t pos = 1 + ssid_len;
    if (pos >= n) {
        reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        return;
    }
    const uint8_t pass_len = p[pos];
    if (pos + 1 + pass_len > n) {
        reportError(ERR_BAD_PACKET, ImprovTypes::Error::ERROR_INVALID_RPC);
        return;
    }
    String pass;
    pass.reserve(pass_len);
    for (uint8_t i = 0; i < pass_len; i++)
        pass += (char)p[pos + 1 + i];

    updateState(STATE_PROVISIONING);

    // Attempt connection (custom callback first, like ImprovWiFi)
    bool ok = false;
    if (customConnectWiFiCallback_) {
        ok = customConnectWiFiCallback_(ssid.c_str(), pass.c_str());
    } else {
        ok = tryConnectToWifi(ssid.c_str(), pass.c_str());
    }

    if (!ok) {
        reportError(ERR_CONNECT, ImprovTypes::Error::ERROR_UNABLE_TO_CONNECT);
        // Return to ready so clients can try again
        updateState(STATE_AUTHORIZED);
        advertiseNow();
        return;
    }

    // Success path
    if (onImprovConnectedCallback_) {
        onImprovConnectedCallback_(ssid.c_str(), pass.c_str());
    }

    // Send device URL BEFORE the PROVISIONED state: sdk-ble-js re-renders its
    // success screen on the state change only, so a URL arriving afterwards
    // never shows up as the "Next" button.
    sendDeviceUrl();

    updateState(STATE_PROVISIONED);
    advertiseNow();

    delay(250); // allow clients to read updated chars
}

void ImprovWiFiBLE::rpcIdentify() {
    if (onImprovIdentifyCallback_)
        onImprovIdentifyCallback_();
}

void ImprovWiFiBLE::rpcDeviceInfo() {
    sendRpcResult(0x03, {firmware_name_, firmware_version_,
                         ImprovTypes::CHIP_FAMILY_DESC[chip_], device_name_});
}

static const char *authName(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:
        case WIFI_AUTH_OWE:
            return "NO";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2 EAP";
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI";
        default:
            return "WPA2"; // some password-protected mode
    }
}

void ImprovWiFiBLE::rpcScanNetworks() {
    // Blocking scan (~2-4 s); runs on the RPC worker task.
    int n = WiFi.scanNetworks();
    if (n == WIFI_SCAN_FAILED) {
        // driver refuses to scan while a connect attempt is in progress;
        // abort it (keeps STA on and stored config) and retry once
        log_d("scan failed, aborting pending connect and retrying");
        WiFi.disconnect(false);
        n = WiFi.scanNetworks();
    }
    if (n < 0) {
        n = 0;
    }

    // strongest first, one entry per SSID, hidden networks skipped
    std::vector<int> order;
    for (int i = 0; i < n; i++) {
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(),
              [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

    // BLE scan result is ONE response: (ssid, rssi, auth) triples.
    // Must fit the length byte and a single notification.
    const size_t mtu_payload = peerMtu() - 3 - 3; // ATT header, cmd+len+checksum
    const size_t budget = std::min<size_t>(255, mtu_payload);
    std::vector<String> strings;
    size_t used = 0;
    for (int i : order) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) {
            continue;
        }
        bool dup = false;
        for (size_t k = 0; k < strings.size(); k += 3) {
            if (strings[k] == ssid) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        const String rssi = String(WiFi.RSSI(i));
        const char *auth = authName(WiFi.encryptionType(i));
        const size_t need = 3 + ssid.length() + rssi.length() + strlen(auth);
        if (used + need > budget) {
            continue; // a shorter SSID may still fit
        }
        used += need;
        strings.push_back(ssid);
        strings.push_back(rssi);
        strings.push_back(auth);
    }
    WiFi.scanDelete();
    log_d("scan: %d found, %u sent, %u/%u bytes", n, strings.size() / 3, used, budget);
    sendRpcResult(0x04, strings);
}

void ImprovWiFiBLE::sendDeviceUrl() {
    // Same as the serial transport: default to http://<IP>, else fill in
    // {LOCAL_IPV4}. Works on a copy so the template survives re-provisioning.
    const String ip = WiFi.localIP().toString();
    String url = device_url_;
    if (url.isEmpty()) {
        url = "http://" + ip;
    } else {
        url.replace("{LOCAL_IPV4}", ip);
    }
    sendRpcResult(0x01, {url});
}

uint16_t ImprovWiFiBLE::peerMtu() {
#ifdef IMPROV_BLE_V4
    return peer_mtu_;
#else
    return server_ ? server_->getPeerMTU(server_->getConnId()) : 23;
#endif
}

void ImprovWiFiBLE::sendRpcResult(uint8_t cmd, const std::vector<String> &strings) {
    // RPC result framing: [cmd][len][str_len][str...]...[checksum]
    std::vector<uint8_t> buf;
    buf.push_back(cmd);
    buf.push_back(0); // payload length, patched below
    for (const String &str : strings) {
        const uint8_t l = static_cast<uint8_t>(str.length());
        buf.push_back(l);
        buf.insert(buf.end(), str.c_str(), str.c_str() + l);
    }
    buf[1] = static_cast<uint8_t>(buf.size() - 2);
    buf.push_back(checksumLSB(buf.data(), buf.size()));

    if (buf.size() > peerMtu() - 3U) {
        log_w("RPC result %u bytes exceeds MTU %u, client may truncate", buf.size(), peerMtu());
    }
    deref(ch_rpc_res_).setValue(buf.data(), buf.size());
    deref(ch_rpc_res_).notify();
}

void ImprovWiFiBLE::updateState(uint8_t s) {
    state_ = s;
    if (ch_state_) {
        deref(ch_state_).setValue(&state_, 1);
        deref(ch_state_).notify();
    }
}

void ImprovWiFiBLE::updateError(uint8_t e) {
    error_ = e;
    if (ch_error_) {
        deref(ch_error_).setValue(&error_, 1);
        deref(ch_error_).notify();
    }
}

void ImprovWiFiBLE::updateCaps(uint8_t caps) {
    caps_ = caps;
    if (ch_caps_) {
        deref(ch_caps_).setValue(&caps_, 1);
    }
}

void ImprovWiFiBLE::reportError(uint8_t bleErr, ImprovTypes::Error apiErr) {
    updateError(bleErr);
    if (onImprovErrorCallback_)
        onImprovErrorCallback_(apiErr);
}

BLEAdvertisementData ImprovWiFiBLE::buildAdvData(uint8_t state,
        uint8_t caps) {
    BLEAdvertisementData ad;

#ifdef IMPROV_BLE_V4
    // Flags(3) + 128-bit service UUID(18) + service data 0x4677(10) = 31 bytes,
    // the legacy ADV limit: no room for a name (it goes in the scan response).
    ad.setFlags(BLEAdvFlag::GeneralDisc | BLEAdvFlag::BrEdrNotSupported);
    ad.addServiceUUID(BLEUUID(SVC_UUID));
    // Improv spec: service data 0x4677 = [state, caps, 0, 0, 0, 0]
    const uint8_t payload[6] = {state, caps, 0x00, 0x00, 0x00, 0x00};
    ad.setServiceData(BLEUUID((uint16_t)SERVICE_DATA_UUID_16), payload,
                      sizeof(payload));
    return ad;
#else

    // Flags: LE General Discoverable + BR/EDR not supported
    ad.setFlags(ESP_BLE_ADV_FLAG_BREDR_NOT_SPT | ESP_BLE_ADV_FLAG_GEN_DISC);

    // REQUIRED: 128-bit Improv Service UUID in the same primary ADV as Service
    // Data (not scan response)
    ad.setCompleteServices(BLEUUID(SVC_UUID));

    // Improv spec: service data 0x4677 = [state, caps, 0, 0, 0, 0].
    // setServiceData() writes the UUID itself. Flags(3) + 128-bit UUID(18) +
    // service data(10) = 31 bytes, the legacy ADV limit: no room for a name
    // (it goes in the scan response).
    uint8_t payload[6] = {state, caps, 0x00, 0x00, 0x00, 0x00};
    ad.setServiceData(BLEUUID((uint16_t)SERVICE_DATA_UUID_16),
                      String((const char *)payload, sizeof(payload)));

    return ad;
#endif
}

void ImprovWiFiBLE::advertiseNow() {
    if (!adv_)
        return;
    deref(adv_).stop();

    // Rebuild adv data with current state/caps; preserve scan response (name)
    BLEAdvertisementData advData = buildAdvData(state_, caps_);
    deref(adv_).setAdvertisementData(advData);

    deref(adv_).start();
}

uint8_t ImprovWiFiBLE::checksumLSB(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += data[i];
    return static_cast<uint8_t>(sum & 0xFF);
}

#endif // IMPROV_WIFI_BLE_ENABLED

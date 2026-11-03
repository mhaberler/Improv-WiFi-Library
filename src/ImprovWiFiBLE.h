#pragma once

// Only compile BLE support if explicitly enabled
#ifdef IMPROV_WIFI_BLE_ENABLED

#include "ImprovTypes.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// Arduino-ESP32 4.x replaced the BLEDevice API with the handle/lambda based
// BLE API (#include <BLE.h>)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 4
#define IMPROV_BLE_V4 1
#include <BLE.h>
#else
#include <BLEDevice.h>
#endif

/**
 * Improv WiFi BLE class
 *
 * Public API matches ImprovWiFi (Serial) so both transports can be used
 * interchangeably from the sketch.
 *
 * Usage example:
 *   ImprovWiFiBLE improvBLE;
 *   improvBLE.setDeviceInfo(...);
 *   improvBLE.onImprovError(...);
 *   improvBLE.onImprovConnected(...);
 *   improvBLE.onImprovIdentify(...);
 *   improvBLE.setCustomConnectWiFi(...);
 */
#ifdef IMPROV_BLE_V4
class ImprovWiFiBLE {
#else
class ImprovWiFiBLE : public BLECharacteristicCallbacks,
                      public BLEServerCallbacks {
#endif
public:
  /**
   * ## Type definitions (same as ImprovWiFi)
   */
  typedef void(OnImprovError)(ImprovTypes::Error);
  typedef void(OnImprovConnected)(const char *ssid, const char *password);
  typedef void(OnImprovIdentify)();
  typedef bool(CustomConnectWiFi)(const char *ssid, const char *password);

  /**
   * ## Constructors
   */
  ImprovWiFiBLE() = default;
  ~ImprovWiFiBLE();

  /**
   * ## Methods (names/signatures match ImprovWiFi)
   */

  // Set details of your device (advertising is set up inside this call)
  void setDeviceInfo(ImprovTypes::ChipFamily chipFamily,
                     const char *firmwareName, const char *firmwareVersion,
                     const char *deviceName, const char *deviceUrl);

  void setDeviceInfo(ImprovTypes::ChipFamily chipFamily,
                     const char *firmwareName, const char *firmwareVersion,
                     const char *deviceName);

  // Callback setters
  void onImprovError(OnImprovError *errorCallback);
  void onImprovConnected(OnImprovConnected *connectedCallback);
  void onImprovIdentify(OnImprovIdentify *identifyCallback);
  void setCustomConnectWiFi(CustomConnectWiFi *connectWiFiCallBack);

  // Default WiFi connect helper (same name as serial transport)
  bool tryConnectToWifi(const char *ssid, const char *password);
  bool tryConnectToWifi(const char *ssid, const char *password,
                        uint32_t delayMs, uint8_t maxAttempts);

  // Mirror of ImprovWiFi::isConnected
  bool isConnected();

  // initialized and BLE running
  bool isAdvertising();

  // Stop BLE provisioning and free its memory: stops advertising, ends the RPC
  // worker (after the RPC in progress) and shuts down the BLE stack.
  // setDeviceInfo() starts it again. releaseMemory=true also returns the BT
  // controller memory to the heap; BLE then cannot be restarted until reboot.
  // Call from loop()/setup(), not from an Improv callback.
  void end(bool releaseMemory = false);

  // Stop Improv only, keep the BLE stack running (for applications that use
  // BLE themselves, e.g. scanning): stops advertising and ends the RPC worker.
  // Arduino 4 also removes the Improv GATT service. Arduino 3 keeps it
  // registered and reuses it, because removing a service there triggers a GATT
  // reset on the next disconnect, which asserts while the app scans.
  // setDeviceInfo() starts Improv again.
  // Like setDeviceInfo(), refuses while a BLE scan runs: pause it around the call.
  // Call from loop()/setup(), not from an Improv callback.
  void stop();

#ifndef IMPROV_BLE_V4
  // BLEServerCallbacks
  void onDisconnect(BLEServer *s) override;

  // BLECharacteristicCallbacks
  void onWrite(BLECharacteristic *c) override;
#endif

private:
  // === Improv BLE UUIDs ===
  static constexpr const char *SVC_UUID =
      "00467768-6228-2272-4663-277478268000";
  static constexpr const char *CHAR_STATE_UUID =
      "00467768-6228-2272-4663-277478268001";
  static constexpr const char *CHAR_ERROR_UUID =
      "00467768-6228-2272-4663-277478268002";
  static constexpr const char *CHAR_RPC_CMD_UUID =
      "00467768-6228-2272-4663-277478268003";
  static constexpr const char *CHAR_RPC_RES_UUID =
      "00467768-6228-2272-4663-277478268004";
  static constexpr const char *CHAR_CAPS_UUID =
      "00467768-6228-2272-4663-277478268005";
  static constexpr uint16_t SERVICE_DATA_UUID_16 =
      0x4677; // for advertisement service data

  enum : uint8_t {
    STATE_AUTH_REQUIRED = 0x01,
    STATE_AUTHORIZED = 0x02,
    STATE_PROVISIONING = 0x03,
    STATE_PROVISIONED = 0x04
  };

  // Basic error mapping for BLE-side status byte
  enum : uint8_t {
    ERR_NONE = 0x00,
    ERR_BAD_PACKET = 0x01,
    ERR_UNKNOWN_CMD = 0x02,
    ERR_CONNECT = 0x03,
    ERR_NOT_AUTH = 0x04,
    ERR_UNKNOWN = 0xFF
  };

  // Internal helpers
  void updateState(uint8_t s);
  void updateError(uint8_t e);
  void updateCaps(uint8_t caps);
  void reportError(uint8_t bleErr, ImprovTypes::Error apiErr);

  // Build advertisement payload (Flags + 128-bit UUID + Service Data)
  BLEAdvertisementData buildAdvData(uint8_t state, uint8_t caps);

  // (Re)apply adv data that reflects current state/caps; keeps scan response
  void advertiseNow();

  static uint8_t checksumLSB(const uint8_t *data, size_t len);

  // RPC handlers
  void handleRpc(const uint8_t *data, size_t len);
  void rpcSendWifi(const uint8_t *payload, size_t n);
  void rpcIdentify();
  void rpcDeviceInfo();
  void rpcScanNetworks();

  // Reassemble RPC commands split across several writes (>20 byte payloads)
  void onRpcWrite(const uint8_t *data, size_t len);

  // send [cmd][len][strings as len+bytes][checksum] on CHAR_RPC_RES
  void sendRpcResult(uint8_t cmd, const std::vector<String> &strings);
  uint16_t peerMtu();

  // send response with device URL (mirrors serial transport semantics)
  void sendDeviceUrl();

  // BLE objects
#ifdef IMPROV_BLE_V4
  BLEServer server_;
  BLEService service_;
  BLECharacteristic ch_state_;
  BLECharacteristic ch_error_;
  BLECharacteristic ch_rpc_cmd_;
  BLECharacteristic ch_rpc_res_;
  BLECharacteristic ch_caps_;
  BLEAdvertising adv_;
#else
  BLEServer *server_{nullptr};
  BLEService *service_{nullptr};
  BLECharacteristic *ch_state_{nullptr};
  BLECharacteristic *ch_error_{nullptr};
  BLECharacteristic *ch_rpc_cmd_{nullptr};
  BLECharacteristic *ch_rpc_res_{nullptr};
  BLECharacteristic *ch_caps_{nullptr};
  BLEAdvertising *adv_{nullptr};
#endif

  // identity
  ImprovTypes::ChipFamily chip_{ImprovTypes::ChipFamily::CF_ESP32};
  String firmware_name_;
  String firmware_version_;
  String device_name_;
  String device_friendly_name_;
  String device_url_;

  // state
  uint8_t state_{STATE_AUTHORIZED};
  bool running_{false}; // setDeviceInfo() succeeded, not stopped/ended
  uint8_t error_{ERR_NONE};
  uint8_t caps_{ImprovTypes::CAPABILITY_IDENTIFY | ImprovTypes::CAPABILITY_DEVICE_INFO |
                ImprovTypes::CAPABILITY_WIFI_SCAN};

  // RPC command reassembly
  std::vector<uint8_t> rx_;
  uint32_t rx_last_ms_{0};

  // RPCs run on a worker task, not in the BLE write callback: the write is
  // acknowledged before the RPC result is notified (Web Bluetooth rejects the
  // client's next write otherwise), and slow RPCs (scan, connect) don't stall
  // the BLE stack.
  struct RpcPacket {
    uint16_t len;
    uint8_t data[2 + 255 + 1]; // cmd + len + payload + checksum
  };
  QueueHandle_t rpc_queue_{nullptr};
  TaskHandle_t rpc_task_{nullptr}; // cleared by the task when it exits
  static constexpr uint16_t RPC_STOP = 0xFFFF; // RpcPacket.len: worker exits
  static void rpcTask(void *arg);
  bool stopWorker(const char *caller); // false if called on the worker itself
  static bool appScanning(const char *caller); // true (and logs) if a scan runs
#ifdef IMPROV_BLE_V4
  uint16_t peer_mtu_{23}; // captured from BLEConnInfo on write
#endif

  // user callbacks
  OnImprovError *onImprovErrorCallback_{nullptr};
  OnImprovConnected *onImprovConnectedCallback_{nullptr};
  OnImprovIdentify *onImprovIdentifyCallback_{nullptr};
  CustomConnectWiFi *customConnectWiFiCallback_{nullptr};
};

#endif // IMPROV_WIFI_BLE_ENABLED
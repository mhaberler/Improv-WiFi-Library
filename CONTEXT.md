# Improv WiFi Library

Device-side implementation of the Improv WiFi provisioning protocol.

## Language

**Transport**:
The channel an Improv client talks over: Serial or BLE. Each has its own framing and its own command table.
_Avoid_: interface, channel

**RPC Command**:
A request from the client to the device, identified by a command ID. IDs are transport-specific: `0x02` is Identify on BLE but Get Current State on Serial.
_Avoid_: message, request

**RPC Result**:
The device's reply to an RPC Command: a list of length-prefixed strings. On BLE a command gets exactly one RPC Result; on Serial a scan yields several.
_Avoid_: response packet

**Capabilities**:
Bitfield the BLE device publishes (characteristic and advertisement) to tell the client which optional RPC Commands it supports: identify, device info, Wi-Fi scan. A client only sends an optional command if its bit is set.

**Device Info**:
The RPC Result describing the device: firmware name, firmware version, chip family, device name.

**Chip Family**:
The ESP chip variant reported in Device Info (e.g. `ESP32-S3`), normally derived from the build target.

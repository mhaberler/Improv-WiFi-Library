#!/usr/bin/env bash
# Rebuild the prebuilt Improv web clients in this directory.
# Requires git and Node.js/npm.
set -euo pipefail

BLE_REPO=https://github.com/mhaberler/sdk-ble-js.git
BLE_COMMIT=7abfedc # fix/rpc-write-await on top of upstream 580431a
SERIAL_REPO=https://github.com/improv-wifi/sdk-serial-js.git
SERIAL_COMMIT=94e3113

here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

build() { # repo commit dest
  git clone -q "$1" "$work/src"
  git -C "$work/src" checkout -q "$2"
  (cd "$work/src" && npm ci && bash script/build)
  rm -rf "$here/$3"
  mkdir -p "$here/$3"
  cp "$work/src/dist/web/"*.js "$here/$3/"
  rm -rf "$work/src"
}

build "$BLE_REPO" "$BLE_COMMIT" ble
build "$SERIAL_REPO" "$SERIAL_COMMIT" serial
echo "updated $here/ble and $here/serial"

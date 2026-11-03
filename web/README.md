# Provisioning web app

Static page with the Improv Wi-Fi web clients for Bluetooth and USB serial,
published at <https://mhaberler.github.io/Improv-WiFi-Library/>.

`ble/` and `serial/` are prebuilt bundles; no Node.js needed to use them.

| Client | Source | Commit |
|---|---|---|
| Bluetooth | [mhaberler/sdk-ble-js](https://github.com/mhaberler/sdk-ble-js/tree/fix/rpc-write-await) (`fix/rpc-write-await`) | `5211171` |
| Serial | [improv-wifi/sdk-serial-js](https://github.com/improv-wifi/sdk-serial-js) | `94e3113` |

The Bluetooth client is newer than the one on improv-wifi.com (device info,
network scan) and carries two fixes on top of upstream `580431a`:

- an RPC whose write fails no longer hangs the dialog; results notified before
  the write is acknowledged no longer make the next request fail with
  "GATT operation already in progress"
- the Wi-Fi form is shown only after device info and networks are loaded

## Run locally

Web Bluetooth and Web Serial need HTTPS or `localhost`:

```sh
npx serve web
```

## Rebuild

Edit the commits in `build.sh`, then run `web/build.sh` (needs git, Node.js).

## License

Both clients are Apache-2.0, see [LICENSE](LICENSE).

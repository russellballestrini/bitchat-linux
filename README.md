# bitchat-linux

A Linux / PC client for the [BitChat](https://github.com/permissionlesstech/bitchat)
mesh, written in C.

Joins the same Bluetooth LE mesh that the iOS, macOS, and Android clients
use. No internet required. Public domain.

## Status

| Stage | Capability | Status |
|-------|-----------|--------|
| 1     | BitchatPacket decode (v1+v2), TLV + announce parser, PKCS#7 padding strip, zlib decompression | **done** |
| 2     | BLE scan + GATT subscribe via BlueZ (sd-bus). Receive-only mesh observer. | in progress |
| 3     | Send announces + public plaintext messages. Relay with TTL. | todo |
| 4     | Noise XX handshake + encrypted private messages. | todo |
| 5     | Nostr (NIP-17, geohash channels), Tor, file transfer | todo |

Stage 1 compiles clean, passes its built-in self-test, and decodes
captured mesh frames. It does **not** yet join the mesh on its own — Stage 2
brings up the radio.

## Build

```sh
sudo apt install build-essential zlib1g-dev     # Debian/Ubuntu
make
make test
```

## Use

```sh
# Decode a frame from hex (captured via btmon, wireshark, etc.)
echo "01 01 07 ..." | ./bitchat-linux --decode

# Run built-in round-trip tests
./bitchat-linux --self-test
```

## Wire format

See `include/packet.h` for the header layout. Source of truth is the Swift
reference implementation:
[`BinaryProtocol.swift`](https://github.com/permissionlesstech/bitchat/blob/main/localPackages/BitFoundation/Sources/BitFoundation/BinaryProtocol.swift).

## BLE identifiers

| | UUID |
|---|---|
| Service | `F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C` |
| Characteristic | `A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D` |

(Testnet swaps the last hex digit of the service UUID: `…4B5A`.)

## Related projects

- [`bitchat`](https://github.com/permissionlesstech/bitchat) — iOS + macOS, Swift (reference)
- [`bitchat-android`](https://github.com/permissionlesstech/bitchat-android) — Android, Kotlin
- **`bitchat-linux`** — Linux / POSIX, C (this repo)

## License

Public domain, matching upstream BitChat. See [LICENSE](LICENSE).

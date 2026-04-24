# bitchat-linux

A Linux / PC client for the [BitChat](https://github.com/permissionlesstech/bitchat)
mesh, written in C.

Joins the same Bluetooth LE mesh that the iOS, macOS, and Android clients
use. No internet required. Public domain.

## Status

| Stage | Capability | Status |
|-------|-----------|--------|
| 1     | BitchatPacket decode (v1+v2), TLV + announce parser, PKCS#7 padding strip, zlib decompression | **done** |
| 2     | BLE scan + GATT subscribe via BlueZ (sd-bus). Receive-only mesh observer. | **done** |
| 3     | Send announces + public plaintext messages. Relay with TTL. | todo |
| 4     | Noise XX handshake + encrypted private messages. | todo |
| 5     | Nostr (NIP-17, geohash channels), Tor, file transfer | todo |

Stage 1 compiles clean, passes its built-in self-test, and decodes
captured mesh frames. It does **not** yet join the mesh on its own — Stage 2
brings up the radio.

## Build

```sh
sudo apt install build-essential zlib1g-dev libsystemd-dev pkg-config   # Debian/Ubuntu
make
make test
```

## Tests

Three layers, all fully synthetic — no BLE hardware, no real peer required:

```sh
make unit          # per-module tests (hex, padding, tlv, announce, packet)
make integration   # multi-module: encoded frame → decoded + TLV extracted
make functional    # CLI: make_fixture → bitchat-linux --decode → grep
make test          # all of the above + --self-test
```

Current coverage:

| Suite | Tests | Focus |
|-------|-------|-------|
| `test_hex`         | 7 | hex decode/encode, case mixing, overflow, rejection |
| `test_padding`     | 6 | PKCS#7 strip edge cases |
| `test_tlv`         | 5 | TLV iteration, truncation rejection |
| `test_announce`    | 5 | AnnouncementPacket TLVs, missing-field rejection, neighbors |
| `test_packet`      | 9 | v1/v2, flags, padding, compression, routing, truncation, fuzz |
| `test_integration` | 4 | padded+compressed announce end-to-end, UTF-8, noise pass-through, TTL preservation |
| `functional.sh`    | 10 | CLI against known-good fixtures + error paths |

The fuzz case in `test_packet` feeds 256 random buffers through the decoder
and asserts no crash — a baseline regression gate for "can't crash on
bytes from the wire". Swap in AFL++ later for deeper fuzzing.

## Use

```sh
# Join the mesh (receive-only). Prints every packet received from any peer.
./bitchat-linux --listen

# Same, but on the testnet service UUID
./bitchat-linux --listen-test

# Decode a frame from hex (captured via btmon, wireshark, etc.)
echo "01 01 07 ..." | ./bitchat-linux --decode

# Run built-in round-trip tests
./bitchat-linux --self-test
```

### BLE permissions

BlueZ typically allows unprivileged D-Bus clients to scan and subscribe via
polkit rules shipped with the distro. If `--listen` complains about access
on `SetDiscoveryFilter` or `StartDiscovery`, either add yourself to the
`bluetooth` group or run the binary under `sudo`.

Confirm you have an adapter:

```sh
bluetoothctl list     # should show at least one controller
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

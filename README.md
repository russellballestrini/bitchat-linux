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
| 3     | Persistent Ed25519 + Curve25519 identity, signed announces + public messages, BLE peripheral (GATT server + LE advertisement), dual-role dispatch, TTL-decrement relay, interactive chat | **done** |
| 4     | Noise XX handshake + encrypted private messages. | todo |
| 5     | Nostr (NIP-17, geohash channels), Tor, file transfer | todo |

A single bitchat-linux process is now a full mesh citizen: it scans and
subscribes as a central, advertises and serves GATT as a peripheral,
signs every outbound public frame with its Ed25519 key, and relays public
traffic with TTL decrement + a 5-minute dedup cache.

## Build

```sh
make deps       # auto-detect distro, install zlib / libsystemd / openssl / python-dbus
make
make test
```

`make deps` dispatches to the right package manager via `/etc/os-release`.
If auto-detection misses, call the explicit target:

```sh
make deps-apt      # Debian, Ubuntu, Mint
make deps-dnf      # Fedora, RHEL, CentOS, Rocky, AlmaLinux
make deps-pacman   # Arch, Manjaro
make deps-zypper   # openSUSE, SLES
```

Manual install (if you prefer):

| Distro | Command |
|--------|---------|
| Debian/Ubuntu | `sudo apt install build-essential zlib1g-dev libsystemd-dev libssl-dev pkg-config python3-dbus python3-gi bluez` |
| Fedora/RHEL   | `sudo dnf install gcc make pkgconf-pkg-config zlib-devel systemd-devel openssl-devel python3-dbus python3-gobject bluez` |
| Arch          | `sudo pacman -S base-devel pkgconf zlib systemd openssl python-dbus python-gobject bluez bluez-utils` |
| openSUSE      | `sudo zypper install gcc make pkg-config zlib-devel systemd-devel libopenssl-devel python3-dbus-python python3-gobject bluez` |

## Tests

Four layers. The first three run anywhere on Linux — no BLE hardware
needed. The fourth requires two BlueZ adapters to round-trip over real
radio; it skips cleanly if you have only one.

```sh
make unit              # per-module tests (hex, padding, tlv, announce, packet)
make integration       # multi-module: encoded frame → decoded + TLV extracted
make functional        # CLI: make_fixture → bitchat-linux --decode → grep
make functional-mock   # canned frames → --listen-stream (no BLE, exercises dispatch)
make test              # unit + self-test + functional + functional-mock
make functional-mesh   # real BLE round-trip across 2 adapters (skip if only 1)
```

Current coverage:

| Suite                | Tests | BLE? | Focus |
|----------------------|-------|------|-------|
| `test_hex`           | 7  | —  | hex decode/encode, case mixing, overflow, rejection |
| `test_padding`       | 6  | —  | PKCS#7 strip edge cases |
| `test_tlv`           | 5  | —  | TLV iteration, truncation rejection |
| `test_announce`      | 5  | —  | AnnouncementPacket TLVs, missing-field rejection, neighbors |
| `test_packet`        | 9  | —  | v1/v2, flags, padding, compression, routing, truncation, fuzz |
| `test_integration`   | 4  | —  | padded+compressed announce end-to-end, UTF-8, Noise pass-through, TTL |
| `functional.sh`      | 10 | —  | CLI against known-good fixtures + error paths |
| `functional_mock.sh` | 6  | —  | stream-mode dispatch, multi-frame, bogus-length rejection |
| `functional_mesh.sh` | 6  | 2× | fake_peer advertises → `--listen-test` on 2nd adapter → decode |

The fuzz case in `test_packet` feeds 256 random buffers through the decoder
and asserts no crash — a baseline regression gate for "can't crash on
bytes from the wire". Swap in AFL++ later for deeper fuzzing.

### The mesh test — real BLE round-trip

One physical radio cannot scan its own advertisement, so same-adapter
self-loopback doesn't work at the radio layer. The mesh test therefore
requires **two adapters**:

- Laptop built-in + USB BLE dongle on one machine, **or**
- Two machines (e.g., two ThinkPads on the same desk) where one runs
  `tests/fake_peer.py` and the other runs `./bitchat-linux --listen-test`

```sh
# single-box with USB dongle
make functional-mesh

# custom adapter assignment
PEER_HCI=hci1 LISTEN_HCI=hci0 make functional-mesh

# two machines: on the "peer" box
python3 tests/fake_peer.py --timeout 60
# on the "listener" box
./bitchat-linux --listen-test
```

On single-adapter boxes the test prints a skip message and returns exit 0
(via TAP-style exit-77 convention) so `make all` still passes in CI.

## Use

```sh
# Full mesh participation: scan + subscribe, advertise + serve GATT,
# send stdin lines as signed public messages, relay what you hear.
./bitchat-linux --chat --nick fox

# Same, but on the testnet service UUID (safe for local testing —
# doesn't collide with real BitChat peers running on mainnet).
./bitchat-linux --chat --nick fox --testnet

# One-shot announce (useful for testing discovery between two boxes).
./bitchat-linux --announce --nick fox --testnet

# Receive-only observer — no identity, no sending, just print what
# crosses the air.
./bitchat-linux --listen
./bitchat-linux --listen-test

# Pick a specific BlueZ adapter (default: first found).
./bitchat-linux --chat --nick fox --adapter /org/bluez/hci1

# Software mock — read length-prefixed frames on stdin. No BLE needed.
./bitchat-linux --listen-stream < captured_frames.bin

# Decode a frame from hex (captured via btmon, wireshark, etc.)
echo "01 01 07 ..." | ./bitchat-linux --decode

# Run built-in round-trip tests
./bitchat-linux --self-test
```

### Two-node test over real BLE

On two Linux boxes within BLE range (laptops with built-in BT, or USB
dongles):

```sh
# Both boxes — fresh identity generated on first run
./bitchat-linux --chat --nick alice --testnet

# (on the other box)
./bitchat-linux --chat --nick bob --testnet
```

Each side will log `[ble] peripheral enabled` + `[ble] discovering`,
then `[announce] <nickname> (<peer-id>)` when it sees the other. Typing
a line on one side prints `<me> hello` locally and `<peer-id> hello` on
the other.

### Identity

Generated on first run and persisted at `0600`:

```
$XDG_CONFIG_HOME/bitchat-linux/identity.bin
(or  ~/.config/bitchat-linux/identity.bin)
```

Contains a 32-byte Ed25519 seed (signing), a 32-byte Curve25519 private
key (noise static), and their public counterparts. Peer ID is derived
as `SHA256(noise_pubkey)[0..8]` — matches `BLEService.swift:2894`.

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

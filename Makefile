# bitchat-linux — C client
#
# Standard targets: all test clean install
# Test targets:     unit integration functional functional-mock functional-mesh self-test
# Setup targets:    deps (auto-detect) deps-apt deps-dnf deps-pacman deps-zypper
# BlueZ targets:    ble-le-only ble-le-only-restore ble-restart ble-status
#                   ble-permanent-le ble-permanent-dual ble-permanent-status
#
# Runtime deps:
#   zlib, libsystemd (sd-bus for BlueZ), OpenSSL 3+ (Ed25519 / X25519 / SHA256)
# Test-only deps:
#   python3 + dbus bindings (for tests/fake_peer.py — real-BLE functional test)
#
# First-time setup:
#   make deps       # install runtime + test deps for your distro
#   make            # build
#   make test       # unit + self-test + functional + mock (skips mesh on 1-adapter box)
#   make clean

CC       ?= cc
CFLAGS   ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L $(shell pkg-config --cflags libsystemd openssl)
LDLIBS_CORE := -lz $(shell pkg-config --libs openssl)
LDLIBS_BLE  := $(shell pkg-config --libs libsystemd)
LDLIBS      := $(LDLIBS_CORE) $(LDLIBS_BLE)

BIN      := bitchat-linux
SRCS     := $(wildcard src/*.c)
OBJS     := $(SRCS:src/%.c=build/%.o)

# Core library objects used by tests (everything except the CLI main and
# the BLE transport — tests don't need sd-bus).
CORE_OBJS := $(filter-out build/main.o build/ble.o, $(OBJS))

# Test helpers compiled into tests/build/
TEST_SUPPORT := tests/build/fixtures.o
TEST_SRCS    := $(wildcard tests/test_*.c)
TEST_BINS    := $(TEST_SRCS:tests/%.c=tests/build/%)
FIXTURE_BIN  := tests/build/make_fixture

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

.PHONY: all clean install test unit integration functional functional-mock functional-mesh self-test \
        deps deps-apt deps-dnf deps-pacman deps-zypper \
        ble-le-only ble-le-only-restore ble-restart ble-status \
        ble-permanent-le ble-permanent-dual ble-permanent-status

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# --- tests ---

tests/build/%.o: tests/%.c
	@mkdir -p tests/build
	$(CC) $(CFLAGS) $(CPPFLAGS) -Itests -c $< -o $@

tests/build/test_%: tests/build/test_%.o $(TEST_SUPPORT) $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CORE)

$(FIXTURE_BIN): tests/build/make_fixture.o $(TEST_SUPPORT) $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CORE)

unit: $(TEST_BINS)
	@echo "== unit + integration =="
	@for t in $(TEST_BINS); do echo "-- $$t --"; $$t || exit 1; done

integration: unit

self-test: $(BIN)
	./$(BIN) --self-test

functional: $(BIN) $(FIXTURE_BIN)
	@echo "== functional (CLI) =="
	tests/functional.sh

# Software mock: canned frames piped through --listen-stream into the
# real dispatch path. Runs anywhere, no BLE needed.
functional-mock: $(BIN) $(FIXTURE_BIN)
	@echo "== functional (mock / no BLE) =="
	tests/functional_mock.sh

# Real BLE round-trip across two adapters. Skips gracefully (TAP exit 77)
# if only one adapter is present on this box — converted to make-friendly
# exit 0 so this target doesn't break `make all` on single-adapter hosts.
functional-mesh: $(BIN) $(FIXTURE_BIN)
	@echo "== functional (BLE mesh, needs 2 adapters) =="
	@tests/functional_mesh.sh; s=$$?; \
	 if [ "$$s" = 77 ]; then echo "(skipped — insufficient BLE hardware)"; exit 0; \
	 else exit $$s; fi

test: unit self-test functional functional-mock
	@echo "all tests passed (mesh test excluded — run 'make functional-mesh' with 2 BLE adapters)"

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -rf build tests/build $(BIN)

# --- dependency installation (distro-aware) ---
#
# Picks a package manager from /etc/os-release. If auto-detection fails
# or you know which you want, call the explicit target directly.
deps:
	@if [ ! -f /etc/os-release ]; then \
	    echo "can't auto-detect — /etc/os-release missing"; \
	    echo "try: make deps-apt  |  deps-dnf  |  deps-pacman  |  deps-zypper"; \
	    exit 1; \
	fi; \
	. /etc/os-release; \
	case "$$ID $$ID_LIKE" in \
	    *fedora*|*rhel*|*centos*|*rocky*|*almalinux*) $(MAKE) deps-dnf ;; \
	    *debian*|*ubuntu*|*mint*)                     $(MAKE) deps-apt ;; \
	    *arch*|*manjaro*|*endeavouros*)               $(MAKE) deps-pacman ;; \
	    *opensuse*|*suse*|*sles*)                     $(MAKE) deps-zypper ;; \
	    *) echo "unknown distro ($$ID $$ID_LIKE)"; \
	       echo "try: make deps-apt | deps-dnf | deps-pacman | deps-zypper"; exit 1 ;; \
	esac

# Debian / Ubuntu / Mint
deps-apt:
	sudo apt-get update
	sudo apt-get install -y \
	    build-essential pkg-config \
	    zlib1g-dev libsystemd-dev libssl-dev \
	    python3-dbus python3-gi \
	    bluez

# Fedora / RHEL / CentOS / Rocky / AlmaLinux
deps-dnf:
	sudo dnf install -y \
	    gcc make pkgconf-pkg-config \
	    zlib-devel systemd-devel openssl-devel \
	    python3-dbus python3-gobject \
	    bluez

# Arch / Manjaro
deps-pacman:
	sudo pacman -S --needed --noconfirm \
	    base-devel pkgconf \
	    zlib systemd openssl \
	    python-dbus python-gobject \
	    bluez bluez-utils

# openSUSE / SLES
deps-zypper:
	sudo zypper install -y \
	    gcc make pkg-config \
	    zlib-devel systemd-devel libopenssl-devel \
	    python3-dbus-python python3-gobject \
	    bluez

# --- BlueZ adapter prep ---
#
# bitchat is LE-only. On a dual-mode adapter that also has classic
# Bluetooth devices paired (e.g. an A2DP headset), Device1.Connect
# probes BR/EDR alongside LE and tears the LE link down when the
# BR/EDR leg fails — the link comes up for ~1s then drops with
# br-connection-canceled / br-connection-key-missing.
#
# Two ways to fix it:
#
#   ble-le-only            — per-session toggle via btmgmt. Forgets on
#                            every `systemctl restart bluetooth` and
#                            every reboot, so you'd run it each time.
#   ble-permanent-le       — flip ControllerMode = le in main.conf.
#                            Survives reboot. Sudo once, never again.
#   ble-permanent-dual     — restore ControllerMode = dual (BR/EDR back).
#   ble-permanent-status   — show the active ControllerMode line.
#   ble-le-only-restore    — per-session BR/EDR back on (matching pair
#                            for ble-le-only).
#   ble-status             — show current adapter settings (LE / BR/EDR).
#   ble-restart            — just `systemctl restart bluetooth`.
#
# Per-session targets default to hci0 — set HCI=hciN to override.
# Permanent targets default to /etc/bluetooth/main.conf —
# set BLUEZ_CONF=/path to override.
HCI ?= 0
BLUEZ_CONF ?= /etc/bluetooth/main.conf

ble-le-only:
	sudo systemctl restart bluetooth
	sudo btmgmt --index $(HCI) power off
	sudo btmgmt --index $(HCI) bredr off
	sudo btmgmt --index $(HCI) power on
	@echo "hci$(HCI) is now LE-only (per-session — resets on reboot)"

ble-le-only-restore:
	sudo btmgmt --index $(HCI) power off
	sudo btmgmt --index $(HCI) bredr on
	sudo btmgmt --index $(HCI) power on
	@echo "hci$(HCI) BR/EDR re-enabled"

# Permanent LE-only via /etc/bluetooth/main.conf [General] ControllerMode.
# Idempotent: strips any existing ControllerMode line (commented or not)
# and re-inserts directly under [General], so re-running just sets it.
ble-permanent-le:
	@grep -q '^\[General\]' $(BLUEZ_CONF) || \
	    { echo "$(BLUEZ_CONF) has no [General] section"; exit 1; }
	sudo sed -i '/^[[:space:]]*#\?[[:space:]]*ControllerMode[[:space:]]*=/d' $(BLUEZ_CONF)
	sudo sed -i '/^\[General\]/a ControllerMode = le' $(BLUEZ_CONF)
	sudo systemctl restart bluetooth
	@echo "$(BLUEZ_CONF): ControllerMode = le (survives reboot)"

ble-permanent-dual:
	sudo sed -i '/^[[:space:]]*#\?[[:space:]]*ControllerMode[[:space:]]*=/d' $(BLUEZ_CONF)
	sudo sed -i '/^\[General\]/a ControllerMode = dual' $(BLUEZ_CONF)
	sudo systemctl restart bluetooth
	@echo "$(BLUEZ_CONF): ControllerMode = dual"

ble-permanent-status:
	@grep -E '^[[:space:]]*ControllerMode[[:space:]]*=' $(BLUEZ_CONF) \
	    || echo "$(BLUEZ_CONF): ControllerMode unset (default = dual)"

ble-restart:
	sudo systemctl restart bluetooth

ble-status:
	@btmgmt --index $(HCI) info | grep -E 'current settings|addr'

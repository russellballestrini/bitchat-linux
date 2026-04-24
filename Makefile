# bitchat-linux — C client
#
# Standard targets: all test clean install
# Test targets: unit integration functional self-test
#
# Runtime deps (Stage 1): zlib (libz)
# Runtime deps (Stage 2): libsystemd (sd-bus, BlueZ D-Bus interface)
#
# Build:   make
# Test:    make test        # runs unit + integration + self-test + functional
# Clean:   make clean

CC       ?= cc
CFLAGS   ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L $(shell pkg-config --cflags libsystemd)
LDLIBS_CORE := -lz
LDLIBS_BLE  := $(shell pkg-config --libs libsystemd)
LDLIBS      := $(LDLIBS_CORE) $(LDLIBS_BLE)

BIN      := bitchat-linux
SRCS     := $(wildcard src/*.c)
OBJS     := $(SRCS:src/%.c=build/%.o)

# Core library objects used by tests (everything except the CLI main and
# the BLE transport — tests don't need sd-bus).
CORE_OBJS := $(filter-out build/main.o build/ble.o, $(OBJS))

# Test helpers compiled into tests/build/
TEST_SUPPORT := tests/build/encoder.o tests/build/fixtures.o
TEST_SRCS    := $(wildcard tests/test_*.c)
TEST_BINS    := $(TEST_SRCS:tests/%.c=tests/build/%)
FIXTURE_BIN  := tests/build/make_fixture

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

.PHONY: all clean install test unit integration functional functional-mock functional-mesh self-test

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

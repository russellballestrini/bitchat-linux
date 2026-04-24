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

.PHONY: all clean install test unit integration functional self-test

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
	@echo "== functional =="
	tests/functional.sh

test: unit self-test functional
	@echo "all tests passed"

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -rf build tests/build $(BIN)

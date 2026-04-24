# bitchat-linux — C client
#
# Standard targets: all test clean install
#
# Runtime deps (Stage 1): zlib (libz)
# Runtime deps (Stage 2): libsystemd (sd-bus, BlueZ D-Bus interface)
#
# Build:  make
# Test:   make test
# Clean:  make clean

CC       ?= cc
CFLAGS   ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
LDLIBS   += -lz

BIN      := bitchat-linux
SRCS     := $(wildcard src/*.c)
OBJS     := $(SRCS:src/%.c=build/%.o)

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

.PHONY: all clean test install

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

test: $(BIN)
	./$(BIN) --self-test

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -rf build $(BIN)

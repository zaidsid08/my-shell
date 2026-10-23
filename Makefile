CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -g
CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
SANITIZE = -fsanitize=address,undefined -fno-omit-frame-pointer

BUILD   = build
BIN     = msh
SRCS    = $(wildcard src/*.c)
LIBSRCS = $(filter-out src/main.c,$(SRCS))
LIBOBJS = $(LIBSRCS:src/%.c=$(BUILD)/%.o)
TESTS   = $(patsubst tests/unit/%.c,$(BUILD)/tests/%,$(wildcard tests/unit/test_*.c))

.PHONY: all test unit integration test-tty debug clean format

all: $(BIN)

$(BIN): $(BUILD)/main.o $(BUILD)/libmsh.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/libmsh.a: $(LIBOBJS)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/tests/%: tests/unit/%.c tests/unit/check.h $(BUILD)/libmsh.a | $(BUILD)/tests
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ $< $(BUILD)/libmsh.a

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

test: unit integration

unit: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

integration: $(BIN)
	@./tests/integration/run.sh ./$(BIN)

# Job control needs a real terminal; this drives msh through a pty.
test-tty: $(BIN)
	@python3 tests/integration/tty_jobs.py ./$(BIN)

# Build into build/asan with AddressSanitizer + UBSan and run the full suite.
debug:
	$(MAKE) test BUILD=build/asan BIN=build/asan/msh \
		CFLAGS="$(CFLAGS) -O0 $(SANITIZE)" LDFLAGS="$(SANITIZE)"

clean:
	rm -rf build msh

format:
	clang-format -i src/*.c src/*.h tests/unit/*.c tests/unit/*.h

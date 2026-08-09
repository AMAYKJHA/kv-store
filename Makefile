CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wconversion -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS :=

# make MODE=release  -> optimized, no asserts
# make              -> debug, sanitizers on
MODE ?= debug
ifeq ($(MODE),release)
  CFLAGS += -O2 -DNDEBUG
else
  CFLAGS += -O0 -g3 -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
endif

SRC_DIR   := src
BUILD_DIR := build/$(MODE)
BIN_DIR   := bin

SERVER_BIN := $(BIN_DIR)/rkv-server
CLI_BIN    := $(BIN_DIR)/rkv-cli

# Everything under src/ except the two entry points is shared library code.
ALL_SRCS  := $(shell find $(SRC_DIR) -name '*.c')
MAIN_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/cli.c
LIB_SRCS  := $(filter-out $(MAIN_SRCS),$(ALL_SRCS))
LIB_OBJS  := $(LIB_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TEST_SRCS := $(wildcard tests/*.c)
TEST_BINS := $(TEST_SRCS:tests/%.c=$(BUILD_DIR)/tests/%)

.PHONY: all server cli test clean format tidy
all: server cli

server: $(SERVER_BIN)
cli: $(CLI_BIN)

$(SERVER_BIN): $(BUILD_DIR)/main.o $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLI_BIN): $(BUILD_DIR)/cli.o $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

# Compile with auto-generated header dependencies (.d files).
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/tests/%: tests/%.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; $$t || exit 1; done

$(BIN_DIR):
	@mkdir -p $@

clean:
	rm -rf build bin

format:
	@find src include tests -name '*.[ch]' | xargs clang-format -i

tidy:
	@find src include tests -name '*.[ch]' | xargs clang-tidy -p . 2>/dev/null || \
	  echo "clang-tidy not configured; skipping"

-include $(shell find build -name '*.d' 2>/dev/null)

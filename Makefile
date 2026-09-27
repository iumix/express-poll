CC = gcc
SRC_DIR = src
BIN_DIR = bin
PROGRAMS = express_poll felica_poll octopus_balance probe_system suica_balance

.PHONY: all clean
ifeq ($(OS),Windows_NT)
all:
	cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=$(CC)
	cmake --build build
clean:
	cmake -E rm -rf bin build
else
CPPFLAGS += $(shell pkg-config --cflags libnfc 2>/dev/null)
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
LDLIBS += $(shell pkg-config --libs libnfc 2>/dev/null || echo -lnfc)
BINS = $(addprefix $(BIN_DIR)/,$(PROGRAMS))
all: $(BINS)
$(BIN_DIR)/%: $(SRC_DIR)/%.c $(SRC_DIR)/common/platform.c $(SRC_DIR)/common/platform.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(SRC_DIR)/common/platform.c $(LDFLAGS) -o $@ $(LDLIBS)
clean:
	rm -rf $(BIN_DIR) build
endif

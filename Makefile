CC = gcc
LDLIBS = -lnfc
SRC_DIR = src
BIN_DIR = bin
SRCS = $(wildcard $(SRC_DIR)/*.c)
BINS = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%, $(SRCS))

.PHONY: all clean

all: $(BINS)

$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $< -o $@ $(LDLIBS)

clean:
	rm -rf $(BIN_DIR)

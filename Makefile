CC = gcc
LDLIBS = -lnfc
SRCS = $(wildcard *.c)
BINS = $(patsubst %.c, bin/%, $(SRCS))
BIN_DIR = bin

.PHONY: all clean

all: $(BINS)

$(BIN_DIR)/%: %.c
	rm -rf $(BIN_DIR)
	@mkdir -p $(@D)
	$(CC) $< -o $@ $(LDLIBS)

clean:
	rm -rf $(BIN_DIR)

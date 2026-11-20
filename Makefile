CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2
LDFLAGS = -lm

BIN_DIR = bin

COMMON_SRCS = common_folder/crc32.c common_folder/logger.c common_folder/utilities.c
SENDER_SRCS = $(wildcard sender_folder/*.c) $(COMMON_SRCS)
RECEIVER_SRCS = $(wildcard receiver_folder/*.c) $(COMMON_SRCS)
EMU_CORE_SRCS = emulator_folder/prng.c emulator_folder/scenario.c \
                emulator_folder/event_queue.c emulator_folder/link.c \
                common_folder/logger.c
EMU_SRCS = emulator_folder/emulator.c $(EMU_CORE_SRCS)

.PHONY: all tests test clean

all: $(BIN_DIR)/sender $(BIN_DIR)/receiver $(BIN_DIR)/emulator

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/sender: $(SENDER_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Icommon_folder -Isender_folder $(SENDER_SRCS) -o $@ $(LDFLAGS)

$(BIN_DIR)/receiver: $(RECEIVER_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Icommon_folder -Ireceiver_folder $(RECEIVER_SRCS) -o $@ $(LDFLAGS)

$(BIN_DIR)/emulator: $(EMU_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Icommon_folder -Iemulator_folder $(EMU_SRCS) -o $@ $(LDFLAGS)

tests: $(BIN_DIR)/test_crc32 $(BIN_DIR)/test_emulator_unit

$(BIN_DIR)/test_crc32: tests/test_crc32.c common_folder/crc32.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -Icommon_folder $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/test_emulator_unit: tests/test_emulator_unit.c $(EMU_CORE_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Icommon_folder -Iemulator_folder $^ -o $@ $(LDFLAGS)

test: tests
	./$(BIN_DIR)/test_crc32
	./$(BIN_DIR)/test_emulator_unit

clean:
	rm -rf $(BIN_DIR)

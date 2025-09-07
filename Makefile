# Makefile for BLE Scanner Client
# Supports Linux and macOS

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
TARGET = ble_client
SOURCE = ble_client.c

# Default target
all: $(TARGET)

# Build the client
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

# Clean build artifacts
clean:
	rm -f $(TARGET)

# Install (copy to /usr/local/bin)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)

# Help
help:
	@echo "BLE Scanner Client Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the ble_client program"
	@echo "  clean     - Remove built files"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  uninstall - Remove from /usr/local/bin"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Usage after building:"
	@echo "  ./ble_client <serial_port> <timeout_ms>"
	@echo "  Example: ./ble_client /dev/ttyUSB0 10000"

.PHONY: all clean install uninstall help

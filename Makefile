# Makefile for Modem Sample Program

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS =

# Target executable
TARGET = modem_sample

# Source files
SOURCES = modem_sample.c serial_port.c modem_control.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = modem_sample.h

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "Build complete: $@"

# Compile source files to object files
%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

# Clean and rebuild
rebuild: clean all

# Install (optional - requires root)
install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/local/bin..."
	@if [ "$$(id -u)" != "0" ]; then \
		echo "Error: Installation requires root privileges. Use: sudo make install"; \
		exit 1; \
	fi
	install -m 755 $(TARGET) /usr/local/bin/
	@echo "Installation complete"

# Uninstall (optional - requires root)
uninstall:
	@echo "Uninstalling $(TARGET) from /usr/local/bin..."
	@if [ "$$(id -u)" != "0" ]; then \
		echo "Error: Uninstallation requires root privileges. Use: sudo make uninstall"; \
		exit 1; \
	fi
	rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstallation complete"

# Help
help:
	@echo "Modem Sample Program - Makefile targets:"
	@echo "  make          - Build the program"
	@echo "  make all      - Build the program"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild"
	@echo "  make install  - Install to /usr/local/bin (requires root)"
	@echo "  make uninstall- Uninstall from /usr/local/bin (requires root)"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET)   - Run the program"
	@echo ""
	@echo "Note: Serial port access requires appropriate permissions."
	@echo "      Add user to 'dialout' group or run with sudo."

.PHONY: all clean rebuild install uninstall help

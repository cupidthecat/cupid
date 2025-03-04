# Makefile for Cupid editor

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -Iinclude
TARGET = cupid
SRC = $(wildcard src/*.c) $(wildcard lib/*.c)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)  # Fix: Update the source path

clean:
	rm -f $(TARGET) *.o *.d *.gc* *.out *.log

.PHONY: all clean

run: $(TARGET)
	./$(TARGET)  # Ensure it runs the compiled binary

copy: $(TARGET)
	@if command -v xclip > /dev/null; then \
		cat $(TARGET) | xclip -selection clipboard; \
		echo "Copied $(TARGET) to clipboard (xclip)"; \
	elif command -v pbcopy > /dev/null; then \
		cat $(TARGET) | pbcopy; \
		echo "Copied $(TARGET) to clipboard (pbcopy)"; \
	else \
		echo "Error: No clipboard utility found (install xclip or pbcopy)"; \
	fi

.PHONY: all clean run copy

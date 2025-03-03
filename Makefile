# Makefile for Cupid editor

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
TARGET = cupid
SRC = src/cupid.c  # Fix: Correct the source path

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)  # Fix: Update the source path

clean:
	rm -f $(TARGET)

.PHONY: all clean

run: $(TARGET)
	./$(TARGET)  # Ensure it runs the compiled binary

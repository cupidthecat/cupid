# Makefile for Cupid editor

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
TARGET = cupid
SRC = cupid.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean

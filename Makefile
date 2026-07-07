CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude -Isrc
LDFLAGS = -lm

SRC = src/main.c src/lsp.c src/parser.c src/json.c
OBJ = $(SRC:.c=.o)
TARGET = lynx-lsp

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall
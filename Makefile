# Makefile
TARGET  = fsave
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2
INCLUDE = -Iinclude
SRC     = src/main.c src/savefile.c
OBJ     = $(SRC:.c=.o)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c include/savefile.h
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)


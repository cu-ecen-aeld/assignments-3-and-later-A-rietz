CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -std=gnu99
TARGET = aesdsocket
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)

.PHONY: all default clean

all: $(TARGET)
default: all

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

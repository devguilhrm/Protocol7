CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
TARGET = servidor

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET)

.PHONY: all clean
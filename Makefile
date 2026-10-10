# OrgaNice - build with:  make   (Linux / macOS / Windows with MinGW or MSYS2)
CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2 -D__USE_MINGW_ANSI_STDIO=1
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
  TARGET = organice.exe
  LDLIBS = -ladvapi32
else
  TARGET = organice
  LDLIBS =
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o organice organice.exe

.PHONY: all clean

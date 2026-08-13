CC = gcc
CFLAGS = -O2 -Wall -Wextra
LDLIBS = -lX11 -lXfixes -lXrandr

TARGET = xscreen-barrier
PREFIX ?= $(HOME)/.local
BINDIR = $(PREFIX)/bin

.PHONY: all build install clean

all: build

build: $(TARGET)

$(TARGET): xscreen-barrier.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

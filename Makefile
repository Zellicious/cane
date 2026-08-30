TARGET   := cane
CC       := gcc
PKG_CFLAGS := $(shell pkg-config --cflags freetype2 luajit physfs)
PKG_LIBS   := $(shell pkg-config --libs freetype2 luajit physfs)
CFLAGS   := -O2 -Wall -Wextra -std=c11 -Iinclude -Ivendor $(PKG_CFLAGS)
LDFLAGS  := $(PKG_LIBS) -lglfw -lGL -lm -lpthread
SRCS     := $(wildcard src/*.c)
OBJS     := $(patsubst src/%.c, build/%.o, $(SRCS))
.PHONY: all clean
all: $(TARGET)
$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful: ./$(TARGET)"
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build:
	mkdir -p build
clean:
	rm -rf build $(TARGET)

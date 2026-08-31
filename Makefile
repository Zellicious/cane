TARGET   := build/cane/cane
CC       := gcc
DIST_DIR := build/cane

LIB_PHYSFS   := $(shell $(CC) -print-file-name=libphysfs.so)
LIB_LUAJIT   := $(shell $(CC) -print-file-name=libluajit-5.1.so)
LIB_GLFW     := $(shell $(CC) -print-file-name=libglfw.so)
LIB_FREETYPE := $(shell $(CC) -print-file-name=libfreetype.so)

PKG_CFLAGS := $(shell pkg-config --cflags freetype2 luajit physfs)
PKG_LIBS   := $(shell pkg-config --libs freetype2 luajit physfs)
CFLAGS   := -O3 -Wall -Wextra -std=c11 -Iinclude -Ivendor $(PKG_CFLAGS)
LDFLAGS  := $(PKG_LIBS) -lglfw -lGL -lm -lpthread -Wl,-rpath,'$$ORIGIN'
SRCS     := $(wildcard src/*.c)
OBJS     := $(patsubst src/%.c, build/%.o, $(SRCS))

.PHONY: all clean bundle

all: bundle

$(TARGET): $(OBJS) | $(DIST_DIR)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful: ./$@"

bundle: $(TARGET)
	cp -L $(LIB_PHYSFS)   $(DIST_DIR)/libphysfs.so.1
	cp -L $(LIB_LUAJIT)   $(DIST_DIR)/libluajit-5.1.so.2
	cp -L $(LIB_GLFW)     $(DIST_DIR)/libglfw.so.3
	cp -L $(LIB_FREETYPE) $(DIST_DIR)/libfreetype.so.6
	@echo "Packaged executable and .so files into $(DIST_DIR)/"

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build $(DIST_DIR):
	mkdir -p $@

clean:
	rm -rf build

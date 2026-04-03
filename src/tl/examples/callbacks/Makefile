TESS ?= tess
CC   ?= cc

.PHONY: all clean run

all: build/main

build/main.c: src/main.tl
	mkdir -p build
	$(TESS) c src/main.tl -o build/main.c

build/callbacks.o: c/callbacks/callbacks.c c/callbacks/callbacks.h
	mkdir -p build
	$(CC) -c c/callbacks/callbacks.c -I c/callbacks -o build/callbacks.o

build/main: build/main.c build/callbacks.o
	$(CC) -Wno-format-security -I c build/main.c build/callbacks.o -o build/main

run: build/main
	./build/main

clean:
	rm -rf build

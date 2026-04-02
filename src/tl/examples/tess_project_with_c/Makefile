TESS ?= tess
CC   ?= cc

.PHONY: all clean run

all: build/main

build/cmathlib.tl: c/mathlib/mathlib.h
	mkdir -p build
	$(TESS) cbind c/mathlib/mathlib.h -o build/cmathlib.tl

build/main.c: src/main.tl build/cmathlib.tl
	$(TESS) c src/main.tl -I build -o build/main.c

build/mathlib.o: c/mathlib/mathlib.c c/mathlib/mathlib.h
	$(CC) -c c/mathlib/mathlib.c -o build/mathlib.o

build/main: build/main.c build/mathlib.o
	$(CC) build/main.c build/mathlib.o -I . -o build/main

run: build/main
	./build/main

clean:
	rm -rf build

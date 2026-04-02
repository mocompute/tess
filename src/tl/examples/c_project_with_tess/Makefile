TESS ?= tess
CC   ?= cc

.PHONY: all clean run

all: build/main

build/libmathlib.a build/mathlib.h: tess/mathlib/package.tl tess/mathlib/src/mathlib.tl
	mkdir -p build
	cd tess/mathlib && $(TESS) lib --static -o ../../build/libmathlib.a

build/main: src/main.c build/libmathlib.a build/mathlib.h
	$(CC) -I build src/main.c build/libmathlib.a -o build/main

run: build/main
	./build/main

clean:
	rm -rf build

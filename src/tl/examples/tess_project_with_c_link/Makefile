TESS ?= tess
CC   ?= cc
AR   ?= ar

.PHONY: all clean run

all: build/main

build/libmathlib.a: c/mathlib/mathlib.c c/mathlib/mathlib.h
	mkdir -p build
	$(CC) -c c/mathlib/mathlib.c -o build/mathlib.o
	$(AR) rcs build/libmathlib.a build/mathlib.o

build/cmathlib.tl: c/mathlib/mathlib.h
	mkdir -p build
	$(TESS) cbind c/mathlib/mathlib.h -o build/cmathlib.tl

build/main: src/main.tl build/cmathlib.tl build/libmathlib.a
	LDFLAGS="-L build" $(TESS) exe src/main.tl -I build -o build/main

run: build/main
	./build/main

clean:
	rm -rf build

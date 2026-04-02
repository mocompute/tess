TESS ?= tess
CC   ?= cc

.PHONY: all clean run

all: main

cmathlib.tl: mathlib.h
	$(TESS) cbind mathlib.h -o cmathlib.tl

main.c: main.tl cmathlib.tl
	$(TESS) c main.tl -o main.c

main: main.c mathlib.c
	$(CC) -I . main.c mathlib.c -o main

run: main
	./main

clean:
	rm -f main main.c cmathlib.tl

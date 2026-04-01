TESS ?= tess
CC   ?= cc

.PHONY: all clean run

all: main

libmathlib.a mathlib.h: mathlib.tl
	$(TESS) lib --static mathlib.tl -o libmathlib.a

main: main.c libmathlib.a mathlib.h
	$(CC) main.c libmathlib.a -o main

run: main
	./main

clean:
	rm -f main libmathlib.a mathlib.h

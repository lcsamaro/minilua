minilua: tiny.c env.o
	gcc -O3 -o minilua tiny.c env.o -Wall -pedantic

minilua-d: tiny.c
	gcc -O0 -g -o minilua-d tiny.c -Wall

minilua-m: minilua
	gcc -Os -o minilua-m tiny.c -Wall -pedantic
	strip -s --strip-unneeded minilua-m

env.o: env.asm
	nasm -felf64 env.asm


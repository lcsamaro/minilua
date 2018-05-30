minilua: minilua.c env.o
	gcc -O3 -o minilua minilua.c env.o -Wall -pedantic

minilua-d: minilua.c
	gcc -O0 -g -o minilua-d minilua.c -Wall

minilua-m: minilua
	gcc -Os -o minilua-m minilua.c -Wall -pedantic
	strip -s --strip-unneeded minilua-m

env.o: env.asm
	nasm -felf64 env.asm


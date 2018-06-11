minilua: minilua.c
	gcc -O3 -o minilua minilua.c -lm -Wall

minilua-d: minilua.c
	gcc -O0 -g -o minilua-d minilua.c -lm -Wall

minilua-m: minilua
	gcc -Os -o minilua-m minilua.c -Wall -lm -pedantic
	strip -s --strip-unneeded minilua-m

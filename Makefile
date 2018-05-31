minilua: minilua.c
	gcc -O3 -o minilua minilua.c -Wall

minilua-d: minilua.c
	gcc -O0 -g -o minilua-d minilua.c -Wall

minilua-m: minilua
	gcc -Os -o minilua-m minilua.c -Wall -pedantic
	strip -s --strip-unneeded minilua-m

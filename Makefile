minilua: minilua.c common.h parser.h parser.c ir.h ir.c lex.h lex.c state.h rhhm.h string.h rhhm.c string.c value.c value.h
	gcc -O3 -o minilua value.c parser.c ir.c lex.c rhhm.c string.c minilua.c -lm -Wall


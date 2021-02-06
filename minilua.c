#include "cc.h"
#include "common.h"
#include "ir.h"
#include "lapi.h"
#include "lex.h"
#include "value.h"
#include "parser.h"
#include "rhhm.h"
#include "string.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
	prof_begin("all");
	
	if (argc < 2) return 0;

	state L;
	lua_init(&L);

	prof_begin("loadfile");
	void *s = lua_loadfile(&L, argv[1]);
	prof_end();

	puts("* PCALL *");

	prof_begin("pcall");
	lua_pcall(&L, s);
	prof_end();

	lua_destroy(&L);
	
//#ifdef DBG
	printf("parser: %lu KB\n", sizeof(parser)/1024);
	printf("ir state: %lu KB\n", sizeof(ir)/1024);
//#endif

	prof_end();
	prof_results();

	return 0;
}


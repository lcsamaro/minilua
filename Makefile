UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
ARCH=elf64
endif

ifeq ($(UNAME), Darwin)
ARCH=macho64 --prefix _
endif


CCFLAGS = -O3 -ffast-math -flto -m64 -g -Wall -Wno-parentheses

CXX = gcc

CXX = g++
CXXFLAGS += $(CCFLAGS) -fno-exceptions

all: minilua

debug: CCFLAGS = -O1 -m64 -g
debug: all

mini: CCFLAGS = -Os -flto -m64
mini: all

env.o: env.asm
	nasm -f $(ARCH) env.asm

common.o: common.c common.h
	gcc $(CCFLAGS) -c common.c

lex.o: lex.c lex.h common.h
	gcc $(CCFLAGS) -c lex.c

parser.o: parser.c parser.h common.h
	gcc $(CCFLAGS) -c parser.c

ir.o: ir.c ir.h common.h
	gcc $(CCFLAGS) -c ir.c

cc.o: cc.c cc.h common.h
	gcc $(CCFLAGS) -c cc.c

lapi.o: lapi.c lapi.h common.h
	gcc $(CCFLAGS) -c lapi.c

minilua: minilua.c common.h value.c value.h rhhm.c rhhm.h string.c string.h env.o lex.o parser.o ir.o cc.o lapi.o common.o
	gcc $(CCFLAGS) -o minilua minilua.c value.c rhhm.c string.c env.o lex.o parser.o ir.o cc.o lapi.o common.o -lm -ldl

scratch: scratch.asm
	nasm -f bin scratch.asm -o scratch.o
	ndisasm -b 64 scratch.o

dump:
	ndisasm -b 64 dump.bin

# -rdynamic


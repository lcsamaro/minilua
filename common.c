#include "common.h"

#include <stdio.h>
#include <time.h>

#if ML_MEASURING

static u64 ntime() {
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec * (u64)1e9 + t.tv_nsec;
}

#define MAX_MARKS 1024

struct structured_bmark {
	const char *tag;
	u8 depth;
	u64 dt;
};

static struct structured_bmark marks[MAX_MARKS];
static struct structured_bmark marks_done[MAX_MARKS];
static int current_mark    = 0;
static int current_depth   = 0;
static int completed_marks = 0;

void prof_begin(const char *tag) {
	marks[current_mark].tag = tag;
	marks[current_mark].depth = current_depth++;
	marks[current_mark].dt = ntime();
	current_mark++;
}

void prof_end() {
	marks_done[completed_marks] = marks[--current_mark];
	marks_done[completed_marks].dt = ntime() - marks_done[completed_marks].dt;
	completed_marks++;
	current_depth--;
}

void prof_results() {
	puts("\n* * * * * * * * results * * * * * * * *");
	for (int i = completed_marks-1; i >= 0; i--) {
		for (int j = 0; j < marks_done[i].depth; j++) {
			printf(" ");
		}
		printf("%-10s %f ms\n", marks_done[i].tag, marks_done[i].dt * (1/1e6));
	}
}

#endif

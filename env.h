#ifndef ENV_H
#define ENV_H

#include "common.h"

u64 ml_indirect_call(u64);
u64 ml_indirect_luacall(u64);

void *ml_get_rbx();
void *ml_get_rbp();
void *ml_get_r12();
void *ml_get_r13();
void *ml_get_r14();
void *ml_get_r15();
void *ml_get_rsp();

#endif // ENV_H

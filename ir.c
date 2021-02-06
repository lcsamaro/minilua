#include "ir.h"
#include "lex.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int ir_is_jmp(u32 op) {
	return op & IR_OP_JMP;
}

int ir_is_mark(u32 op) {
	return op & IR_MARK;
}

int ir_current(ir *c) {
	return vsize(c->ops);
}

int ir_newvar(ir *c) {
	return c->iv++;
}

int ir_init(ir *c) {
	if (rhhm_init(&c->ctt_map, IR_CTT_MAX*2, 0)) {
		return 1;
	}
	vclear(c->ctts);
	vclear(c->ops);
	c->iv = 0;
	c->iphi = IR_OP_MAX;
	c->phidepth = 0;
	return 0;
}

void ir_destroy(ir *c) {
	rhhm_destroy(&c->ctt_map);
}

int ir_ctt(ir *c, bv v) {
	if (vfull(c->ctts)) abort();

	bv idx = hm_get(&c->ctt_map, v);
	if (idx.u != bv_nil) return idx.i;

	vpush(c->ctts, v); //c->ctts[c->ic++] = v;

	idx.i = -vsize(c->ctts); //-c->ic;
	hm_set(&c->ctt_map, v, idx);

	return -vsize(c->ctts); //-c->ic;
}

int ir_op(ir *c, i16 op, i16 a, i16 b, u16 t) {
	if (vfull(c->ops)) abort();
	tac tmp;
	tmp.op = op;
	tmp.a = a;
	tmp.b = b;
	tmp.target = t;
	vpush(c->ops, tmp);
	/*int i = c->io++;
	c->ops[i].op = op;
	c->ops[i].a = a;
	c->ops[i].b = b;
	c->ops[i].target = t;*/
	return t;
}

int ir_phi_begin(ir *c, int type) {
	int i = --c->iphi;
	vget(c->ops, i).op = IR_OP_NOOP; // mark

	c->phidepth++;

	c->phi_join_pos[c->phidepth] = vsize(c->ops);
	c->phi_join_type[c->phidepth] = type;

	return c->phidepth;
}

int ir_phi_ins(ir *c, int val, int old, u16 *assignment) {
	if (!c->phidepth) return 1;

	int i = c->iphi;
	while (vget(c->ops, i).op != IR_OP_NOOP) {
		if (vget(c->ops, i).a == old || vget(c->ops, i).b == old) {
			goto found;
		}
		i++;
	}

	i = --c->iphi;
	vget(c->ops, i).op = IR_OP_PHI;
	vget(c->ops, i).b = old;
	vget(c->ops, i).target = old;

found:
	vget(c->ops, i).a = val;
	return 0;
}

int ir_phi_commit(ir *c, u16 *assignment) {
	int i, j;
	i = j = c->iphi;

	while (vget(c->ops, c->iphi++).op != IR_OP_NOOP) j++;
	j--;

	int jointype = c->phi_join_type[c->phidepth];
	int joinpos = c->phi_join_pos[c->phidepth];
	int until = vsize(c->ops);
	int shift = (j-i)+1;

	c->phidepth--;
	while (j >= i) {
		int old = vget(c->ops, j).target;
		int olda = vget(c->ops, j).a;
		int oldb = vget(c->ops, j).b;
		int nv = ir_newvar(c);
		
		assignment[nv] = assignment[old];
		assignment[old] = nv;
		assignment[olda] = nv;
		assignment[oldb] = nv;

		if (jointype != PHI_COND) { // fix loop var usages
			for (int k = joinpos; k < until; k++) {
				if (vget(c->ops, k).op == IR_OP_NOOP) continue;
				int replace = vget(c->ops, j).target; //a;
				if (vget(c->ops, k).a == replace) vget(c->ops, k).a = nv;
				if (vget(c->ops, k).b == replace && vget(c->ops, k).op != IR_OP_CALL)
					vget(c->ops, k).b = nv;
			}
		}

		vget(c->ops, j).target = nv;
		vpush(c->ops, vget(c->ops, j));

		if (c->phidepth) { // commit to upper level
			ir_phi_ins(c, nv, old, assignment);
		}

		j--;
	}

	if (jointype != PHI_COND && shift > 0) {
		for (int k = joinpos; k < until; k++) // fix jumps
			if (ir_is_jmp(vget(c->ops, k).op) && vget(c->ops, k).target > joinpos)
				vget(c->ops, k).target += shift;

		//printf("shifting block of sz %d down %d positions\n", (until-joinpos) + shift, shift);
		memmove(vbegin(c->ops) + joinpos + shift, vbegin(c->ops) + joinpos, ((until-joinpos) + shift) * sizeof(tac));
		memcpy(vbegin(c->ops) + joinpos, vbegin(c->ops) + joinpos + shift + (until-joinpos), shift * sizeof(tac));
	}

	return shift;
}

void ir_phi_elim(ir *c) { // eliminate phi nodes

	u16 var_live_end[IR_OP_MAX] = {0}; // last use of var
	for (int i = 0; i < vsize(c->ops); i++) {
		int op = vget(c->ops, i).op;
		if (op == IR_OP_NOOP) continue;
		int a = vget(c->ops, i).a;
		int b = vget(c->ops, i).b;
		int target = vget(c->ops, i).target;

		if (op == IR_FUNCTION_BEGIN || op == IR_FUNCTION_END) continue;

		if (!ir_is_jmp(op) && target != IR_NO_TARGET) var_live_end[target] = i;
		if (op != IR_OP_PHI) {
			if (a != IR_NO_ARG) var_live_end[a] = i;
			if (b != IR_NO_ARG && op != IR_OP_CALL) var_live_end[b] = i;
		}
	}
	u16 var_live_ini[IR_OP_MAX]; // first use of var
	memset(var_live_ini, 0xff, sizeof var_live_ini);
	for (int i = vsize(c->ops); i-- > 0; ) {
		int op = vget(c->ops, i).op;
		if (op == IR_OP_NOOP || op == IR_FUNCTION_BEGIN || op == IR_FUNCTION_END) continue;
		int target = vget(c->ops, i).target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) var_live_ini[target] = i;
	}

#ifdef DBG
	printf("  .");
	for (int j = 0; j < c->iv; j++) printf("%2d", j % 10);
	puts("");
	for (int i = 0; i < vsize(c->ops); i++) {
		printf("%2d. ", i);
		for (int j = 0; j < c->iv; j++) {
			if (i >= var_live_ini[j] && i <= var_live_end[j]) {
				printf("@ ");
			} else {
				printf(". ");
			}
		}
		printf("\n");
	}
#endif

	for (int i = 0; i < vsize(c->ops); i++) {
		int op = vget(c->ops, i).op;
		int a = vget(c->ops, i).a;
		int b = vget(c->ops, i).b;
		int target = vget(c->ops, i).target;
		if (op == IR_OP_PHI) {
			int ai = var_live_ini[a];
			int ae = var_live_end[a];
			int bi = var_live_ini[b];
			int be = var_live_end[b];

#ifdef DBG
			printf(" 1.sub %d for %d\n", a, target);
			printf(" 2.sub %d for %d\n", b, target);
#endif

			//if (ae < bi || be < ai) {
				// CSSA only

				while (ai <= ae) {
					if (vget(c->ops, ai).a == a) vget(c->ops, ai).a = target;
					if (vget(c->ops, ai).b == a) vget(c->ops, ai).b = target;
					if (!ir_is_jmp(vget(c->ops, ai).op) && vget(c->ops, ai).target == a)
						vget(c->ops, ai).target = target;
					ai++;
				}
				while (bi <= be) {
					if (vget(c->ops, bi).a == b) vget(c->ops, bi).a = target;
					if (vget(c->ops, bi).b == b) vget(c->ops, bi).b = target;
					if (!ir_is_jmp(vget(c->ops, bi).op) && vget(c->ops, bi).target == b)
						vget(c->ops, bi).target = target;
					bi++;
				}
				vget(c->ops, i).op = IR_OP_NOOP;

				if (ai < var_live_ini[target]) var_live_ini[target] = ai;
				if (bi < var_live_ini[target]) var_live_ini[target] = bi;

				if (ae > var_live_end[target]) var_live_ini[target] = ae;
				if (be > var_live_end[target]) var_live_ini[target] = be;
			/*} else {
				c->ops[i].op = IR_OP_NOOP;
			}*/
		}
	}
}

/*opt*/
static int fold(ir *c, tac *t) {
	if (t->op == IR_OP_LCOPY && t->a < 0) return t->a;
	if (t->a >= 0 || t->b >= 0) return 0;

	bv a = vget(c->ctts, -t->a-1);
	bv b = vget(c->ctts, -t->b-1);
	bv v;
	switch (t->op) {
	case '+': v = bv_add(NULL, a, b); break;
	case '*': v = bv_mul(NULL, a, b); break;
	case '-': v = bv_sub(NULL, a, b); break;
	case '/': v = bv_div(NULL, a, b); break;
	default: return 0;
	}
	return ir_ctt(c, v);
}

void ir_opt(ir *c) {

	BSET(stored, IR_OP_MAX);

	u16 var_live_end[IR_OP_MAX] = {0}; // last use of var
	for (int i = 0; i < IR_OP_MAX; i++) var_live_end[i] = -1;
	for (int i = 0; i < vsize(c->ops); i++) {
		int op = vget(c->ops, i).op;
		if (op == IR_OP_NOOP || ir_is_mark(op)) continue;

		int a = vget(c->ops, i).a;
		int b = vget(c->ops, i).b;
		//int target = vget(c->ops, i).target;

		if (op == IR_OP_PHI) {
			if (a >= 0) BSET_SET(stored, a);
			if (b >= 0) BSET_SET(stored, b);
		}

		if (a >= 0 && a != IR_NO_ARG) var_live_end[a] = i;
		if (b >= 0 && b != IR_NO_ARG && op != IR_OP_CALL) var_live_end[b] = i;
	}
	u16 var_live_ini[IR_OP_MAX]; // first use of var
	memset(var_live_ini, 0xff, sizeof var_live_ini);
	for (int i = vsize(c->ops); i-- > 0; ) {
		int op = vget(c->ops, i).op;
		if (op == IR_OP_NOOP || ir_is_mark(op)) continue;
		int target = vget(c->ops, i).target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) var_live_ini[target] = i;
	}

	for (int i = 1; i < vsize(c->ops); i++) { // peephole
		int o0 = vget(c->ops, i-1).op;
		int a0 = vget(c->ops, i-1).a;
		int b0 = vget(c->ops, i-1).b;
		int t0 = vget(c->ops, i-1).target;
		int o1 = vget(c->ops, i  ).op;
		int a1 = vget(c->ops, i  ).a;
		//int b1 = vget(c->ops, i  ).b;
		int t1 = vget(c->ops, i  ).target;

		if (o0 == LEX_NE && o1 == IR_OP_JZ && a1 == t0) {
			vget(c->ops, i).op = IR_OP_JE; vget(c->ops, i).a = a0; vget(c->ops, i).b = b0;
			vget(c->ops, i-1).op = IR_OP_NOOP;
		} else if (o0 == LEX_NE && o1 == IR_OP_JNZ && a1 == t0) {
			vget(c->ops, i).op = IR_OP_JNE; vget(c->ops, i).a = a0; vget(c->ops, i).b = b0;
			vget(c->ops, i-1).op = IR_OP_NOOP;
		} else if (o0 == LEX_EQ && o1 == IR_OP_JZ && a1 == t0) {
			vget(c->ops, i).op = IR_OP_JNE; vget(c->ops, i).a = a0; vget(c->ops, i).b = b0;
			vget(c->ops, i-1).op = IR_OP_NOOP;
		} else if (o0 == LEX_EQ && o1 == IR_OP_JNZ && a1 == t0) {
			vget(c->ops, i).op = IR_OP_JE; vget(c->ops, i).a = a0; vget(c->ops, i).b = b0;
			vget(c->ops, i-1).op = IR_OP_NOOP;
		}


		if (o1 == IR_OP_LCOPY && t0 == a1 && var_live_end[a1] == i) {
			vget(c->ops, i-1).target = t1;
			vget(c->ops, i).op = IR_OP_NOOP;
		}

	}
}

#define COLORF(c) "\x1b[3" #c "m"
#define COLORFB(c) "\x1b[9" #c "m"
#define COLORB(c) "\x1b[4" #c "m"
//#define RESETF "\x1b[39m"
#define RESETB "\x1b[49m"
#define RESET "\x1b[0m"
#define RESETF RESET

void ir_disp(ir *c) {
	int i;
	for (i = 0; i < vsize(c->ops); i++) {

		tac cur = vget(c->ops, i);

		if (cur.op == IR_OP_NOOP) {
			//puts("--- nop ---");
			continue;
		}
		printf("%04d ", i);
		if (cur.op == IR_LOOP_HEADER) {
			puts("--- loop-header ---");
			continue;
		}
		if (cur.op == IR_LOOP_BEGIN) {
			puts("--- loop-block ---");
			continue;
		}
		if (cur.op == IR_LOOP_END) {
			puts("--- loop-end ---");
			continue;
		}
		if (cur.op == IR_FUNCTION_BEGIN) {
			printf("--- function-header --- %d\n", cur.target);
			continue;
		}
		if (cur.op == IR_FUNCTION_END) {
			printf("--- function-end --- %d\n", cur.target);
			continue;
		}

		printf(COLORF(4));
		if (!ir_is_jmp(cur.op)) {
			switch (c->types[cur.target]) {
			/*case IR_TYPE_NONE: printf("%3s ", "<?>"); break;
			case IR_TYPE_ANY: printf("%3s ", "any"); break;
			case IR_TYPE_NUM: printf("%3s ", "num"); break;
			case IR_TYPE_INT: printf("%3s ", "int"); break;*/
			}
		} else {
			//printf("    ");
		}
		printf(RESETF);

		if (cur.op < 256) {
			printf("%c    ", cur.op);
		} else {
			switch (cur.op) {
			case LEX_EQ:       printf("eq   "); break;
			case LEX_NE:       printf("ne   "); break;
			case IR_OP_NEWTBL: printf("tnew "); break;
			case IR_OP_LCOPY:  printf("lcpy "); break;
			case IR_OP_COPY:   printf("cpy  "); break;
			case IR_OP_TLOAD:  printf("tget "); break;
			case IR_OP_TSTORE: printf("tset "); break;
			case IR_OP_GLOAD:  printf(COLORF(5) "gget " RESETF); break;
			case IR_OP_GSTORE: printf(COLORF(5) "gset " RESETF); break;
			case IR_OP_JZ:     printf("jz   "); break;
			case IR_OP_JNZ:    printf("jnz  "); break;
			case IR_OP_JMP:    printf("jmp  "); break;
			case IR_OP_PHI:    printf("phi  "); break;
			case IR_OP_RET:    printf("ret  "); break;
			case IR_OP_INC:    printf("inc  "); break;
			case IR_OP_DEC:    printf("dec  "); break;
			case IR_OP_JE:     printf("JE   "); break;
			case IR_OP_JNE:    printf("JNE  "); break;
			case IR_OP_CALL:   printf("call "); break;
			case IR_OP_ARG:    printf("arg  "); break;
			case IR_OP_PARAM:  printf("par  "); break;
			}
		}
		if (cur.a != IR_NO_ARG) {
			if (cur.a < 0) {
				if (bv_is_str(vget(c->ctts, -cur.a-1))) {
					printf("%10s", "str");
				} else if (bv_is_sstr(vget(c->ctts, -cur.a-1))) {
					printf(COLORFB(2));
					printf("%10.*s",
						bv_get_sstr_len(vget(c->ctts, -cur.a-1)),
						bv_get_sstr(&vget(c->ctts, -cur.a-1)));
					printf(RESETF);
				} else {
					printf(COLORFB(4));
					printf("%10.5g", vget(c->ctts, -cur.a-1).d);
					printf(RESETF);
				}
			} else printf("%10d", cur.a);
		} else printf("%10s", "-");
		printf(" ");
		if (cur.b != IR_NO_ARG) {
			if (cur.b < 0) {
				if (bv_is_str(vget(c->ctts, -cur.b-1))) {
					printf("%10s", "str");
				} else if (bv_is_sstr(vget(c->ctts, -cur.b-1))) {
					printf(COLORFB(2));
					printf("%10.*s",
						bv_get_sstr_len(vget(c->ctts, -cur.b-1)),
						bv_get_sstr(&vget(c->ctts, -cur.b-1)));
					printf(RESETF);
				} else {
					printf(COLORFB(4));
					printf("%10.5g", vget(c->ctts, -cur.b-1).d);
					printf(RESETF);
				}
			} else printf("%10d", cur.b);
		} else printf("%10s", "-");
		if (cur.target != IR_NO_TARGET) printf("%10d", cur.target);
		else printf("%10s", "-");
		if (ir_is_jmp(cur.op)) printf(" ~>" RESETF);
		puts("");
	}
}


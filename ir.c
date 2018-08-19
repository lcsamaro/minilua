#include "ir.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int ir_is_jmp(u32 op) {
	return op & IR_OP_JMP;
}

int ir_current(ir *c) {
	return c->io;
}

int ir_newvar(ir *c) {
	return c->iv++;
}

void ir_init(ir *c) {
	rhhm_init_fixed(&c->ctt_map, c->ctt_tbl, IR_CTT_MAX*2);
	c->ic = c->io = c->iv = 0;
	c->iphi = IR_OP_MAX;
	c->phidepth = 0;
}

int ir_ctt(ir *c, bv v) {
	if (c->ic >= IR_CTT_MAX) abort();

	bv idx = hm_get(&c->ctt_map, v);
	if (idx.u != bv_nil) return idx.i;

	c->ctts[c->ic++] = v;

	idx.i = -c->ic;
	hm_set(&c->ctt_map, v, idx);

	return -c->ic;
}

int ir_op(ir *c, i16 op, i16 a, i16 b, u16 t) {
	if (c->io >= IR_OP_MAX) abort();
	int i = c->io++;
	c->ops[i].op = op;
	c->ops[i].a = a;
	c->ops[i].b = b;
	c->ops[i].target = t;
	return t;
}

int ir_phi_begin(ir *c, int type) {
	int i = --c->iphi;
	c->ops[i].op = IR_OP_NOOP; // mark

	c->phidepth++;

	c->phi_join_pos[c->phidepth] = c->io;
	c->phi_join_type[c->phidepth] = type;

	return c->phidepth;
}

int ir_phi_ins(ir *c, int val, int old, u16 *assignment) {
	if (!c->phidepth) return 1;

	int i = c->iphi;
	while (c->ops[i].op != IR_OP_NOOP) {
		if (c->ops[i].a == old ||
			c->ops[i].b == old) {
			goto found;
		}
		i++;
	}

	i = --c->iphi;
	c->ops[i].op = IR_OP_PHI;
	c->ops[i].b = old;
	c->ops[i].target = old;

found:
	c->ops[i].a = val;
	return 0;
}

int ir_phi_commit(ir *c, u16 *assignment) {
	int i, j;
	i = j = c->iphi;

	while (c->ops[c->iphi++].op != IR_OP_NOOP) j++;
	j--;

	int jointype = c->phi_join_type[c->phidepth];
	int joinpos = c->phi_join_pos[c->phidepth];
	int until = c->io;
	int shift = (j-i)+1;

	c->phidepth--;
	while (j >= i) {
		int old = c->ops[j].target;
		int olda = c->ops[j].a;
		int oldb = c->ops[j].b;
		int nv = ir_newvar(c);
		
		assignment[nv] = assignment[old];
		assignment[old] = nv;
		assignment[olda] = nv;
		assignment[oldb] = nv;

		if (jointype != PHI_COND) { // fix loop var usages
			for (int k = joinpos; k < until; k++) {
				int replace = c->ops[j].target; //a;
				if (c->ops[k].a == replace) c->ops[k].a = nv;
				if (c->ops[k].b == replace) c->ops[k].b = nv;
			}
		}

		c->ops[j].target = nv;
		c->ops[c->io++] = c->ops[j];

		if (c->phidepth) { // commit to upper level
			ir_phi_ins(c, nv, old, assignment);
		}

		j--;
	}

	if (jointype != PHI_COND && shift > 0) {
		for (int k = joinpos; k < until; k++) // fix jumps
			if (ir_is_jmp(c->ops[k].op) && c->ops[k].target > joinpos)
				c->ops[k].target += shift;

		//printf("shifting block of sz %d down %d positions\n", (until-joinpos) + shift, shift);
		memmove(c->ops + joinpos + shift, c->ops + joinpos, ((until-joinpos) + shift) * sizeof(tac));
		memcpy(c->ops + joinpos, c->ops + joinpos + shift + (until-joinpos), shift * sizeof(tac));
	}

	return shift;
}

void ir_phi_elim(ir *c) { // eliminate phi nodes

	// TODO

	puts(" * elim * ");
	u16 var_live_end[IR_OP_MAX] = {0}; // last use of var
	for (int i = 0; i < c->io; i++) {
		int op = c->ops[i].op;
		if (op == IR_OP_NOOP) continue;
		int a = c->ops[i].a;
		int b = c->ops[i].b;
		int target = c->ops[i].target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) var_live_end[target] = i;
		if (op != IR_OP_PHI) {
			if (a != IR_NO_ARG) var_live_end[a] = i;
			if (b != IR_NO_ARG) var_live_end[b] = i;
		}
	}
	u16 var_live_ini[IR_OP_MAX]; // first use of var
	memset(var_live_ini, 0xff, sizeof var_live_ini);
	for (int i = c->io; i-- > 0; ) {
		int op = c->ops[i].op;
		if (op == IR_OP_NOOP) continue;
		int target = c->ops[i].target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) var_live_ini[target] = i;
	}

	printf("  .");
	for (int j = 0; j < c->iv; j++) printf("%2d", j % 10);
	puts("");
	for (int i = 0; i < c->io; i++) {
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
	for (int i = 0; i < c->io; i++) {
		int op = c->ops[i].op;
		int a = c->ops[i].a;
		int b = c->ops[i].b;
		int target = c->ops[i].target;
		if (op == IR_OP_PHI) {
			int ai = var_live_ini[a];
			int ae = var_live_end[a];
			int bi = var_live_ini[b];
			int be = var_live_end[b];
			if (ae < bi || be < ai) {
				while (ai <= ae) {
					if (c->ops[ai].a == a) c->ops[ai].a = target;
					if (c->ops[ai].b == a) c->ops[ai].b = target;
					if (!ir_is_jmp(c->ops[ai].op) && c->ops[ai].target == a) c->ops[ai].target = target;
					ai++;
				}
				while (bi <= be) {
					if (c->ops[bi].a == b) c->ops[bi].a = target;
					if (c->ops[bi].b == b) c->ops[bi].b = target;
					if (!ir_is_jmp(c->ops[bi].op) && c->ops[bi].target == b) c->ops[bi].target = target;
					bi++;
				}
				c->ops[i].op = IR_OP_NOOP;

				if (ai < var_live_ini[target]) var_live_ini[target] = ai;
				if (bi < var_live_ini[target]) var_live_ini[target] = bi;

				if (ae > var_live_end[target]) var_live_ini[target] = ae;
				if (be > var_live_end[target]) var_live_ini[target] = be;
			} else {
				c->ops[i].op = IR_OP_NOOP;
			}
		}
	}
}

/* typing */
#define PASSES 8
void ir_infer(ir *c) {
	memset(c->types, IR_TYPE_NONE, IR_OP_MAX);
	int lastn = 0;
	for (int p = 0; p < PASSES; p++) {
		for (int i = 0; i < c->io; i++) {
			int op = c->ops[i].op;
			int a = IR_TYPE_NONE;
			if (c->ops[i].a < 0) {
				bv v = c->ctts[-c->ops[i].a -1];
				if (bv_is_double(v)) {
					double dummy;
					if (modf(v.d, &dummy) == 0.0) {
						c->types[c->ops[i].a] = IR_TYPE_INT;
					} else {
						c->types[c->ops[i].a] = IR_TYPE_NUM;
					}
				} else {
					c->types[c->ops[i].a] = IR_TYPE_ANY;
				}
			}
			a = c->types[c->ops[i].a];

			int b = IR_TYPE_NONE;
			if (c->ops[i].b < 0) {
				bv v = c->ctts[-c->ops[i].b -1];
				if (bv_is_double(v)) {
					double dummy;
					if (modf(v.d, &dummy) == 0.0) {
						c->types[c->ops[i].b] = IR_TYPE_INT;
					} else {
						c->types[c->ops[i].b] = IR_TYPE_NUM;
					}
				} else {
					c->types[c->ops[i].b] = IR_TYPE_ANY;
				}
			}
			b = c->types[c->ops[i].b];

			int t = c->types[c->ops[i].target];

			switch (op) {
			case '+': case '-': case '*':
				if (a == b) {
					c->types[c->ops[i].target] = a;
				} else /*if ((a == IR_TYPE_INT && b == IR_TYPE_NUM) &&
					(a == IR_TYPE_NUM && b == IR_TYPE_INT))*/ {
					c->types[c->ops[i].target] = IR_TYPE_NUM;
				}
				break;
			case '/': case '%':
				c->types[c->ops[i].target] = IR_TYPE_NUM;
				break;
			case IR_OP_LCOPY:
				c->types[c->ops[i].target] = a;
				break;
			case IR_OP_PHI:
				if (a == IR_TYPE_NONE) {
					c->types[c->ops[i].target] = b;
				} else if (b == IR_TYPE_NONE) {
					c->types[c->ops[i].target] = a;
				} else if (a == b) {
					c->types[c->ops[i].target] = a;
				}
				break;
			}

		}

		int n = 0;
		for (int i = 0; i < c->io; i++) n += (c->types[i] != IR_TYPE_NONE ? 1 : 0);
		printf("pass %d: %d\n", p, n);
		if (n == lastn) break;
		lastn = n;
	}
}

/*opt*/
int fold(ir *c, tac *t) {
	if (t->op == IR_OP_LCOPY && t->a < 0) return t->a;
	if (t->a >= 0 || t->b >= 0) return 0;

	bv a = c->ctts[-t->a-1];
	bv b = c->ctts[-t->b-1];
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
	ir_infer(c);

	BITFIELD(stored, IR_OP_MAX);

	for (int i = 1; i < c->io; i++) { // peephole
		int o0 = c->ops[i-1].op;
		int a0 = c->ops[i-1].a;
		int b0 = c->ops[i-1].b;
		int t0 = c->ops[i-1].target;
		int o1 = c->ops[i].op;
		int a1 = c->ops[i].a;
		int b1 = c->ops[i].b;
		int t1 = c->ops[i].target;

		if (o0 == LEX_NE && o1 == IR_OP_JZ && a1 == t0) {
			c->ops[i].op = IR_OP_JE; c->ops[i].a = a0; c->ops[i].b = b0;
		} else if (o0 == '+' && b0 < 0 && c->ctts[-b0 - 1].d == 1.0) {
			/*c->ops[i-1].op = IR_OP_INC;
			c->ops[i-1].b = IR_NO_ARG;*/
		}

		//if (o1 != IR_OP_PHI && o0 == IR_OP_LCOPY && b1 == t0) c->ops[i].b = a0;
	}

	u16 var_live_end[IR_OP_MAX] = {0}; // last use of var
	for (int i = 0; i < IR_OP_MAX; i++) var_live_end[i] = -1;
	for (int i = 0; i < c->io; i++) {
		int op = c->ops[i].op;
		if (op == IR_OP_NOOP) continue;
		int a = c->ops[i].a;
		int b = c->ops[i].b;
		int target = c->ops[i].target;

		if (op == IR_OP_PHI) {
			if (a >= 0) BITFIELD_SET(stored, a);
			if (b >= 0) BITFIELD_SET(stored, b);
		}

		if (a >= 0 && a != IR_NO_ARG) var_live_end[a] = i;
		if (b >= 0 && b != IR_NO_ARG) var_live_end[b] = i;
	}
	u16 var_live_ini[IR_OP_MAX]; // first use of var
	memset(var_live_ini, 0xff, sizeof var_live_ini);
	for (int i = c->io; i-- > 0; ) {
		int op = c->ops[i].op;
		if (op == IR_OP_NOOP) continue;
		int target = c->ops[i].target;
		if (!ir_is_jmp(op) && target != IR_NO_TARGET) var_live_ini[target] = i;
	}


	for (int i = 0; i < c->io; i++) {
		int op = c->ops[i].op;
		int target = c->ops[i].target;
		int nt;

		if (ir_is_jmp(op) || op == IR_OP_PHI ||
			op == IR_LOOP_HEADER || op == IR_LOOP_BEGIN) continue;

		int ini = var_live_ini[target];
		int end = var_live_end[target];

		if (op != IR_OP_DISP &&
			op != IR_OP_GLOAD &&
			op != IR_OP_GSTORE &&
			end == (u16)-1) {
			c->ops[i].op = IR_OP_NOOP;
			continue;
		}
		if (ini > end) continue;
		if (nt = fold(c, c->ops+i)) {
			do {
				if (op == IR_OP_LCOPY && BITFIELD_GET(stored, target)/* && nt < 0*/) break;
				c->ops[i].op = IR_OP_NOOP;
				for (int j = ini; j <= end; j++) {
					if (c->ops[j].a == target)
						c->ops[j].a = nt;
					if (c->ops[j].b == target)
						c->ops[j].b = nt;
				}
			} while (0);
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
	int i = c->ic;
	for (i = 0; i < c->io; i++) {
		if (c->ops[i].op == IR_OP_NOOP) {
			//puts("-noop-");
			continue;
		}
		if (c->ops[i].op == IR_LOOP_HEADER) {
			puts("-----------LOOP-HEADER----------");
			continue;
		}
		if (c->ops[i].op == IR_LOOP_BEGIN) {
			puts("-----------LOOP-BLOCK----------");
			continue;
		}


		printf("%04d ", i);
		printf(COLORF(4));
		if (!ir_is_jmp(c->ops[i].op)) {
			switch (c->types[c->ops[i].target]) {
			case IR_TYPE_NONE: printf("%3s ", "<?>"); break;
			case IR_TYPE_ANY: printf("%3s ", "any"); break;
			case IR_TYPE_NUM: printf("%3s ", "num"); break;
			case IR_TYPE_INT: printf("%3s ", "int"); break;
			}
		} else {
			printf("    ");
		}
		printf(RESETF);

		if (c->ops[i].op < 256) {
			printf("%c     ", c->ops[i].op);
		} else {
			switch (c->ops[i].op) {
			case LEX_EQ:       printf("eq    "); break;
			case LEX_NE:       printf("ne    "); break;
			case IR_OP_NEWTBL: printf("tnew  "); break;
			case IR_OP_LCOPY:  printf("lcopy "); break;
			case IR_OP_TLOAD:  printf("tload "); break;
			case IR_OP_TSTORE: printf("tstor "); break;
			case IR_OP_GLOAD:  printf("gload "); break;
			case IR_OP_GSTORE: printf("gstor "); break;
			case IR_OP_COPY:   printf("copy  "); break;
			case IR_OP_JZ:     printf("jz    "); break;
			case IR_OP_JNZ:    printf("jnz   "); break;
			case IR_OP_JMP:    printf("jmp   "); break;
			case IR_OP_PHI:    printf("phi   "); break;
			case IR_OP_DISP:   printf("disp  "); break;
			case IR_OP_INC:    printf("inc   "); break;
			case IR_OP_DEC:    printf("dec   "); break;
			case IR_OP_JE:     printf("JE    "); break;
			case IR_OP_JNE:    printf("JNE   "); break;
			}
		}
		if (c->ops[i].a != IR_NO_ARG) {
			if (c->ops[i].a < 0) {
				if (bv_is_str(c->ctts[-c->ops[i].a-1])) {
					printf("%10s", "str");
				} else if (bv_is_sstr(c->ctts[-c->ops[i].a-1])) {
					printf(COLORFB(2));
					printf("%10.*s",
						bv_get_sstr_len(c->ctts[-c->ops[i].a-1]),
						bv_get_sstr(&c->ctts[-c->ops[i].a-1]));
					printf(RESETF);
				} else {
					printf(COLORFB(4));
					printf("%10.5g", c->ctts[-c->ops[i].a-1].d);
					printf(RESETF);
				}
			} else printf("%10d", c->ops[i].a);
		} else printf("%10s", "-");
		printf(" ");
		if (c->ops[i].b != IR_NO_ARG) {
			if (c->ops[i].b < 0) {
				if (bv_is_str(c->ctts[-c->ops[i].b-1])) {
					printf("%10s", "str");
				} else if (bv_is_sstr(c->ctts[-c->ops[i].b-1])) {
					printf(COLORFB(2));
					printf("%10.*s",
						bv_get_sstr_len(c->ctts[-c->ops[i].b-1]),
						bv_get_sstr(&c->ctts[-c->ops[i].b-1]));
					printf(RESETF);
				} else {
					printf(COLORFB(4));
					printf("%10.5g", c->ctts[-c->ops[i].b-1].d);
					printf(RESETF);
				}
			} else printf("%10d", c->ops[i].b);
		} else printf("%10s", "-");
		if (c->ops[i].target != IR_NO_TARGET) printf("%10d", c->ops[i].target);
		else printf("%10s", "-");
		if (ir_is_jmp(c->ops[i].op)) printf(" ~>" RESETF);
		puts("");
	}
}


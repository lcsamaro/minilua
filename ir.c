#include "ir.h"

#include <assert.h>
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
		if (assignment[c->ops[i].a] == assignment[old] ||
			assignment[c->ops[i].b] == assignment[old]) {
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
		assignment[assignment[old]] = nv; // TODO: needed?
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

	for (int i = 0; i < c->io; i++) {
		printf("%2d. ", i);
		for (int j = 0; j < c->iv; j++) {
			if (i >= var_live_ini[j] && i <= var_live_end[j]) {
				printf("@ ");
			} else {
				printf("  ");
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

#define COLORF(c) "\x1b[3" #c "m"
#define COLORB(c) "\x1b[4" #c "m"
#define RESETF "\x1b[39m"
#define RESETB "\x1b[49m"

void ir_disp(ir *c) {
	printf("#\top\t%15s  %15s  %15s\n", "arg 1", "arg 2", "target");
	int i = c->ic;
	while (--i >= 0) {
		if (bv_is_sstr(c->ctts[i]) || bv_is_str(c->ctts[i])) {
			printf("\tC\t%15s  %15s  %15d\n", "str", "-", -i-1);
		} else {
			printf("\tC\t%15.1lf  %15s  %15d\n", c->ctts[i].d, "-", -i-1);
		}
	}
	for (i = 0; i < c->io; i++) {
		if (ir_is_jmp(c->ops[i].op)) printf(COLORF(3));

		if (c->ops[i].op == IR_OP_NOOP) {
			puts("** nop **");
			continue;
		}

		printf("%d\t", i);

		if (c->ops[i].op < 256) {
			printf("%c", c->ops[i].op);
		} else {
			switch (c->ops[i].op) {
			case LEX_EQ: printf("eq"); break;
			case LEX_NE: printf("ne"); break;
			case IR_OP_NEWTBL: printf("tnew"); break;
			case IR_OP_LCOPY: printf("lcopy"); break;
			case IR_OP_TLOAD: printf("tload"); break;
			case IR_OP_TSTORE: printf("tstor"); break;
			case IR_OP_GLOAD: printf("gload"); break;
			case IR_OP_GSTORE: printf("gstor"); break;
			case IR_OP_COPY: printf("copy"); break;
			case IR_OP_JZ: printf("jz"); break;
			case IR_OP_JNZ: printf("jnz"); break;
			case IR_OP_JMP: printf("jmp"); break;
			case IR_OP_PHI: printf("phi"); break;
			case IR_OP_DISP: printf("disp"); break;
			case IR_OP_INC: printf("inc"); break; case IR_OP_DEC: printf("dec"); break;

			case IR_OP_JE: printf("JE"); break;
			}
		}
		printf("\t");
		if (c->ops[i].a != IR_NO_ARG) {
			if (c->ops[i].a < 0) printf("%15.1f C", c->ctts[-c->ops[i].a-1].d);
			else printf("%15d  ", c->ops[i].a);
		} else printf("%15s  ", "-");
		if (c->ops[i].b != IR_NO_ARG) {
			if (c->ops[i].b < 0) printf("%15.1f C", c->ctts[-c->ops[i].b-1].d);
			else printf("%15d  ", c->ops[i].b);
		} else printf("%15s  ", "-");
		if (c->ops[i].target != IR_NO_TARGET) printf("%15d", c->ops[i].target);
		else printf("%15s", "-");
		if (ir_is_jmp(c->ops[i].op)) printf(" ~>" RESETF);
		puts("");
	}
}


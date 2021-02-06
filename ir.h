#ifndef IR_H
#define IR_H

#include "common.h"
#include "value.h"
#include "rhhm.h"
#include "vector.h"

typedef struct {
	i16 op;
	i16 a;
	i16 b;
	u16 target;
} tac;

enum {
	IR_OP_LCOPY = 1<<10, // local load

	IR_OP_TLOAD, // table load
	IR_OP_TSTORE, // table store

	IR_OP_GLOAD, // global table load
	IR_OP_GSTORE, // global table store

	IR_OP_NEWTBL, // new table

	IR_OP_INC,
	IR_OP_DEC,

	IR_OP_PHI,

	IR_OP_COPY,

	//IR_OP_PHI_COPY,

	IR_OP_PARAM,
	IR_OP_ARG,
	IR_OP_CALL,
	IR_OP_RET,

	IR_OP_DISP, // dbg

	IR_OP_NOOP
};

enum {
	IR_OP_JMP = 1<<11,
	IR_OP_JZ,
	IR_OP_JNZ,

	IR_OP_JE,
	IR_OP_JNE
};

enum {
	IR_MARK = 1<<12,
	// loop marks
	IR_LOOP_HEADER,
	IR_LOOP_BEGIN,
	IR_LOOP_END,

	IR_FUNCTION_BEGIN,
	IR_FUNCTION_END
};


#define IR_NO_ARG (-32768)
#define IR_NO_TARGET 0xffff 
#define IR_DEPTH_MAX 128
#define IR_CTT_MAX (1<<12)
#define IR_OP_MAX  (1<<16)
#define IR_PHI_MAX  (1<<12)

enum {
	PHI_COND = 0,
	PHI_DO,
	PHI_LOOP,
	PHI_REPEAT
};

vdef(vector_ctt, bv,  IR_CTT_MAX);
vdef(vector_op,  tac, IR_OP_MAX);

typedef struct ir {
	rhhm ctt_map;

	vector_ctt ctts; //bv ctts[IR_CTT_MAX];
	vector_op   ops; //tac ops[IR_OP_MAX];

	int iv;

	// phi
	int phi_join_pos[IR_DEPTH_MAX];
	int phi_join_type[IR_DEPTH_MAX];
	int iphi;
	int phidepth;

	u8 types[IR_OP_MAX];
} ir;


int ir_is_jmp(u32 op);
int ir_is_mark(u32 op);
int ir_current(ir *c);

int ir_newvar(ir *c);
int ir_init(ir *c);
void ir_destroy(ir *c);
int ir_ctt(ir *c, bv v);

int ir_op(ir *c, i16 op, i16 a, i16 b, u16 t);

int ir_phi_begin(ir *c, int type);
int ir_phi_ins(ir *c, int val, int old, u16 *assignment);
//int ir_phi_restore(ir *c); // restore and swap
int ir_phi_commit(ir *c, u16 *assignment);

void ir_phi_elim(ir *c);

void ir_opt(ir *c);

void ir_disp(ir *c);

#endif // IR_H


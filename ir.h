#ifndef IR_H
#define IR_H

#include "common.h"
#include "lex.h" // remove lex dep
#include "value.h"
#include "rhhm.h"

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
	// loop marks
	IR_LOOP_HEADER = 1<<12,
	IR_LOOP_BEGIN,
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

enum {
	IR_TYPE_NONE = 0,
	IR_TYPE_ANY,
	IR_TYPE_NUM,
	IR_TYPE_INT,
	IR_TYPE_STR,
	IR_TYPE_TBL
};

typedef struct {
	rhhm_value ctt_tbl[IR_CTT_MAX*2];
	rhhm ctt_map;

	bv ctts[IR_CTT_MAX];
	tac ops[IR_OP_MAX];

	int ic;
	int io;
	int iv;

	// phi
	int phi_join_pos[IR_DEPTH_MAX];
	int phi_join_type[IR_DEPTH_MAX];
	int iphi;
	int phidepth;

	u8 types[IR_OP_MAX];
} ir;


int ir_is_jmp(u32 op);
int ir_current(ir *c);

int ir_newvar(ir *c);
void ir_init(ir *c);
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


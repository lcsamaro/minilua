#ifndef LEX_H
#define LEX_H

typedef struct {
	char *s;
	int type;
	int length;
} token;

enum { LEX_NUM = 1<<8, LEX_BLANKS, LEX_COMMENT, LEX_ID, LEX_STR, LEX_EQ, LEX_NE, LEX_GE, LEX_LE, LEX_NOT,
	LEX_DO, LEX_END, LEX_WHILE, LEX_REPEAT, LEX_UNTIL, LEX_IF,
	LEX_THEN, LEX_ELSEIF, LEX_ELSE, LEX_FOR, LEX_IN, LEX_FUNCTION,
	LEX_LOCAL, LEX_RETURN, LEX_BREAK, LEX_NIL, LEX_FALSE, LEX_TRUE,
	LEX_AND, LEX_OR, LEX_CAT, LEX_VARARG };

int lex(char *s, token *t);

#endif // LEX_H


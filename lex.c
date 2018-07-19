#include "common.h"
#include "lex.h"
#include "rhhm.h"

#include <ctype.h>

rhhm_value reserved_tbl[64];
rhhm reserved;

void lex_init() {
	rhhm_init_fixed(&reserved, reserved_tbl, 64);
	rhhm_insert_cstr(&reserved, "not",      LEX_NOT);
	rhhm_insert_cstr(&reserved, "do",       LEX_DO);
	rhhm_insert_cstr(&reserved, "end",      LEX_END);
	rhhm_insert_cstr(&reserved, "while",    LEX_WHILE);
	rhhm_insert_cstr(&reserved, "repeat",   LEX_REPEAT);
	rhhm_insert_cstr(&reserved, "until",    LEX_UNTIL);
	rhhm_insert_cstr(&reserved, "if",       LEX_IF);
	rhhm_insert_cstr(&reserved, "then",     LEX_THEN);
	rhhm_insert_cstr(&reserved, "elseif",   LEX_ELSEIF);
	rhhm_insert_cstr(&reserved, "else",     LEX_ELSE);
	rhhm_insert_cstr(&reserved, "for",      LEX_FOR);
	rhhm_insert_cstr(&reserved, "in",       LEX_IN);
	rhhm_insert_cstr(&reserved, "function", LEX_FUNCTION);
	rhhm_insert_cstr(&reserved, "local",    LEX_LOCAL);
	rhhm_insert_cstr(&reserved, "return",   LEX_RETURN);
	rhhm_insert_cstr(&reserved, "break",    LEX_BREAK);
	rhhm_insert_cstr(&reserved, "nil",      LEX_NIL);
	rhhm_insert_cstr(&reserved, "false",    LEX_FALSE);
	rhhm_insert_cstr(&reserved, "true",     LEX_TRUE);
	rhhm_insert_cstr(&reserved, "and",      LEX_AND);
	rhhm_insert_cstr(&reserved, "or",       LEX_OR);

	rhhm_insert_cstr(&reserved, "disp", LEX_DISP); // dbg
}

int lex(const char *s, token *t) {
	t->s = s;
	switch(*s++) {
	case ' ': case '\t': case '\r': case '\n':
		while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
		t->type = LEX_BLANKS;
		break;
	case '0': case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9':
		while (isdigit(*s)) s++;
		t->type = LEX_NUM;
		break;
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
	case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
	case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z': case '_':
		while (isalnum(*s) || *s == '_') s++;
		t->type = rhhm_get_str(&reserved, t->s, s-t->s);
		if (t->type == -1) t->type = LEX_ID;
		break;
	case '\'': case '\"':
		break;
	case '-':
		if (*s != '-') t->type = *t->s;
		else {
			while (*s && *s != '\r' && *s != '\n') s++;
			t->type = LEX_COMMENT;
		}
		break;
	case '.':
		break;
	case '~':
		if (*s != '=') return 1;
		t->type = LEX_NE;
		s++;
		break;
	case '<':
		if (*s == '=') {
			t->type = LEX_LE;
			s++;
		} else t->type = '<';
		break;
	case '>':
		if (*s == '=') {
			t->type = LEX_GE;
			s++;
		} else t->type = '>';
		break;
	case '=':
		if (*s == '=') {
			t->type = LEX_EQ;
			s++;
		} else t->type = '=';
		break;
	case '^': case '%': case '+': case '*': case '/': case ',': case ';':
	case '(': case ')': case '[': case ']': case '{': case '}': case '#':
		t->type = *t->s;
		break;
	default: return 1;
	}
	t->length = s - t->s;
	return 0;
}


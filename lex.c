#include "common.h"
#include "lex.h"

#include <ctype.h>

#define VALID(a) (isalnum(a) || a == '_')

int lex(char *s, token *t) {
	int dot = 0;
	t->s = s;
	switch(*s++) {
	case ' ': case '\t': case '\r': case '\n':
		while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
		t->type = LEX_BLANKS;
		break;
	case '.':
		dot = 1;
		if (*s == '.' && s[1] == '.') {
			t->type = LEX_VARARG;
			break;
		} else if (*s == '.') {
			t->type = LEX_CAT;
			break;
		} else if (!isdigit(*s)) {
			t->type = '.';
			break;
		}
	case '0': case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9':
		while (isdigit(*s)) s++;
		if (*s == '.' && !dot) {
			s++;
			while (isdigit(*s)) s++;
		}
		if (*s == 'e' || *s == 'E') {
			s++;
			if (*s == '-' || *s == '+') s++;
			while (isdigit(*s)) s++;
		}
		t->type = LEX_NUM;
		break;
	case 'a':
		if (*s == 'n' && s[1] == 'd') {
			s+=2;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_AND;
			break;
		}
		goto lex_id;
	case 'b':
		if (*s == 'r' && s[1] == 'e' && s[2] == 'a' && s[3] == 'k') {
			s+=4;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_BREAK;
			break;
		}
		goto lex_id;
	case 'd':
		if (*s == 'o') {
			s++;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_DO;
			break;
		}
		goto lex_id;
	case 'e':
		if (*s == 'l' && s[1] == 's' && s[2] == 'e') {
			s+=3;
			if (*s == 'i' && s[1] == 'f' && !VALID(s[2])) {
				s+=2;
				t->type = LEX_ELSEIF;
				break;
			}
			if (VALID(*s)) goto lex_id;
			t->type = LEX_ELSE;
			break;
		}
		if (*s == 'n' && s[1] == 'd') {
			s+=2;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_END;
			break;
		}
		goto lex_id;
	case 'f':
		if (*s == 'o' && s[1] == 'r') {
			s+=2;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_FOR;
			break;
		}
		if (*s == 'a' && s[1] == 'l' && s[2] == 's' && s[3] == 'e') {
			s+=4;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_FALSE;
			break;
		}
		if (*s == 'u' && s[1] == 'n' && s[2] == 'c' && s[3] == 't' && s[4] == 'i' && s[5] == 'o' && s[6] == 'n') {
			s+=7;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_FUNCTION;
			break;
		}
		goto lex_id;
	case 'i':
		if (*s == 'f') {
			s++;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_IF;
			break;
		}
		if (*s == 'n') {
			s++;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_IN;
			break;
		}
		goto lex_id;
	case 'l':
		if (*s == 'o' && s[1] == 'c' && s[2] == 'a' && s[3] == 'l') {
			s+=4;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_LOCAL;
			break;
		}
		goto lex_id;
	case 'o':
		if (*s == 'r') {
			s++;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_OR;
			break;
		}
		goto lex_id;
	case 'n':
		if (*s == 'o' && s[1] == 't') {
			s+=2;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_NOT;
			break;
		}
		if (*s == 'i' && s[1] == 'f') {
			s+=2;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_NIL;
			break;
		}
		goto lex_id;
	case 'r':
		if (*s == 'e' && s[1] == 'p' && s[2] == 'e' && s[3] == 'a' && s[4] == 't') {
			s+=5;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_REPEAT;
			break;
		}
		if (*s == 'e' && s[1] == 't' && s[2] == 'u' && s[3] == 'r' && s[4] == 'n') {
			s+=5;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_RETURN;
			break;
		}
		goto lex_id;
	case 't':
		if (*s == 'h' && s[1] == 'e' && s[2] == 'n') {
			s+=3;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_THEN;
			break;
		}
		if (*s == 'r' && s[1] == 'u' && s[2] == 'e') {
			s+=3;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_TRUE;
			break;
		}
		goto lex_id;
	case 'u':
		if (*s == 'n' && s[1] == 't' && s[2] == 'i' && s[3] == 'l') {
			s+=4;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_UNTIL;
			break;
		}
		goto lex_id;
	case 'w':
		if (*s == 'h' && s[1] == 'i' && s[2] == 'l' && s[3] == 'e') {
			s+=4;
			if (VALID(*s)) goto lex_id;
			t->type = LEX_WHILE;
			break;
		}
		goto lex_id;
	case 'c': case 'g': case 'h': case 'j': case 'k': case 'm':
	case 'p': case 'q': case 's': case 'v': case 'x': case 'y':
	case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z': case '_':
		goto lex_id;
	case '\'': case '\"':
		while (*s != *t->s) s++;
		t->type = LEX_STR;
		s++;
		break;
	case '-':
		if (*s != '-') t->type = *t->s;
		else {
			while (*s && *s != '\r' && *s != '\n') s++;
			t->type = LEX_COMMENT;
		}
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

lex_id:
	while (VALID(*s)) s++;
	t->type = LEX_ID;
	t->length = s - t->s;
	return 0;
}


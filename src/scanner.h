#ifndef XAN_SCANNER_H
#define XAN_SCANNER_H

#include <stddef.h>

#define TOKEN_BUILDER(X) \
	X(TOKEN_LEFT_PAREN, grouping, call,    PREC_CALL ),\
	X(TOKEN_RIGHT_PAREN, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_LEFT_BRACE, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_RIGHT_BRACE, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_COMMA, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_DOT, NULL,     NULL,    PREC_INDEX ),\
	X(TOKEN_MINUS, unary,    binary,  PREC_TERM ),\
	X(TOKEN_PLUS, NULL,     binary,  PREC_TERM ),\
	X(TOKEN_SEMICOLON, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_SLASH, NULL,     binary,  PREC_FACTOR ),\
	X(TOKEN_STAR, NULL,     binary,  PREC_FACTOR ),\
	X(TOKEN_BANG, unary,    NULL,    PREC_NONE ),\
	X(TOKEN_BANG_EQUAL, NULL,     binary,  PREC_EQUALITY ),\
	X(TOKEN_EQUAL, NULL,     assign,  PREC_ASSIGNMENT ),\
	X(TOKEN_EQUAL_EQUAL, NULL,     binary,  PREC_EQUALITY ),\
	X(TOKEN_GREATER, NULL,     binary,  PREC_COMPARISON ),\
	X(TOKEN_GREATER_EQUAL , NULL,     binary,  PREC_COMPARISON ),\
	X(TOKEN_LESS, NULL,     binary,  PREC_COMPARISON ),\
	X(TOKEN_LESS_EQUAL, NULL,     binary,  PREC_COMPARISON ),\
	X(TOKEN_IDENTIFIER, variable, NULL,    PREC_NONE ),\
	X(TOKEN_STRING, string,   NULL,    PREC_NONE ),\
	X(TOKEN_NUMBER, number,   NULL,    PREC_NONE ),\
	X(TOKEN_AND, NULL,     binary,    PREC_AND ),\
	X(TOKEN_CLASS, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_ELSE, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_FALSE, literal,  NULL,    PREC_NONE ),\
	X(TOKEN_FOR, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_FUN, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_IF, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_NIL, literal,  NULL,    PREC_NONE ),\
	X(TOKEN_OR, NULL,     binary,    PREC_OR ),\
	X(TOKEN_PRINT, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_RETURN, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_SUPER, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_THIS, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_TRUE, literal,  NULL,    PREC_NONE ),\
	X(TOKEN_VAR, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_WHILE, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_ERROR, NULL,     NULL,    PREC_NONE ),\
	X(TOKEN_EOF, NULL,     NULL,    PREC_NONE )

#define BUILD_ENUM(t, a, b, c) t
typedef enum {
	TOKEN_BUILDER(BUILD_ENUM)
} TokenType;
#undef BUILD_ENUM

#define BUILD_NAMES(t, a, b, c) #t
static const char* const TokenNames[] = {
	TOKEN_BUILDER(BUILD_NAMES)
};
#undef BUILD_NAMES

typedef struct {
	TokenType type;
	const char *start;
	size_t length;
	size_t line;
} Token;

typedef struct Scanner Scanner;

Scanner* initScanner(const char *source);
void endScanner(Scanner*);
Token scanToken(Scanner *s);

#endif /* XAN_SCANNER_H */

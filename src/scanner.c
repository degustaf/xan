#include "scanner.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct Scanner {
	const char *start;
	const char *current;
	size_t line;
};

Scanner* initScanner(const char *source) {
	Scanner *ret = malloc(sizeof(Scanner));
	ret->start = source;
	ret->current = source;
	ret->line = 1;

	return ret;
}

Scanner *duplicateScanner(Scanner *s) {
	Scanner *ret = malloc(sizeof(Scanner));
	ret->start = s->start;
	ret->current = s->current;
	ret->line = s->line;

	return ret;
}

void endScanner(Scanner *s) {
	free(s);
}

static inline bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static inline bool isAlpha(char c) {
	return  (c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			c == '_';
}

static bool isAtEnd(const Scanner *const s) {
	return *s->current == '\0';
}

static Token makeToken(const Scanner *const s, TokenType type) {
	Token token;
	token.type = type;
	token.start = s->start;
	token.length = s->current - s->start;
	token.line = s->line;

	return token;
}

static Token errorToken(const Scanner *const s, const char *message) {
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = strlen(message);
	token.line = s->line;

	return token;
}

static char advance(Scanner *s) {
	s->current++;
	return s->current[-1];
}

static bool match(Scanner *s, char expected) {
	if(isAtEnd(s)) return false;
	if(*s->current != expected) return false;

	s->current++;
	return true;
}

static char peek(const Scanner *const s) {
	return *s->current;
}

static char peekNext(const Scanner *const s) {
	if(isAtEnd(s))
		return '\0';
	return s->current[1];
}

static void skipWhitespace(Scanner *s) {
	while(true) {
		char c = peek(s);
		switch(c) {
			case '\n':
				s->line++;
			case ' ':
			case '\r':
			case '\t':
				advance(s);
				break;
			case '/':
				if(peekNext(s) == '/') {
					while(peek(s) != '\n' && !isAtEnd(s))
						advance(s);
				} else {
					return;
				}
				break;
			default:
				return;
		}
	}
}

static Token string(Scanner *s) {
	while(peek(s) != '"' && !isAtEnd(s)) {
		if(peek(s) == '\n') s->line++;
		advance(s);
	}

	if(isAtEnd(s))
		return errorToken(s, "Unterminated string.");

	advance(s);	// Consume the closing ".
	return makeToken(s, TOKEN_STRING);
}

static Token number(Scanner *s) {
	while(isDigit(peek(s)))
		advance(s);

	if(peek(s) == '.' && isDigit(peekNext(s))) {
		advance(s);
		while(isDigit(peek(s)))
			advance(s);
	}

	return makeToken(s, TOKEN_NUMBER);
}

static TokenType checkKeyword(const Scanner *s, size_t start, size_t length, const char *rest, TokenType type) {
	if((s->current == s->start + start + length) && 
			(memcmp(s->start + start, rest, length) == 0))
		return type;
	return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner *s) {
	switch(s->start[0]) {
		case 'a': return checkKeyword(s, 1, 2, "nd", TOKEN_AND);
		case 'c':
			if(s->current - s->start > 1) {
				switch(s->start[1]) {
					case 'a': return checkKeyword(s, 2, 3, "tch", TOKEN_CATCH);
					case 'l': return checkKeyword(s, 2, 3, "ass", TOKEN_CLASS);
				}
			}
		case 'e': return checkKeyword(s, 1, 3, "lse", TOKEN_ELSE);
		case 'f':
			if(s->current - s->start > 1) {
				switch(s->start[1]) {
					case 'a': return checkKeyword(s, 2, 3, "lse", TOKEN_FALSE);
					case 'o': return checkKeyword(s, 2, 1, "r", TOKEN_FOR);
					case 'u': return checkKeyword(s, 2, 1, "n", TOKEN_FUN);
				}
			}
			break;
		case 'i': return checkKeyword(s, 1, 1, "f", TOKEN_IF);
		case 'n': return checkKeyword(s, 1, 2, "il", TOKEN_NIL);
		case 'o': return checkKeyword(s, 1, 1, "r", TOKEN_OR);
		case 'r': return checkKeyword(s, 1, 5, "eturn", TOKEN_RETURN);
		case 's': return checkKeyword(s, 1, 4, "uper", TOKEN_SUPER);
		case 't':
			if(s->current - s->start > 1) {
				switch (s->start[1]) {
					case 'h': 
						if(s->current - s->start > 2) {
							switch (s->start[2]) {
								case 'i': return checkKeyword(s, 3, 1, "s", TOKEN_THIS);
								case 'r': return checkKeyword(s, 3, 2, "ow", TOKEN_THROW);
							}
						}
					case 'r': 
						if(s->current - s->start > 2) {
							switch (s->start[2]) {
								case 'u': return checkKeyword(s, 3, 1, "e", TOKEN_TRUE);
								case 'y': return checkKeyword(s, 3, 0, "", TOKEN_TRY);
							}
						}
				}
			}
			break;
		case 'v': return checkKeyword(s, 1, 2, "ar", TOKEN_VAR);
		case 'w': return checkKeyword(s, 1, 4, "hile", TOKEN_WHILE);
	}
	return TOKEN_IDENTIFIER;
}

static Token identifier(Scanner *s) {
	while(isAlpha(peek(s)) || isDigit(peek(s)))
		advance(s);

	return makeToken(s, identifierType(s));
}

Token scanToken(Scanner *s) {
	skipWhitespace(s);
	s->start = s->current;
	if(isAtEnd(s))
		return makeToken(s, TOKEN_EOF);
	char c = advance(s);
	if(isDigit(c))
		return number(s);
	if(isAlpha(c))
		return identifier(s);

	switch(c) {
		case '(': return makeToken(s, TOKEN_LEFT_PAREN);
		case ')': return makeToken(s, TOKEN_RIGHT_PAREN);
		case '{': return makeToken(s, TOKEN_LEFT_BRACE);
		case '}': return makeToken(s, TOKEN_RIGHT_BRACE);
		case '[': return makeToken(s, TOKEN_LEFT_BRACKET);
		case ']': return makeToken(s, TOKEN_RIGHT_BRACKET);
		case ';': return makeToken(s, TOKEN_SEMICOLON);
		case ':': return makeToken(s, TOKEN_COLON);
	   	case ',': return makeToken(s, TOKEN_COMMA);
		case '.': return makeToken(s, TOKEN_DOT);
		case '-': return makeToken(s, TOKEN_MINUS);
		case '+': return makeToken(s, TOKEN_PLUS);
		case '/': return makeToken(s, TOKEN_SLASH);
		case '*': return makeToken(s, TOKEN_STAR);
		case '!':
			return makeToken(s, match(s, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
		case '=':
			return makeToken(s, match(s, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
		case '<':
			return makeToken(s, match(s, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
		case '>':
			return makeToken(s, match(s, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
		case '"': return string(s);
	}

	return errorToken(s, "Unexpected character.");
}

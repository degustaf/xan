#include "parse.h"

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "scanner.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#ifdef DEBUG_PARSER
#define IN_FUNC printf("In function %s\ttoken %.*s\n", __func__, (int)p->previous.length, p->previous.start); \
				fflush(stdout);
#else /* DEBUG_PARSER */
#define IN_FUNC
#endif /* DEBUG_PARSER */

typedef struct {
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
	Scanner *s;
	VM *vm;
	Chunk *compilingChunk;
	Reg nextReg;
} Parser;

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,	// =
	PREC_OR,			// or
	PREC_AND,			// and
	PREC_EQUALITY,		// == !=
	PREC_COMPARISON,	// < > <= >=
	PREC_TERM,			// + -
	PREC_FACTOR,		// * /
	PREC_UNARY,			// ! - +
	PREC_CALL,			// . () []
	PREC_PRIMARY,
} Precedence;

typedef Reg (*ParseFn)(Parser*, bool);
typedef Reg (*ParseFn2)(Parser *, Reg);

typedef struct {
	ParseFn prefix;
	ParseFn2 infix;
	Precedence precedence;
} ParseRule;

static Chunk *currentChunk(Parser *p) {
	return p->compilingChunk;
}

static void errorAt(Parser *p, const Token *token, const char *message) {
	if(p->panicMode)
		return;
	p->panicMode = true;
	fprintf(stderr, "[line %zu] Error", token->line);

	if(token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	} else if(token->type == TOKEN_ERROR) {
		// Nothing
	} else {
		fprintf(stderr, " at '%.*s'", (int)token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	p->hadError = true;
}

static void errorAtPrevious(Parser *p, const char *message) {
	errorAt(p, &p->previous, message);
}

static void errorAtCurrent(Parser *p, const char *message) {
	errorAt(p, &p->current, message);
}

static void advance(Parser *p) {
	p->previous = p->current;

	while(true) {
		p->current = scanToken(p->s);
		if(p->current.type != TOKEN_ERROR)
			break;
		errorAtCurrent(p, p->current.start);
	}
}

static void consume(Parser *p, TokenType type, const char *message) {
	if(p->current.type == type) {
		advance(p);
		return;
	}

	errorAtCurrent(p, message);
}

static bool check(Parser *p, TokenType type) {
	return p->current.type == type;
}

static bool match(Parser *p, TokenType type) {
	if(!check(p, type))
		return false;
	advance(p);
	return true;
}

static Reg nextReg(Parser *p) {
	return p->nextReg++;
}

static void emitBytecode(Parser *p, uint32_t bc) {
	writeChunk(currentChunk(p), bc, p->previous.line);
}

static void emitReturn(Parser *p, Reg r) {
	emitBytecode(p, OP_A(OP_RETURN, r));
}

static uint16_t makeConstant(Parser *p, Value v) {
	size_t constant = addConstant(currentChunk(p), v);
	if(constant > UINT16_MAX) {
		errorAtPrevious(p, "Too many constants in one chunk.");
		return 0;
	}
	return (uint16_t)constant;
}

static uint16_t identifierConstant(Parser *p, Token *name) {
	return makeConstant(p, OBJ_VAL(copyString(name->start, name->length, p->vm)));
}

static Reg emitConstant(Parser *p, Value v) {
	Reg r = nextReg(p);
	emitBytecode(p, OP_AD(OP_CONST_NUM, r, makeConstant(p, v)));
	return r;
}

static void endParser(Parser *p) {
	emitReturn(p, 0);
#ifdef DEBUG_PRINT_CODE
	if(!p->hadError) {
		disassembleChunk(currentChunk(p), "code");
	}
#endif
	endScanner(p->s);
}

static Reg expression(Parser *p);
static void declaration(Parser *p);
static void statement(Parser *p);
static ParseRule* getRule(TokenType type);
static Reg parsePrecedence(Parser *p, Precedence precedence);

static Reg binary(Parser *p, Reg r1) {
	IN_FUNC;
	TokenType operatorType = p->previous.type;

	ParseRule *rule = getRule(operatorType);
	Reg r2 = parsePrecedence(p, (Precedence)(rule->precedence + 1));
	Reg r3 = nextReg(p);

	switch(operatorType) {
		// TODO
		case TOKEN_PLUS:
			emitBytecode(p, OP_ABC(OP_ADDVV, r3, r1, r2));
			return r3;
		case TOKEN_MINUS:
			emitBytecode(p, OP_ABC(OP_SUBVV, r3, r1, r2));
			return r3;
		case TOKEN_STAR:
			emitBytecode(p, OP_ABC(OP_MULVV, r3, r1, r2));
			return r3;
		case TOKEN_SLASH:
			emitBytecode(p, OP_ABC(OP_DIVVV, r3, r1, r2));
			return r3;
		case TOKEN_BANG_EQUAL:
			emitBytecode(p, OP_ABC(OP_NEQ, r3, r1, r2));
			return r3;
		case TOKEN_EQUAL_EQUAL:
			emitBytecode(p, OP_ABC(OP_EQUAL, r3, r1, r2));
			return r3;
		case TOKEN_GREATER:
			emitBytecode(p, OP_ABC(OP_GREATER, r3, r1, r2));
			return r3;
		case TOKEN_GREATER_EQUAL:
			emitBytecode(p, OP_ABC(OP_GEQ, r3, r1, r2));
			return r3;
		case TOKEN_LESS:
			emitBytecode(p, OP_ABC(OP_LESS, r3, r1, r2));
			return r3;
		case TOKEN_LESS_EQUAL:
			emitBytecode(p, OP_ABC(OP_LEQ, r3, r1, r2));
			return r3;
		default: return (Reg)(-1);
	}
}

#define PRIM_CASE(P) \
		case TOKEN_##P: \
			emitBytecode(p, OP_AD(OP_PRIMITIVE, r, PRIM_##P)); \
			break
static Reg literal(Parser *p, bool canAssign) {
	Reg r = nextReg(p);
	switch(p->previous.type) {
		PRIM_CASE(FALSE);
		PRIM_CASE(NIL);
		PRIM_CASE(TRUE);
		default:
			assert(false);
	}
	return r;
}
#undef PRIM_CASE

static Reg grouping(Parser *p, bool canAssign) {
	IN_FUNC;
	Reg r = expression(p);
	consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
	return r;
}

static Reg number(Parser *p, bool canAssign) {
	IN_FUNC;
	double value = strtod(p->previous.start, NULL);
	return emitConstant(p, NUMBER_VAL(value));
}

static Reg string(Parser *p, bool canAssign) {
	IN_FUNC;
	return emitConstant(p, OBJ_VAL(copyString(p->previous.start + 1,
											  p->previous.length - 2, p->vm)));
}

static Reg namedVariable(Parser *p, Token name, bool canAssign) {
	IN_FUNC;
	uint16_t arg = identifierConstant(p, &name);
	Reg r;

	if(canAssign && match(p, TOKEN_EQUAL)) {
		r = expression(p);
		emitBytecode(p, OP_AD(OP_SET_GLOBAL, r, arg));
	} else {
		r = nextReg(p);
		emitBytecode(p, OP_AD(OP_GET_GLOBAL, r, arg));
	}
	return r;
}

static Reg variable(Parser *p, bool canAssign) {
	IN_FUNC;
	return namedVariable(p, p->previous, canAssign);
}

static Reg unary(Parser *p, bool canAssign) {
	IN_FUNC;
	TokenType operatorType = p->previous.type;

	Reg r = parsePrecedence(p, PREC_UNARY);

	switch(operatorType) {
		case TOKEN_BANG: emitBytecode(p, OP_AD(OP_NOT,r,r)); return r;
		case TOKEN_MINUS: emitBytecode(p, OP_AD(OP_NEGATE,r,r)); return r;
		default: return (Reg)(-1);
	}
}

ParseRule rules[] = {
	{ grouping, NULL,    PREC_CALL },       // TOKEN_LEFT_PAREN      
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_PAREN     
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_LEFT_BRACE
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_BRACE     
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_COMMA           
	{ NULL,     NULL,    PREC_CALL },       // TOKEN_DOT             
	{ unary,    binary,  PREC_TERM },       // TOKEN_MINUS           
	{ NULL,     binary,  PREC_TERM },       // TOKEN_PLUS            
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_SEMICOLON       
	{ NULL,     binary,  PREC_FACTOR },     // TOKEN_SLASH           
	{ NULL,     binary,  PREC_FACTOR },     // TOKEN_STAR            
	{ unary,    NULL,    PREC_NONE },       // TOKEN_BANG            
	{ NULL,     binary,  PREC_EQUALITY },   // TOKEN_BANG_EQUAL      
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL           
	{ NULL,     binary,  PREC_EQUALITY },   // TOKEN_EQUAL_EQUAL     
	{ NULL,     binary,  PREC_COMPARISON }, // TOKEN_GREATER         
	{ NULL,     binary,  PREC_COMPARISON }, // TOKEN_GREATER_EQUAL   
	{ NULL,     binary,  PREC_COMPARISON }, // TOKEN_LESS            
	{ NULL,     binary,  PREC_COMPARISON }, // TOKEN_LESS_EQUAL      
	{ variable, NULL,    PREC_NONE },       // TOKEN_IDENTIFIER      
	{ string,   NULL,    PREC_NONE },       // TOKEN_STRING          
	{ number,   NULL,    PREC_NONE },       // TOKEN_NUMBER          
	{ NULL,     NULL,    PREC_AND },        // TOKEN_AND             
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_CLASS           
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_ELSE            
	{ literal,  NULL,    PREC_NONE },       // TOKEN_FALSE           
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_FOR             
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_FUN             
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_IF              
	{ literal,  NULL,    PREC_NONE },       // TOKEN_NIL             
	{ NULL,     NULL,    PREC_OR },         // TOKEN_OR              
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_PRINT           
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_RETURN          
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_SUPER           
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_THIS            
	{ literal,  NULL,    PREC_NONE },       // TOKEN_TRUE            
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_VAR             
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE           
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR           
	{ NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
};

static Reg parsePrecedence(Parser *p, Precedence precedence) {
	IN_FUNC;
	advance(p);
	ParseFn prefixRule = getRule(p->previous.type)->prefix;
	if(prefixRule == NULL) {
		errorAt(p, &p->previous, "Expect expression.");
		return (Reg)(-1);
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	Reg r = prefixRule(p, canAssign);

#ifdef DEBUG_PARSER
	fprintf(stderr, "precedence = %d\ngetRule->precedence = %d\n", precedence, getRule(p->current.type)->precedence);
#endif

	while(precedence <= getRule(p->current.type)->precedence) {
		advance(p);
		ParseFn2 infixRule = getRule(p->previous.type)->infix;
		r = infixRule(p, r);
	}

	if(canAssign && match(p, TOKEN_EQUAL)) {
		errorAt(p, &p->previous, "Invalid assignment target.");
		expression(p);
	}

	return r;
}

static uint16_t parseVariable(Parser *p, const char *errorMessage) {
	IN_FUNC;
	consume(p, TOKEN_IDENTIFIER, errorMessage);
	return identifierConstant(p, &p->previous);
}

static void defineVariable(Parser *p, uint16_t global, Reg r) {
	IN_FUNC;
	emitBytecode(p, OP_AD(OP_DEFINE_GLOBAL, r, global));
}

static ParseRule* getRule(TokenType type) {
#ifdef DEBUG_PARSER
	printf("In function %s\t tokenType = %d\n", __func__, type);
#endif /* DEBUG_PARSER */
	return &rules[type];
}

static Reg expression(Parser *p) {
	IN_FUNC;
	return parsePrecedence(p, PREC_ASSIGNMENT);
}

static void varDeclaration(Parser *p) {
	IN_FUNC;
	uint16_t global = parseVariable(p, "Expect variable name.");
	Reg r = match(p, TOKEN_EQUAL) ? expression(p) : emitConstant(p, NIL_VAL);
	consume(p, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(p, global, r);
}

static void expressionStatement(Parser *p) {
	IN_FUNC;
	expression(p);
	consume(p, TOKEN_SEMICOLON, "Expect ';' after expression.");
}

static void printStatement(Parser *p) {
	IN_FUNC;
	Reg r = expression(p);
	consume(p, TOKEN_SEMICOLON, "Expect ';' after value.");
	emitBytecode(p, OP_A(OP_PRINT, r));
}

static void synchronize(Parser *p) {
	p->panicMode = false;
	while(p->current.type != TOKEN_EOF) {
		if(p->previous.type == TOKEN_SEMICOLON) return;

		switch(p->current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUN:
			case TOKEN_VAR:
			case TOKEN_FOR:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_PRINT:
			case TOKEN_RETURN:
				return;
			default:
				;
		}
		advance(p);
	}
}

static void declaration(Parser *p) {
	IN_FUNC;
	if(match(p, TOKEN_VAR)) {
		varDeclaration(p);
	} else {
		statement(p);
	}

	if(p->panicMode)
		synchronize(p);
}

static void statement(Parser *p) {
	IN_FUNC;
	if(match(p, TOKEN_PRINT)) {
		printStatement(p);
	} else {
		expressionStatement(p);
	}
}

bool parse(VM *vm, const char *source, Chunk *c) {
	Parser p;
	p.s = initScanner(source);
	p.compilingChunk = c;
	p.hadError = false;
	p.panicMode = false;
	p.nextReg = 0;
	p.vm = vm;

	advance(&p);
	while(!match(&p, TOKEN_EOF)) {
		declaration(&p);
	}
	endParser(&p);
	return !p.hadError;
}

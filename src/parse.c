#include "parse.h"

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scanner.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
	Token name;
	int depth;
} Local;

typedef struct {
	Local locals[UINT8_COUNT];
	size_t localCount;
	int scopeDepth;
} Compiler;

typedef struct {
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
	Scanner *s;
	VM *vm;
	Chunk *compilingChunk;
	Reg nextReg;
	Reg actVar;
	Reg maxReg;

	Compiler *currentCompiler;
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
	PREC_UNARY,			// ! -
	PREC_CALL,			// ()
	PREC_INDEX,			// . []
	PREC_PRIMARY,
} Precedence;

static Chunk *currentChunk(Parser *p) {
	return p->compilingChunk;
}

#ifdef DEBUG_PARSER
#define PRINT_FUNCTION fprintf(stderr, "In function %s\tnextReg = %d\n", __func__, p->nextReg)
#else /* DEBUG_PARSER */
#define PRINT_FUNCTION
#endif /* DEBUG_PARSER */

static size_t emitBytecode(Parser *p, uint32_t bc) {
	PRINT_FUNCTION;
	return writeChunk(currentChunk(p), bc, p->previous.line);
}

typedef enum {
#define PRIM_EXTYPE(x) x##_EXTYPE
	PRIMITIVE_BUILDER(PRIM_EXTYPE, COMMA),
#undef PRIM_EXTYPE
	STRING_EXTYPE,
	NUMBER_EXTYPE,
	RELOC_TYPE,
	NONRELOC_TYPE,
	GLOBAL_TYPE,
	LOCAL_TYPE,		// u.r.r stack register.
} expressionType;

typedef struct {
	expressionType type;
	union {
		struct {
			uint32_t info;
		} s;
		struct {
			Reg r;
			Reg assignable;		// used as bool
		} r;
		Value v;
	} u;
	size_t true_jump;
	size_t false_jump;
} expressionDescription;

#ifdef DEBUG_PARSER
static void printExpr(FILE *restrict stream, expressionDescription *e) {
	fprintf(stream, "{type = %d; ", e->type);
	switch(e->type) {
		case STRING_EXTYPE:
		case NUMBER_EXTYPE:
			fprintf(stream, "u.v = '");
			fprintValue(stream, e->u.v);
			fprintf(stream, "'");
			break;
		default:
			fprintf(stream, "u.s.info = %d", e->u.s.info);
	}
	fprintf(stream, "; true_jump = %zd; false_jump = %zd;}\n", e->true_jump, e->false_jump);
}
#else /* DEBUG_PARSER */
#define printExpr(stream,e)
#endif /* DEBUG_PARSER */

#define expr_hasjump(e)  ((e)->true_jump != (e)->false_jump)

static inline void exprInit(expressionDescription *e, expressionType type, uint16_t info) {
	e->type = type;
	if(e->type == GLOBAL_TYPE || e->type == LOCAL_TYPE) {
		e->u.r.r = info;
		e->u.r.assignable = true;
	} else {
		e->u.s.info = info;
	}
	e->true_jump = e->false_jump = NO_JUMP;
}

static void regFree(Parser *p, Reg r) {
	PRINT_FUNCTION;
	if(r >= p->actVar) {
		p->nextReg--;
#ifdef DEBUG_PARSER
		fprintf(stderr, "nextReg = %d\tr = %d\n", p->nextReg, r);
#endif /* DEBUG_PARSER */
		assert(r == p->nextReg);
	}
}

static void exprFree(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	if(e->type == NONRELOC_TYPE)
		regFree(p, e->u.r.r);
}

static void exprDischarge(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	switch(e->type) {
		case GLOBAL_TYPE:
			e->u.s.info = emitBytecode(p, OP_AD(OP_GET_GLOBAL, 0, e->u.r.r));
			e->type = RELOC_TYPE;
			return;
		case LOCAL_TYPE:
			e->type = NONRELOC_TYPE;
			return;
		default:
			return;
	}
}

static void regBump(Parser *p, int n) {
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\tn = %d\n", p->nextReg, n);
#endif /* DEBUG_PARSER */
	Reg sz = p->nextReg + n;
	if(sz > p->maxReg)
		p->maxReg = sz;
}

static void regReserve(Parser *p, int n) {
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\tn = %d\n", p->nextReg, n);
#endif /* DEBUG_PARSER */
	regBump(p, n);
	p->nextReg += n;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
}

static uint16_t makeConstant(Parser *p, Value v);

#define codePtr(chunk, e) (&((chunk)->code[(e)->u.s.info]))

static void exprToRegNoBranch(Parser *p, expressionDescription *e, Reg r) {
	PRINT_FUNCTION;
	uint32_t instruction = 0;
	Reg assignable = false;
	exprDischarge(p, e);
	switch(e->type) {
#define PRIMITIVE_CASES(x) case x##_EXTYPE
		PRIMITIVE_BUILDER(PRIMITIVE_CASES, :):
#undef PRIMITIVE_CASES
			instruction = OP_AD(OP_PRIMITIVE, r, (primitive)e->type);
			break;
		case STRING_EXTYPE:
		case NUMBER_EXTYPE:
			instruction = OP_AD(OP_CONST_NUM, r, makeConstant(p, e->u.v));
			break;
		case RELOC_TYPE:
			setbc_a(codePtr(currentChunk(p), e), r);
			goto NO_INSTRUCTION;
		case NONRELOC_TYPE:
			if(e->u.r.r == r)
				goto NO_INSTRUCTION;
			assert(false);
			assignable = e->u.r.assignable;
			break;
		case GLOBAL_TYPE:
			if(e->u.r.r == r)
				goto NO_INSTRUCTION;
			assert(false);
			assignable = e->u.r.assignable;
			break;
		default:
			assert(p->panicMode || false);
	}
	emitBytecode(p, instruction);
NO_INSTRUCTION:
	e->u.r.r = r;
	e->u.r.assignable = assignable;
	e->type = NONRELOC_TYPE;
}

static void exprToReg(Parser *p, expressionDescription *e, Reg r) {
	PRINT_FUNCTION;
	exprToRegNoBranch(p, e, r);
	// Handle jumps
	e->true_jump = e->false_jump = NO_JUMP;
	e->u.r.r = r;
	e->u.r.assignable &= (e->type == NONRELOC_TYPE || e->type == GLOBAL_TYPE || e->type == LOCAL_TYPE);
	e->type = NONRELOC_TYPE;
}

static void exprNextReg(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	exprDischarge(p, e);
	exprFree(p, e);
	regReserve(p, 1);
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
	exprToReg(p, e, p->nextReg - 1);
}

static Reg exprAnyReg(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	exprDischarge(p, e);
	if(e->type == NONRELOC_TYPE) {
		if(!expr_hasjump(e))
			return e->u.r.r;
		if(e->u.r.r >= p->actVar) {
			exprToReg(p, e, e->u.r.r);
			return e->u.r.r;
		}
		exprNextReg(p, e);
		return e->u.r.r;
	}
	exprNextReg(p, e);
	return e->u.s.info;
}

static void expr_toval(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	if(expr_hasjump(e))
		exprAnyReg(p, e);
	else
		exprDischarge(p, e);
}

static size_t emit_AD(Parser *p, ByteCode op, Reg a, Reg d){
	return emitBytecode(p, OP_AD(op, a, d));
}

static size_t emit_ABC(Parser *p, ByteCode op, Reg a, Reg b, Reg c) {
	return emitBytecode(p, OP_ABC(op, a, b, c));
}

static void emit_arith(Parser *p, ByteCode op, expressionDescription *e1, expressionDescription *e2) {
	PRINT_FUNCTION;

	expr_toval(p, e2);
	Reg rc = exprAnyReg(p, e2);
	expr_toval(p, e1);
	Reg rb = exprAnyReg(p, e1);

#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
	if((e1->type == NONRELOC_TYPE) && (e1->u.r.r >= p->actVar)) p->nextReg--;
	if((e2->type == NONRELOC_TYPE) && (e2->u.r.r >= p->actVar)) p->nextReg--;
	e1->u.s.info = emit_ABC(p, op, 0, rb, rc);
	e1->type = RELOC_TYPE;
}

static void emit_comp(Parser *p, ByteCode op, expressionDescription *e1, expressionDescription *e2) {
	PRINT_FUNCTION;

	expr_toval(p, e1);
	Reg rb = exprAnyReg(p, e1);
	expr_toval(p, e2);
	Reg rc = exprAnyReg(p, e2);

#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
	if((e1->type == NONRELOC_TYPE) && (e1->u.r.r >= p->actVar)) p->nextReg--;
	if((e2->type == NONRELOC_TYPE) && (e2->u.r.r >= p->actVar)) p->nextReg--;
	e1->u.s.info = emit_ABC(p, op, 0, rb, rc);
	e1->type = RELOC_TYPE;
}

static void emit_binop(Parser *p, ByteCode op, expressionDescription *e1, expressionDescription *e2) {
	PRINT_FUNCTION;
	if((op == OP_ADDVV) || (op == OP_SUBVV) || (op == OP_MULVV) || (op == OP_DIVVV)) {	// TODO make this a single comparison: op <= ??????
		emit_arith(p, op, e1, e2);
	}else{
		assert((op == OP_EQUAL) || (op == OP_NEQ) || (op == OP_LESS) || (op == OP_LEQ) || (op == OP_GREATER) || (op == OP_GEQ));
		emit_comp(p, op, e1, e2);
	}
	// TODO
}

static void emit_store(Parser *p, expressionDescription *variable, expressionDescription *e) {
	PRINT_FUNCTION;
	if(variable->type == LOCAL_TYPE) {
		exprFree(p, e);
		exprToReg(p, e, variable->u.r.r);
		return;
	// TODO Upvalue
	} else if(variable->type == GLOBAL_TYPE) {
		Reg r = exprAnyReg(p, e);
		emit_AD(p, OP_SET_GLOBAL, r, variable->u.r.r);
	}
	// TODO indexed
}

typedef void (*ParseFn)(Parser*, expressionDescription*);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

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
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
	return p->nextReg++;
}

static void markInitialized(Parser *p) {
	p->currentCompiler->locals[p->currentCompiler->localCount-1].depth = p->currentCompiler->scopeDepth;
}

static void emitDefine(Parser *p, expressionDescription *v, expressionDescription *e) {
	PRINT_FUNCTION;
	printExpr(stderr, v);
	printExpr(stderr, e);
	if(p->currentCompiler->scopeDepth > 0) {
		regReserve(p, 1);
		markInitialized(p);
		exprFree(p, e);
		exprToReg(p, e, v->u.r.r);
		return;
	}
	Reg r = exprAnyReg(p, e);
	emitBytecode(p, OP_AD(OP_DEFINE_GLOBAL, r, v->u.r.r));
	exprFree(p, e);
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

static void identifierConstant(Parser *p, expressionDescription *e, Token *name, int r) {
	PRINT_FUNCTION;
	if(r < 0)
		exprInit(e, GLOBAL_TYPE,
				makeConstant(p, OBJ_VAL(copyString(name->start, name->length, p->vm))));
	else {
		exprInit(e, LOCAL_TYPE, r);
		e->u.r.assignable = true;
	}
}

static bool identifiersEqual(Token *a, Token *b) {
	return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Parser *p) {
	for(size_t i = p->currentCompiler->localCount; i>0; i--) {
		Local *local = &p->currentCompiler->locals[i-1];
		if(identifiersEqual(&p->previous, &local->name)) {
			if(local->depth == -1)
				errorAtPrevious(p, "Cannot read local variable in its own initializer.");
			return i-1;
		}
	}
	return -1;
}

static int addLocal(Parser *p, Token name) {
	PRINT_FUNCTION;
	Compiler *c = p->currentCompiler;
	if(c->localCount == UINT8_COUNT) {
		errorAtPrevious(p, "");
	}
	Local *local = &c->locals[c->localCount++];
	local->name = name;
	local->depth = -1;
	p->actVar++;
	return c->localCount-1;
}

static int declareVariable(Parser *p) {
	PRINT_FUNCTION;
	if(p->currentCompiler->scopeDepth == 0)	// Global scope
		return -1;

	Token *name = &p->previous;
	for(int i=p->currentCompiler->localCount-1; i>=0; i--) {
		Local *local = &p->currentCompiler->locals[i];
		if(local->depth != -1 && local->depth < p->currentCompiler->scopeDepth) {
			break;
		}
		if(identifiersEqual(name, &local->name))
			errorAtPrevious(p, "Variable with this name already declared in this scope.");
	}
	return addLocal(p, *name);
}

static Reg emitConstant(Parser *p, Value v) {
	PRINT_FUNCTION;
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

static void variable(Parser *p, expressionDescription *e);
static void expression(Parser *p, expressionDescription *e);
static void declaration(Parser *p);
static void statement(Parser *p);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Parser *p, expressionDescription *e, Precedence precedence);

static void assign(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	printExpr(stderr, e);
	if((e->type != NONRELOC_TYPE && e->type != GLOBAL_TYPE && e->type != LOCAL_TYPE) || !e->u.r.assignable) {
		errorAtPrevious(p, "Invalid assignment target.");
		return;
	}
	expressionDescription e2;
	expression(p, &e2);
	emit_store(p, e, &e2);
	e->type = NONRELOC_TYPE;
	e->u.r.r = e2.type == NONRELOC_TYPE ? e2.u.r.r : e2.u.s.info;
}

static void binary(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	TokenType operatorType = p->previous.type;

	ParseRule *rule = getRule(operatorType);

	ByteCode op;
	switch(operatorType) {
		// TODO
		case TOKEN_PLUS: op = OP_ADDVV; break;
		case TOKEN_MINUS: op = OP_SUBVV; break;
		case TOKEN_STAR: op = OP_MULVV; break;
		case TOKEN_SLASH: op = OP_DIVVV; break;
		case TOKEN_BANG_EQUAL: op = OP_NEQ; break;
		case TOKEN_EQUAL_EQUAL: op = OP_EQUAL; break;
		case TOKEN_GREATER: op = OP_GREATER; break;
		case TOKEN_GREATER_EQUAL: op = OP_GEQ; break;
		case TOKEN_LESS: op = OP_LESS; break;
		case TOKEN_LESS_EQUAL: op = OP_LEQ; break;
		default: assert(false);
	}

	expressionDescription e2;
	parsePrecedence(p, &e2, (Precedence)(rule->precedence + 1));

	emit_binop(p, op, e, &e2);
}

#define PRIM_CASE(P) \
		case TOKEN_##P: \
			exprInit(e, P##_EXTYPE, 0); \
			break
static void literal(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	switch(p->previous.type) {
		PRIM_CASE(FALSE);
		PRIM_CASE(NIL);
		PRIM_CASE(TRUE);
		default:
			assert(false);
	}
}
#undef PRIM_CASE

static void grouping(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	expression(p, e);
	consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
	if(e->type == NONRELOC_TYPE || e->type == GLOBAL_TYPE)
		e->u.r.assignable = false;
}

static void number(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	double value = strtod(p->previous.start, NULL);
	e->type = NUMBER_EXTYPE;
	e->u.v = NUMBER_VAL(value);
	e->true_jump = e->false_jump = NO_JUMP;
}

static void string(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	e->type = STRING_EXTYPE;
	e->u.v = OBJ_VAL(copyString(p->previous.start + 1, p->previous.length - 2, p->vm));
	e->true_jump = e->false_jump = NO_JUMP;
}

static void unary(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	TokenType operatorType = p->previous.type;

	parsePrecedence(p, e, PREC_UNARY);

	ByteCode op;
	switch(operatorType) {
		case TOKEN_BANG: op = OP_NOT; break;
		case TOKEN_MINUS: op = OP_NEGATE; break;
		default: assert(false);
	}
	exprAnyReg(p, e);
	exprFree(p, e);
	exprInit(e, RELOC_TYPE, emit_AD(p, op, 0, e->u.s.info));
}

#define BUILD_RULES(t, prefix, infix, precedence) \
{ prefix, infix, precedence }
ParseRule rules[] = {
	TOKEN_BUILDER(BUILD_RULES)
};
#undef BUILD_RULES

static void parsePrecedence(Parser *p, expressionDescription *e, Precedence precedence) {
	PRINT_FUNCTION;
	advance(p);
	ParseFn prefixRule = getRule(p->previous.type)->prefix;
	if(prefixRule == NULL) {
		errorAtPrevious(p, "Expect expression.");
		return;
	}

	prefixRule(p, e);

#ifdef DEBUG_PARSER
	fprintf(stderr, "precedence = %d\tgetRule->precedence = %d\n", precedence, getRule(p->current.type)->precedence);
#endif

	while(precedence <= getRule(p->current.type)->precedence) {
		advance(p);
		ParseFn infixRule = getRule(p->previous.type)->infix;
		infixRule(p, e);
	}

	/*
	// This is supposed to catch errors. Do we still need this?
	if((e->type == NONRELOC_TYPE) && e->u.r.assignable && match(p, TOKEN_EQUAL)) {
		errorAtPrevious(p, "Invalid assignment target.");
		expression(p, e);
	}
	*/
}

static void variable(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	int arg = resolveLocal(p);
	identifierConstant(p, e, &p->previous, arg);
	while(PREC_INDEX <= getRule(p->current.type)->precedence) {
		advance(p);
		ParseFn infixRule = getRule(p->previous.type)->infix;
		infixRule(p, e);
	}
	if(p->previous.type == TOKEN_EQUAL) {
		assign(p, e);
	}
}

static void parseVariable(Parser *p, expressionDescription *e, const char *errorMessage) {
	PRINT_FUNCTION;
	consume(p, TOKEN_IDENTIFIER, errorMessage);
	int arg = declareVariable(p);
	identifierConstant(p, e, &p->previous, arg);
}

static ParseRule* getRule(TokenType type) {
#ifdef DEBUG_PARSER
	fprintf(stderr, "In function %s\t tokenType = %s\n", __func__, TokenNames[type]);
#endif /* DEBUG_PARSER */
	return &rules[type];
}

static void expression(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	parsePrecedence(p, e, PREC_ASSIGNMENT);
}

static void varDeclaration(Parser *p) {
	PRINT_FUNCTION;
	expressionDescription v;
	parseVariable(p, &v, "Expect variable name.");
	printExpr(stderr, &v);
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
	expressionDescription e;
	if(match(p, TOKEN_EQUAL)) {
		expression(p, &e);
	} else {
		exprInit(&e, NIL_EXTYPE, 0); \
	}
	consume(p, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	if(!p->panicMode)
		emitDefine(p, &v, &e);
}

static void expressionStatement(Parser *p) {
	PRINT_FUNCTION;
	expressionDescription e;
	expression(p, &e);
	consume(p, TOKEN_SEMICOLON, "Expect ';' after expression.");
}

static void printStatement(Parser *p) {
	PRINT_FUNCTION;
	expressionDescription e;
	expression(p, &e);
	printExpr(stderr, &e);
	Reg r = exprAnyReg(p, &e);
	printExpr(stderr, &e);
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
	PRINT_FUNCTION;
	if(match(p, TOKEN_VAR)) {
		varDeclaration(p);
	} else {
		statement(p);
	}

	if(p->panicMode)
		synchronize(p);
}

static void block(Parser *p) {
	PRINT_FUNCTION;
	while(!check(p, TOKEN_RIGHT_BRACE) && !check(p, TOKEN_EOF))
		declaration(p);
	consume(p, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void beginScope(Compiler *current) {
	current->scopeDepth++;
}

static size_t endScope(Compiler *c) {
	size_t i;
	for(i = c->localCount; i>0; i--)
		if(c->locals[i-1].depth < c->scopeDepth)
			break;

	c->scopeDepth--;
	assert(c->scopeDepth >= 0);
	size_t ret = c->localCount - i;
	c->localCount = i;
	return ret;
}

static void statement(Parser *p) {
	PRINT_FUNCTION;
	if(match(p, TOKEN_PRINT)) {
		printStatement(p);
	} else if(match(p, TOKEN_LEFT_BRACE)) {
		beginScope(p->currentCompiler);
		block(p);
		regReserve(p, -endScope(p->currentCompiler));
	} else {
		expressionStatement(p);
	}
}

static Compiler* initCompiler(Compiler *compiler) {
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	return compiler;
}

static void initParser(Parser *p, VM *vm, Compiler *compiler, const char *source, Chunk *c) {
	PRINT_FUNCTION;
	p->s = initScanner(source);
	p->currentCompiler = initCompiler(compiler);
	p->compilingChunk = c;
	p->hadError = false;
	p->panicMode = false;
	p->vm = vm;
	p->nextReg = p->actVar = p->maxReg = 0;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->nextReg);
#endif /* DEBUG_PARSER */
}

bool parse(VM *vm, const char *source, Chunk *c) {
	Parser p;
	Compiler compiler;
	initParser(&p, vm, &compiler, source, c);

	advance(&p);
	while(!match(&p, TOKEN_EOF)) {
		declaration(&p);
	}
	endParser(&p);
	return !p.hadError;
}

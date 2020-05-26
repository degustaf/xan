#include "parse.h"

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "scanner.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef enum {
	TYPE_FUNCTION,
	TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
	struct Compiler *enclosing;
	ObjFunction *function;
	FunctionType type;
	Local locals[UINT8_COUNT];
	uint16_t upvalues[UINT8_COUNT];
	size_t localCount;
	int scopeDepth;
	OP_position pendingJumpList;
	OP_position last_target;
	Reg nextReg;
	Reg actVar;
	Reg maxReg;
} Compiler;

typedef struct {
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
	Scanner *s;
	VM *vm;

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

static Chunk *currentChunk(Compiler *c) {
	return &c->function->chunk;
}

typedef enum {
#define PRIM_EXTYPE(x) x##_EXTYPE
	PRIMITIVE_BUILDER(PRIM_EXTYPE, COMMA),
#undef PRIM_EXTYPE
	FUNCTION_EXTYPE,	// = 3
	STRING_EXTYPE,
	NUMBER_EXTYPE,
	RELOC_EXTYPE,
	NONRELOC_EXTYPE,	// u.r.r is result register.
	GLOBAL_EXTYPE,
	UPVAL_EXTYPE,		// u.s.info is ????
	LOCAL_EXTYPE,		// u.r.r is stack register.
	JUMP_EXTYPE,		// u.s.info is instruction number.
	CALL_EXTYPE,		// u.s.info is instruction number.
	VOID_EXTYPE,
} expressionType;

typedef struct {
	expressionType type;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	struct
#else
	union
#endif
	{
		struct {
			size_t info;
			size_t aux;
		} s;
		struct {
			Reg r;
		} r;
		Value v;
	} u;
	bool assignable;
	OP_position true_jump;
	OP_position false_jump;
} expressionDescription;

#ifdef DEBUG_PARSER
static void printExpr(FILE *restrict stream, expressionDescription *e) {
	fprintf(stream, "{%p: type = %d; ", (void*)e, e->type);
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	fprintf(stream, "u.s.info = %zd; ", e->u.s.info);
	fprintf(stream, "u.r.r = %d; ", e->u.r.r);
	fprintf(stream, "u.v = %p:'", (void*)AS_OBJ(e->u.v));
	if((e->type == STRING_EXTYPE) && AS_OBJ(e->u.v)) {
		fprintValue(stream, e->u.v);
	} else if(e->type == NUMBER_EXTYPE) {
		fprintValue(stream, e->u.v);
	}
	fprintf(stream, "'");
#else /* DEBUG_EXPRESSION_DESCRIPTION */
	switch(e->type) {
		case STRING_EXTYPE:
			fprintf(stream, "u.v = %p:'", (void*)AS_OBJ(e->u.v));
			fprintValue(stream, e->u.v);
			fprintf(stream, "'");
			break;
		case NUMBER_EXTYPE:
			fprintf(stream, "u.v = '");
			fprintValue(stream, e->u.v);
			fprintf(stream, "'");
			break;
		case NONRELOC_EXTYPE:
		case LOCAL_EXTYPE:
			fprintf(stream, "u.r.r = %d", e->u.r.r);
			break;
		default:
			fprintf(stream, "u.s.info = %zd", e->u.s.info);
	}
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
	fprintf(stream, "; true_jump = %d; false_jump = %d;}\n", e->true_jump, e->false_jump);
}
#else /* DEBUG_PARSER */
#define printExpr(stream,e)
#endif /* DEBUG_PARSER */

#ifdef DEBUG_PARSER
#define PRINT_FUNCTION fprintf(stderr, "In function %s\n", __func__)
#elif defined(DEBUG_JUMP_LISTS)
#define PRINT_FUNCTION fprintf(stderr, "In function %s\n", __func__)
#else /* DEBUG_PARSER */
#define PRINT_FUNCTION
#endif /* DEBUG_PARSER */

#ifdef DEBUG_JUMP_LISTS
	static void printPendingJumps(Compiler *c) {
		fprintf(stderr, "pendingJumpList = %d\ncode = {", c->pendingJumpList);
		for(size_t i=0; i < currentChunk(c)->count; i++) {
			fprintf(stderr, "%x, ", currentChunk(c)->code[i]);
		}
		fprintf(stderr, "}\n");
	}
#else
#define printPendingJumps(c)
#endif /* DEBUG_JUMP_LISTS */

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

XAN_STATIC_ASSERT(OP_JUMP_IF_TRUE - OP_COPY_JUMP_IF_TRUE == OP_JUMP_IF_FALSE - OP_COPY_JUMP_IF_FALSE);

static bool jump_patch_test_reg(Chunk *c, OP_position pc, Reg r) {
	uint32_t *ip = &c->code[pc >= 1 ? pc-1 : pc];
	ByteCode op = OP(*ip);
	if(op == OP_COPY_JUMP_IF_TRUE || op == OP_COPY_JUMP_IF_FALSE) {
		if(r == NO_REG || r == RD(*ip)) {
			setbc_op(ip, op+(OP_JUMP_IF_TRUE - OP_COPY_JUMP_IF_TRUE));
			setbc_a(ip, 0);
		} else {
			setbc_a(ip, r);
		}
		return true;
	} else if(RA(*ip) == NO_REG) {
		if(r == NO_REG) {
			*ip = OP_AJump(OP_JUMP, RA(c->code[pc]), 0);
		} else {
			setbc_a(ip, r);
			if(r >= RA(ip[1]))
				setbc_a(ip+1, r+1);
		}
		return true;
	}
	return false;
}

static void jump_patch_instruction(Parser *p, OP_position src, OP_position dest) {
	PRINT_FUNCTION;
	OP_position offset = dest - (src+1) + JUMP_BIAS;
#if defined(DEBUG_PARSER) || defined(DEBUG_JUMP_LISTS)
	fprintf(stderr, "src = %u\tdest = %u\toffset = %x\n", src, dest, offset);
#endif /* DEBUG_PARSER */
	assert(dest != NO_JUMP);
	if(offset > MAX_D)
		errorAtPrevious(p, "Too much code to jump over.");
	uint32_t *jump = &currentChunk(p->currentCompiler)->code[src];
	setbc_d(jump, offset);
}

static OP_position jump_next(Chunk *c, OP_position pc) {
#ifdef DEBUG_PARSER
	fprintf(stderr, "pc = %u\tNO_JUMP = %u\n", pc, NO_JUMP);
#endif /* DEBUG_PARSER */
	ptrdiff_t delta = RJump(c->code[pc]);
	if((OP_position)delta == NO_JUMP)
		return NO_JUMP;
	OP_position ret = (OP_position)(((ptrdiff_t)pc+1)+delta);
#ifdef DEBUG_PARSER
	fprintf(stderr, "code = %x\tdelta = %zd\tret = %u\n", c->code[pc], delta, ret);
#endif /* DEBUG_PARSER */
	return ret;
}

static void jump_patch_value(Parser *p, OP_position jump_list, OP_position value_target, Reg r, OP_position default_target) {
	Compiler *c = p->currentCompiler;
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "code = {");
	for(size_t i=0; i < currentChunk(c)->count; i++) {
		fprintf(stderr, "%x, ", currentChunk(c)->code[i]);
	}
	fprintf(stderr, "}\nvalue_target = %u\tdefault_target = %u\tr = %u\n", value_target, default_target, r);
#endif /* DEBUG_PARSER */
	while(jump_list != NO_JUMP) {
#ifdef DEBUG_PARSER
		fprintf(stderr, "jump_list = %u\t", jump_list);
		fprintf(stderr, "code = {");
		for(size_t i=0; i < currentChunk(c)->count; i++) {
			fprintf(stderr, "%x, ", currentChunk(c)->code[i]);
		}
		fprintf(stderr, "}\n");
#endif /* DEBUG_PARSER */
		OP_position next = jump_next(currentChunk(c), jump_list);
		if(jump_patch_test_reg(currentChunk(c), jump_list, r))
			jump_patch_instruction(p, jump_list, value_target);
		else
			jump_patch_instruction(p, jump_list, default_target);
		jump_list = next;
	}
}

static size_t emitBytecode(Parser *p, uint32_t bc) {
	PRINT_FUNCTION;
	Compiler *compiler = p->currentCompiler;
#ifdef DEBUG_PARSER
	fprintf(stderr, "bc = %x\n", bc);
#endif
	Chunk *c = currentChunk(compiler);
	printPendingJumps(compiler);
	jump_patch_value(p, compiler->pendingJumpList, c->count, NO_REG, c->count);
	compiler->pendingJumpList = NO_JUMP;
	return writeChunk(c, bc, p->previous.line);
}

static size_t emit_AD(Parser *p, ByteCode op, Reg a, uint16_t d) {
	return emitBytecode(p, OP_AD(op, a, d));
}

static size_t emit_AJ(Parser *p, ByteCode op, Reg a, uint16_t j) {
	return emitBytecode(p, OP_AJump(op, a, j));
}

static size_t emit_ABC(Parser *p, ByteCode op, Reg a, Reg b, Reg c) {
	return emitBytecode(p, OP_ABC(op, a, b, c));
}

#define expr_hasjump(e)  ((e)->true_jump != (e)->false_jump)

static inline void exprInit(expressionDescription *e, expressionType type, uint16_t info) {
	e->type = type;
	if(e->type == GLOBAL_EXTYPE) {
		e->u.s.info = info;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
		e->u.r.r = 0;
		AS_OBJ(e->u.v) = NULL;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
		e->assignable = true;
	} else if(e->type == LOCAL_EXTYPE) {
		assert(info <= UINT8_MAX);
		e->u.r.r = info;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
		e->u.s.info = 0;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
		e->assignable = true;
	} else {
		e->u.s.info = info;
	}
	e->true_jump = e->false_jump = NO_JUMP;
}

static void regFree(Compiler *c, Reg r) {
	PRINT_FUNCTION;
	if(r >= c->actVar) {
		c->nextReg--;
#ifdef DEBUG_PARSER
		fprintf(stderr, "nextReg = %d\tr = %d\n", c->nextReg, r);
#endif /* DEBUG_PARSER */
		assert(r == c->nextReg);
	}
}

static void exprFree(Compiler *c, expressionDescription *e) {
	PRINT_FUNCTION;
	if(e->type == NONRELOC_EXTYPE)
		regFree(c, e->u.r.r);
}

static void exprDischarge(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	switch(e->type) {
		case UPVAL_EXTYPE:
			e->u.s.info = emit_AD(p, OP_GET_UPVAL, 0, e->u.s.info);
			e->type = RELOC_EXTYPE;
			return;
		case GLOBAL_EXTYPE:
#ifdef DEBUG_EXPRESSION_DESCRIPTION
			assert(e->u.r.r == 0);
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
			e->u.s.info = emit_AD(p, OP_GET_GLOBAL, 0, e->u.s.info);
			e->type = RELOC_EXTYPE;
			return;
		case CALL_EXTYPE:
			e->u.s.info = e->u.s.aux;
			// intentional fallthrough.
		case LOCAL_EXTYPE:
			e->type = NONRELOC_EXTYPE;
			return;
		default:
			return;
	}
}

static void regBump(Compiler *c, int n) {
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\tn = %d\n", c->nextReg, n);
#endif /* DEBUG_PARSER */
	Reg sz = c->nextReg + n;
	if(sz > c->maxReg)
		c->maxReg = sz;
}

static void regReserve(Compiler *c, int n) {
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\tn = %d\n", c->nextReg, n);
#endif /* DEBUG_PARSER */
	regBump(c, n);
	c->nextReg += n;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", c->nextReg);
#endif /* DEBUG_PARSER */
}

static uint16_t makeConstant(Parser *p, Value v);

#define codePtr(chunk, e) (&((chunk)->code[(e)->u.s.info]))

static void exprToRegNoBranch(Parser *p, expressionDescription *e, Reg r) {
	PRINT_FUNCTION;
	uint32_t instruction = 0;
	Reg assignable = false;
	printExpr(stderr, e);
	exprDischarge(p, e);
	printExpr(stderr, e);
	switch(e->type) {
#define PRIMITIVE_CASES(x) case x##_EXTYPE
		PRIMITIVE_BUILDER(PRIMITIVE_CASES, :):
#undef PRIMITIVE_CASES
			instruction = OP_AD(OP_PRIMITIVE, r, (primitive)e->type);
			break;
		case STRING_EXTYPE:
		case NUMBER_EXTYPE:
		case FUNCTION_EXTYPE:
			instruction = OP_AD(OP_CONST_NUM, r, makeConstant(p, e->u.v));
			break;
		case RELOC_EXTYPE:
			setbc_a(codePtr(currentChunk(p->currentCompiler), e), r);
			goto NO_INSTRUCTION;
		case NONRELOC_EXTYPE:
			if(e->u.r.r == r)
				goto NO_INSTRUCTION;
			instruction = OP_AD(OP_MOV, r, e->u.r.r);
			assignable = e->assignable;
			break;
		case GLOBAL_EXTYPE:
			assert(false);
			if(e->u.r.r == r)
				goto NO_INSTRUCTION;
			assignable = e->assignable;
			break;
		case VOID_EXTYPE:
		case JUMP_EXTYPE:
			return;
		default:
#ifdef DEBUG_PARSER
			fprintf(stderr, "e->type = %d\n", e->type);
#endif
			assert(p->panicMode || false);
	}
	emitBytecode(p, instruction);
	printExpr(stderr, e);
NO_INSTRUCTION:
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	e->u.s.info = 0;
	AS_OBJ(e->u.v) = NULL;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
	e->u.r.r = r;
	e->assignable = assignable;
	e->type = NONRELOC_EXTYPE;
}

static void jump_append(Parser *p, OP_position *list1, OP_position list2) {
	PRINT_FUNCTION;
	if(list2 == NO_JUMP)
		return;
	if(*list1 == NO_JUMP) {
		*list1 = list2;
	} else {
		OP_position list = *list1;
		OP_position next;
		while((next = jump_next(currentChunk(p->currentCompiler), list)) != NO_JUMP)
			list = next;
		jump_patch_instruction(p, list, list2);
	}
}

static bool jump_novalue(Chunk *c, OP_position list) {
	while(list != NO_JUMP) {
		uint32_t p = c->code[list >= 1 ? list-1 : list];
		if(!(OP(p) == OP_COPY_JUMP_IF_TRUE || OP(p) == OP_COPY_JUMP_IF_FALSE || RA(p) == NO_REG))
			return true;
		list = jump_next(c, list);
	}
	return false;
}

static size_t emit_jump(Parser *p) {
	PRINT_FUNCTION;
	Compiler *c = p->currentCompiler;
	// TODO handle closing upvalues.
	printPendingJumps(c);
	OP_position pjl = c->pendingJumpList;
	c->pendingJumpList = NO_JUMP;
	OP_position j = emit_AJ(p, OP_JUMP, c->nextReg, (uint16_t)NO_JUMP);
	jump_append(p, &j, pjl);
	printPendingJumps(c);
	return j;
}

static void jump_to_here(Parser *p, OP_position list) {
	PRINT_FUNCTION;
	Compiler *c = p->currentCompiler;
	c->last_target = currentChunk(c)->count;
	printPendingJumps(c);
	jump_append(p, &p->currentCompiler->pendingJumpList, list);
	printPendingJumps(c);
}

static void jump_patch(Parser *p, OP_position list, OP_position target) {
	if(target == currentChunk(p->currentCompiler)->count) {
		jump_to_here(p, list);
	} else {
		assert(target < currentChunk(p->currentCompiler)->count);
		jump_patch_value(p, list, target, NO_REG, target);
	}
}

XAN_STATIC_ASSERT(((int)OP_EQUAL^1) == (int)OP_NEQ);
XAN_STATIC_ASSERT(((int)OP_GREATER^1) == (int)OP_LEQ);
XAN_STATIC_ASSERT(((int)OP_LESS^1) == (int)OP_GEQ);

static void invertCond(Compiler *c, expressionDescription *e) {
	uint32_t *ip = &currentChunk(c)->code[e->u.s.info - 1];
	setbc_op(ip, OP(*ip)^1);
}

static OP_position emit_branch(Parser *p, expressionDescription *e, bool cond) {
	if(e->type == RELOC_EXTYPE) {
		uint32_t *ip = &currentChunk(p->currentCompiler)->code[e->u.s.info];
		if(OP(*ip) == OP_NOT) {
			*ip = OP_AD(cond ? OP_JUMP_IF_FALSE : OP_JUMP_IF_TRUE, 0, RD(*ip));
			return emit_jump(p);
		}
	}
	if(e->type != NONRELOC_EXTYPE) {
		regReserve(p->currentCompiler, 1);
		exprToRegNoBranch(p, e, p->currentCompiler->nextReg-1);
	}
	emit_AD(p, cond ? OP_COPY_JUMP_IF_TRUE : OP_COPY_JUMP_IF_FALSE, NO_REG, e->u.s.info);
	OP_position pc = emit_jump(p);
	exprFree(p->currentCompiler, e);
	return pc;
}

static void emit_branch_true(Parser *p, expressionDescription *e) {
	exprDischarge(p, e);
	// TODO handle constant expressions.
	OP_position pc;
	if(e->type == JUMP_EXTYPE) {
		invertCond(p->currentCompiler, e);
		pc = e->u.s.info;
	} else {
		pc = emit_branch(p, e, false);
	}
	jump_append(p, &e->false_jump, pc);
	jump_to_here(p, e->true_jump);
	e->true_jump = NO_JUMP;
}

static void emit_branch_false(Parser *p, expressionDescription *e) {
	exprDischarge(p, e);
	// TODO handle constant expressions.
	OP_position pc;
	if(e->type == JUMP_EXTYPE) {
		pc = e->u.s.info;
	} else {
		pc = emit_branch(p, e, true);
	}
	jump_append(p, &e->true_jump, pc);
	jump_to_here(p, e->false_jump);
	e->false_jump = NO_JUMP;
}

static void exprToReg(Parser *p, expressionDescription *e, Reg r) {
	PRINT_FUNCTION;
	printExpr(stderr, e);
	exprToRegNoBranch(p, e, r);
	printExpr(stderr, e);
	if(e->type == JUMP_EXTYPE)
		jump_append(p, &e->true_jump, e->u.s.info);
	printExpr(stderr, e);
	if(expr_hasjump(e)) {
		OP_position jump_false = NO_JUMP;
		OP_position jump_true = NO_JUMP;
		if(jump_novalue(currentChunk(p->currentCompiler), e->true_jump) || jump_novalue(currentChunk(p->currentCompiler), e->false_jump)) {
			OP_position jump_val = e->type == JUMP_EXTYPE ? NO_JUMP : emit_jump(p);
			jump_false = emit_AD(p, OP_PRIMITIVE, r, FALSE_EXTYPE);
			emit_AJ(p, OP_JUMP, p->currentCompiler->nextReg, 1);
			jump_true = emit_AD(p, OP_PRIMITIVE, r, TRUE_EXTYPE);
			jump_to_here(p, jump_val);
		}
		OP_position jump_end = currentChunk(p->currentCompiler)->count;
		p->currentCompiler->last_target = jump_end;
		jump_patch_value(p, e->false_jump, jump_end, r, jump_false);
		jump_patch_value(p, e->true_jump, jump_end, r, jump_true);
	}
	printExpr(stderr, e);
	e->true_jump = e->false_jump = NO_JUMP;
	e->u.r.r = r;
	e->assignable &= (e->type == NONRELOC_EXTYPE || e->type == GLOBAL_EXTYPE || e->type == LOCAL_EXTYPE);
	e->type = NONRELOC_EXTYPE;
}

static void exprNextReg(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	exprDischarge(p, e);
	printExpr(stderr, e);
	exprFree(p->currentCompiler, e);
	printExpr(stderr, e);
	regReserve(p->currentCompiler, 1);
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->currentCompiler->nextReg);
#endif /* DEBUG_PARSER */
	printExpr(stderr, e);
	exprToReg(p, e, p->currentCompiler->nextReg - 1);
	printExpr(stderr, e);
}

static Reg exprAnyReg(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	printExpr(stderr, e);
	exprDischarge(p, e);
	printExpr(stderr, e);
	if(e->type == NONRELOC_EXTYPE) {
		if(!expr_hasjump(e))
			return e->u.r.r;
		if(e->u.r.r >= p->currentCompiler->actVar) {
			exprToReg(p, e, e->u.r.r);
			return e->u.r.r;
		}
		exprNextReg(p, e);
		return e->u.r.r;
	}
	printExpr(stderr, e);
	exprNextReg(p, e);
	printExpr(stderr, e);
	return e->u.r.r;
}

static void expr_toval(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	if(expr_hasjump(e))
		exprAnyReg(p, e);
	else
		exprDischarge(p, e);
}

static void emit_arith(Parser *p, ByteCode op, expressionDescription *e1, expressionDescription *e2) {
	PRINT_FUNCTION;

	expr_toval(p, e2);
	Reg rc = exprAnyReg(p, e2);
	expr_toval(p, e1);
	Reg rb = exprAnyReg(p, e1);

#ifdef DEBUG_PARSER
	fprintf(stderr, "emit_arith_freeReg nextReg = %d\n", p->currentCompiler->nextReg);
#endif /* DEBUG_PARSER */
	if((e1->type == NONRELOC_EXTYPE) && (e1->u.r.r >= p->currentCompiler->actVar)) p->currentCompiler->nextReg--;
	if((e2->type == NONRELOC_EXTYPE) && (e2->u.r.r >= p->currentCompiler->actVar)) p->currentCompiler->nextReg--;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	e1->u.r.r = 0;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
	e1->u.s.info = emit_ABC(p, op, 0, rb, rc);
	e1->type = RELOC_EXTYPE;
}

static void emit_comp(Parser *p, ByteCode op, expressionDescription *e1, expressionDescription *e2) {
	PRINT_FUNCTION;

	expr_toval(p, e1);
	Reg rb = exprAnyReg(p, e1);
	expr_toval(p, e2);
	Reg rc = exprAnyReg(p, e2);

#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->currentCompiler->nextReg);
#endif /* DEBUG_PARSER */
	if((e1->type == NONRELOC_EXTYPE) && (e1->u.r.r >= p->currentCompiler->actVar)) p->currentCompiler->nextReg--;
	if((e2->type == NONRELOC_EXTYPE) && (e2->u.r.r >= p->currentCompiler->actVar)) p->currentCompiler->nextReg--;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	e1->u.r.r = 0;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
	e1->u.s.info = emit_ABC(p, op, 0, rb, rc);
	e1->type = RELOC_EXTYPE;
}

static void emit_binop_left(Parser *p, ByteCode op, expressionDescription *e) {
	switch(op) {
		case OP_AND:
			emit_branch_true(p, e);
			break;
		case OP_OR:
			emit_branch_false(p, e);
			break;
		default:
			// TODO constants
			exprAnyReg(p, e);
	}
}

static void emit_binop(Parser *p, ByteCode op, expressionDescription *e1, expressionDescription *e2) {
	PRINT_FUNCTION;
	if((op == OP_ADDVV) || (op == OP_SUBVV) || (op == OP_MULVV) || (op == OP_DIVVV)) {	// TODO make this a single comparison: op <= ??????
		emit_arith(p, op, e1, e2);
	} else if(op == OP_AND) {
		assert(e1->true_jump == NO_JUMP);
		exprDischarge(p, e2);
		jump_append(p, &e2->false_jump, e1->false_jump);
		*e1 = *e2;
	} else if(op == OP_OR) {
		assert(e1->false_jump == NO_JUMP);
		exprDischarge(p, e2);
		jump_append(p, &e2->true_jump, e1->true_jump);
		*e1 = *e2;
	} else {
		assert((op == OP_EQUAL) || (op == OP_NEQ) || (op == OP_LESS) || (op == OP_LEQ) || (op == OP_GREATER) || (op == OP_GEQ));
		emit_comp(p, op, e1, e2);
	}
}

static void emit_store(Parser *p, expressionDescription *variable, expressionDescription *e) {
	PRINT_FUNCTION;
	if(variable->type == LOCAL_EXTYPE) {
		exprFree(p->currentCompiler, e);
		exprToReg(p, e, variable->u.r.r);
		return;
	} else if(variable->type == UPVAL_EXTYPE) {
		expr_toval(p, e);
		emit_AD(p, OP_SET_UPVAL, variable->u.s.info, exprAnyReg(p, e));
	// TODO Upvalue
	} else if(variable->type == GLOBAL_EXTYPE) {
		Reg r = exprAnyReg(p, e);
		emit_AD(p, OP_SET_GLOBAL, r, variable->u.s.info);
	}
	// TODO indexed
}

typedef void (*ParseFn)(Parser*, expressionDescription*);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

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

static void markInitialized(Parser *p) {
	if(p->currentCompiler->scopeDepth == 0)
		return;
	p->currentCompiler->locals[p->currentCompiler->localCount-1].depth = p->currentCompiler->scopeDepth;
}

static void emitDefine(Parser *p, expressionDescription *v, expressionDescription *e) {
	PRINT_FUNCTION;
	printExpr(stderr, v);
	printExpr(stderr, e);
	if(p->currentCompiler->scopeDepth > 0) {
		markInitialized(p);
		exprFree(p->currentCompiler, e);
		exprToReg(p, e, v->u.r.r);
		return;
	}
	exprAnyReg(p, e);
	emitBytecode(p, OP_AD(OP_DEFINE_GLOBAL, e->u.r.r, v->u.s.info));
	exprFree(p->currentCompiler, e);
}

static void emitReturn(Parser *p, expressionDescription *e) {
	emitBytecode(p, OP_AD(OP_RETURN, exprAnyReg(p, e), 2));
}

static Compiler* initCompiler(Parser *p, Compiler *compiler, FunctionType type) {
	compiler->enclosing = p->currentCompiler;
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->function = newFunction(p->vm);
	if(type != TYPE_SCRIPT) {
		compiler->function->name = copyString(p->previous.start, p->previous.length, p->vm);
	}
	compiler->pendingJumpList = NO_JUMP;
	compiler->nextReg = compiler->actVar = compiler->maxReg = 0;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", compiler->nextReg);
#endif /* DEBUG_PARSER */
	Local *local = &compiler->locals[compiler->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	local->name.start = "";
	local->name.length = 0;
	return compiler;
}

static ObjFunction *endCompiler(Parser *p) {
	Chunk *c = currentChunk(p->currentCompiler);
	if((c->count == 0) || (p->currentCompiler->pendingJumpList != NO_JUMP) || (OP(c->code[c->count-1])) != OP_RETURN) {
		expressionDescription e;
		exprInit(&e, NIL_EXTYPE, 0);
		emitReturn(p, &e);
	}
	ObjFunction *f = p->currentCompiler->function;
	f->stackUsed = p->currentCompiler->maxReg;
	f->uv = ALLOCATE(uint16_t, f->uvCount);
	for(size_t i=0; i<f->uvCount; i++)
		f->uv[i] = p->currentCompiler->upvalues[i];
#ifdef DEBUG_PRINT_CODE
	if(!p->hadError) {
		disassembleChunk(currentChunk(p->currentCompiler), f->name == NULL ? "<scripts>" : f->name->chars);
	}
#endif
	p->currentCompiler = p->currentCompiler->enclosing;
	return f;
}

static uint16_t makeConstant(Parser *p, Value v) {
	size_t constant = addConstant(currentChunk(p->currentCompiler), v);
	if(constant > UINT16_MAX) {
		errorAtPrevious(p, "Too many constants in one chunk.");
		return 0;
	}
	return (uint16_t)constant;
}

static bool identifiersEqual(Token *a, Token *b) {
	return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Parser *p, Compiler *c, Token *name) {
	for(int i = c->localCount; i>0; i--) {
		Local *local = &c->locals[i-1];
		if(identifiersEqual(name, &local->name)) {
			if(local->depth == -1)
				errorAtPrevious(p, "Cannot read local variable in its own initializer.");
			return i-2;
		}
	}
	return -1;
}

static int addLocal(Parser *p, Token name) {
	PRINT_FUNCTION;
	Compiler *c = p->currentCompiler;
	if(c->localCount == UINT8_COUNT) {
		errorAtPrevious(p, "Too many local variables in function.");
	}
	c->actVar++;
	Local *local = &c->locals[c->localCount++];
	local->name = name;
	local->depth = -1;
	local->isCaptured = false;
	return c->localCount-2;
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
	int ret = addLocal(p, *name);
	return ret;
}

/*
static Reg incReg(Parser *p) {
	PRINT_FUNCTION;
#ifdef DEBUG_PARSER
	fprintf(stderr, "nextReg = %d\n", p->currentCompiler->nextReg);
#endif 
	return p->currentCompiler->nextReg++;
}

static Reg emitConstant(Parser *p, Value v) {
	PRINT_FUNCTION;
	Reg r = incReg(p);
	emitBytecode(p, OP_AD(OP_CONST_NUM, r, makeConstant(p, v)));
	return r;
}
*/

static ParseRule* getRule(TokenType type);
static void parsePrecedence(Parser *p, expressionDescription *e, Precedence precedence);
static void expression(Parser *p, expressionDescription *e);

static void assign_adjust(Parser *p, /* uint8_t nVars, uint8_t nExps,*/ expressionDescription *e) {
	if(e->type == CALL_EXTYPE) {
		setbc_b(&currentChunk(p->currentCompiler)->code[e->u.r.r], 1);
	} else {
		if(e->type != VOID_EXTYPE)
			exprNextReg(p, e);
	}
}

static void assign(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	printExpr(stderr, e);
	if((e->type != NONRELOC_EXTYPE && e->type != GLOBAL_EXTYPE && e->type != LOCAL_EXTYPE && e->type != UPVAL_EXTYPE) || !e->assignable) {
		errorAtPrevious(p, "Invalid assignment target.");
		return;
	}
	expressionDescription e2;
	parsePrecedence(p, &e2, PREC_ASSIGNMENT);
	if(e2.type == CALL_EXTYPE) {
		e2.u.s.info = e2.u.s.aux;
		e2.type = NONRELOC_EXTYPE;
	}
	emit_store(p, e, &e2);
	e->type = NONRELOC_EXTYPE;
	e->u.r.r = e2.type == NONRELOC_EXTYPE ? e2.u.r.r : e2.u.s.info;
}

static void binary(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	printExpr(stderr, e);
	TokenType operatorType = p->previous.type;

	ParseRule *rule = getRule(operatorType);

	ByteCode op;
	switch(operatorType) {
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
		case TOKEN_AND: op = OP_AND; break;
		case TOKEN_OR: op = OP_OR; break;
		default: assert(false);
	}

	emit_binop_left(p, op, e);

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
	parsePrecedence(p, e, PREC_ASSIGNMENT);
	consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
	if(e->type == NONRELOC_EXTYPE || e->type == GLOBAL_EXTYPE)
		e->assignable = false;
}

static Reg exprList(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	uint8_t n = 1;
	expression(p, e);
	while(match(p, TOKEN_COMMA)) {
		exprNextReg(p, e);
		expression(p, e);
		n++;
	}
	return n;
}

static Reg argumentList(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	expressionDescription args;
	Reg nargs;
	if(match(p, TOKEN_RIGHT_PAREN)) {
		args.type = VOID_EXTYPE;
		nargs = 0;
	} else {
		nargs = exprList(p, &args);
		// TODO if(args.type == CALL_EXTYPE)
		consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
	}
	assert(e->type == NONRELOC_EXTYPE);
	Reg base = e->u.r.r;
	if(args.type != VOID_EXTYPE)
		exprNextReg(p, &args);
	uint32_t ins = OP_ABC(OP_CALL, base, 1, nargs);
	exprInit(e, CALL_EXTYPE, emitBytecode(p, ins));
	e->u.s.aux = base;
	p->currentCompiler->nextReg = base+1;
	return nargs;
}

static void call(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	exprNextReg(p, e);
	argumentList(p, e);
}

static void number(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	double value = strtod(p->previous.start, NULL);
	e->type = NUMBER_EXTYPE;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	e->u.r.r = 0;
	e->u.s.info = 0;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
	e->u.v = NUMBER_VAL(value);
	e->true_jump = e->false_jump = NO_JUMP;
}

static void string(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	e->type = STRING_EXTYPE;
#ifdef DEBUG_EXPRESSION_DESCRIPTION
	e->u.r.r = 0;
	e->u.s.info = 0;
#endif /* DEBUG_EXPRESSION_DESCRIPTION */
	e->u.v = OBJ_VAL(copyString(p->previous.start + 1, p->previous.length - 2, p->vm));
	e->true_jump = e->false_jump = NO_JUMP;
}

static void unary(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;

	ByteCode op;
	switch(p->previous.type) {
		case TOKEN_BANG: op = OP_NOT; break;
		case TOKEN_MINUS: op = OP_NEGATE; break;
		default: assert(false);
	}

	parsePrecedence(p, e, PREC_UNARY);

	printExpr(stderr, e);
	exprAnyReg(p, e);
	printExpr(stderr, e);
	exprFree(p->currentCompiler, e);
	exprInit(e, RELOC_EXTYPE, emit_AD(p, op, 0, e->u.r.r));
}

static void uv_mark(Compiler *c, int arg) {
	c->locals[arg].isCaptured = true;
}

static size_t var_lookup_uv(Parser *p, Compiler *c, int arg, expressionDescription *e, bool isLocal) {
	size_t uv_count = c->function->uvCount;
	uint16_t larg = isLocal ? UV_IS_LOCAL | arg : arg;
	for(size_t i=0; i<uv_count; i++) {
		uint16_t *uv = c->upvalues + i;
		if(*uv == larg)
			return i;
	}
	if(uv_count == UINT8_COUNT) {
		errorAtPrevious(p, "Too many closure variables in function.");
		exit(EXIT_COMPILE_ERROR);
	}
	c->upvalues[uv_count] = larg;
	return c->function->uvCount++;
}

static int var_lookup(Parser *p, Compiler *c, Token *name, expressionDescription *e, bool local) {
	if(c) {
		int arg = resolveLocal(p, c, name);
		if(arg >= 0) {
			exprInit(e, LOCAL_EXTYPE, arg);
			e->assignable = true;
			if(!local)
				uv_mark(c, arg+1);
			return arg;		// TODO vstack?
		} else {
			arg = var_lookup(p, c->enclosing, name, e, false);
			if(arg >= 0) {
				e->u.s.info = var_lookup_uv(p, c, arg, e, e->type == LOCAL_EXTYPE);
				e->type = UPVAL_EXTYPE;
				e->assignable = true;
				return e->u.s.info;
			}
		}
	} else {
		exprInit(e, GLOBAL_EXTYPE,
				makeConstant(p, OBJ_VAL(copyString(name->start, name->length, p->vm))));
	}
	return -1;
}

static void variable(Parser *p, expressionDescription *e) {
	PRINT_FUNCTION;
	var_lookup(p, p->currentCompiler, &p->previous, e, true);
}

#define BUILD_RULES(t, prefix, infix, precedence) \
{ prefix, infix, precedence }
ParseRule rules[] = {
	TOKEN_BUILDER(BUILD_RULES)
};
#undef BUILD_RULES

static ParseRule* getRule(TokenType type) {
#ifdef DEBUG_PARSER
	fprintf(stderr, "In function %s\t tokenType = %s\n", __func__, TokenNames[type]);
#endif /* DEBUG_PARSER */
	return &rules[type];
}

static void parsePrecedence(Parser *p, expressionDescription *e, Precedence precedence) {
	PRINT_FUNCTION;
	advance(p);
	ParseFn prefixRule = getRule(p->previous.type)->prefix;
	if(prefixRule == NULL) {
		errorAtPrevious(p, "Expect expression.");
		exit(EXIT_COMPILE_ERROR);
		// return;
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
	if((e->type == NONRELOC_EXTYPE) && e->u.r.assignable && match(p, TOKEN_EQUAL)) {
		errorAtPrevious(p, "Invalid assignment target.");
		parsePrecedence(p, e, PREC_ASSIGNMENT);
	}
	*/
}

static void parseVariable(Parser *p, expressionDescription *e, const char *errorMessage) {
	PRINT_FUNCTION;
	consume(p, TOKEN_IDENTIFIER, errorMessage);
	int r = declareVariable(p);
	if(r < 0)
		exprInit(e, GLOBAL_EXTYPE,
				makeConstant(p, OBJ_VAL(copyString(p->previous.start, p->previous.length, p->vm))));
	else {
		exprInit(e, LOCAL_EXTYPE, r);
		e->assignable = true;
	}
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
	fprintf(stderr, "nextReg = %d\n", p->currentCompiler->nextReg);
#endif /* DEBUG_PARSER */
	expressionDescription e;
	if(match(p, TOKEN_EQUAL)) {
		expression(p, &e);
	} else {
		exprInit(&e, NIL_EXTYPE, 0);
	}
	consume(p, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	assign_adjust(p, &e);
	emitDefine(p, &v, &e);
}

static void expressionStatement(Parser *p) {
	PRINT_FUNCTION;
	expressionDescription e;
	expression(p, &e);
	consume(p, TOKEN_SEMICOLON, "Expect ';' after expression.");
	printExpr(stderr, &e);
	exprAnyReg(p, &e);
	printExpr(stderr, &e);
	exprFree(p->currentCompiler, &e);
	printExpr(stderr, &e);
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

static OP_position expressionCondition(Parser *p) {
	expressionDescription e;
	expression(p, &e);

	if(e.type == NIL_EXTYPE) e.type = FALSE_EXTYPE;
	emit_branch_true(p, &e);
	return e.false_jump;
}

static void declaration(Parser *p);
static void statement(Parser *p);

static void ifStatement(Parser *p) {
	PRINT_FUNCTION;
	consume(p, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	OP_position escapelist = NO_JUMP;
	OP_position flist = expressionCondition(p);
	consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	statement(p);
	if(match(p, TOKEN_ELSE)) {
		jump_append(p, &escapelist, emit_jump(p));
		jump_to_here(p, flist);
		statement(p);
	} else {
		jump_append(p, &escapelist, flist);
	}
	jump_to_here(p, escapelist);
}

static void whileStatement(Parser *p) {
	consume(p, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	OP_position start, exit_condition;
	start = p->currentCompiler->last_target = currentChunk(p->currentCompiler)->count;
	exit_condition = expressionCondition(p);
	consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	statement(p);
	jump_patch(p, emit_jump(p), start);
	jump_to_here(p, exit_condition);
}

static void beginScope(Compiler *current) {
	current->scopeDepth++;
}

static size_t endScope(Parser *p) {
	Compiler *c = p->currentCompiler;
	size_t i;
	bool closeUpvalues = false;
	for(i = c->localCount; i>0; i--) {
		Local *l = &c->locals[i-1];
		if(l->depth < c->scopeDepth)
			break;
		closeUpvalues |= l->isCaptured;
		c->actVar--;
	}

	if(closeUpvalues)
		emit_AD(p, OP_CLOSE_UPVALUES, c->actVar, 0);
	c->nextReg = c->actVar;
	c->scopeDepth--;
	assert(c->scopeDepth >= 0);
	size_t ret = c->localCount - i;
	c->localCount = i;
	return ret;
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

static void forStatement(Parser *p) {
	consume(p, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

	beginScope(p->currentCompiler);
	if(match(p, TOKEN_SEMICOLON)) {
		// no initializer.
	} else if(match(p, TOKEN_VAR)) {
		varDeclaration(p);
	} else {
		expressionStatement(p);
	}
	OP_position start = p->currentCompiler->last_target = currentChunk(p->currentCompiler)->count;

	OP_position exit_condition = NO_JUMP;
	if(p->panicMode) {
		synchronize(p);
	} else if(!match(p, TOKEN_SEMICOLON)) {
		exit_condition = expressionCondition(p);
		consume(p, TOKEN_SEMICOLON, "Expected ';' after loop condition.");
	}

	if(!match(p, TOKEN_RIGHT_PAREN)) {
		OP_position bodyJump = emit_jump(p);

		OP_position incrementStart = currentChunk(p->currentCompiler)->count;
		expressionDescription e;
		expression(p, &e);
		consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after for clause.");

		jump_patch(p, emit_jump(p), start);
		start = incrementStart;
		jump_patch(p, bodyJump, currentChunk(p->currentCompiler)->count);
	}
	statement(p);
	endScope(p);
	jump_patch(p, emit_jump(p), start);
	jump_to_here(p, exit_condition);
}

static void block(Parser *p) {
	PRINT_FUNCTION;
	while(!check(p, TOKEN_RIGHT_BRACE) && !check(p, TOKEN_EOF))
		declaration(p);
	consume(p, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(Parser *p, expressionDescription *e, FunctionType type) {
	Compiler c;
	p->currentCompiler = initCompiler(p, &c, type);
	beginScope(&c);

	// Compile parameters.
	consume(p, TOKEN_LEFT_PAREN, "Expect '(' after function name.");
	if(!check(p, TOKEN_RIGHT_PAREN)) {
		do {
			p->currentCompiler->function->arity++;
			if(p->currentCompiler->function->arity > 255) {
				errorAtCurrent(p, "Cannot have more than 255 parameters.");
			}

			expressionDescription v;
			parseVariable(p, &v, "Expect parameter name.");
			regReserve(p->currentCompiler, 1);
			markInitialized(p);
		} while(match(p, TOKEN_COMMA));
	}
	consume(p, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

	// Compile the body.
	consume(p, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block(p);
	endScope(p);

	if(!p->hadError) {
		ObjFunction *f = endCompiler(p);
		exprInit(e, RELOC_EXTYPE, emit_AD(p, OP_CLOSURE, p->currentCompiler->actVar, makeConstant(p, OBJ_VAL(f))));
	}
}

static void funDeclaration(Parser *p) {
	expressionDescription name, e;
	parseVariable(p, &name, "Expect function name.");
	markInitialized(p);
	function(p, &e, TYPE_FUNCTION);
	if(!p->hadError) {
		exprNextReg(p, &e);
		emitDefine(p, &name, &e);
	}
}

static void returnStatement(Parser *p) {
	if(p->currentCompiler->type == TYPE_SCRIPT)
		errorAtPrevious(p, "Cannot return from top-level code.");
	expressionDescription e;
	if(match(p, TOKEN_SEMICOLON)) {
		exprInit(&e, NIL_EXTYPE, 0);
	} else {
		expression(p, &e);
		consume(p, TOKEN_SEMICOLON, "Expect ';' after return value.");
	}
	emitReturn(p, &e);
}

static void statement(Parser *p) {
	PRINT_FUNCTION;
	if(match(p, TOKEN_PRINT)) {
		printStatement(p);
	} else if(match(p, TOKEN_IF)) {
		ifStatement(p);
	} else if(match(p, TOKEN_RETURN)) {
		returnStatement(p);
	} else if(match(p, TOKEN_WHILE)) {
		whileStatement(p);
	} else if(match(p, TOKEN_FOR)) {
		forStatement(p);
	} else if(match(p, TOKEN_LEFT_BRACE)) {
		beginScope(p->currentCompiler);
		block(p);
		endScope(p);
	} else {
		expressionStatement(p);
	}
	assert(p->currentCompiler->nextReg >= p->currentCompiler->actVar);
	p->currentCompiler->nextReg = p->currentCompiler->actVar;
}

static void declaration(Parser *p) {
	PRINT_FUNCTION;
	if(match(p, TOKEN_FUN)) {
		funDeclaration(p);
	} else if(match(p, TOKEN_VAR)) {
		varDeclaration(p);
	} else {
		statement(p);
	}

	if(p->panicMode)
		synchronize(p);
}

static void initParser(Parser *p, VM *vm, Compiler *compiler, const char *source) {
	PRINT_FUNCTION;
	p->s = initScanner(source);
	p->vm = vm;
	p->hadError = false;
	p->panicMode = false;
	p->currentCompiler = NULL;
	p->currentCompiler = initCompiler(p, compiler, TYPE_SCRIPT);
}

ObjFunction *parse(VM *vm, const char *source) {
	Parser p;
	Compiler compiler;
	initParser(&p, vm, &compiler, source);

	advance(&p);
	while(!match(&p, TOKEN_EOF)) {
		declaration(&p);
	}
	endScanner(p.s);

	if(p.hadError)
		return NULL;

	ObjFunction *f = endCompiler(&p);
	return f;
}

#ifndef XAN_CHUNK_H
#define XAN_CHUNK_H

#include <assert.h>

#include "object.h"

#define COMMA ,
#define OPCODE_BUILDER(X, sep) \
	X(OP_CONST_NUM,			ADConst)sep		/*  0 */ \
	X(OP_PRIMITIVE, 		ADprim)sep \
	X(OP_NEGATE,			AD)sep \
	X(OP_NOT,				AD)sep \
	X(OP_DEFINE_GLOBAL,		ADstr)sep \
	X(OP_SET_GLOBAL,		ADstr)sep		/*  5 */ \
	X(OP_GET_GLOBAL,		ADstr)sep \
	X(OP_RETURN,			ADret)sep \
	X(OP_EQUAL,				ABC)sep \
	X(OP_NEQ,				ABC)sep \
	X(OP_GREATER,			ABC)sep			/* 10 */ \
	X(OP_LEQ,				ABC)sep \
	X(OP_GEQ,				ABC)sep \
	X(OP_LESS,				ABC)sep \
	X(OP_ADDVV,				ABC)sep \
	X(OP_SUBVV,				ABC)sep			/* 15 */ \
	X(OP_MULVV,				ABC)sep \
	X(OP_DIVVV,				ABC)sep \
	X(OP_MODVV,				ABC)sep \
	X(OP_JUMP,				J)sep \
	X(OP_COPY_JUMP_IF_FALSE,AD)sep			/* 20 */ \
	X(OP_COPY_JUMP_IF_TRUE, AD)sep \
	X(OP_JUMP_IF_FALSE,		D)sep \
	X(OP_JUMP_IF_TRUE, 		D)sep \
	X(OP_MOV,				AD)sep \
	X(OP_CALL,				ABCcall)sep		/* 25 */ \
	X(OP_GET_UPVAL,			AD)sep \
	X(OP_SET_UPVAL,			AD)sep \
	X(OP_CLOSURE,			AD)sep \
	X(OP_CLOSE_UPVALUES,	A)sep \
	X(OP_CLASS,				ADConst)sep		/* 30 */ \
	X(OP_GET_PROPERTY,		ABC)sep \
	X(OP_SET_PROPERTY,		ABC)sep \
	X(OP_METHOD,			ABC)sep \
	X(OP_INHERIT,			AD)sep \
	X(OP_GET_SUPER,			ABC)sep			/* 35 */ \
	X(OP_NEW_ARRAY,			AD)sep \
	X(OP_DUPLICATE_ARRAY,	AD)sep \
	X(OP_NEW_TABLE,			AD)sep \
	X(OP_DUPLICATE_TABLE,	AD)sep \
	X(OP_GET_SUBSCRIPT,		ABC)sep			/* 40 */ \
	X(OP_SET_SUBSCRIPT,		ABC)sep \
	X(OP_BEGIN_TRY,			AJ)sep \
	X(OP_END_TRY,			J)sep \
	X(OP_THROW,				A)sep \
	X(OP_JUMP_IF_NOT_EXC,	AJ)sep			/* 45 */ \
	X(OP_INVOKE,			ABCcall)sep
#define BUILD_OPCODES(op, _) op

typedef enum {
	OPCODE_BUILDER(BUILD_OPCODES, COMMA)

	OP_COUNT,
	OP_OR = OP_COPY_JUMP_IF_FALSE,	// For parsing
	OP_AND = OP_COPY_JUMP_IF_TRUE,	// For parsing
	// OP_ADDVK,
	// OP_SUBVK,
	// OP_MULVK,
	// OP_DIVVK,
	// OP_ADDKV,
	// OP_SUBKV,
	// OP_MULKV,
	// OP_DIVKV,
	// OP_INVOKE,			// Registers: M,N,O,P?
} ByteCode;

#define COMMA ,
#define PRIMITIVE_BUILDER(macro, delim) \
	macro(NIL) delim macro(TRUE) delim macro(FALSE)
#define MAKE_ENUM(x) PRIM_##x
typedef enum {
	PRIMITIVE_BUILDER(MAKE_ENUM, COMMA),
	MAX_PRIMITIVE
} primitive;
#undef MAKE_ENUM

static inline Value getPrimitive(primitive p) {
	switch(p) {
		case PRIM_NIL:   return NIL_VAL;
		case PRIM_TRUE:  return BOOL_VAL(true);
		case PRIM_FALSE: return BOOL_VAL(false);
		default: assert(false); return OBJ_VAL(NULL);
	}
}

// These macros are designed to put data into the uint32_t bytecodes.
#define OP_A(op,a) (((uint32_t) (op)) | (((uint32_t) (a)) << 8))
#define OP_D(op,d) (((uint32_t) (op)) | (((uint32_t) (d)) << 16))
#define OP_AD(op,a,d) (OP_A((op),(a)) | (((uint32_t) (d)) << 16))
#define OP_ABC(op,a,b,c) OP_AD((op), (a), (((uint16_t) (b)) | (((uint16_t) (c)) << 8)))
#define OP_AJump(op,a,j) OP_AD((op), (a), (OP_position)((int32_t)(j)+JUMP_BIAS))

#define MAX_REG 0xff
#define MAX_D 0xffff
#define JUMP_BIAS 0x8000
#define NO_REG MAX_REG
#define NO_JUMP (~(OP_position)0)

// These macros are designed to pull data out of the uint32_t bytecodes.
#define OP(x) ((Reg)(MAX_REG & ((uint32_t)(x))))
#define RA(x) ((Reg)(((uint16_t)(MAX_D & ((uint32_t)(x)))) >> 8))
#define RD(x) ((uint16_t)((x) >> 16))
#define RJump(x) ((ptrdiff_t)RD(x)-JUMP_BIAS)
#define RB(x) ((Reg)(MAX_REG & RD(x)))
#define RC(x) ((Reg)(RD(x) >> 8))

#define RM(x) (Reg)(0x3f & (((uint32_t)(x)) >>  8))
#define RN(x) (Reg)(0x3f & (((uint32_t)(x)) >> 14))
#define RO(x) (Reg)(0x3f & (((uint32_t)(x)) >> 20))
#define RP(x) (Reg)(0x3f & (((uint32_t)(x)) >> 26))

static inline void setbc(uint32_t *p, uint8_t x, uint8_t ofs) {
	((uint8_t*)(p))[ENDIAN_SELECT(ofs, 3-ofs)] = x;
}
static inline void setbc_d(uint32_t *p, uint16_t x) {
	((uint16_t*)p)[ENDIAN_SELECT(1,0)] = x;
}
#define setbc_op(p, x)	setbc(p, (x), 0)
#define setbc_a(p, x)	setbc(p, (x), 1)
#define setbc_b(p, x)	setbc(p, (x), 2)
#define setbc_c(p, x)	setbc(p, (x), 3)

void initChunk(VM *vm, Chunk *chunk);
void finalizeChunk(Chunk *chunk);
size_t writeChunk(VM *vm, Chunk *chunk, uint32_t opcode, size_t line);
size_t addConstant(VM *vm, Chunk *chunk, Value value);	// Caller is responsible to ensure that value is findable by the GC.

#endif /* XAN_CHUNK_H */

#ifndef XAN_CHUNK_H
#define XAN_CHUNK_H

#include <assert.h>
#include <stdint.h>

#include "value.h"

typedef enum {
	OP_CONST_NUM,		// Registers: A,D
	OP_PRIMITIVE,		// Registers: A,D
	OP_NEGATE,			// Registers: A,D
	OP_NOT,				// Registers: A,D
	OP_DEFINE_GLOBAL,	// Registers: A,D
	OP_SET_GLOBAL,		// Registers: A,D
	OP_GET_GLOBAL,		// Registers: A,D
	OP_EQUAL,			// Registers: A,B,C
	OP_NEQ,				// Registers: A,B,C
	OP_GREATER,			// Registers: A,B,C
	OP_GEQ,				// Registers: A,B,C
	OP_LESS,			// Registers: A,B,C
	OP_LEQ,				// Registers: A,B,C
	// OP_ADDVK,
	// OP_SUBVK,
	// OP_MULVK,
	// OP_DIVVK,
	// OP_ADDKV,
	// OP_SUBKV,
	// OP_MULKV,
	// OP_DIVKV,
	OP_ADDVV,			// Registers: A,B,C
	OP_SUBVV,			// Registers: A,B,C
	OP_MULVV,			// Registers: A,B,C
	OP_DIVVV,			// Registers: A,B,C
	OP_PRINT,			// Registers: A
	OP_RETURN,			// Registers: A
} ByteCode;

#define PRIMITIVE_BUILDER(macro) \
	macro(PRIM_NIL), macro(PRIM_TRUE), macro(PRIM_FALSE)
#define MAKE_ENUM(x) x
typedef enum {
	PRIMITIVE_BUILDER(MAKE_ENUM),
	MAX_PRIMITIVE
} primitive;
#undef MAKE_ENUM

static inline Value getPrimitive(primitive p) {
	switch(p) {
		case PRIM_NIL:   return NIL_VAL;
		case PRIM_TRUE:  return BOOL_VAL(true);
		case PRIM_FALSE: return BOOL_VAL(false);
		default: assert(false);
	}
}

typedef uint8_t Reg;		// A register, i.e. a stack offset.

// These macros are designed to put data into the uint32_t bytecodes.
#define OP_A(op,a) (((uint32_t) (op)) | (((uint32_t) (a)) << 8))
#define OP_D(op,d) (((uint32_t) (op)) | (((uint32_t) (d)) << 16))
#define OP_AD(op,a,d) (OP_A((op),(a)) | (((uint32_t) (d)) << 16))
#define OP_ABC(op,a,b,c) OP_AD((op), (a), (((uint16_t) (b)) | (((uint16_t) (c)) << 8)))

// These macros are designed to pull data out of the uint32_t bytecodes.
#define OP(x) ((Reg)(0xff & ((uint32_t)(x))))
#define RA(x) ((Reg)(((uint16_t)(0xffff & ((uint32_t)(x)))) >> 8))
#define RD(x) ((uint16_t)((x) >> 16))
#define RB(x) ((Reg)(0xff & RD(x)))
#define RC(x) ((Reg)(RD(x) >> 8))

typedef struct {
	size_t count;
	size_t capacity;
	uint32_t *code;
	size_t *lines;
	ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint32_t opcode, size_t line);
size_t addConstant(Chunk *chunk, Value value);

#endif /* XAN_CHUNK_H */

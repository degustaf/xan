#ifndef XAN_TYPE_H
#define XAN_TYPE_H

#include "common.h"
#include "scanner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define FRAMES_MAX 256
#define BASE_STACK_SIZE 16

#define OBJ_BUILDER(X, SEP) \
	X(STRING)SEP \
	X(NATIVE)SEP \
	X(FUNCTION)SEP \
	X(CLOSURE)SEP \
	X(UPVALUE)SEP \
	X(CLASS)SEP \
	X(MODULE)SEP \
	X(ARRAY)SEP \
	X(INSTANCE)SEP \
	X(BOUND_METHOD)SEP \
	X(TABLE)

typedef enum {
#define ENUM_BUILDER(x) OBJ_##x
#define COMMA ,
	OBJ_BUILDER(ENUM_BUILDER, COMMA)
#undef ENUM_BUILDER
} ObjType;

typedef uint8_t Reg;			// a register, i.e. a stack offset.
typedef uint32_t OP_position;	// an index into a bytecode array.

static const char* const ObjTypeNames[] = {
#define STRING_BUILDER(x) "OBJ_" #x
	OBJ_BUILDER(STRING_BUILDER, COMMA)
#undef STRING_BUILDER
};

struct sObj {
	ObjType type;
	bool isMarked;
	struct sObj *next;
};

typedef enum {
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
	VAL_OBJ,
} ValueType;

typedef struct {
	ValueType type;
	union {
		bool boolean;
		double number;
		Obj *obj;
	} as;
} Value;

typedef struct sObjClass ObjClass;
typedef struct sObjTable ObjTable;

#define INSTANCE_FIELDS \
	Obj obj; \
	ObjClass *klass; \
	ObjTable *fields

typedef struct sObjString ObjString;
#define KEY(e) AS_STRING(e[0])
#define VALUE(e) e[1]

struct sObjTable {
	INSTANCE_FIELDS;
	size_t count;
	ssize_t capacityMask;
	Value *entries;
};

typedef struct {
	INSTANCE_FIELDS;
	size_t capacity;
	size_t count;
	Value *values;
} ObjArray;

// TODO turn chars into a flexible array member to optimize by minimizing allocations.
struct sObjString {
	Obj obj;
	size_t length;
	uint32_t hash;
	char *chars;
};

typedef struct {
	size_t count;
	size_t capacity;
	uint32_t *code;
	size_t *lines;
	ObjArray *constants;
} Chunk;

typedef struct {
	Obj obj;
	int arity;
	size_t uvCount;
	Reg stackUsed;
	Chunk chunk;
	ObjString *name;
	uint16_t uv[];
} ObjFunction;

#define UV_IS_LOCAL 0x100

typedef struct sUpvalue {
	Obj obj;
	Value *location;
	Value closed;
	struct sUpvalue *next;
} ObjUpvalue;

typedef struct {
	Obj obj;
	ObjFunction *f;
	ObjUpvalue **upvalues;
	size_t uvCount;
} ObjClosure;

struct sObjClass {
	Obj obj;
	ObjString *name;
	ObjTable *methods;
};

typedef struct {
	INSTANCE_FIELDS;
} ObjInstance;

typedef struct {
	Obj obj;
	Value receiver;
	Obj *method;
} ObjBoundMethod;

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef enum {
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
	struct Compiler *enclosing;
	ObjString *name;
	Chunk chunk;
	FunctionType type;
	Local locals[UINT8_COUNT];
	uint16_t upvalues[UINT8_COUNT];
	size_t uvCount;
	int arity;
	size_t localCount;
	int scopeDepth;
	OP_position pendingJumpList;
	OP_position last_target;
	Reg nextReg;
	Reg actVar;
	Reg maxReg;
} Compiler;

typedef struct {
	ObjClosure *c;
	uint32_t *ip;
	Value *slots;
} CallFrame;

struct sVM {
	CallFrame frames[FRAMES_MAX];
	size_t frameCount;
	Value *stack;
	Value *stackTop;
	Value *stackLast;
	size_t stackSize;

	ObjTable *strings;
	ObjTable *globals;
	ObjString *initString;
	ObjUpvalue *openUpvalues;
	Obj *objects;
	Compiler *currentCompiler;
	size_t bytesAllocated;
	size_t nextGC;
	size_t grayCount;
	size_t grayCapacity;
	Obj** grayStack;
};

typedef Value (*NativeFn)(VM *vm, int argCount, Value *args);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

typedef struct {
	const char *const name;
	NativeFn method;
} NativeDef;

typedef struct {
	const char *const name;
	NativeDef *methods;
} classDef;

typedef struct {
	Obj obj;
	ObjString *name;
	ObjTable *items;
} ObjModule;

typedef struct {
	const char *const name;
	classDef *classes;
	NativeDef *methods;
} ModuleDef;

#endif /* XAN_TYPE_H */

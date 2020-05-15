#ifndef XAN_OBJECT_H
#define XAN_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)    (AS_OBJ(value)->type)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)   isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)   isObjType(value, OBJ_STRING)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)   (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)   ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)  (AS_STRING(value)->chars)

typedef enum {
	OBJ_STRING,
	OBJ_NATIVE,
	OBJ_FUNCTION,
} ObjType;

struct sObj {
	ObjType type;
	struct sObj *next;
};

typedef struct {
	Obj obj;
	int arity;
	Reg stackUsed;
	Chunk chunk;
	ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

// TODO turn chars into a flexible array member to optimize by minimizing allocations.
struct sObjString {
	Obj obj;
	size_t length;
	uint32_t hash;
	char *chars;
};

typedef struct sVM VM;

ObjFunction *newFunction(VM *vm);
ObjNative *newNative(VM *vm, NativeFn function);
ObjString *takeString(char *chars, size_t length, VM *vm);
ObjString *copyString(const char *chars, size_t length, VM *vm);
void fprintObject(FILE *restrict stream, Value value);
void printObject(Value value);

static inline bool isObjType(Value v, ObjType t) {
	return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

#endif /* XAN_OBJECT_H */

#ifndef XAN_OBJECT_H
#define XAN_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "value.h"

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)
#define IS_STRING(value)  isObjType(value, OBJ_STRING)
#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
	OBJ_STRING,
} ObjType;

struct sObj {
	ObjType type;
	struct sObj *next;
};

// TODO turn chars into a flexible array member to optimize by minimizing allocations.
struct sObjString {
	Obj obj;
	size_t length;
	uint32_t hash;
	char *chars;
};

typedef struct sVM VM;

ObjString *takeString(char *chars, size_t length, VM *vm);
ObjString *copyString(const char *chars, size_t length, VM *vm);
void printObject(Value value);

static inline bool isObjType(Value v, ObjType t) {
	return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

#endif /* XAN_OBJECT_H */

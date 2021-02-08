#ifndef XAN_OBJECT_H
#define XAN_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "type.h"

#define IS_OBJ(value)			((value).type == VAL_OBJ)
#define AS_OBJ(value)			((value).as.obj)
#define OBJ_TYPE(value)			(AS_OBJ(value)->type)
#define SAME_VAL_TYPE(v1, v2)	((v1).type == (v2).type)

static inline bool isObjType(Value v, ObjType t) {
	return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

#define IS_TYPE(t) \
	static inline bool IS_##t(Value value) { \
		return isObjType(value, OBJ_##t); \
	}

#define NOTHING
OBJ_BUILDER(IS_TYPE, NOTHING)

static inline bool HAS_PROPERTIES(Value value) {
	return IS_INSTANCE(value) || IS_ARRAY(value) || IS_STRING(value);
}

#define AS_ARRAY(value)        ((ObjArray*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_MODULE(value)       (((ObjModule*)AS_OBJ(value)))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_TABLE(value)        ((ObjTable*)AS_OBJ(value))
#define AS_EXCEPTION(value)    ((ObjException*)AS_OBJ(value))

#define AS_CSTRING(value)      (AS_STRING(value)->chars)

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
#define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
#define OBJ_VAL(object)   ((Value){ VAL_OBJ, { .obj = (Obj*)object } })

ObjArray *newArray(VM *vm, size_t count);
ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, Value method);
ObjClass *newClass(VM *vm, ObjString *name);
ObjClosure *newClosure(VM *vm, ObjFunction *f);
ObjFunction *newFunction(VM *vm, size_t uvCount, size_t varArityCount);
ObjInstance *newInstance(VM *vm, ObjClass *klass);
ObjModule * newModule(VM *vm, ObjString *name);
ObjNative *newNative(VM *vm, NativeFn function);

ObjUpvalue *newUpvalue(VM *vm, Value *slot);
ObjClass *copyClass(VM *vm, ObjClass *klass);
void defineNative(VM *vm, ObjTable *t, Value *slots, const NativeDef *f);
void defineNativeClass(VM *vm, ObjTable *t, Value *slots, ObjClass *def);
ObjModule *defineNativeModule(VM *vm, Value *slots, ModuleDef *def);
ObjArray *duplicateArray(VM *vm, ObjArray *source);
void setArray(VM *vm, ObjArray *array, int idx, Value v);
bool getArray(ObjArray *array, int idx, Value *ret);
ObjString *takeString(char *chars, size_t length, VM *vm);
ObjString *copyString(const char *chars, size_t length, VM *vm);
void fprintObject(FILE *restrict stream, Value value);
void printObject(Value value);

bool valuesEqual(Value, Value);
void fprintValue(FILE *restrict stream, Value value);
void printValue(Value value);

#endif /* XAN_OBJECT_H */

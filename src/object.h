#ifndef XAN_OBJECT_H
#define XAN_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_ARRAY(value)        isObjType(value, OBJ_ARRAY)

#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (AS_STRING(value)->chars)
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_ARRAY(value)        ((ObjArray*)AS_OBJ(value))

ObjFunction *newFunction(VM *vm, size_t uvCount);
ObjUpvalue *newUpvalue(VM *vm, Value *slot);
ObjClosure *newClosure(VM *vm, ObjFunction *f);
ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, Value method);
ObjClass *newClass(VM *vm, ObjString *name);
ObjClass *copyClass(VM *vm, ObjClass *klass);
ObjInstance *newInstance(VM *vm, ObjClass *klass);
ObjNative *newNative(VM *vm, NativeFn function);
ObjArray *newArray(VM *vm, size_t count);
ObjArray *duplicateArray(VM *vm, ObjArray *source);
void setArray(VM *vm, ObjArray *array, int idx, Value v);
bool getArray(VM *vm, ObjArray *array, int idx, Value *ret);
ObjString *takeString(char *chars, size_t length, VM *vm);
ObjString *copyString(const char *chars, size_t length, VM *vm);
void fprintObject(FILE *restrict stream, Value value);
void printObject(Value value);

static inline bool isObjType(Value v, ObjType t) {
	return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

#endif /* XAN_OBJECT_H */

#ifndef XAN_OBJECT_H
#define XAN_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

static inline bool isObjType(Value v, ObjType t) {
	return IS_OBJ(v) && AS_OBJ(v)->type == t;
}

#define IS_TYPE(t) \
	static inline bool IS_##t(Value value) { \
		return isObjType(value, OBJ_##t); \
	}

#define NOTHING
OBJ_BUILDER(IS_TYPE, NOTHING)

#define AS_ARRAY(value)        ((ObjArray*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_MODULE(value)       (((ObjModule*)AS_OBJ(value)))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))

#define AS_CSTRING(value)      (AS_STRING(value)->chars)

ObjFunction *newFunction(VM *vm, size_t uvCount);
ObjUpvalue *newUpvalue(VM *vm, Value *slot);
ObjClosure *newClosure(VM *vm, ObjFunction *f);
ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, Value method);
ObjClass *newClass(VM *vm, ObjString *name);
ObjClass *copyClass(VM *vm, ObjClass *klass);
void defineNative(VM *vm, Table *t, CallFrame *frame, const NativeDef *f);
void defineNativeClass(VM *vm, Table *t, CallFrame *frame, classDef *def);
ObjModule * newModule(VM *vm, ObjString *name);
void defineNativeModule(VM *vm, CallFrame *frame, ModuleDef *def);
ObjInstance *newInstance(VM *vm, ObjClass *klass);
ObjNative *newNative(VM *vm, NativeFn function);
ObjArray *newArray(VM *vm, size_t count);
ObjArray *duplicateArray(VM *vm, ObjArray *source);
void setArray(VM *vm, ObjArray *array, int idx, Value v);
bool getArray(ObjArray *array, int idx, Value *ret);
ObjString *takeString(char *chars, size_t length, VM *vm);
ObjString *copyString(const char *chars, size_t length, VM *vm);
void fprintObject(FILE *restrict stream, Value value);
void printObject(Value value);

#endif /* XAN_OBJECT_H */

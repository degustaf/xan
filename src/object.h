#ifndef XAN_OBJECT_H
#define XAN_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "type.h"

#define OBJ_TYPE(value)			(AS_OBJ(value)->type)

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

#ifdef TAGGED_NAN
	#define SIGN_BIT		  ((uint64_t) 0x8000000000000000)
	#define QNAN			  ((uint64_t) 0x7ffc000000000000)
	#define TAG_NIL			  1
	#define TAG_BOOL		  2
	#define TAG_FALSE		  0
	#define TAG_TRUE 		  1

	#define NIL_VAL			  ((Value){ .u = (QNAN | TAG_NIL) })
	#define FALSE_VAL		  ((Value){ .u = (QNAN | TAG_BOOL | TAG_FALSE) })
	#define TRUE_VAL		  ((Value){ .u = (QNAN | TAG_BOOL | TAG_TRUE) })
	#define BOOL_VAL(value)	  ((value) ? TRUE_VAL : FALSE_VAL)
	#define NUMBER_VAL(value) ((Value){.number = value})
	#define OBJ_VAL(obj)	  ((Value){ .u = (SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))})
	#define IP_VAL(ptr)		  ((Value){ .ip = (ptr)})

	#define IS_BOOL(value)	  (((value).u | TAG_TRUE) == TRUE_VAL.u)
	#define IS_NIL(value)	  ((value).u == NIL_VAL.u)
	#define IS_NUMBER(value)  (((value).u & QNAN) != QNAN)
	#define IS_OBJ(value)	  (((value).u & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

	#define AS_BOOL(value)	  ((value).u == TRUE_VAL.u)
	#define AS_NUMBER(value)  ((value).number)
	#define AS_OBJ(value)	  ((Obj*)(uintptr_t)(((value).u) & ~(SIGN_BIT | QNAN)))
	#define AS_IP(value)	  ((value).ip)
#else /* TAGGED_NAN */
	#define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
	#define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
	#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
	#define OBJ_VAL(object)   ((Value){ VAL_OBJ, { .obj = (Obj*)object } })
	#define IP_VAL(ptr)		  ((Value){ VAL_NIL, { .ip = (ptr)}})

	#define IS_BOOL(value)    ((value).type == VAL_BOOL)
	#define IS_NIL(value)     ((value).type == VAL_NIL)
	#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
	#define IS_OBJ(value)	  ((value).type == VAL_OBJ)

	#define AS_BOOL(value)    ((value).as.boolean)
	#define AS_NUMBER(value)  ((value).as.number)
	#define AS_OBJ(value)	  ((value).as.obj)
	#define AS_IP(value)	  ((value).as.ip)
#endif /* TAGGED_NAN */

static inline bool isObjType(Value v, ObjType t) {
	return IS_OBJ(v) && OBJ_TYPE(v) == t;
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
void defineNative(VM *vm, ObjTable *t, const NativeDef *f);
void defineNativeClass(VM *vm, ObjTable *t, ObjClass *def);
ObjModule *defineNativeModule(VM *vm, ModuleDef *def);
ObjArray *duplicateArray(VM *vm, ObjArray *source);
void setArray(VM *vm, ObjArray *array, int idx, Value v);	// It is the caller's responsability to ensure that v is findable by the GC.
bool getArray(ObjArray *array, int idx, Value *ret);
ObjString *takeString(char *chars, size_t length, VM *vm);
ObjString *copyString(const char *chars, size_t length, VM *vm);
void fprintObject(FILE *restrict stream, Value value);
void printObject(Value value);

#ifdef TAGGED_NAN
static inline bool valuesEqual(Value a, Value b) {
	if(IS_NUMBER(a) && IS_NUMBER(b))
		return AS_NUMBER(a) == AS_NUMBER(b);
	return a.u == b.u;
}
#else /* TAGGED_NAN */
bool valuesEqual(Value, Value);
#endif /* TAGGED_NAN */
void fprintValue(FILE *restrict stream, Value value);
void printValue(Value value);

#endif /* XAN_OBJECT_H */

#include "array.h"

#include <assert.h>

#include "exception.h"
#include "memory.h"

ObjArray *newArray(VM *vm, size_t count) {
	ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->count = 0;
	array->capacity = 0;
	array->values = NULL;
	array->klass = &arrayDef;
	if(count) {
		size_t capacity = round_up_pow_2(count);
		fwdWriteBarrier(vm, OBJ_VAL(array));
		array->values = GROW_ARRAY(array->values, Value, 0, capacity);
		array->count = count;
		array->capacity = capacity;
	}
	return array;
}

static bool ArrayInit(VM *vm, int argCount, Value *args) {
	if(argCount > 2) {
		ExceptionFormattedStr(vm, "Method 'init' of class 'array' expected 0 argument but got %d.", argCount);
		return false;
	}
	size_t count = argCount == 0 ? 0 : (size_t)AS_NUMBER(args[0]);
	Value v = argCount < 2 ? NIL_VAL : args[1];
	ObjArray *ret = newArray(vm, count);
	args[-1] = OBJ_VAL(ret);
	for(size_t i=0; i<ret->count; i++)
		ret->values[i] = v;
	return true;
}

static bool ArrayCount(VM *vm, int argCount, Value *args) {
	if(argCount > 0) {
		ExceptionFormattedStr(vm, "Method 'count' of class 'array' expected 0 argument but got %d.", argCount);
		return false;
	}
	assert(IS_ARRAY(args[-1]));
	args[-1] = NUMBER_VAL(AS_ARRAY(args[-1])->count);
	return true;
}

void writeValueArray(VM *vm, ObjArray *array, Value value) {
	if(array->capacity < array->count + 1) {
		size_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(array->values, Value, oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	array->count++;
}

NativeDef arrayMethods[] = {
	{"init", &ArrayInit},
	{"count", &ArrayCount},
	// {"__subscript", &ArrayInit},
	{NULL, NULL}
};

ObjClass arrayDef = {
	{
		OBJ_CLASS,
		false,
		NULL,
	},
	"Array",
	arrayMethods,
	NULL,
	NULL,
	false
};

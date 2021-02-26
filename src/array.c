#include "array.h"

#include <assert.h>

#include "class.h"
#include "exception.h"
#include "memory.h"

ObjArray *newArray(VM *vm, size_t count) {
	ObjArray *array = ALLOCATE_OBJ(vm, ObjArray, OBJ_ARRAY);
	array->count = 0;
	array->capacity = 0;
	array->values = NULL;
	array->fields = NULL;
	array->klass = &arrayDef;
	if(count) {
		size_t capacity = round_up_pow_2(count);
		vm->base[0] = OBJ_VAL(array);
		array->values = GROW_ARRAY(vm, array->values, Value, 0, capacity);
		array->count = count;
		array->capacity = capacity;
	}
	return array;
}

static bool ArrayNew(VM *vm, int argCount, Value *args) {
	if(argCount > 1) {
		ExceptionFormattedStr(vm, "Method 'new' of class 'array' expected 1 argument but got %d.", argCount);
		return false;
	}
	if(!IS_NUMBER(args[0])) {
		ExceptionFormattedStr(vm, "Method 'new' of class 'array' expects it's first argument to be a number.");
		return false;
	}
	newArray(vm, (size_t)AS_NUMBER(args[0]));
	return true;
}

static bool ArrayInit(VM *vm, int argCount, __attribute__((unused)) Value *args) {
	if(argCount > 2) {
		ExceptionFormattedStr(vm, "Method 'init' of class 'array' expected 2 argument but got %d.", argCount);
		return false;
	}
	size_t count = argCount == 0 ? 0 : (size_t)AS_NUMBER(vm->base[0]);
	Value v = argCount < 2 ? NIL_VAL : vm->base[1];
	incCFrame(vm, 1, argCount + 3);
	ObjArray *ret = newArray(vm, count);
	decCFrame(vm);
	vm->base[0] = OBJ_VAL(ret);
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
	args[0] = NUMBER_VAL(AS_ARRAY(args[-1])->count);
	return true;
}

static bool ArrayAppend(VM *vm, int argCount, Value *args) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, "Method 'append' of class 'array' expected 1 argument but got %d.", argCount);
		return false;
	}
	assert(IS_ARRAY(args[-1]));
	writeValueArray(vm, AS_ARRAY(args[-1]), args[0]);
	return true;
}

void writeValueArray(VM *vm, ObjArray *array, Value value) {
	if(array->capacity < array->count + 1) {
		size_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(vm, array->values, Value, oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	writeBarrier(vm, array);
	array->count++;
}

NativeDef arrayMethods[] = {
	{"init", &ArrayInit},
	{"new", &ArrayNew},
	{"append", &ArrayAppend},
	{"count", &ArrayCount},
	// {"__subscript", &ArrayInit},
	{NULL, NULL}
};

ObjClass arrayDef = {
	CLASS_HEADER,
	"Array",
	arrayMethods,
	RUNTIME_CLASSDEF_FIELDS,
	false
};

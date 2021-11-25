#include "array.h"

#include <assert.h>

#include "class.h"
#include "exception.h"
#include "memory.h"

ObjArray *newArray(VM *vm, thread *currentThread, size_t count) {
	ObjArray *array = ALLOCATE_OBJ(vm, ObjArray, OBJ_ARRAY);
	array->count = 0;
	array->capacity = 0;
	array->values = NULL;
	array->fields = NULL;
	array->klass = &arrayDef;
	if(count) {
		size_t capacity = round_up_pow_2(count);
		currentThread->base[0] = OBJ_VAL(array);
		array->values = GROW_ARRAY(vm, array->values, Value, 0, capacity);
		array->count = count;
		array->capacity = capacity;
	}
	return array;
}

static bool ArrayNew(VM *vm, thread *currentThread, int argCount) {
	if(argCount > 1) {
		ExceptionFormattedStr(vm, currentThread, "Method 'new' of class 'array' expected 1 argument but got %d.", argCount);
		return false;
	}
	if(!IS_NUMBER(currentThread->base[0])) {
		ExceptionFormattedStr(vm, currentThread, "Method 'new' of class 'array' expects it's first argument to be a number.");
		return false;
	}
	newArray(vm, currentThread, (size_t)AS_NUMBER(currentThread->base[0]));
	return true;
}

static bool ArrayInit(VM *vm, thread *currentThread, int argCount) {
	if(argCount > 2) {
		ExceptionFormattedStr(vm, currentThread, "Method 'init' of class 'array' expected 2 argument but got %d.", argCount);
		return false;
	}
	size_t count = argCount == 0 ? 0 : (size_t)AS_NUMBER(currentThread->base[0]);
	Value v = argCount < 2 ? NIL_VAL : currentThread->base[1];
	incCFrame(vm, currentThread, 1, argCount + 3);
	ObjArray *ret = newArray(vm, currentThread, count);
	decCFrame(currentThread);
	currentThread->base[0] = OBJ_VAL(ret);
	for(size_t i=0; i<ret->count; i++)
		ret->values[i] = v;
	return true;
}

static bool ArrayCount(VM *vm, thread *currentThread, int argCount) {
	if(argCount > 0) {
		ExceptionFormattedStr(vm, currentThread, "Method 'count' of class 'array' expected 0 argument but got %d.", argCount);
		return false;
	}
	assert(IS_ARRAY(currentThread->base[-1]));
	currentThread->base[0] = NUMBER_VAL(AS_ARRAY(currentThread->base[-1])->count);
	return true;
}

static bool ArrayAppend(VM *vm, thread *currentThread, int argCount) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, currentThread, "Method 'append' of class 'array' expected 1 argument but got %d.", argCount);
		return false;
	}
	assert(IS_ARRAY(currentThread->base[-1]));
	writeValueArray(vm, AS_ARRAY(currentThread->base[-1]), currentThread->base[0]);
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
	// {"__subscript", &},
	{NULL, NULL}
};

ObjClass arrayDef = {
	CLASS_HEADER,
	"Array",
	arrayMethods,
	RUNTIME_CLASSDEF_FIELDS,
	false
};

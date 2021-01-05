#include "array.h"

#include <assert.h>

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

Value ArrayInit(VM *vm, int argCount, Value *args) {
	size_t count = argCount == 0 ? 0 : (size_t)AS_NUMBER(args[0]);
	Value v = argCount < 2 ? NIL_VAL : args[1];
	ObjArray *ret = newArray(vm, count);
	for(size_t i=0; i<ret->count; i++)
		ret->values[i] = v;
	return OBJ_VAL(ret);
}

Value ArrayCount(VM *vm, int argCount, Value *args) {
	assert(IS_ARRAY(*args));
	return NUMBER_VAL(AS_ARRAY(*args)->count);
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

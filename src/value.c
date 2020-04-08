#include "value.h"

#include "memory.h"
#include "object.h"

bool valuesEqual(Value a, Value b) {
	if(a.type != b.type) return false;

	switch(a.type) {
		case VAL_BOOL:		return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL:		return true;
		case VAL_NUMBER:	return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ:		return AS_OBJ(a) == AS_OBJ(b);
	}
	return false;
}

void initValueArray(ValueArray *array) {
	array->values = NULL;
	array->capacity = 0;
	array->count = 0;
}

void writeValueArray(ValueArray *array, Value value) {
	if(array->capacity < array->count + 1) {
		size_t oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(array->values, Value, oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	array->count++;
}

void freeValueArray(ValueArray *array) {
	FREE_ARRAY(Value, array->values, array->capacity);
	initValueArray(array);
}

void fprintValue(FILE *restrict stream, Value value) {
	switch(value.type) {
		case VAL_BOOL:
			fprintf(stream, AS_BOOL(value) ? "true" : "false");
			break;
		case VAL_NIL:
			fprintf(stream, "nil");
			break;
		case VAL_NUMBER:
			fprintf(stream, "%g", AS_NUMBER(value));
			break;
		case VAL_OBJ:
			fprintObject(stream, value);
			break;
	}
}

void printValue(Value value) {
	fprintValue(stdout, value);
}

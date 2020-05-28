#ifndef XAN_VALUE_H
#define XAN_VALUE_H

#include "type.h"

#include <stdio.h>

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)

#define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
#define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
#define OBJ_VAL(object)   ((Value){ VAL_OBJ, { .obj = (Obj*)object } })

bool valuesEqual(Value, Value);
void initValueArray(ValueArray *array);
void writeValueArray(VM *vm, ValueArray *array, Value value);
void freeValueArray(VM *vm, ValueArray *array);
void fprintValue(FILE *restrict stream, Value value);
void printValue(Value value);

#endif /* XAN_VALUE_H */

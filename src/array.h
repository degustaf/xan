#ifndef XAN_ARRAY_H
#define XAN_ARRAY_H

#include "type.h"

void writeValueArray(VM *vm, ObjArray *array, Value value);

extern ObjClass arrayDef;

#endif /* XAN_ARRAY_H */

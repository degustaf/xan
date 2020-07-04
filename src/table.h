#ifndef XAN_TABLE_H
#define XAN_TABLE_H

#include "type.h"

#include <stdio.h>

ObjTable *newTable(VM *vm, size_t count);
bool tableGet(ObjTable *t, ObjString *key, Value *value);
bool tableSet(VM *vm, ObjTable *t, ObjString *key, Value value);
bool tableDelete(ObjTable *t, ObjString *key);
void tableAddAll(VM *vm, ObjTable *from, ObjTable *to);
ObjString *tableFindString(ObjTable *t, const char *chars, size_t length, uint32_t hash);
void fprintTable(FILE *restrict stream, ObjTable *t);
ObjTable *duplicateTable(VM *vm, ObjTable *source);
void markTable(VM *vm, ObjTable *t);
void tableRemoveWhite(ObjTable *t);
void freeTable(VM *vm, ObjTable *t);
size_t count(ObjTable *t);

extern classDef tableDef;

#endif /* XAN_TABLE_H */

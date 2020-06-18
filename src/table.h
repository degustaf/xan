#ifndef XAN_TABLE_H
#define XAN_TABLE_H

#include "value.h"

void initTable(Table *t);
void freeTable(VM *vm, Table *t);
bool tableGet(Table *t, ObjString *key, Value *value);
bool tableSet(VM *vm, Table *t, ObjString *key, Value value);
bool tableDelete(Table *t, ObjString *key);
void tableAddAll(VM *vm, Table *from, Table *to);
ObjString *tableFindString(Table *t, const char *chars, size_t length, uint32_t hash);

#endif /* XAN_TABLE_H */

#ifndef XAN_TABLE_H
#define XAN_TABLE_H

#include "type.h"

bool tableGet(ObjTable *t, ObjString *key, Value *value);
bool tableSet(VM *vm, ObjTable *t, ObjString *key, Value value);
bool tableDelete(ObjTable *t, ObjString *key);
void tableAddAll(VM *vm, ObjTable *from, ObjTable *to);
ObjString *tableFindString(ObjTable *t, const char *chars, size_t length, uint32_t hash);

#endif /* XAN_TABLE_H */

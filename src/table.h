#ifndef XAN_TABLE_H
#define XAN_TABLE_H

#include "object.h"

typedef struct {
	ObjString *key;
	Value value;
} Entry;

typedef struct {
	size_t count;
	size_t capacity;
	Entry *entries;
} Table;

void initTable(Table *t);
void freeTable(VM *vm, Table *t);
bool tableGet(Table *t, ObjString *key, Value *value);
bool tableSet(VM *vm, Table *t, ObjString *key, Value value);
bool tableDelete(Table *t, ObjString *key);
void tableAddAll(VM *vm, Table *from, Table *to);
void tableRemoveWhite(Table *t);
ObjString *tableFindString(Table *t, const char *chars, size_t length, uint32_t hash);

#endif /* XAN_TABLE_H */

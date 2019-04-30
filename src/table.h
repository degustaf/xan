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
void freeTable(Table *t);
bool tableGet(Table *t, ObjString *key, Value *value);
bool tableSet(Table *t, ObjString *key, Value value);
bool tableDelete(Table *t, ObjString *key);
void tableAddAll(Table *from, Table *to);
ObjString *tableFindString(Table *t, const char *chars, size_t length, uint32_t hash);

#endif /* XAN_TABLE_H */

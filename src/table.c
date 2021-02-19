#include "table.h"

#include <assert.h>
#include <string.h>

#include "class.h"
#include "exception.h"
#include "memory.h"
#include "object.h"

/**********************************************************************************
  TODO If the hash table becomes a bottleneck, the algorithm should be changed to
  a Robin Hood hash.
  http://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/
 **********************************************************************************/

#define TABLE_MAX_LOAD 0.75

struct sObjTable {
	INSTANCE_FIELDS;
	size_t count;
	size_t capacityMask;
	Value *entries;
};

#define KEY(e) e[0]
#define VALUE(e) e[1]

static uint32_t hash(Value v) {
	assert(IS_STRING(v) || IS_NUMBER(v));
	if(IS_STRING(v))
		return AS_STRING(v)->hash;
	assert(IS_NUMBER(v));
	if((double)(int64_t)AS_NUMBER(v) == AS_NUMBER(v))
		return (uint32_t)(int64_t)AS_NUMBER(v);
	union {double x; uint64_t i;}temp;
	temp.x = AS_NUMBER(v);
	return (temp.i & 0xffffffff) ^ (temp.i >> 32);
}

bool TableInit (VM *vm, int argCount, __attribute__((unused)) Value *args) {
	assert((argCount & 1) == 0);

	incFrame(vm, 1, vm->base + argCount + 1, NULL);
	ObjTable *t = newTable(vm, argCount);
	decFrame(vm);
	vm->base[-3] = OBJ_VAL(t);

	for(int i = 0; i<argCount; i+=2) {
		assert(IS_STRING(vm->base[i]));
		tableSet(vm, t, vm->base[i], vm->base[i+1]);
	}

	return true;
}

static Value* findEntry(Value *entries, size_t capacityMask, Value key) {
	assert(IS_STRING(key) || IS_NUMBER(key));
	uint32_t index = hash(key) & (capacityMask - 1);	// Even, i.e. key
	Value *tombstone = NULL;

	while(true) {
		Value *e = &entries[index];

		if(IS_NIL(*e)) {
			if(IS_NIL(VALUE(e))) {
				// Empty entry.
				return tombstone ? tombstone : e;
			} else {
				// We found a tombstone.
				if(tombstone == NULL)
					tombstone = e;
			}
		} else if(valuesEqual(key, KEY(e))) {
			// We found the key.
			return e;
		}

		index = (index+2) & capacityMask;
	}
}

bool tableGet(ObjTable *t, Value key, Value *ret) {
	if(t->entries == NULL)
		return false;

	Value *e = findEntry(t->entries, t->capacityMask, key);
	if(IS_NIL(*e))
		return false;

	*ret = VALUE(e);
	return true;
}

static void adjustCapacity(VM *vm, ObjTable *t, size_t capacityMask) {
	Value *entries = ALLOCATE(vm, Value, capacityMask + 1);
	for(size_t i=0; i<=capacityMask; i++)
		entries[i] = NIL_VAL;

	t->count = 0;
	for(size_t i=1; i<=t->capacityMask; i+=2) {
		Value *e = &t->entries[i-1];
		if(IS_NIL(*e))
			continue;
		Value *dest = findEntry(entries, capacityMask, KEY(e));
		*dest = KEY(e);
		VALUE(dest) = VALUE(e);
		t->count++;
	}

	if(t->capacityMask)
		FREE_ARRAY(&vm->gc, Value, t->entries, t->capacityMask + 1);
	t->entries = entries;
	t->capacityMask = capacityMask;
}

ObjTable *newTable(VM *vm, size_t count) {
	ObjTable *t = ALLOCATE_OBJ(vm, ObjTable, OBJ_TABLE);
	t->count = 0;
	t->capacityMask = 0;
	t->entries = NULL;
	t->klass = NULL;
	t->fields = NULL;
	if(count) {
		size_t capacityMask = round_up_pow_2(2 * count) - 1;
		vm->base[0] = OBJ_VAL(t);
		adjustCapacity(vm, t, capacityMask);
	}

	return t;
}

static bool TableNew(VM *vm, int argCount, Value *args) {
	if(argCount > 1) {
		ExceptionFormattedStr(vm, "Method 'new' of class 'table' expected 1 argument but got %d.", argCount);
		return false;
	}
	if(!IS_NUMBER(args[0])) {
		ExceptionFormattedStr(vm, "Method 'new' of class 'table' expects it's first argument to be a number.");
		return false;
	}

	incFrame(vm, 1, vm->base + argCount + 1, NULL);
	args[-3] = OBJ_VAL(newTable(vm, (size_t)(AS_NUMBER(args[0]))));
	decFrame(vm);
	return true;
}

bool tableSet(VM *vm, ObjTable *t, Value key, Value value) {
	if(t->count + 1 > (t->capacityMask / 2) * TABLE_MAX_LOAD) {
		size_t capacity = GROW_CAPACITY(t->capacityMask + 1);
		assert(capacity >= 1);
		adjustCapacity(vm, t, capacity-1);
	}

	Value *e = findEntry(t->entries, t->capacityMask, key);

	bool isNewKey = IS_NIL(*e);
	if(isNewKey && IS_NIL(VALUE(e)))
		t->count++;

	writeBarrier(vm, t);
	KEY(e) = key;
	VALUE(e) = value;
	return isNewKey;
}

bool tableDelete(ObjTable *t, Value key) {
	if(t->count == 0)
		return false;

	// Find the entry.
	Value *e = findEntry(t->entries, t->capacityMask, key);
	if(IS_NIL(*e))
		return false;

	// Place a tombstone in the entry.
	*e = NIL_VAL;
	VALUE(e) = BOOL_VAL(true);

	return true;
}

void tableAddAll(VM *vm, ObjTable *from, ObjTable *to) {
	for(size_t i=1; i<=from->capacityMask; i+=2) {
		Value *e = &from->entries[i-1];
		if(!IS_NIL(*e))
			tableSet(vm, to, KEY(e), VALUE(e));
	}
}

ObjString *tableFindString(ObjTable *t, const char *chars, size_t length, uint32_t hash) {
	if(t->entries == NULL)
		return NULL;

	uint32_t index = hash & (t->capacityMask - 1);	// Even, i.e. key

	while(true) {
		Value *e = &t->entries[index];
		if(IS_NIL(*e) ) {
			if(IS_NIL(VALUE(e)))
				return NULL;
		} else {
			assert(IS_STRING(*e));
			if(AS_STRING(KEY(e))->length == length && AS_STRING(KEY(e))->hash == hash
					&& memcmp(AS_STRING(KEY(e))->chars, chars, length) == 0)
				return AS_STRING(KEY(e));
		}

		index = (index+2) & t->capacityMask;
	}
}

void fprintTable(FILE *restrict stream, ObjTable *t) {
	if(t->count == 0) {
		fprintf(stream, "{}");
		return;
	}

	fprintf(stream, "{");
	size_t i = 0;
	while(IS_NIL(t->entries[i])) i+=2;
	fprintValue(stream, t->entries[i]);
	fprintf(stream, ": ");
	fprintValue(stream, t->entries[i+1]);
	for(i+=2; i <= t->capacityMask; i+=2) {
		if(IS_NIL(t->entries[i]))
			continue;
		fprintf(stream, ", ");
		fprintValue(stream, t->entries[i]);
		fprintf(stream, ": ");
		fprintValue(stream, t->entries[i+1]);
	}
	fprintf(stream, "}");
}

ObjTable *duplicateTable(VM *vm, ObjTable *source) {
	ObjTable *dest = newTable(vm, (source->capacityMask+1)/2);
	tableAddAll(vm, source, dest);
	return dest;
}

size_t count(ObjTable *t) {
	return t->count;
}

void markTable(GarbageCollector *gc, ObjTable *t) {
	for(size_t i=1; i<=t->capacityMask; i+=2) {
		Value *e = &t->entries[i-1];
		markValue(gc, KEY(e));
		markValue(gc, VALUE(e));
	}
}

void tableRemoveWhite(ObjTable *t) {
	for(size_t i=1; i<=t->capacityMask; i+=2) {
		Value *e = &t->entries[i-1];
		if(!IS_NIL(*e) && isWhite(AS_OBJ(*e)))
			tableDelete(t, KEY(e));
	}
}

void freeTable(GarbageCollector *gc, ObjTable *t) {
	if(t->capacityMask)
		FREE_ARRAY(gc, Value, t->entries, t->capacityMask+1);
	FREE(gc, ObjTable, t);
}

NativeDef tableMethods[] = {
	{"init", &TableInit},
	{"new", &TableNew},
	{NULL, NULL}
};

ObjClass tableDef = {
	CLASS_HEADER,
	"Table",
	tableMethods,
	RUNTIME_CLASSDEF_FIELDS,
	false
};

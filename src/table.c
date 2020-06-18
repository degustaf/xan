#include "table.h"

#include <string.h>

#include "memory.h"

/**********************************************************************************
  TODO If the hash table becomes a bottleneck, the algorithm should be changed to
  a Robin Hood hash.
  http://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/
 **********************************************************************************/

#define TABLE_MAX_LOAD 0.75

void initTable(Table *t) {
	t->count = 0;
	t->capacityMask = -1;
	t->entries = NULL;
}

void freeTable(VM *vm, Table *t) {
	FREE_ARRAY(Entry, t->entries, t->capacityMask + 1);
	initTable(t);
}

static Entry* findEntry(Entry *entries, ssize_t capacityMask, ObjString *key) {
	uint32_t index = key->hash & capacityMask;
	Entry *tombstone = NULL;

	while(true) {
		Entry *e = &entries[index];

		if(e->key == NULL) {
			if(IS_NIL(e->value)) {
				// Empty entry.
				return tombstone ? tombstone : e;
			} else {
				// We found a tombstone.
				if(tombstone == NULL)
					tombstone = e;
			}
		} else if(e->key == key) {
			// We found the key.
			return e;
		}

		index = (index+1) & capacityMask;
	}
}

bool tableGet(Table *t, ObjString *key, Value *value) {
	if(t->entries == NULL)
		return false;

	Entry *e = findEntry(t->entries, t->capacityMask, key);
	if(e->key == NULL)
		return false;

	*value = e->value;
	return true;
}

static void adjustCapacity(VM *vm, Table *t, ssize_t capacityMask) {
	Entry *entries = ALLOCATE(Entry, capacityMask + 1);
	for(ssize_t i=0; i<=capacityMask; i++) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}

	t->count = 0;
	for(ssize_t i=0; i<=t->capacityMask; i++) {
		Entry *e = &t->entries[i];
		if(e->key == NULL)
			continue;
		Entry *dest = findEntry(entries, capacityMask, e->key);
		dest->key = e->key;
		dest->value = e->value;
		t->count++;
	}

	FREE_ARRAY(Entry, t->entries, t->capacityMask);
	t->entries = entries;
	t->capacityMask = capacityMask;
}

bool tableSet(VM *vm, Table *t, ObjString *key, Value value) {
	if(t->count + 1 > (t->capacityMask + 1) * TABLE_MAX_LOAD) {
		ssize_t capacity = GROW_CAPACITY(t->capacityMask + 1);
		adjustCapacity(vm, t, capacity-1);
	}

	Entry *e = findEntry(t->entries, t->capacityMask, key);

	bool isNewKey = e->key == NULL;
	if(isNewKey && IS_NIL(e->value))
		t->count++;

	e->key = key;
	e->value = value;
	return isNewKey;
}

bool tableDelete(Table *t, ObjString *key) {
	if(t->count == 0)
		return false;

	// Find the entry.
	Entry *e = findEntry(t->entries, t->capacityMask, key);
	if(e->key == NULL)
		return false;

	// Place a tombstone in the entry.
	e->key = NULL;
	e->value = BOOL_VAL(true);

	return true;
}

void tableAddAll(VM *vm, Table *from, Table *to) {
	for(ssize_t i=0; i<=from->capacityMask; i++) {
		Entry *e = &from->entries[i];
		if(e->key)
			tableSet(vm, to, e->key, e->value);
	}
}

ObjString *tableFindString(Table *t, const char *chars, size_t length, uint32_t hash) {
	if(t->entries == NULL)
		return NULL;

	uint32_t index = hash & t->capacityMask;

	while(true) {
		Entry *e = &t->entries[index];
		if(e->key == NULL) {
			if(IS_NIL(e->value))
				return NULL;
		} else if(e->key->length == length && e->key->hash == hash
				&& memcmp(e->key->chars, chars, length) == 0) {
			return e->key;
		}

		index = (index+1) & t->capacityMask;
	}
}

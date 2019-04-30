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
	t->capacity = 0;
	t->entries = NULL;
}

void freeTable(Table *t) {
	FREE_ARRAY(Entry, t->entries, t->capacity);
	initTable(t);
}

static Entry* findEntry(Entry *entries, size_t capacity, ObjString *key) {
	uint32_t index = key->hash % capacity;
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

		index = (index+1) % capacity;
	}
}

bool tableGet(Table *t, ObjString *key, Value *value) {
	if(t->entries == NULL)
		return false;

	Entry *e = findEntry(t->entries, t->capacity, key);
	if(e->key == NULL)
		return false;

	*value = e->value;
	return true;
}

static void adjustCapacity(Table *t, size_t capacity) {
	Entry *entries = ALLOCATE(Entry, capacity);
	for(size_t i=0; i<capacity; i++) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}

	t->count = 0;
	for(size_t i=0; i<t->capacity; i++) {
		Entry *e = &t->entries[i];
		if(e->key == NULL)
			continue;
		Entry *dest = findEntry(entries, capacity, e->key);
		dest->key = e->key;
		dest->value = e->value;
		t->count++;
	}

	FREE_ARRAY(Entry, t->entries, t->capacity);
	t->entries = entries;
	t->capacity = capacity;
}

bool tableSet(Table *t, ObjString *key, Value value) {
	if(t->count + 1 > t->capacity * TABLE_MAX_LOAD) {
		size_t capacity = GROW_CAPACITY(t->capacity);
		adjustCapacity(t, capacity);
	}

	Entry *e = findEntry(t->entries, t->capacity, key);

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
	Entry *e = findEntry(t->entries, t->capacity, key);
	if(e->key == NULL)
		return false;

	// Place a tombstone in the entry.
	e->key = NULL;
	e->value = BOOL_VAL(true);

	return true;
}

void tableAddAll(Table *from, Table *to) {
	for(size_t i=0; i<from->capacity; i++) {
		Entry *e = &from->entries[i];
		if(e->key)
			tableSet(to, e->key, e->value);
	}
}

ObjString *tableFindString(Table *t, const char *chars, size_t length, uint32_t hash) {
	if(t->entries == NULL)
		return NULL;

	uint32_t index = hash % t->capacity;

	while(true) {
		Entry *e = &t->entries[index];
		if(e->key == NULL) {
			if(IS_NIL(e->value))
				return NULL;
		} else if(e->key->length == length && e->key->hash == hash
				&& memcmp(e->key->chars, chars, length) == 0) {
			return e->key;
		}

		index = (index+1) % t->capacity;
	}
}

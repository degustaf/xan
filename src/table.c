#include "table.h"

#include <assert.h>
#include <string.h>

#include "memory.h"
#include "object.h"

/**********************************************************************************
  TODO If the hash table becomes a bottleneck, the algorithm should be changed to
  a Robin Hood hash.
  http://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/
 **********************************************************************************/

#define TABLE_MAX_LOAD 0.75

static Value* findEntry(Value *entries, ssize_t capacityMask, ObjString *key) {
	uint32_t index = key->hash & (capacityMask - 1);	// Even, i.e. key
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
		} else if(KEY(e) == key) {
			// We found the key.
			return e;
		}

		index = (index+2) & capacityMask;
	}
}

bool tableGet(ObjTable *t, ObjString *key, Value *value) {
	if(t->entries == NULL)
		return false;

	Value *e = findEntry(t->entries, t->capacityMask, key);
	if(IS_NIL(*e))
		return false;

	*value = VALUE(e);
	return true;
}

static void adjustCapacity(VM *vm, ObjTable *t, ssize_t capacityMask) {
	Value *entries = ALLOCATE(Value, capacityMask + 1);
	for(ssize_t i=0; i<=capacityMask; i++)
		entries[i] = NIL_VAL;

	t->count = 0;
	for(ssize_t i=0; i<=t->capacityMask; i+=2) {
		Value *e = &t->entries[i];
		if(IS_NIL(*e ))
			continue;
		Value *dest = findEntry(entries, capacityMask, KEY(e));
		*dest = OBJ_VAL(KEY(e));
		VALUE(dest) = VALUE(e);
		t->count++;
	}

	FREE_ARRAY(Value, t->entries, t->capacityMask);
	t->entries = entries;
	t->capacityMask = capacityMask;
}

bool tableSet(VM *vm, ObjTable *t, ObjString *key, Value value) {
	if(t->count + 1 > (t->capacityMask / 2) * TABLE_MAX_LOAD) {
		ssize_t capacity = GROW_CAPACITY(t->capacityMask + 1);
		adjustCapacity(vm, t, capacity-1);
	}

	Value *e = findEntry(t->entries, t->capacityMask, key);

	bool isNewKey = IS_NIL(*e);
	if(isNewKey && IS_NIL(VALUE(e)))
		t->count++;

	*e = OBJ_VAL(key);
	VALUE(e) = value;
	return isNewKey;
}

bool tableDelete(ObjTable *t, ObjString *key) {
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
	for(ssize_t i=0; i<=from->capacityMask; i+=2) {
		Value *e = &from->entries[i];
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
			if(KEY(e)->length == length && KEY(e)->hash == hash
					&& memcmp(KEY(e)->chars, chars, length) == 0)
				return KEY(e);
		}

		index = (index+2) & t->capacityMask;
	}
}

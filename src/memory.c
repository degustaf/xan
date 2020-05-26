#include "memory.h"

#include <stdlib.h>

#include "object.h"

void* reallocate(void* previous, size_t oldSize, size_t newSize) {
	if(newSize == 0) {
		free(previous);
		return NULL;
	}

	return realloc(previous, newSize);
}

static void freeObject(Obj *object) {
	switch(object->type) {
		case OBJ_FUNCTION: {
			ObjFunction *f = (ObjFunction*)object;
			freeChunk(&f->chunk);
			FREE(ObjFunction, object);
			break;
		}
		case OBJ_CLOSURE: {
			ObjClosure *cl = (ObjClosure*)object;
			FREE_ARRAY(ObjUpvalue*, cl->upvalues, cl->uvCount);
			FREE(ObjClosure, object);
			break;
		}
		case OBJ_NATIVE: {
			FREE(ObjNative, object);
			break;
		}
		case OBJ_STRING: {
			ObjString *string = (ObjString*)object;
			FREE_ARRAY(char, string->chars, string->length + 1);
			FREE(ObjString, object);
			break;
		}
		case OBJ_UPVALUE:
			FREE(ObjUpvalue, object);
			break;
	}
}

void freeObjects(VM *vm) {
	Obj *object = vm->objects;
	while(object) {
		Obj *next = object->next;
		freeObject(object);
		object = next;
	}
}

#include "memory.h"

#include <stdlib.h>

#include "object.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif /* DEBUG_LOG_GC */

void* reallocate(VM *vm, void* previous, size_t oldSize, size_t newSize) {
	vm->bytesAllocated += newSize - oldSize;

	if(newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
		collectGarbage(vm);
#endif /* DEBUG_STRESS_GC */
		if(vm->bytesAllocated > vm->nextGC) {
			collectGarbage(vm);
		}
	}

	if(newSize == 0) {
		free(previous);
		return NULL;
	}

	return realloc(previous, newSize);
}

static void markObject(VM *vm, Obj *o) {
	if(o == NULL)
		return;
	if(o->isMarked)
		return;
#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)o);
	printValue(OBJ_VAL(o));
	printf("\n");
#endif /* DEBUG_LOG_GC */
	o->isMarked = true;
	if(vm->grayCapacity < vm->grayCount+1) {
		vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
		vm->grayStack = realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
	}
	vm->grayStack[vm->grayCount++] = o;
}

static void markValue(VM *vm, Value v) {
	if(!IS_OBJ(v))
		return;
	markObject(vm, AS_OBJ(v));
}

static void markArray(VM *vm, ValueArray *array) {
	for(size_t i=0; i<array->count; i++)
		markValue(vm, array->values[i]);
}

static void markTable(VM *vm, Table *t) {
	for(size_t i=0; i<t->capacity; i++) {
		Entry *e = &t->entries[i];
		markObject(vm, (Obj*)e->key);
		markValue(vm, e->value);
	}
}

static void markCompilerRoots(VM *vm) {
	for(Compiler *c = vm->currentCompiler; c != NULL; c = c->enclosing) {
		markObject(vm, (Obj*)c->name);
		markArray(vm, &c->chunk.constants);
	}
}

static void markRoots(VM *vm) {
	for(Value *slot = vm->stack; slot < vm->stackTop; slot++) {
		markValue(vm, *slot);
	}
	markValue(vm, vm->temp4GC);
	markObject(vm, (Obj*)vm->initString);

	for(size_t i=0; i<vm->frameCount; i++)
		markObject(vm, (Obj*)vm->frames[i].c);

	for(ObjUpvalue *u = vm->openUpvalues; u != NULL; u = u->next)
		markObject(vm, (Obj*)u);

	markTable(vm, &vm->globals);
	markCompilerRoots(vm);
}

static void blackenObject(VM *vm, Obj *o) {
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)o);
	printValue(OBJ_VAL(o));
	printf("\n");
#endif /* DEBUG_LOG_GC */
	switch(o->type) {
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *bound = (ObjBoundMethod*)o;
			markValue(vm, bound->receiver);
			markObject(vm, (Obj*)bound->method);
			break;
		}
		case OBJ_CLASS: {
			ObjClass *klass = (ObjClass*)o;
			markObject(vm, (Obj*)klass->name);
			markTable(vm, &klass->methods);
			break;
		}
		case OBJ_CLOSURE: {
			ObjClosure *cl = (ObjClosure*)o;
			markObject(vm, (Obj*)cl->f);
			for(size_t i=0; i<cl->uvCount; i++)
				markObject(vm, (Obj*)cl->upvalues[i]);
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction *f = (ObjFunction*)o;
			markObject(vm, (Obj*)f->name);
			markArray(vm, &f->chunk.constants);
			break;
		}
		case OBJ_INSTANCE: {
			ObjInstance *instance = (ObjInstance*)o;
			markObject(vm, (Obj*)instance->klass);
			markTable(vm, &instance->fields);
			break;
		}
		case OBJ_UPVALUE:
			markValue(vm, ((ObjUpvalue*)o)->closed);
			break;
		case OBJ_NATIVE:
		case OBJ_STRING:
			break;
	}
}

static void traceReferences(VM *vm) {
	while(vm->grayCount > 0) {
		Obj *o = vm->grayStack[--vm->grayCount];
		blackenObject(vm, o);
	}
}

static void freeObject(VM *vm, Obj *object) {
#ifdef DEBUG_LOG_GC
	printf("%p free type %s\n", (void*)object, ObjTypeNames[object->type]);
#endif /* DEBUG_LOG_GC */
	switch(object->type) {
		case OBJ_BOUND_METHOD:
			FREE(ObjBoundMethod, object);
			break;
		case OBJ_FUNCTION: {
			ObjFunction *f = (ObjFunction*)object;
			freeChunk(vm, &f->chunk);
			FREE(ObjFunction, object);
			break;
		}
		case OBJ_CLOSURE: {
			ObjClosure *cl = (ObjClosure*)object;
			FREE_ARRAY(ObjUpvalue*, cl->upvalues, cl->uvCount);
			FREE(ObjClosure, object);
			break;
		}
		case OBJ_STRING: {
			ObjString *string = (ObjString*)object;
			FREE_ARRAY(char, string->chars, string->length + 1);
			FREE(ObjString, object);
			break;
		}
		case OBJ_INSTANCE: {
			ObjInstance *instance = (ObjInstance*)object;
			freeTable(vm, &instance->fields);
			FREE(ObjInstance, object);
			break;
		}
		case OBJ_CLASS: {
			ObjClass *klass = (ObjClass*)object;
			freeTable(vm, &klass->methods);
			FREE(ObjClass, object);
			break;
		}
		case OBJ_NATIVE:
			FREE(ObjNative, object);
			break;
		case OBJ_UPVALUE:
			FREE(ObjUpvalue, object);
			break;
	}
}

static void sweep(VM *vm) {
	Obj **o = &vm->objects;
	while(*o) {
		if((*o)->isMarked) {
			(*o)->isMarked = false;
			o = &(*o)->next;
		} else {
			Obj *unreached = *o;
			*o = (*o)->next;
			freeObject(vm, unreached);
		}
	}
}

void collectGarbage(VM *vm) {
#ifdef DEBUG_LOG_GC
	printf("-- gc begin\n");
	size_t before = vm->bytesAllocated;
#endif /* DEBUG_LOG_GC */

	markRoots(vm);
	traceReferences(vm);
	tableRemoveWhite(&vm->strings);
	sweep(vm);
	vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
	printf("-- gc end\n");
	printf("   collected %ld bytes (from %ld to %ld next at %ld\n",
			before - vm->bytesAllocated, before, vm->bytesAllocated, vm->nextGC);
#endif /* DEBUG_LOG_GC */
}

void freeObjects(VM *vm) {
	Obj *object = vm->objects;
	while(object) {
		Obj *next = object->next;
		freeObject(vm, object);
		object = next;
	}
	free(vm->grayStack);
}

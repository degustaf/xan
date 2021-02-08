#include "memory.h"

#include <assert.h>
#include <stdlib.h>

#include "object.h"
#include "table.h"
#include "exception.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif /* DEBUG_LOG_GC */

Obj* allocateObject(size_t size, ObjType type, VM *vm) {
	Obj *object = (Obj*)reallocate(vm, NULL, 0, size);
	object->type = type;
	object->isMarked = false;
	object->next = vm->objects;
	vm->objects = object;
#ifdef DEBUG_LOG_GC
	printf("%p allocate %ld for %s\n", (void*)object, size, ObjTypeNames[type]);
#endif /* DEBUG_LOG_GC */
	return object;
}

static void markObject(VM *vm, Obj *o) {
	if(o == NULL)
		return;
	if(o->isMarked)
		return;
#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)o);
	if((o->type == OBJ_CLASS) && ((ObjClass*)o)->name == NULL) {
		// During startup class->name might be NULL. No need to pollute
		// printValue with a check, when it only matters if debugging the GC.
		printf("<Class %s>", ((ObjClass*)o)->cname);
	} else {
		printValue(OBJ_VAL(o));
	}
	printf("\n");
#endif /* DEBUG_LOG_GC */
	o->isMarked = true;
	if(vm->grayCapacity < vm->grayCount+1) {
		vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
		vm->grayStack = realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
	}
	vm->grayStack[vm->grayCount++] = o;
}

void markValue(VM *vm, Value v) {
	if(!IS_OBJ(v))
		return;
	markObject(vm, AS_OBJ(v));
}

static void markArray(VM *vm, ObjArray *array) {
	array->obj.isMarked = true;
	for(size_t i=0; i<array->count; i++)
		markValue(vm, array->values[i]);
}

static void markCompilerRoots(VM *vm) {
	for(Compiler *c = vm->currentCompiler; c != NULL; c = c->enclosing) {
		markObject(vm, (Obj*)c->name);
		markObject(vm, (Obj*)c->chunk.constants);
		markObject(vm, (Obj*)c->chunk.constantIndices);
	}
	for(ClassCompiler *c = vm->currentClassCompiler; c != NULL; c = c->enclosing)
		markObject(vm, (Obj*)c->methods);
}

static void markRoots(VM *vm) {
#ifdef DEBUG_LOG_GC
	printf("stackTop = %ld (%p)\n", vm->stackTop - vm->stack, (void*)vm->stackTop);
#endif
	for(Value *slot = vm->stack; slot < vm->stackTop; slot++) {
#ifdef DEBUG_LOG_GC
		printf("stack[%ld] (%p) is ", slot - vm->stack, (void*)slot);
		if(IS_CLASS(*slot) && AS_CLASS(*slot)->name == NULL) {
			// During startup class->name might be NULL. No need to pollute
			// printValue with a check, when it only matters if debugging the GC.
			printf("<Class %s>", (AS_CLASS(*slot))->cname);
		} else {
			printValue(*slot);
		}
		printf("\n");
#endif
		markValue(vm, *slot);
	}
	markObject(vm, (Obj*)vm->initString);
	markValue(vm, vm->exception);

	for(size_t i=0; i<vm->frameCount; i++)
		markObject(vm, (Obj*)vm->frames[i].c);

	for(ObjUpvalue *u = vm->openUpvalues; u != NULL; u = u->next)
		markObject(vm, (Obj*)u);

	markObject(vm, (Obj*)vm->globals);
	// We don't want to mark the entries, so we'll manually mark vm->strings here.
	if(vm->strings)
		((Obj*)vm->strings)->isMarked = true;
	markCompilerRoots(vm);
}

static void blackenObject(VM *vm, Obj *o) {
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)o);
	if((o->type == OBJ_CLASS) && ((ObjClass*)o)->name == NULL) {
		// During startup class->name might be NULL. No need to pollute
		// printValue with a check, when it only matters if debugging the GC.
		printf("<Class %s>", ((ObjClass*)o)->cname);
	} else {
		printValue(OBJ_VAL(o));
	}
	printf("\n");
#endif /* DEBUG_LOG_GC */
	switch(o->type) {
		case OBJ_ARRAY: {
			ObjArray *array = (ObjArray*)o;
			markArray(vm, array);
			break;
		}
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *bound = (ObjBoundMethod*)o;
			markValue(vm, bound->receiver);
			markObject(vm, (Obj*)bound->method);
			break;
		}
		case OBJ_CLASS: {
			ObjClass *klass = (ObjClass*)o;
			markObject(vm, (Obj*)klass->name);
			markObject(vm, (Obj*)klass->methods);
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
			markObject(vm, (Obj*)f->chunk.constants);
			break;
		}
		case OBJ_INSTANCE: {
			ObjInstance *instance = (ObjInstance*)o;
			markObject(vm, (Obj*)instance->klass);
			markObject(vm, (Obj*)instance->fields);
			break;
		}
		case OBJ_MODULE: {
			ObjModule *module = (ObjModule*)o;
			markObject(vm, (Obj*)module->name);
			markObject(vm, (Obj*)module->items);
			break;
		}
		case OBJ_TABLE:
			markTable(vm, (ObjTable*) o);
			break;
		case OBJ_UPVALUE:
			markValue(vm, ((ObjUpvalue*)o)->closed);
			break;
		case OBJ_EXCEPTION: {
			ObjException *e = (ObjException*)o;
			markValue(vm, e->msg);
			break;
		}
		case OBJ_STRING: {
			ObjString *s = (ObjString*)o;
			markObject(vm, (Obj*)s->klass);
			markObject(vm, (Obj*)s->fields);
			break;
		}
		case OBJ_NATIVE:
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
		case OBJ_ARRAY: {
			ObjArray *array = (ObjArray*)object;
			FREE_ARRAY(Value, array->values, array->capacity);
			FREE(ObjArray, object);
			break;
		}
		case OBJ_BOUND_METHOD:
			FREE(ObjBoundMethod, object);
			break;
		case OBJ_CLASS: {
			ObjClass *klass = (ObjClass*)object;
			if(klass->cname) {
				// this means that the class is in static memory, and isn't freeable.
				// We set its' internal pointers to NULL so we don't accidentally dereference
				// a pointer to freed memory.
				klass->name = NULL;
				klass->methods = NULL;
			} else {
				FREE(ObjClass, object);
			}
			break;
		}
		case OBJ_CLOSURE: {
			ObjClosure *cl = (ObjClosure*)object;
			FREE_ARRAY(ObjUpvalue*, cl->upvalues, cl->uvCount);
			FREE(ObjClosure, object);
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction *f = (ObjFunction*)object;
			freeChunk(vm, &f->chunk);
			FREE(ObjFunction, object);
			break;
		}
		case OBJ_INSTANCE: {
			FREE(ObjInstance, object);
			break;
		}
		case OBJ_MODULE: {
			FREE(ObjModule, object);
			break;
		}
		case OBJ_NATIVE:
			FREE(ObjNative, object);
			break;
		case OBJ_STRING: {
			ObjString *string = (ObjString*)object;
			FREE_ARRAY(char, string->chars, string->length + 1);
			FREE(ObjString, object);
			break;
		}
		case OBJ_TABLE:
			freeTable(vm, (ObjTable*)object);
			break;
		case OBJ_UPVALUE:
			FREE(ObjUpvalue, object);
			break;
		case OBJ_EXCEPTION:
			FREE(ObjException, object);
			break;
	}
}

void freeChunk(VM *vm, Chunk *chunk) {
	FREE_ARRAY(uint32_t, chunk->code, chunk->capacity);
	FREE_ARRAY(size_t, chunk->lines, chunk->capacity);
	chunk->constants = NULL;
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	chunk->constants = NULL;
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
	if(vm->strings)
		tableRemoveWhite(vm->strings);
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

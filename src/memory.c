#include "memory.h"

#include <assert.h>
#include <stdlib.h>

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "exception.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif /* DEBUG_LOG_GC */

static void pushGrayStack(GarbageCollector *gc, Obj *o) {
	if(gc->grayCapacity < gc->grayCount+1) {
		gc->grayCapacity = GROW_CAPACITY(gc->grayCapacity);
		gc->grayStack = realloc(gc->grayStack, sizeof(Obj*) * gc->grayCapacity);
	}
	gc->grayStack[gc->grayCount++] = o;
}

static void markObject(GarbageCollector *gc, Obj *o) {
	if(o == NULL)
		return;
	if(o->isBlack)
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
	o->isBlack = true;
	pushGrayStack(gc, o);
}

void markValue(GarbageCollector *gc, Value v) {
	if(!IS_OBJ(v))
		return;
	markObject(gc, AS_OBJ(v));
}

static void markArray(GarbageCollector *gc, ObjArray *array) {
	array->obj.isBlack = true;
	for(size_t i=0; i<array->count; i++)
		markValue(gc, array->values[i]);
}

static void markCompilerRoots(VM *vm) {
	for(Compiler *c = vm->currentCompiler; c != NULL; c = c->enclosing) {
		markObject(&vm->gc, (Obj*)c->name);
		markObject(&vm->gc, (Obj*)c->chunk.constants);
		markObject(&vm->gc, (Obj*)c->chunk.constantIndices);
	}
	for(ClassCompiler *c = vm->currentClassCompiler; c != NULL; c = c->enclosing)
		markObject(&vm->gc, (Obj*)c->methods);
}

static void markRoots(VM *vm) {
#ifdef DEBUG_LOG_GC
	printf("stackTop = %ld (%p)\n", vm->stackTop - vm->stack, (void*)vm->stackTop);
#endif /* DEBUG_LOG_GC */
	/*
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
		markValue(&vm->gc, *slot);
	}
	*/

	Value *base = vm->base;
	for(Value *slot = vm->stackTop - 1; slot >= vm->stack; slot--) {
		for(;(slot >= base - 1) && (slot >= vm->stack); slot--)		// TODO: remove reference to vm->stack. Once frames are included in the stack, this should be unnecesary.
			markValue(&vm->gc, *slot);
		if(slot <= vm->stack)										// TODO: This should be removable once frames are on stack.
			break;
		markValue(&vm->gc, *--slot);	// mark base[-3]
		if(slot < vm->stack)
			break;
		assert(base >= vm->stack + 3);
		if(IS_NIL(*slot)) {
			base -= AS_IP(base[-2]);
		} else {
			uint32_t *ip = (uint32_t*)(AS_IP(base[-2]));
			uint32_t bytecode = *(ip - 1);
			Reg ra = RA(bytecode) + 1;
			base -= ra + 2;
		}
	}
	markObject(&vm->gc, (Obj*)vm->initString);
	markObject(&vm->gc, (Obj*)vm->newString);
	markValue(&vm->gc, vm->exception);

	for(ObjUpvalue *u = vm->openUpvalues; u != NULL; u = u->next)
		markObject(&vm->gc, (Obj*)u);

	markObject(&vm->gc, (Obj*)vm->globals);
	// We don't want to mark the entries, so we'll manually mark vm->strings here.
	if(vm->strings)
		((Obj*)vm->strings)->isBlack = true;
	markCompilerRoots(vm);
}

static void blackenObject(GarbageCollector *gc, Obj *o) {
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
	o->isGrey = false;
	switch(o->type) {
		case OBJ_ARRAY: {
			ObjArray *array = (ObjArray*)o;
			markObject(gc, (Obj*)array->klass);
			markObject(gc, (Obj*)array->fields);
			markArray(gc, array);
			break;
		}
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *bound = (ObjBoundMethod*)o;
			markValue(gc, bound->receiver);
			markObject(gc, (Obj*)bound->method);
			break;
		}
		case OBJ_CLASS: {
			ObjClass *klass = (ObjClass*)o;
			markObject(gc, (Obj*)klass->name);
			markObject(gc, (Obj*)klass->methods);
			break;
		}
		case OBJ_CLOSURE: {
			ObjClosure *cl = (ObjClosure*)o;
			markObject(gc, (Obj*)cl->f);
			for(size_t i=0; i<cl->uvCount; i++)
				markObject(gc, (Obj*)cl->upvalues[i]);
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction *f = (ObjFunction*)o;
			markObject(gc, (Obj*)f->name);
			markObject(gc, (Obj*)f->chunk.constants);
			break;
		}
		case OBJ_INSTANCE: {
			ObjInstance *instance = (ObjInstance*)o;
			markObject(gc, (Obj*)instance->klass);
			markObject(gc, (Obj*)instance->fields);
			break;
		}
		case OBJ_MODULE: {
			ObjModule *module = (ObjModule*)o;
			markObject(gc, (Obj*)module->name);
			markObject(gc, (Obj*)module->items);
			break;
		}
		case OBJ_TABLE:
			markTable(gc, (ObjTable*) o);
			break;
		case OBJ_UPVALUE:
			markValue(gc, ((ObjUpvalue*)o)->closed);
			break;
		case OBJ_EXCEPTION: {
			ObjException *e = (ObjException*)o;
			markObject(gc, (Obj*)e->klass);
			markObject(gc, (Obj*)e->fields);
			markValue(gc, e->msg);
			break;
		}
		case OBJ_STRING: {
			ObjString *s = (ObjString*)o;
			markObject(gc, (Obj*)s->klass);
			markObject(gc, (Obj*)s->fields);
			break;
		}
		case OBJ_NATIVE:
			break;
	}
}

static void traceReferences(GarbageCollector *gc) {
	while(gc->grayCount > 0) {
		Obj *o = gc->grayStack[--gc->grayCount];
		blackenObject(gc, o);
	}
}

static void freeObject(GarbageCollector *gc, Obj *object) {
#ifdef DEBUG_LOG_GC
	printf("%p free type %s\n", (void*)object, ObjTypeNames[object->type]);
#endif /* DEBUG_LOG_GC */
	switch(object->type) {
		case OBJ_ARRAY: {
			ObjArray *array = (ObjArray*)object;
			FREE_ARRAY(gc, Value, array->values, array->capacity);
			FREE(gc, ObjArray, object);
			break;
		}
		case OBJ_BOUND_METHOD:
			FREE(gc, ObjBoundMethod, object);
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
				FREE(gc, ObjClass, object);
			}
			break;
		}
		case OBJ_CLOSURE: {
			ObjClosure *cl = (ObjClosure*)object;
			FREE_ARRAY(gc, ObjUpvalue*, cl->upvalues, cl->uvCount);
			FREE(gc, ObjClosure, object);
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction *f = (ObjFunction*)object;
			freeChunk(gc, &f->chunk);
			_free(gc, object, sizeof(ObjFunction) + f->uvCount * sizeof(uint16_t) + (f->maxArity - f->minArity + 1) * sizeof(size_t));
			break;
		}
		case OBJ_INSTANCE: {
			FREE(gc, ObjInstance, object);
			break;
		}
		case OBJ_MODULE: {
			FREE(gc, ObjModule, object);
			break;
		}
		case OBJ_NATIVE:
			FREE(gc, ObjNative, object);
			break;
		case OBJ_STRING: {
			ObjString *string = (ObjString*)object;
			FREE_ARRAY(gc, char, string->chars, string->length + 1);
			FREE(gc, ObjString, object);
			break;
		}
		case OBJ_TABLE:
			freeTable(gc, (ObjTable*)object);
			break;
		case OBJ_UPVALUE:
			FREE(gc, ObjUpvalue, object);
			break;
		case OBJ_EXCEPTION:
			FREE(gc, ObjException, object);
			break;
	}
}

void freeChunk(GarbageCollector *gc, Chunk *chunk) {
	FREE_ARRAY(gc, uint32_t, chunk->code, chunk->capacity);
	FREE_ARRAY(gc, size_t, chunk->lines, chunk->capacity);
	chunk->constants = NULL;
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	chunk->constants = NULL;
}

static void sweep(GarbageCollector *gc, bool nextGCisMajor) {
	Obj **o = &gc->objects;
	if(nextGCisMajor) {
		while(*o) {
			if((*o)->isBlack) {
				(*o)->isBlack = false;
				o = &(*o)->next;
			} else {
				Obj *unreached = *o;
				*o = (*o)->next;
				freeObject(gc, unreached);
			}
		}
	} else {
		while(*o) {
			if((*o)->isBlack) {
				o = &(*o)->next;
			} else {
				Obj *unreached = *o;
				*o = (*o)->next;
				freeObject(gc, unreached);
			}
		}
	}
}

void collectGarbage(VM *vm) {
#ifdef DEBUG_LOG_GC
	printf("-- gc begin\n");
	size_t before = vm->gc.bytesAllocated;
#endif /* DEBUG_LOG_GC */

	markRoots(vm);
	traceReferences(&vm->gc);
	if(vm->strings)
		tableRemoveWhite(vm->strings);
	bool nextGCisMajor = vm->gc.bytesAllocated > vm->gc.nextMajorGC;
	sweep(&vm->gc, nextGCisMajor);
	vm->gc.nextMinorGC = vm->gc.bytesAllocated * GC_MINOR_HEAP_GROW_FACTOR;
	if(vm->gc.nextGCisMajor)
		vm->gc.nextMajorGC = vm->gc.bytesAllocated * GC_HEAP_GROW_FACTOR;
	vm->gc.nextGCisMajor = nextGCisMajor;

#ifdef DEBUG_LOG_GC
	printf("-- gc end\n");
	printf("   collected %zu bytes (from %zu to %zu next at %zu\n",
			before - vm->gc.bytesAllocated, before, vm->gc.bytesAllocated, vm->gc.nextMinorGC);
#endif /* DEBUG_LOG_GC */
}

void freeObjects(GarbageCollector *gc) {
	Obj *object = gc->objects;
	while(object) {
		Obj *next = object->next;
		freeObject(gc, object);
		object = next;
	}
	free(gc->grayStack);
}

void* reallocate(VM *vm, void* previous, size_t oldSize, size_t newSize) {
	GarbageCollector *gc = &vm->gc;
	assert(gc->bytesAllocated  + newSize >= oldSize);	// We won't drop bytes allocated below 0.
	gc->bytesAllocated += newSize - oldSize;

	if(newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
		collectGarbage(vm);
#endif /* DEBUG_STRESS_GC */
		if(gc->bytesAllocated > gc->nextMajorGC) {
			collectGarbage(vm);
		}
	}

	if(newSize == 0) {
		free(previous);
		return NULL;
	}

	return realloc(previous, newSize);
}

void _free(GarbageCollector *gc, void* previous, size_t oldSize) {
	assert((previous != NULL) || (oldSize == 0));
	assert(gc->bytesAllocated >= oldSize);
	gc->bytesAllocated -= oldSize;
#ifdef DEBUG_LOG_GC
	printf("%p free %ld: %zu bytes allocated total.\n", previous, oldSize, gc->bytesAllocated);
#endif /* DEBUG_LOG_GC */
	free(previous);
}

Obj* allocateObject(size_t size, ObjType type, VM *vm) {
	Obj *object = (Obj*)reallocate(vm, NULL, 0, size);
	object->type = type;
	object->isBlack = false;
	object->isGrey = true;
	object->next = vm->gc.objects;
	vm->gc.objects = object;
#ifdef DEBUG_LOG_GC
	printf("%p allocate %ld for %s: %zu bytes allocated total.\n", (void*)object, size, ObjTypeNames[type], vm->gc.bytesAllocated);
#endif /* DEBUG_LOG_GC */
	return object;
}

void setGrey(GarbageCollector *gc, Obj *o) {
	o->isGrey = true;
	if(o->isBlack)
		pushGrayStack(gc, o);
}

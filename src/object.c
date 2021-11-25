#include "object.h"

#include <stdio.h>
#include <string.h>

#include "class.h"
#include "chunk.h"
#include "exception.h"
#include "memory.h"
#include "table.h"

ObjFunction *newFunction(VM *vm, thread *currentThread, size_t uvCount, size_t varArityCount) {
	ObjFunction *f = (ObjFunction*)allocateObject(sizeof(*f) + uvCount * sizeof(uint16_t) + varArityCount * sizeof(size_t), OBJ_FUNCTION, vm);
	currentThread->base[0] = OBJ_VAL(f);

	f->minArity = 0;
	f->maxArity = 0;
	f->uvCount = uvCount;
	f->stackUsed = 0;
	f->name = NULL;
	f->code_offsets = (size_t*)&f->uv[f->uvCount];
	initChunk(vm, currentThread, &f->chunk);
	return f;
}

ObjUpvalue *newUpvalue(VM *vm, Value *slot) {
	ObjUpvalue *uv = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
	uv->location = slot;
	uv->closed = NIL_VAL;
	uv->next = NULL;
	return uv;
}

ObjClosure *newClosure(VM *vm, ObjFunction *f) {
	ObjUpvalue **uvs = ALLOCATE(vm, ObjUpvalue*, f->uvCount);
	for(size_t i=0; i<f->uvCount; i++)
		uvs[i] = NULL;
	ObjClosure *cl = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
	cl->f = f;
	cl->upvalues = uvs;
	cl->uvCount = f->uvCount;
	cl->ip = f->chunk.code;
	return cl;
}

ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, Value method) {
	ObjBoundMethod *ret = ALLOCATE_OBJ(vm, ObjBoundMethod, OBJ_BOUND_METHOD);
	ret->receiver = receiver;
	assert(IS_OBJ(method));
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));
	ret->method = AS_OBJ(method);
	return ret;
}

ObjClass *newClass(VM *vm, thread *currentThread, ObjString *name) {
	ObjClass *klass = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
	klass->name = name;
	klass->methods = NULL;
	klass->cname = NULL;
	klass->methodsArray = NULL;
	currentThread->base[0] = OBJ_VAL(klass);	// name is still reachable through klass.
	incCFrame(vm, currentThread, 1, 3);
	klass->methods = newTable(vm, currentThread, 0);
	decCFrame(currentThread);
	writeBarrier(vm, klass);
	return klass;
}

void defineNative(VM *vm, thread *currentThread, ObjTable *t, const NativeDef *f) {
	currentThread->base[0] = OBJ_VAL(copyString(vm, currentThread, f->name, strlen(f->name)));
	currentThread->base[1] = OBJ_VAL(newNative(vm, f->method));
	assert(IS_STRING(currentThread->base[0]));
	tableSet(vm, t, currentThread->base[0], currentThread->base[1]);
}

void defineNativeClass(VM *vm, thread *currentThread, ObjTable *t, ObjClass *klass) {
	klass->name = copyString(vm, currentThread, klass->cname, strlen(klass->cname));
	writeBarrier(vm, klass);

	// klass is statically allocated, so it avoids newClass. This puts it in the linked list of objects for the garbage collector.
	klass->obj.next = vm->gc.objects;
	vm->gc.objects = (Obj*)klass;

	currentThread->base[0] = OBJ_VAL(klass);

	incCFrame(vm, currentThread, 2, 3);
	klass->methods = newTable(vm, currentThread, 0);
	writeBarrier(vm, klass);
	assert(klass->methods);
	for(size_t i = 0; klass->methodsArray[i].name; i++) {
		defineNative(vm, currentThread, klass->methods, &klass->methodsArray[i]);
		// defineNative uses currentThread->base[0] for the function name and currentThread->base[1] for the function.
		if(AS_STRING(currentThread->base[0]) == vm->newString)
			klass->newFn = AS_OBJ(currentThread->base[1]);
	}
	decCFrame(currentThread);

	tableSet(vm, t, OBJ_VAL(klass->name), OBJ_VAL(klass));
}

ObjModule * newModule(VM *vm, thread *currentThread, ObjString *name) {
	currentThread->base[0] = OBJ_VAL(name);
	ObjModule *module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
	module->name = name;
	module->klass = NULL;
	module->fields = NULL;
	currentThread->base[0] = OBJ_VAL(module);
	module->klass = &moduleDef;
	module->fields = newTable(vm, currentThread, 0);
	writeBarrier(vm, module);
	return module;
}

ObjModule *defineNativeModule(VM *vm, thread *currentThread, ModuleDef *def) {
	ObjString *name = copyString(vm, currentThread, def->name, strlen(def->name));
	ObjModule *ret = newModule(vm, currentThread, name);
	currentThread->base[0] = OBJ_VAL(ret);		// For GC.

	incCFrame(vm, currentThread, 1, 3);
	for(ObjClass **c = def->classes; *c; c++)
		defineNativeClass(vm, currentThread, ret->fields, *c);
	assert(def->methods);
	for(NativeDef *m = def->methods; m->name; m++)
		defineNative(vm, currentThread, ret->fields, m);
	decCFrame(currentThread);

	return ret;
}

ObjInstance *newInstance(VM *vm, thread *currentThread, ObjClass *klass) {
	ObjInstance *o = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
	o->klass = klass;
	o->fields = NULL;
	currentThread->base[0] = OBJ_VAL(o);
	incCFrame(vm, currentThread, 1, 3);
	o->fields = newTable(vm, currentThread, 0);
	decCFrame(currentThread);
	writeBarrier(vm, o);
	return o;
}

ObjNative *newNative(VM *vm, NativeFn function) {
	ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjArray *duplicateArray(VM *vm, thread *currentThread, ObjArray *source) {
	ObjArray *dest = newArray(vm, currentThread, source->count);
	for(size_t i=0; i<source->count; i++)
		dest->values[i] = source->values[i];
	return dest;
}

void setArray(VM *vm, ObjArray *array, int idx, Value v) {
	// The caller is required to ensure that v is findable by the GC.
	assert(idx >= 0);
	if((size_t)idx >= array->capacity) {
		size_t capacity = GROW_CAPACITY(round_up_pow_2(idx));
		array->values = GROW_ARRAY(vm, array->values, Value, array->capacity, capacity);
		array->capacity = capacity;
	}

	for(size_t i = array->count; i < (size_t)idx; i++)
		array->values[i] = NIL_VAL;

	array->values[idx] = v;
	if((size_t)idx >= array->count)
		array->count = (size_t)idx + 1;

}

bool getArray(ObjArray *array, int idx, Value *ret) {
	if((idx < 0) || ((size_t)idx >= array->count))
		return true;

	*ret = array->values[idx];
	return false;
}

static void fprintFunction(FILE *restrict stream, ObjFunction *f) {
	if(f->name == NULL) {
		fprintf(stream, "<script>");
	} else {
		fprintf(stream, "<fn %s>", f->name->chars);
	}
}

static void fprintArray(FILE *restrict stream, ObjArray *array) {
	if(array->count == 0) {
		fprintf(stream, "[]");
		return;
	}

	fprintf(stream, "[");
	fprintValue(stream, array->values[0]);
	for(size_t i = 1; i < array->count; i++) {
		fprintf(stream, ", ");
		fprintValue(stream, array->values[i]);
	}
	fprintf(stream, "]");
}

void fprintObject(FILE *restrict stream, Value value) {
	switch(OBJ_TYPE(value)) {
		case OBJ_ARRAY:
			fprintArray(stream, AS_ARRAY(value));
			break;
		case OBJ_BOUND_METHOD: {
				Obj *method = AS_BOUND_METHOD(value)->method;
				if(method->type == OBJ_CLOSURE) {
					fprintFunction(stream, ((ObjClosure*)method)->f);
					break;
				}
				assert(method->type == OBJ_NATIVE);
				fprintf(stream, "<native method>");
				break;
			}
		case OBJ_CLASS:
			fprintf(stream, "<Class %s>", AS_CLASS(value)->name->chars);
			break;
		case OBJ_CLOSURE:
			fprintFunction(stream, AS_CLOSURE(value)->f);
			break;
		case OBJ_FUNCTION:
			fprintFunction(stream, AS_FUNCTION(value));
			break;
		case OBJ_INSTANCE:
			fprintf(stream, "%s instance", AS_INSTANCE(value)->klass->name->chars);
			break;
		case OBJ_MODULE:
			fprintf(stream, "<Module %s>", AS_MODULE(value)->name->chars);
			break;
		case OBJ_NATIVE:
			fprintf(stream, "<native fn>");
			break;
		case OBJ_STRING:
			if(AS_STRING(value)->length == 0) {
				fprintf(stream, "''");
			} else {
				fprintf(stream, "%s", AS_CSTRING(value));
			}
			break;
		case OBJ_TABLE:
			fprintTable(stream, AS_TABLE(value));
			break;
		case OBJ_UPVALUE:
			fprintf(stream, "upvalue");
			break;
		case OBJ_THREAD:
			fprintf(stream, "thread");
			break;
		case OBJ_EXCEPTION:
			fprintf(stream, "Error: ");
			fprintValue(stream, AS_EXCEPTION(value)->msg);
			fprintf(stream, ": ");
			break;
	}
}

void printObject(Value value) {
	fprintObject(stdout, value);
}

#ifndef TAGGED_NAN
bool valuesEqual(Value a, Value b) {
	if(a.type != b.type) return false;

	switch(a.type) {
		case VAL_BOOL:		return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL:		return true;
		case VAL_NUMBER:	return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ:		return AS_OBJ(a) == AS_OBJ(b);
	}
	return false;
}
#endif /* TAGGED_NAN */

void fprintValue(FILE *restrict stream, Value value) {
	if(IS_BOOL(value)) {
		fprintf(stream, AS_BOOL(value) ? "true" : "false");
	} else if(IS_NIL(value)) {
		fprintf(stream, "nil");
	} else if(IS_NUMBER(value)) {
		fprintf(stream, "%g", AS_NUMBER(value));
	} else {
		assert(IS_OBJ(value));
		fprintObject(stream, value);
	}
}

void printValue(Value value) {
	fprintValue(stdout, value);
}

NativeDef moduleMethods[] = {
	{NULL, NULL}
};

ObjClass moduleDef = {
	CLASS_HEADER,
	"Module",
	moduleMethods,
	RUNTIME_CLASSDEF_FIELDS,
	false
};

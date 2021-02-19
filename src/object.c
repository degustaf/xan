#include "object.h"

#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "exception.h"
#include "memory.h"
#include "table.h"

ObjFunction *newFunction(VM *vm, size_t uvCount, size_t varArityCount) {
	ObjFunction *f = (ObjFunction*)allocateObject(sizeof(*f) + uvCount * sizeof(uint16_t) + varArityCount * sizeof(size_t), OBJ_FUNCTION, vm);
	*vm->base = OBJ_VAL(f);

	f->minArity = 0;
	f->maxArity = 0;
	f->uvCount = uvCount;
	f->stackUsed = 0;
	f->name = NULL;
	f->code_offsets = (size_t*)&f->uv[f->uvCount];
	initChunk(vm, &f->chunk);
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

ObjClass *newClass(VM *vm, ObjString *name) {
	ObjClass *klass = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
	klass->name = name;
	klass->methods = NULL;
	klass->cname = NULL;
	klass->methodsArray = NULL;
	vm->base[0] = OBJ_VAL(klass);	// name is still reachable through klass.
	klass->methods = newTable(vm, 0);
	writeBarrier(vm, klass);
	return klass;
}

void defineNative(VM *vm, ObjTable *t, const NativeDef *f) {
	vm->base[0] = OBJ_VAL(copyString(f->name, strlen(f->name), vm));
	vm->base[1] = OBJ_VAL(newNative(vm, f->method));
	assert(IS_STRING(vm->base[0]));
	tableSet(vm, t, vm->base[0], vm->base[1]);
}

void defineNativeClass(VM *vm, ObjTable *t, ObjClass *klass) {
	assert(vm->frameCount + 1 < vm->frameSize);
	// This prevents a reallocation of vm->frames when incFrame is called.

	klass->name = copyString(klass->cname, strlen(klass->cname), vm);
	writeBarrier(vm, klass);

	// klass is statically allocated, so it avoids newClass. This puts it in the linked list of objects for the garbage collector.
	// TODO This shouldn't be needed. but we'll get it working before trying to remove.
	klass->obj.next = vm->gc.objects;
	vm->gc.objects = (Obj*)klass;

	vm->base[0] = OBJ_VAL(klass);

	incFrame(vm, 2, vm->base + 1, NULL);
	klass->methods = newTable(vm, 0);
	writeBarrier(vm, klass);
	assert(klass->methods);
	for(size_t i = 0; klass->methodsArray[i].name; i++) {
		defineNative(vm, klass->methods, &klass->methodsArray[i]);
		// defineNative uses vm->base[0] for the function name and vm->base[1] for the function.
		if(AS_STRING(vm->base[0]) == vm->newString)
			klass->newFn = AS_OBJ(vm->base[1]);
	}
	decFrame(vm);

	tableSet(vm, t, OBJ_VAL(klass->name), OBJ_VAL(klass));
}

ObjModule * newModule(VM *vm, ObjString *name) {
	vm->base[0] = OBJ_VAL(name);
	ObjModule *module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
	module->name = name;
	module->items = NULL;
	vm->base[0] = OBJ_VAL(module);
	module->items = newTable(vm, 0);
	writeBarrier(vm, module);
	return module;
}

ObjModule *defineNativeModule(VM *vm, ModuleDef *def) {
	assert(vm->frameCount + 2 < vm->frameSize);
	// This prevents a reallocation of vm->frames when incFrame is called, or when defineNativeClass is called.

	ObjString *name = copyString(def->name, strlen(def->name), vm);
	ObjModule *ret = newModule(vm, name);
	vm->base[0] = OBJ_VAL(ret);		// For GC.

	incFrame(vm, 1, vm->base + 1, NULL);
	for(ObjClass **c = def->classes; *c; c++)
		defineNativeClass(vm, ret->items, *c);
	assert(def->methods);
	for(NativeDef *m = def->methods; m->name; m++)
		defineNative(vm, ret->items, m);
	decFrame(vm);

	return ret;
}

ObjInstance *newInstance(VM *vm, ObjClass *klass) {
	ObjInstance *o = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
	o->klass = klass;
	o->fields = NULL;
	vm->base[0] = OBJ_VAL(o);
	o->fields = newTable(vm, 0);
	writeBarrier(vm, o);
	return o;
}

ObjNative *newNative(VM *vm, NativeFn function) {
	ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjArray *duplicateArray(VM *vm, ObjArray *source) {
	ObjArray *dest = newArray(vm, source->count);
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
			fprintf(stream, "%s", AS_CSTRING(value));
			break;
		case OBJ_TABLE:
			fprintTable(stream, AS_TABLE(value));
			break;
		case OBJ_UPVALUE:
			fprintf(stream, "upvalue");
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

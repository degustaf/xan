#ifndef XAN_VM_H
#define XAN_VM_H

#include <assert.h>
#include <stddef.h>

#include "object.h"
#include "type.h"

void growStack(VM *vm, size_t space_needed);

static inline void incCFrame(VM *vm, Reg stackUsed, size_t shift) {
	if(vm->base + shift + 1 + stackUsed + 1 > vm->stackLast) {
		size_t base_index = vm->base + shift + 1 - vm->stack;
		growStack(vm, base_index + stackUsed + 1 + 1);
	}
	if(vm->base + shift + 1 + stackUsed + 1 > vm->stackTop)
		vm->stackTop = vm->base + shift + 1 + stackUsed + 1;

	vm->base += shift + 1;
	vm->base[-2] = IP_VAL(shift+1);
	vm->base[-3] = NIL_VAL;
}

static inline uint32_t* decCFrame(VM *vm) {
	assert(IS_NIL(vm->base[-3]));
	vm->base -= AS_IP(vm->base[-2]);
	return NULL;
}

#endif /* XAN_VM_H */

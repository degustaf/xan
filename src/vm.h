#ifndef XAN_VM_H
#define XAN_VM_H

#include <assert.h>
#include <stddef.h>

#include "object.h"
#include "type.h"

void growStack(VM *vm, thread *currentThread, size_t space_needed);

static inline void incCFrame(VM *vm, thread *currentThread, Reg stackUsed, size_t shift) {
	if(currentThread->base + shift + stackUsed + 2 > currentThread->stackLast) {
		size_t base_index = currentThread->base + shift + 1 - currentThread->stack;
		growStack(vm, currentThread, base_index + stackUsed + 2);
	}
	if(currentThread->base + shift + stackUsed + 2 > currentThread->stackTop)
		currentThread->stackTop = currentThread->base + shift + stackUsed + 2;

	currentThread->base += shift + 1;
	currentThread->base[-2] = IP_VAL(shift+1);
	currentThread->base[-3] = NIL_VAL;
}

static inline uint32_t* decCFrame(thread *currentThread) {
	assert(IS_NIL(currentThread->base[-3]));
	currentThread->base -= AS_IP(currentThread->base[-2]);
	return NULL;
}

uint32_t* call(VM *vm, thread *currentThread, ObjClosure *function, Reg calleeReg, Reg argCount, uint32_t *ip);
InterpretResult run(VM *vm, thread *currentThread);

#endif /* XAN_VM_H */

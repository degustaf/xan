#include "debug.h"

#include <stdio.h>

#include "object.h"

static void InstructionA(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	printf("%-16s register %d\n", name, reg);
}

static void InstructionABC(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint8_t regA = RA(bytecode);
	uint8_t regB = RB(bytecode);
	uint8_t regC = RC(bytecode);
	printf("%-16s Reg %4d Reg %4d -> Reg %4d\n", name, regB, regC, regA);
}

static void InstructionABCcall(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint8_t regA = RA(bytecode);
	uint8_t regB = RB(bytecode);
	uint8_t regC = RC(bytecode);
	printf("%-16s call Reg %4d with arg count %4d returning %4d\n", name, regA, regC, regB);
}

static void InstructionAD(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s register %4d to register %d\n", name, constant, reg);
}

static void InstructionADConst(const char *name, Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s %4d '", name, constant);
	if(constant < chunk->count)
		printValue(chunk->constants->values[constant]);
	else
		printf("Out of Range");
	printf("' to register %d\n", reg);
}

static void InstructionADprim(__attribute__((unused)) const char *name, __attribute__((unused))Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	Value p = getPrimitive(RD(bytecode));
	printf("%-16s '", "OP_PRIMITIVE");
	printValue(p);
	printf("' to register %d\n", reg);
}

static void InstructionADstr(const char *name, Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s variable %4d '", name, constant);
	if(constant < chunk->count)
		printValue(chunk->constants->values[constant]);
	else
		printf("Out of Range");
	printf("' to register %d\n", reg);
}

static void InstructionADret(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t count = RD(bytecode);
	printf("%-16s return %4d - 1 registers starting at %d\n", name, count, reg);
}

static void InstructionD(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint16_t constant = RD(bytecode);
	printf("%-16s register %4d\n", name, constant);
}

static void InstructionJ(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	int16_t constant = RJump(bytecode);
	printf("%-16s jump %4d\n", name, constant);
}

static void InstructionAJ(const char *name, __attribute__((unused)) Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	int16_t constant = RJump(bytecode);
	printf("%-16s register %4d jump %d\n", name, reg, constant);
}

void disassembleInstruction(Chunk* chunk, size_t offset) {
	uint32_t bytecode = chunk->code[offset];
	printf("%04zu %08x ", offset, bytecode);

	if(offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4zu ", chunk->lines[offset]);
	}
#define BUILD_DISASSEMBLY(op, type) \
	case op: \
		Instruction##type(#op, chunk, bytecode); \
		break

	switch(OP(bytecode)) {
		OPCODE_BUILDER(BUILD_DISASSEMBLY, ;)
		default:
			printf("Unknown opcode %d\n", OP(bytecode));
			return;
	}
}

void disassembleChunk(Chunk* chunk) {
	printf(" = Constants\n");
	for(size_t i = 0; i < chunk->constants->count; i++) {
		printf("%03zu\t'", i);
		printValue(chunk->constants->values[i]);
		printf("'\n");
	}
	printf("\n");

	for(size_t offset = 0; offset < chunk->count; offset++) {
		disassembleInstruction(chunk, offset);
	}
}

void disassembleFunction(ObjFunction *f) {
	printf("== %s ==\n", f->name == NULL ? "<scripts>" : f->name->chars);

	printf("upvalues:");
	if(f->uvCount > 0) {
		printf("0x%x", f->uv[0]);
		for(size_t i = 1; i<f->uvCount; i++)
			printf(", 0x%x", f->uv[i]);
	}
	printf("\n");

	disassembleChunk(&f->chunk);
}

static void dumpValueArray(Value *valueArray, Value *ArrayTop, size_t count) {
	printf("{");
	size_t top = ArrayTop - valueArray;
	if(count < top) top = count;
	printValue(valueArray[0]);
	for(size_t i = 1; i < top; i++) {
		printf(", ");
		printValue(valueArray[i]);
	}
	printf("}\n");
}

void dumpStack(VM *vm, size_t count) {
	printf("stack = ");
	dumpValueArray(vm->stack, vm->stackLast, count);
	printf("frames = [");
	for(size_t i = 0; i<vm->frameCount; i++)
		printf("%ld, ", vm->frames[i].slots - vm->stack);
	printf("]\n");
	printf("frame[%zu] = stack[%ld] = ", vm->frameCount - 1, vm->frames[vm->frameCount - 1].slots - vm->stack);
	dumpValueArray(vm->frames[vm->frameCount - 1].slots, vm->stackLast, count);
}

void dumpOpenUpvalues(VM *vm) {
	printf("Open upvalues = {");
	for(ObjUpvalue **uv = &vm->openUpvalues; *uv != NULL; uv = &(*uv)->next) {
		printf("%p:", (void*)(*uv)->location);
		printObject(*(*uv)->location);
		printf(", ");
	}
	printf("}\n");
}

void dumpClosedUpvalues(ObjClosure *c) {
	if(c) {
		printf("Closed upvalues(%zu) = {", c->uvCount);
		fflush(stdout);
		for(size_t i = 0; i<c->uvCount; i++) {
			printObject(*c->upvalues[i]->location);
			printf(", ");
		}
		printf("}\n");
	} else {
		printf("closure = NULL.\n");
	}
}

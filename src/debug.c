#include "debug.h"

#include <stdio.h>

static void simpleInstruction(const char *name) {
	printf("%s\n", name);
}

static void constantInstruction(const char *name, Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s %4d '", name, constant);
	if(constant < chunk->count)
		printValue(chunk->constants.values[constant]);
	else
		printf("Out of Range");
	printf("' to register %d\n", reg);
}

static void primitiveInstruction(uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	Value p = getPrimitive(RD(bytecode));
	printf("%-16s '", "OP_PRIMITIVE");
	printValue(p);
	printf("' to register %d\n", reg);
}

static void InstructionA(const char *name, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	printf("%-16s register %d\n", name, reg);
}

static void InstructionAD(const char *name, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s register %4d to register %d\n", name, constant, reg);
}

static void InstructionADstr(const char *name, Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s variable %4d '", name, constant);
	if(constant < chunk->count)
		printValue(chunk->constants.values[constant]);
	else
		printf("Out of Range");
	printf("' to register %d\n", reg);
}

static void InstructionABC(const char *name, uint32_t bytecode) {
	uint8_t regA = RA(bytecode);
	uint8_t regB = RB(bytecode);
	uint8_t regC = RC(bytecode);
	printf("%-16s Reg %4d Reg %4d -> Reg %4d\n", name, regB, regC, regA);
}

static void InstructionD(const char *name, uint32_t bytecode) {
	uint16_t constant = RD(bytecode);
	printf("%-16s register %4d\n", name, constant);
}

static void InstructionJ(const char *name, uint32_t bytecode) {
	int16_t constant = (int16_t)RD(bytecode) - JUMP_BIAS;
	printf("%-16s register %4d\n", name, constant);
}

static void callInstruction(const char *name, uint32_t bytecode) {
	uint8_t regA = RA(bytecode);
	uint8_t regB = RB(bytecode);
	uint8_t regC = RC(bytecode);
	printf("%-16s call Reg %4d with arg count Reg %4d returning Reg %4d\n", name, regA, regB, regC);
}

static void returnInstruction(const char *name, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t count = RD(bytecode);
	printf("%-16s return %4d - 1 registers starting at %d\n", name, count, reg);
}

void disassembleInstruction(Chunk* chunk, size_t offset) {
	uint32_t bytecode = chunk->code[offset];
	printf("%04zu %08x ", offset, bytecode);

	if(offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4zu ", chunk->lines[offset]);
	}

	switch(OP(bytecode)) {
		case OP_CONST_NUM:
			constantInstruction("OP_CONST_NUM", chunk, bytecode);
			break;
		case OP_PRIMITIVE:
			primitiveInstruction(bytecode);
			break;
		case OP_NEGATE:
			InstructionAD("OP_NEGATE", bytecode);
			break;
		case OP_NOT:
			InstructionAD("OP_NOT", bytecode);
			break;
		case OP_DEFINE_GLOBAL:
			InstructionADstr("OP_DEFINE_GLOBAL", chunk, bytecode);
			break;
		case OP_SET_GLOBAL:
			InstructionADstr("OP_SET_GLOBAL", chunk, bytecode);
			break;
		case OP_GET_GLOBAL:
			InstructionADstr("OP_GET_GLOBAL", chunk, bytecode);
			break;
		case OP_RETURN:
			returnInstruction("OP_RETURN", bytecode);
			return;
		case OP_EQUAL:
			InstructionABC("OP_EQUAL", bytecode);
			break;
		case OP_NEQ:
			InstructionABC("OP_NEQ", bytecode);
			break;
		case OP_GREATER:
			InstructionABC("OP_GREATER", bytecode);
			break;
		case OP_LEQ:
			InstructionABC("OP_LEQ", bytecode);
			break;
		case OP_GEQ:
			InstructionABC("OP_GEQ", bytecode);
			break;
		case OP_LESS:
			InstructionABC("OP_LESS", bytecode);
			break;
		case OP_ADDVV:
			InstructionABC("OP_ADDVV", bytecode);
			break;
		case OP_SUBVV:
			InstructionABC("OP_SUBVV", bytecode);
			break;
		case OP_MULVV:
			InstructionABC("OP_MULVV", bytecode);
			break;
		case OP_DIVVV:
			InstructionABC("OP_DIVVV", bytecode);
			break;
		case OP_PRINT:
			InstructionA("OP_PRINT", bytecode);
			return;
		case OP_JUMP:
			InstructionJ("OP_JUMP", bytecode);
			return;
		case OP_COPY_JUMP_IF_FALSE:
			InstructionAD("OP_COPY_JUMP_IF_FALSE", bytecode);
			return;
		case OP_COPY_JUMP_IF_TRUE:
			InstructionAD("OP_COPY_JUMP_IF_TRUE", bytecode);
			return;
		case OP_JUMP_IF_FALSE:
			InstructionD("OP_JUMP_IF_FALSE", bytecode);
			return;
		case OP_JUMP_IF_TRUE:
			InstructionD("OP_JUMP_IF_TRUE", bytecode);
			return;
		case OP_MOV:
			InstructionAD("OP_MOV", bytecode);
			return;
		case OP_CALL:
			callInstruction("OP_CALL", bytecode);
			return;
		case OP_GET_UPVAL:
			InstructionAD("OP_GET_UPVAL", bytecode);
			return;
		case OP_SET_UPVAL:
			InstructionAD("OP_SET_UPVAL", bytecode);
			return;
		case OP_CLOSURE:
			InstructionAD("OP_CLOSURE", bytecode);
			return;
		case OP_CLOSE_UPVALUES:
			InstructionA("OP_CLOSE_UPVALUES", bytecode);
			break;
		default:
			printf("Unknown opcode %d\n", OP(bytecode));
			return;
	}
}

void disassembleChunk(Chunk* chunk, const char *name) {
	printf("== %s ==\n", name);

	printf(" = Constants\n");
	for(size_t i = 0; i < chunk->constants.count; i++) {
		printf("%03zu\t'", i);
		printValue(chunk->constants.values[i]);
		printf("'\n");
	}
	printf("\n");

	for(size_t offset = 0; offset < chunk->count; offset++) {
		disassembleInstruction(chunk, offset);
	}
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
	printf("frame = ");
	dumpValueArray(vm->frames[vm->frameCount - 1].slots, vm->stackLast, count);
}

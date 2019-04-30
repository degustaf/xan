#include "debug.h"

#include <stdio.h>

static void simpleInstruction(const char *name) {
	printf("%s\n", name);
}

static void constantInstruction(const char *name, Chunk *chunk, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s %4d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("' to register %d\n", reg);
}

static void primitiveInstruction(uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	Value p = getPrimitive(RD(bytecode));
	printf("%-16s '", "OP_PRIMITIVE");
	printValue(p);
	printf("' to register %d\n", reg);
}

static void InstructionAD(const char *name, uint32_t bytecode) {
	uint8_t reg = RA(bytecode);
	uint16_t constant = RD(bytecode);
	printf("%-16s register %4d to register %d\n", name, constant, reg);
}

static void InstructionABC(const char *name, uint32_t bytecode) {
	uint8_t regA = RA(bytecode);
	uint8_t regB = RB(bytecode);
	uint8_t regC = RC(bytecode);
	printf("%-16s Reg %4d Reg %4d -> Reg %4d\n", name, regB, regC, regA);
}

void disassembleInstruction(Chunk* chunk, size_t offset) {
	printf("%04zu ", offset);
	if(offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4zu ", chunk->lines[offset]);
	}

	uint32_t bytecode = chunk->code[offset];
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
		case OP_SET_GLOBAL:
			InstructionAD("OP_SET_GLOBAL", bytecode);
			break;
		case OP_GET_GLOBAL:
			InstructionAD("OP_GET_GLOBAL", bytecode);
			break;
		case OP_DEFINE_GLOBAL:
			InstructionAD("OP_DEFINE_GLOBAL", bytecode);
			break;
		case OP_EQUAL:
			InstructionABC("OP_EQUAL", bytecode);
			break;
		case OP_NEQ:
			InstructionABC("OP_NEQ", bytecode);
			break;
		case OP_GREATER:
			InstructionABC("OP_GREATER", bytecode);
			break;
		case OP_GEQ:
			InstructionABC("OP_GEQ", bytecode);
			break;
		case OP_LESS:
			InstructionABC("OP_LESS", bytecode);
			break;
		case OP_LEQ:
			InstructionABC("OP_LEQ", bytecode);
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
			simpleInstruction("OP_PRINT");
			return;
		case OP_RETURN:
			simpleInstruction("OP_RETURN");
			return;
		default:
			printf("Unknown opcode %d\n", OP(bytecode));
			return;
	}
}

void disassembleChunk(Chunk* chunk, const char *name) {
	printf("== %s ==\n", name);

	for(size_t offset = 0; offset < chunk->count; offset++) {
		disassembleInstruction(chunk, offset);
	}
}
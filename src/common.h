#ifndef XAN_COMMON_H
#define XAN_COMMON_H

#include "xan.h"
#include "arch.h"

#if defined(__GNUC__) || defined(__clang__)
	#define COMPUTED_GOTO
#elif defined(_MSC_VER)
	#undef  COMPUTED_GOTO
#endif
// This isn't ready yet
// #undef  COMPUTED_GOTO

#undef DEBUG_PARSER
#undef DEBUG_EXPRESSION_DESCRIPTION
#undef DEBUG_JUMP_LISTS
#undef DEBUG_PRINT_CODE
#undef DEBUG_TRACE_EXECUTION
#undef DEBUG_STACK_USAGE
#undef DEBUG_UPVALUE_USAGE
#define DEBUG_STRESS_GC
#undef DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)
#define GC_HEAP_GROW_FACTOR 2

#define EXIT_COMPILE_ERROR 65
#define EXIT_RUNTIME_ERROR 70

#define XAN_STATIC_ASSERT(x) extern void assert_##__LINE__(int STATIC_ASSERTION_FAILED[(x)?1:-1])

#endif /* XAN_COMMON_H */

#ifndef XAN_COMMON_H
#define XAN_COMMON_H

#include "arch.h"

#undef DEBUG_PARSER
#undef DEBUG_PRINT_CODE
#undef DEBUG_TRACE_EXECUTION

#define UINT8_COUNT (UINT8_MAX + 1)

#define XAN_STATIC_ASSERT(x) extern void assert_##__LINE__(int STATIC_ASSERTION_FAILED[(x)?1:-1])

#endif /* XAN_COMMON_H */

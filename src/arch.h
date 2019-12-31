#ifndef XAN_ARCH_H
#define XAN_ARCH_H

#define XAN_LITTLE_ENDIAN	0
#define XAN_BIG_ENDIAN		1

#if defined(__x86_64__)
	#define X64_ARCH
	#define ENDIAN	XAN_LITTLE_ENDIAN
#elif defined(__i386__)
	#define X86_ARCH
	#define ENDIAN	XAN_LITTLE_ENDIAN
#else
	#error "Unsupported Architecture."
#endif

// #if defined(__linux__)
// 	#define OS_LINUX
// #else
// 	#error "Unsupported OS."
// #endif

#if ENDIAN == XAN_BIG_ENDIAN
	#define ENDIAN_SELECT(le, be)	be
	#define ENDIAN_LOHI(lo, hi)		hi lo
#else
	#define ENDIAN_SELECT(le, be)	le
	#define ENDIAN_LOHI(lo, hi)		lo hi
#endif

#endif /* XAN_ARCH_H */

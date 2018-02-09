#ifndef __PT_HELPERS_H
#define __PT_HELPERS_H

#include <stdint.h>
#include <math.h>
#include "pt_header.h"

#ifdef _MSC_VER
#define inline __forceinline
#endif

#if defined (_WIN32) && defined (_DEBUG)
#define PT_ASSERT(X) if (!(X)) __debugbreak()
#else
#define PT_ASSERT(X)
#endif

#ifndef true
#define true 1
#define false 0
#endif

#define SWAP16(value) \
( \
    (((uint16_t)((value) & 0x00FF)) << 8) | \
    (((uint16_t)((value) & 0xFF00)) >> 8)   \
)

#define SWAP32(value) \
( \
    (((uint32_t)((value) & 0x000000FF)) << 24) | \
    (((uint32_t)((value) & 0x0000FF00)) <<  8) | \
    (((uint32_t)((value) & 0x00FF0000)) >>  8) | \
    (((uint32_t)((value) & 0xFF000000)) >> 24)   \
)

/* arithmetic shift right on signed variables macros (works properly on all platforms, though not branchless...) */
#if defined (__APPLE__) || defined (_WIN32)
#define  SAR8(x, n) ((x) >> (n))
#define SAR16(x, n) ((x) >> (n))
#define SAR32_1(x)  ((x) >>  1 )
#else
#define  SAR8(x, n) ((x) >= 0) ? ((x) >> (n)) : (((1 <<  8) - (1 << (8  - (n)))) | ((x) >> (n)))
#define SAR16(x, n) ((x) >= 0) ? ((x) >> (n)) : (((1 << 16) - (1 << (16 - (n)))) | ((x) >> (n)))
#define SAR32_1(x)  ((x) >= 0) ? ((x) >>  1 ) : (0x80000000 | ((x) >> 1))
#endif

#define SGN(x) (((x) >= 0) ? 1 : -1)
#define ABS(a) (((a) < 0) ? -(a) : (a))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#define LERP(x, y, z) ((x) + ((y) - (x)) * (z))

#define RGB_R(x) (((x) & 0x00FF0000) >> 16)
#define RGB_G(x) (((x) & 0x0000FF00) >>  8)
#define RGB_B(x) (((x) & 0x000000FF) >>  0)

#define TO_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

void showErrorMsgBox(const char *fmt, ...);

int32_t my_strnicmp(const char *s1, const char *s2, size_t n);

#ifndef _WIN32
int8_t changePathToHome(void);
#endif
int8_t sampleNameIsEmpty(char *name);
int8_t moduleNameIsEmpty(char *name);
void updateWindowTitle(int8_t modified);
void recalcChordLength(void);
uint8_t hexToInteger2(char *ptr);

#endif

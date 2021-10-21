#ifndef __LIB_KERNEL_FIXED_POINT_H
#define __LIB_KERNEL_FIXED_POINT_H

/* A fixed point arithmetic library.
 *
 * Decimal place is situated after position (BINARY_POINT)
 *         <------(>0)----->.<----(<0)---->
 *
 * Default BINARY POINT is 14 (for a 17-14 format, 1<<14 = 0x2000), though this
 * can be altered easily.
 */

#include <stdint.h>

#define BINARY_POINT 0x2000

typedef struct fixed32 {
	int32_t val;
} fixed32;

inline fixed32 int_to_fixed(int32_t n)
{
	return (fixed32){ .val = n * BINARY_POINT };
}

inline int32_t fixed_to_int_floor(fixed32 x)
{
	return x.val / BINARY_POINT;
}

inline int32_t fixed_to_int_round(fixed32 x)
{
	if (x.val >= 0)
		return (x.val + (BINARY_POINT / 2)) / BINARY_POINT;
	else
		return (x.val - (BINARY_POINT / 2)) / BINARY_POINT;
}

inline fixed32 add(fixed32 x, fixed32 y)
{
	return (fixed32){ .val = x.val + y.val };
}

inline fixed32 add_f_i(fixed32 x, int32_t n)
{
	return (fixed32){ .val = x.val + (n * BINARY_POINT) };
}

inline fixed32 sub(fixed32 x, fixed32 y)
{
	return (fixed32){ .val = x.val - y.val };
}

inline fixed32 sub_f_i(fixed32 x, int32_t n)
{
	return (fixed32){ .val = x.val - (n * BINARY_POINT) };
}

inline fixed32 sub_i_f(int32_t n, fixed32 x)
{
	return (fixed32){ .val = (n * BINARY_POINT) - x.val };
}

inline fixed32 mult(fixed32 x, fixed32 y)
{
	return (fixed32){ .val = (int32_t)(((int64_t)x.val * y.val) / BINARY_POINT) };
}

inline fixed32 mult_f_i(fixed32 x, int32_t n)
{
	return (fixed32){ .val = x.val * n };
}

inline fixed32 div(fixed32 x, fixed32 y)
{
	return (fixed32){ .val = (int32_t)(((int64_t)x.val * BINARY_POINT) / y.val) };
}

inline fixed32 div_f_i(fixed32 x, int32_t n)
{
	return (fixed32){ .val = x.val / n };
}

inline fixed32 div_i_f(int32_t n, fixed32 x)
{
	return div(int_to_fixed(n), x);
}

#endif

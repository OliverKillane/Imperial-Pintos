#include <stdint.h>

static const int32_t BINARY_POINT = 1 << 14;

const int32_t PRI_MAX = 63;

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

inline int32_t calc_load_avg(int32_t old_load_avg, int32_t ready_threads)
{
	// Preparing constants used in this function
	fixed32 fifty_nine = int_to_fixed(59);
	fixed32 sixty = int_to_fixed(60);
	fixed32 one = int_to_fixed(1);
	fixed32 fifty_nine_sixtieths = div(fifty_nine, sixty);
	fixed32 one_sixtieth = div(one, sixty);

	// calculating (59/60) * old_load_avg
	fixed32 old_load_weighted = mult_f_i(fifty_nine_sixtieths, old_load_avg);
	// calculating (1/60) * ready_threads
	fixed32 new_load_weighted = mult_f_i(one_sixtieth, ready_threads);
	// calculating (59/60) * old_load_avg + (1/60) * ready_threads
	fixed32 new_load_avg = add(old_load_weighted, new_load_weighted);
	return fixed_to_int_round(new_load_avg);
}

inline int32_t calc_priority(int32_t recent_cpu, int32_t nice)
{
	// calculating recent_cpu / 4
	fixed32 one_fourth_recent_cpu = div_f_i(int_to_fixed(recent_cpu), 4);
	// calculating (recent_cpu / 4) - (nice * 2)
	fixed32 inverse_priority = sub_f_i(one_fourth_recent_cpu, nice * 2);
	// calculating PRI_MAX - (recent_cpu / 4) - (nice * 2)
	fixed32 new_priority = sub_i_f(PRI_MAX, inverse_priority);
	return fixed_to_int_round(new_priority);
}

inline int32_t calc_recent_cpu(int32_t load_avg, int32_t recent_cpu,
															 int32_t nice)
{
	// calculating 2 * load_avg
	int32_t twice_load_avg = 2 * load_avg;
	// calculating 2 * load_avg + 1
	fixed32 twice_load_avg_and_one = int_to_fixed(twice_load_avg + 1);
	// calculating (2 * load_avg) / (2 * load_avg + 1)
	fixed32 load_avg_ratio = div_i_f(twice_load_avg, twice_load_avg_and_one);

	// calculating (2 * load_avg) / (2 * load_avg + 1) * recent_cpu
	fixed32 discounted_recent_cpu = mult_f_i(load_avg_ratio, recent_cpu);

	// calculating (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
	fixed32 new_recent_cpu = add_f_i(discounted_recent_cpu, nice);
	return fixed_to_int_round(new_recent_cpu);
}

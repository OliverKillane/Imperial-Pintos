/* Verifies that lowering a thread's priority so that it is no
   longer the highest-priority thread in the system causes it to
   yield immediately. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include <fixed-point.h>
#include <stdbool.h>

bool
fixed_equal (fixed32 a, fixed32 b) {
    return a.val == b.val;
}

void
test_fixed_point (void) 
{
    add_test();
    sub_test();
    fixed_to_int_test();
    mult_test();
    pass();
}

void
sub_test (void) {
    fixed32 HALF = div_i_f (1, int_to_fixed(2));
    fixed32 SEVEN = int_to_fixed (7);
    fixed32 FIVE = int_to_fixed (5);
    // test that 5.0 - 7.0 == -2.0
    ASSERT (fixed_equal (sub (FIVE,  SEVEN), int_to_fixed (-2)));

    // test that 5.0 - (-7.0) == 12.0
    ASSERT (fixed_equal (sub (FIVE,  int_to_fixed (-7)), int_to_fixed (12)));

    // test that 0.5 - 0.5 = 1
    ASSERT (fixed_equal (sub (HALF, HALF), int_to_fixed (0)));

    // test that 0.5 - 1 = 0.5 - 1.0
    ASSERT (fixed_equal (sub_f_i (HALF, 1), sub (HALF, int_to_fixed (1))));

    // test that 1 - 0.5 = 1.0 - 0.5
    ASSERT (fixed_equal (sub_i_f (1, HALF), sub (int_to_fixed (1), HALF)));
}

void 
fixed_to_int_test (void) {
    fixed32 HALF = div_i_f (1, int_to_fixed(2));
    fixed32 SEVEN = int_to_fixed (7);
    fixed32 FIVE = int_to_fixed (5);
    // test that round(0.5 + 7) == 8
    ASSERT (fixed_to_int_round ( add(SEVEN, HALF)) == 8);

    // test that floor(0.5 + 7) == 7
    ASSERT (fixed_to_int_floor ( add(SEVEN, HALF)) == 7);
}

void
mult_test (void)
{
    fixed32 HALF = div_i_f (1, int_to_fixed(2));
    fixed32 QUARTER = div_i_f (1, int_to_fixed(4));
    fixed32 SEVEN = int_to_fixed (7);
    fixed32 FIVE = int_to_fixed (5);
    // test that 5.0 * 7.0 == 35.0
    ASSERT (fixed_equal (mult (FIVE,  SEVEN), int_to_fixed (35)));
    
    // test that 5.0 * (-7.0) == -35.0
    ASSERT (fixed_equal (mult (FIVE, int_to_fixed (-7)), int_to_fixed (-35)));

    // test that 0.5 * 0.5 = 0.25
    ASSERT (fixed_equal (mult (HALF, HALF), QUARTER));

    // test that 0.5 * 1 = 0.5 * 1.0
    ASSERT (fixed_equal (mult_f_i (HALF, 1), mult (HALF, int_to_fixed (1))));
}

void
add_test (void)
{
    fixed32 HALF = div_i_f (1, int_to_fixed(2));
    fixed32 SEVEN = int_to_fixed (7);
    fixed32 FIVE = int_to_fixed (5);
    // test that 5.0 + 7.0 == 12.0
    ASSERT (fixed_equal (add (FIVE,  SEVEN), int_to_fixed (12)));
    
    // test that 5.0 + (-7.0) == -2.0
    ASSERT (fixed_equal (add (FIVE, int_to_fixed (-7)), int_to_fixed (-2)));

    // test that 0.5 + 0.5 = 1
    ASSERT (fixed_equal (add (HALF, HALF), int_to_fixed (1)));

    // test that 0.5 + 1 = 0.5 + 1.0
    ASSERT (fixed_equal (add_f_i (HALF, 1), add (HALF, int_to_fixed (1))));
}
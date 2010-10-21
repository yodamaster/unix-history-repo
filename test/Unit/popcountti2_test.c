//===-- popcountti2_test.c - Test __popcountti2 ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file tests __popcountti2 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#if __x86_64

#include "int_lib.h"
#include <stdio.h>
#include <stdlib.h>

// Returns: count of 1 bits

si_int __popcountti2(ti_int a);

int naive_popcount(ti_int a)
{
    int r = 0;
    for (; a; a = (tu_int)a >> 1)
        r += a & 1;
    return r;
}

int test__popcountti2(ti_int a)
{
    si_int x = __popcountti2(a);
    si_int expected = naive_popcount(a);
    if (x != expected)
    {
        twords at;
        at.all = a;
        printf("error in __popcountti2(0x%.16llX%.16llX) = %d, expected %d\n",
               at.s.high, at.s.low, x, expected);
    }
    return x != expected;
}

char assumption_1[sizeof(ti_int) == 2*sizeof(di_int)] = {0};
char assumption_2[sizeof(di_int)*CHAR_BIT == 64] = {0};

#endif

int main()
{
#if __x86_64
    if (test__popcountti2(0))
        return 1;
    if (test__popcountti2(1))
        return 1;
    if (test__popcountti2(2))
        return 1;
    if (test__popcountti2(0xFFFFFFFFFFFFFFFDLL))
        return 1;
    if (test__popcountti2(0xFFFFFFFFFFFFFFFELL))
        return 1;
    if (test__popcountti2(0xFFFFFFFFFFFFFFFFLL))
        return 1;
    if (test__popcountti2(make_ti(0xFFFFFFFFFFFFFFFFLL, 0xFFFFFFFFFFFFFFFDLL)))
        return 1;
    if (test__popcountti2(make_ti(0xFFFFFFFFFFFFFFFFLL, 0xFFFFFFFFFFFFFFFELL)))
        return 1;
    if (test__popcountti2(make_ti(0xFFFFFFFFFFFFFFFFLL, 0xFFFFFFFFFFFFFFFFLL)))
        return 1;
    int i;
    for (i = 0; i < 10000; ++i)
        if (test__popcountti2(((ti_int)rand() << 96) | ((ti_int)rand() << 64) |
                              ((ti_int)rand() << 32) | rand()))
            return 1;

#endif
   return 0;
}

//===-- floatunssidfvfp_test.c - Test __floatunssidfvfp -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file tests __floatunssidfvfp for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>


extern double __floatunssidfvfp(unsigned int a);

#if __arm__
int test__floatunssidfvfp(unsigned int a)
{
    double actual = __floatunssidfvfp(a);
    double expected = a;
    if (actual != expected)
        printf("error in test__floatunssidfvfp(%u) = %f, expected %f\n",
               a, actual, expected);
    return actual != expected;
}
#endif

int main()
{
#if __arm__
    if (test__floatunssidfvfp(0))
        return 1;
    if (test__floatunssidfvfp(1))
        return 1;
    if (test__floatunssidfvfp(0x7FFFFFFF))
        return 1;
    if (test__floatunssidfvfp(0x80000000))
        return 1;
    if (test__floatunssidfvfp(0xFFFFFFFF))
        return 1;
#endif
    return 0;
}

//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.

//  -*- mode:C; c-basic-offset:4; tab-width:4; intent-tabs-mode:nil;  -*-
// CONFIG error: incompatible block pointer types assigning

#import <stdio.h>
#import <stdlib.h>

int main(int argc, char *argv[]) {
#ifndef __cplusplus
    char (^rot13)();
    rot13 = ^(char c) { return (char)(((c - 'a' + 13) % 26) + 'a'); };
    char n = rot13('a');
    char c = rot13('p');
    if ( n != 'n' || c != 'c' ) {
        printf("%s: rot13('a') returned %c, rot13('p') returns %c\n", argv[0], n, c);
        exit(1);
    }
#else
// yield characteristic error message for C++
#error incompatible block pointer types assigning
#endif
#ifndef __clang__
// yield characteristic error message for C++
#error incompatible block pointer types assigning
#endif
    printf("%s: success\n", argv[0]);
    exit(0);
}

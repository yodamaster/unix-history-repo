/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ansi.h	8.2 (Berkeley) 1/4/94
 * $FreeBSD$
 */

#ifndef	_MACHINE_ANSI_H_
#define	_MACHINE_ANSI_H_

/*
 * Types which are fundamental to the implementation and may appear in
 * more than one standard header are defined here.  Standard headers
 * then use:
 *	#ifdef	_BSD_SIZE_T_
 *	typedef	_BSD_SIZE_T_	size_t;
 *	#undef	_BSD_SIZE_T_
 *	#endif
 */
#define	_BSD_CLOCK_T_	unsigned long		/* clock()... */
#define	_BSD_CLOCKID_T_	int			/* clock_gettime()... */
#define	_BSD_FFLAGS_T_	__uint_least32_t	/* file flags */
#define	_BSD_MBSTATE_T_	__mbstate_t		/* mbstate_t */
#define	_BSD_PTRDIFF_T_	int			/* ptr1 - ptr2 */
#define	_BSD_RUNE_T_	_BSD_CT_RUNE_T_		/* rune_t (see below) */
#define	_BSD_SEGSZ_T_	__int32_t		/* segment size (in pages) */
#define	_BSD_SIZE_T_	unsigned int		/* sizeof() */
#define	_BSD_SOCKLEN_T_	__uint32_t		/* socklen_t (duh) */
#define	_BSD_SSIZE_T_	int			/* byte count or error */
#define	_BSD_TIME_T_	int			/* time()... */
#define	_BSD_TIMER_T_	int			/* timer_gettime()... */
#define	_BSD_WCHAR_T_	_BSD_CT_RUNE_T_		/* wchar_t (see below) */
#define	_BSD_WINT_T_	_BSD_CT_RUNE_T_		/* wint_t (see below) */

/*
 * Types which are fundamental to the implementation and must be used
 * in more than one standard header although they are only declared in
 * one (perhaps nonstandard) header are defined here.  Standard headers
 * use _BSD_XXX_T_ without undef'ing it.
 */
#define	_BSD_CT_RUNE_T_	int			/* arg type for ctype funcs */
#define	_BSD_OFF_T_	__int64_t		/* file offset */
#define	_BSD_PID_T_	int			/* process [group] */

#if defined(__GNUC__) && (__GNUC__ > 2 || __GNUC__ == 2 && __GNUC_MINOR__ > 95)
#define	_BSD_VA_LIST_	__builtin_va_list	/* internally known to gcc */
#else
#define	_BSD_VA_LIST_	char *			/* va_list */
#endif /* post GCC 2.95 */
#if defined __GNUC__ && !defined(__GNUC_VA_LIST) && !defined(__NO_GNUC_VA_LIST)
#define __GNUC_VA_LIST
typedef _BSD_VA_LIST_ __gnuc_va_list;		/* compatibility w/GNU headers*/
#endif

/*
 * The rune type above is declared to be an ``int'' instead of the more natural
 * ``unsigned long'' or ``long''.  Two things are happening here.  It is not
 * unsigned so that EOF (-1) can be naturally assigned to it and used.  Also,
 * it looks like 10646 will be a 31 bit standard.  This means that if your
 * ints cannot hold 32 bits, you will be in trouble.  The reason an int was
 * chosen over a long is that the is*() and to*() routines take ints (says
 * ANSI C), but they use _BSD_CT_RUNE_T_ instead of int.  By changing it
 * here, you lose a bit of ANSI conformance, but your programs will still
 * work.
 *
 * NOTE: rune_t is not covered by ANSI nor other standards, and should not
 * be instantiated outside of lib/libc/locale.  Use wchar_t.  wchar_t and
 * rune_t must be the same type.  Also wint_t must be no narrower than
 * wchar_t, and should also be able to hold all members of the largest
 * character set plus one extra value (WEOF). wint_t must be at least 16 bits.
 */

/*
 * Frequencies of the clock ticks reported by clock() and times().  They
 * are the same as stathz for bogus historical reasons.  They should be
 * 1e6 because clock() and times() are implemented using getrusage() and
 * there is no good reason why they should be less accurate.  There is
 * the bad reason that (broken) programs might not like clock_t or
 * CLOCKS_PER_SEC being ``double'' (``unsigned long'' is not large enough
 * to hold the required 24 hours worth of ticks if the frequency is
 * 1000000ul, and ``unsigned long long'' would be nonstandard).
 */
#define	_BSD_CLK_TCK_		128
#define	_BSD_CLOCKS_PER_SEC_	128

/*
 * We define this here since both <stddef.h> and <sys/types.h> needs it.
 */
#define __offsetof(type, field) ((size_t)(&((type *)0)->field))

/*
 * XXX this paragraph is very out of date.
 * Typedefs for especially magic types.  #define's wouldn't work in the
 * __GNUC__ case, since __attribute__(()) only works in certain contexts.
 * This is not in <machine/types.h>, since that has too much namespace
 * pollution for inclusion in ANSI headers, yet we need __int64_t in at
 * least <stdio.h>.
 */
#ifdef __GNUC__
typedef	int __attribute__((__mode__(__DI__)))		__int64_t;
typedef	unsigned int __attribute__((__mode__(__DI__)))	__uint64_t;
#else
/* LONGLONG */
typedef	long long					__int64_t;
/* LONGLONG */
typedef	unsigned long long				__uint64_t;
#endif
/*
 * Internal names for basic integral types.  Omit the typedef if
 * not possible for a machine/compiler combination.
 */
typedef	__signed char		__int8_t;
typedef	unsigned char		__uint8_t;
typedef	short			__int16_t;
typedef	unsigned short		__uint16_t;
typedef	int			__int32_t;
typedef	unsigned int		__uint32_t;

typedef	int			__intptr_t;
typedef	unsigned int		__uintptr_t;

typedef	__signed char		__int_least8_t;
typedef	unsigned char		__uint_least8_t;
typedef	short			__int_least16_t;
typedef	unsigned short		__uint_least16_t;
typedef	int			__int_least32_t;
typedef	unsigned int		__uint_least32_t;
typedef	__int64_t		__int_least64_t;
typedef	__uint64_t		__uint_least64_t;

typedef	int			__int_fast8_t;
typedef	unsigned int		__uint_fast8_t;
typedef	int			__int_fast16_t;
typedef	unsigned int		__uint_fast16_t;
typedef	int			__int_fast32_t;
typedef	unsigned int		__uint_fast32_t;
typedef	__int64_t		__int_fast64_t;
typedef	__uint64_t		__uint_fast64_t;

typedef	__int64_t		__intmax_t;
typedef	__uint64_t		__uintmax_t;

#define	__INT8_C(c)		(c)
#define	__INT16_C(c)		(c)
#define	__INT32_C(c)		(c)
#define	__INT64_C(c)		(c ## LL)

#define	__UINT8_C(c)		(c)
#define	__UINT16_C(c)		(c)
#define	__UINT32_C(c)		(c ## U)
#define	__UINT64_C(c)		(c ## ULL)

#define	__INTMAX_C(c)		(c ## LL)
#define	__UINTMAX_C(c)		(c ## ULL)

/*
 * mbstate_t is an opaque object to keep conversion state, during multibyte
 * stream conversions.  The content must not be referenced by user programs.
 */
typedef union {
	char		__mbstate8[128];
	__int64_t	_mbstateL;		/* for alignment */
} __mbstate_t;

#endif	/* !_MACHINE_ANSI_H_ */

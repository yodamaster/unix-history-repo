/*-
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CPUFUNC_H_
#define _MACHINE_CPUFUNC_H_

#ifdef _KERNEL

#include <sys/types.h>
#include <machine/chipset.h>
#include <machine/alpha_cpu.h>

#ifdef __GNUC__

static __inline void
breakpoint(void)
{
	__asm __volatile("call_pal 0x81"); /* XXX bugchk */
}

#endif

/*
 * Bogus interrupt manipulation
 */
static __inline void
disable_intr(void)
{
	alpha_pal_swpipl(ALPHA_PSL_IPL_HIGH);
}

static __inline void
enable_intr(void)
{
	alpha_pal_swpipl(ALPHA_PSL_IPL_0);
}

static __inline u_int
save_intr(void)
{
	return alpha_pal_rdps() & ALPHA_PSL_IPL_MASK;
}

static __inline void
restore_intr(u_int ipl)
{
	alpha_pal_swpipl(ipl);
}

#endif /* _KERNEL */

#endif /* !_MACHINE_CPUFUNC_H_ */

/*-
 * Copyright (c) 1999 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_SIGFRAME_H_
#define _MACHINE_SIGFRAME_H_ 1

struct	osigframe {
	/*
	 * The first three members may be used by applications.
	 */

	register_t	sf_signum;

	/*
	 * Either 'int' for old-style FreeBSD handler or 'siginfo_t *'
	 * pointing to sf_siginfo for SA_SIGINFO handlers.
	 */
	register_t	sf_arg2;

	/* Points to sf_siginfo.si_sc. */
	register_t	sf_scp;

	/*
	 * The following arguments are not constrained by the
	 * function call protocol.
	 * Applications are not supposed to access these members,
	 * except using the pointers we provide in the first three
	 * arguments.
	 */

	char		*sf_addr;

	union {
		__osiginfohandler_t	*sf_action;
		__sighandler_t		*sf_handler;
	} sf_ahu;

	/* In the SA_SIGINFO case, sf_arg2 points here. */
	osiginfo_t	sf_siginfo;
};

struct sigframe {
	/*
	 * The first three members may be used by applications.
	 */
	register_t	sf_signum;
	register_t	sf_siginfo;		/* code or pointer to sf_si */
	register_t	sf_ucontext;		/* points to sf_uc */
	register_t	__spare__;		/* Align sf_ahu */
	union {
		__siginfohandler_t	*sf_action;
		__sighandler_t		*sf_handler;
	} sf_ahu;
	register_t	sf_sigreturn;	/* sigreturn to use */
	siginfo_t	sf_si;		/* = *sf_siginfo (SA_SIGINFO case) */
	ucontext_t	sf_uc;		/* = *sf_ucontext */
};

#endif /* _MACHINE_SIGFRAME_H_ */

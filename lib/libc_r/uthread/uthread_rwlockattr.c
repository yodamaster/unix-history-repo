/*-
 * Copyright (c) 1998 Alex Nash
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

#ifdef _THREAD_SAFE
#include <errno.h>
#include <stdlib.h>

#include <pthread.h>
#include "pthread_private.h"

int
pthread_rwlockattr_destroy (pthread_rwlockattr_t *rwlockattr)
{
	pthread_rwlockattr_t prwlockattr;

	if (rwlockattr == NULL)
		return(EINVAL);

	prwlockattr = *rwlockattr;

	if (prwlockattr == NULL)
		return(EINVAL);

	free(prwlockattr);

	return(0);
}

int
pthread_rwlockattr_getpshared (const pthread_rwlockattr_t *rwlockattr,
	int *pshared)
{
	*pshared = (*rwlockattr)->pshared;

	return(0);
}

int
pthread_rwlockattr_init (pthread_rwlockattr_t *rwlockattr)
{
	pthread_rwlockattr_t prwlockattr;

	if (rwlockattr == NULL)
		return(EINVAL);

	prwlockattr = (pthread_rwlockattr_t)
		malloc(sizeof(struct pthread_rwlockattr));

	if (prwlockattr == NULL)
		return(ENOMEM);

	prwlockattr->pshared 	= PTHREAD_PROCESS_PRIVATE;
	*rwlockattr		= prwlockattr;

	return(0);
}

int
pthread_rwlockattr_setpshared (pthread_rwlockattr_t *rwlockattr,
	int *pshared)
{
	int ps = *pshared;

	/* only PTHREAD_PROCESS_PRIVATE is supported */
	if (ps != PTHREAD_PROCESS_PRIVATE)
		return(EINVAL);

	(*rwlockattr)->pshared = ps;

	return(0);
}

#endif /* _THREAD_SAFE */

/*-
 * Copyright (c) 2004, David Xu <davidxu@freebsd.org>
 * Copyright (c) 2002, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/eventhandler.h>
#include <sys/umtx.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#define TYPE_SIMPLE_LOCK	0
#define TYPE_SIMPLE_WAIT	1
#define TYPE_NORMAL_UMUTEX	2
#define TYPE_PI_UMUTEX		3
#define TYPE_PP_UMUTEX		4
#define TYPE_CV			5	

/* Key to represent a unique userland synchronous object */
struct umtx_key {
	int	hash;
	int	type;
	int	shared;
	union {
		struct {
			vm_object_t	object;
			uintptr_t	offset;
		} shared;
		struct {
			struct vmspace	*vs;
			uintptr_t	addr;
		} private;
		struct {
			void		*a;
			uintptr_t	b;
		} both;
	} info;
};

/* Priority inheritance mutex info. */
struct umtx_pi {
	/* Owner thread */
	struct thread		*pi_owner;

	/* Reference count */
	int			pi_refcount;

 	/* List entry to link umtx holding by thread */
	TAILQ_ENTRY(umtx_pi)	pi_link;

	/* List entry in hash */
	TAILQ_ENTRY(umtx_pi)	pi_hashlink;

	/* List for waiters */
	TAILQ_HEAD(,umtx_q)	pi_blocked;

	/* Identify a userland lock object */
	struct umtx_key		pi_key;
};

/* A userland synchronous object user. */
struct umtx_q {
	/* Linked list for the hash. */
	TAILQ_ENTRY(umtx_q)	uq_link;

	/* Umtx key. */
	struct umtx_key		uq_key;

	/* Umtx flags. */
	int			uq_flags;
#define UQF_UMTXQ	0x0001

	/* The thread waits on. */
	struct thread		*uq_thread;

	/*
	 * Blocked on PI mutex. read can use chain lock
	 * or sched_lock, write must have both chain lock and
	 * sched_lock being hold.
	 */
	struct umtx_pi		*uq_pi_blocked;

	/* On blocked list */
	TAILQ_ENTRY(umtx_q)	uq_lockq;

	/* Thread contending with us */
	TAILQ_HEAD(,umtx_pi)	uq_pi_contested;

	/* Inherited priority from PP mutex */
	u_char			uq_inherited_pri;
};

TAILQ_HEAD(umtxq_head, umtx_q);

/* Userland lock object's wait-queue chain */
struct umtxq_chain {
	/* Lock for this chain. */
	struct mtx		uc_lock;

	/* List of sleep queues. */
	struct umtxq_head	uc_queue;

	/* Busy flag */
	char			uc_busy;

	/* Chain lock waiters */
	int			uc_waiters;

	/* All PI in the list */
	TAILQ_HEAD(,umtx_pi)	uc_pi_list;
};

#define	UMTXQ_LOCKED_ASSERT(uc)		mtx_assert(&(uc)->uc_lock, MA_OWNED)

/*
 * Don't propagate time-sharing priority, there is a security reason,
 * a user can simply introduce PI-mutex, let thread A lock the mutex,
 * and let another thread B block on the mutex, because B is
 * sleeping, its priority will be boosted, this causes A's priority to
 * be boosted via priority propagating too and will never be lowered even
 * if it is using 100%CPU, this is unfair to other processes.
 */

#define UPRI(td)	(((td)->td_ksegrp->kg_user_pri >= PRI_MIN_TIMESHARE &&\
			  (td)->td_ksegrp->kg_user_pri <= PRI_MAX_TIMESHARE) ?\
			 PRI_MAX_TIMESHARE : (td)->td_ksegrp->kg_user_pri)

#define	GOLDEN_RATIO_PRIME	2654404609U
#define	UMTX_CHAINS		128
#define	UMTX_SHIFTS		(__WORD_BIT - 7)

#define THREAD_SHARE		0
#define PROCESS_SHARE		1
#define AUTO_SHARE		2

#define	GET_SHARE(flags)	\
    (((flags) & USYNC_PROCESS_SHARED) == 0 ? THREAD_SHARE : PROCESS_SHARE)

static uma_zone_t		umtx_pi_zone;
static struct umtxq_chain	umtxq_chains[UMTX_CHAINS];
static MALLOC_DEFINE(M_UMTX, "umtx", "UMTX queue memory");
static int			umtx_pi_allocated;

SYSCTL_NODE(_debug, OID_AUTO, umtx, CTLFLAG_RW, 0, "umtx debug");
SYSCTL_INT(_debug_umtx, OID_AUTO, umtx_pi_allocated, CTLFLAG_RD,
    &umtx_pi_allocated, 0, "Allocated umtx_pi");

static void umtxq_sysinit(void *);
static void umtxq_hash(struct umtx_key *key);
static struct umtxq_chain *umtxq_getchain(struct umtx_key *key);
static void umtxq_lock(struct umtx_key *key);
static void umtxq_unlock(struct umtx_key *key);
static void umtxq_busy(struct umtx_key *key);
static void umtxq_unbusy(struct umtx_key *key);
static void umtxq_insert(struct umtx_q *uq);
static void umtxq_remove(struct umtx_q *uq);
static int umtxq_sleep(struct umtx_q *uq, const char *wmesg, int timo);
static int umtxq_count(struct umtx_key *key);
static int umtxq_signal(struct umtx_key *key, int nr_wakeup);
static int umtx_key_match(const struct umtx_key *k1, const struct umtx_key *k2);
static int umtx_key_get(void *addr, int type, int share,
	struct umtx_key *key);
static void umtx_key_release(struct umtx_key *key);
static struct umtx_pi *umtx_pi_alloc(void);
static void umtx_pi_free(struct umtx_pi *pi);
static int do_unlock_pp(struct thread *td, struct umutex *m, uint32_t flags);
static void umtx_thread_cleanup(struct thread *td);
static void umtx_exec_hook(void *arg __unused, struct proc *p __unused,
	struct image_params *imgp __unused);
SYSINIT(umtx, SI_SUB_EVENTHANDLER+1, SI_ORDER_MIDDLE, umtxq_sysinit, NULL);

static void
umtxq_sysinit(void *arg __unused)
{
	int i;

	umtx_pi_zone = uma_zcreate("umtx pi", sizeof(struct umtx_pi),
		NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	for (i = 0; i < UMTX_CHAINS; ++i) {
		mtx_init(&umtxq_chains[i].uc_lock, "umtxql", NULL,
			 MTX_DEF | MTX_DUPOK);
		TAILQ_INIT(&umtxq_chains[i].uc_queue);
		TAILQ_INIT(&umtxq_chains[i].uc_pi_list);
		umtxq_chains[i].uc_busy = 0;
		umtxq_chains[i].uc_waiters = 0;
	}
	EVENTHANDLER_REGISTER(process_exec, umtx_exec_hook, NULL,
	    EVENTHANDLER_PRI_ANY);
}

struct umtx_q *
umtxq_alloc(void)
{
	struct umtx_q *uq;

	uq = malloc(sizeof(struct umtx_q), M_UMTX, M_WAITOK | M_ZERO);
	TAILQ_INIT(&uq->uq_pi_contested);
	uq->uq_inherited_pri = PRI_MAX;
	return (uq);
}

void
umtxq_free(struct umtx_q *uq)
{
	free(uq, M_UMTX);
}

static inline void
umtxq_hash(struct umtx_key *key)
{
	unsigned n = (uintptr_t)key->info.both.a + key->info.both.b;
	key->hash = ((n * GOLDEN_RATIO_PRIME) >> UMTX_SHIFTS) % UMTX_CHAINS;
}

static inline int
umtx_key_match(const struct umtx_key *k1, const struct umtx_key *k2)
{
	return (k1->type == k2->type &&
		k1->info.both.a == k2->info.both.a &&
	        k1->info.both.b == k2->info.both.b);
}

static inline struct umtxq_chain *
umtxq_getchain(struct umtx_key *key)
{
	return (&umtxq_chains[key->hash]);
}

/*
 * Set chain to busy state when following operation
 * may be blocked (kernel mutex can not be used).
 */
static inline void
umtxq_busy(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_assert(&uc->uc_lock, MA_OWNED);
	while (uc->uc_busy != 0) {
		uc->uc_waiters++;
		msleep(uc, &uc->uc_lock, 0, "umtxqb", 0);
		uc->uc_waiters--;
	}
	uc->uc_busy = 1;
}

/*
 * Unbusy a chain.
 */
static inline void
umtxq_unbusy(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_assert(&uc->uc_lock, MA_OWNED);
	KASSERT(uc->uc_busy != 0, ("not busy"));
	uc->uc_busy = 0;
	if (uc->uc_waiters)
		wakeup_one(uc);
}

/*
 * Lock a chain.
 */
static inline void
umtxq_lock(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_lock(&uc->uc_lock);
}

/*
 * Unlock a chain.
 */
static inline void
umtxq_unlock(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_unlock(&uc->uc_lock);
}

/*
 * Insert a thread onto the umtx queue.
 */
static inline void
umtxq_insert(struct umtx_q *uq)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_INSERT_TAIL(&uc->uc_queue, uq, uq_link);
	uq->uq_flags |= UQF_UMTXQ;
}

/*
 * Remove thread from the umtx queue.
 */
static inline void
umtxq_remove(struct umtx_q *uq)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	if (uq->uq_flags & UQF_UMTXQ) {
		TAILQ_REMOVE(&uc->uc_queue, uq, uq_link);
		uq->uq_flags &= ~UQF_UMTXQ;
	}
}

/*
 * Check if there are multiple waiters
 */
static int
umtxq_count(struct umtx_key *key)
{
	struct umtxq_chain *uc;
	struct umtx_q *uq;
	int count = 0;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_FOREACH(uq, &uc->uc_queue, uq_link) {
		if (umtx_key_match(&uq->uq_key, key)) {
			if (++count > 1)
				break;
		}
	}
	return (count);
}

/*
 * Check if there are multiple PI waiters and returns first
 * waiter.
 */
static int
umtxq_count_pi(struct umtx_key *key, struct umtx_q **first)
{
	struct umtxq_chain *uc;
	struct umtx_q *uq;
	int count = 0;

	*first = NULL;
	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_FOREACH(uq, &uc->uc_queue, uq_link) {
		if (umtx_key_match(&uq->uq_key, key)) {
			if (++count > 1)
				break;
			*first = uq;
		}
	}
	return (count);
}

/*
 * Wake up threads waiting on an userland object.
 */
static int
umtxq_signal(struct umtx_key *key, int n_wake)
{
	struct umtxq_chain *uc;
	struct umtx_q *uq, *next;
	int ret;

	ret = 0;
	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_FOREACH_SAFE(uq, &uc->uc_queue, uq_link, next) {
		if (umtx_key_match(&uq->uq_key, key)) {
			umtxq_remove(uq);
			wakeup(uq);
			if (++ret >= n_wake)
				break;
		}
	}
	return (ret);
}

/*
 * Wake up specified thread.
 */
static inline void
umtxq_signal_thread(struct umtx_q *uq)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	umtxq_remove(uq);
	wakeup(uq);
}

/*
 * Put thread into sleep state, before sleeping, check if
 * thread was removed from umtx queue.
 */
static inline int
umtxq_sleep(struct umtx_q *uq, const char *wmesg, int timo)
{
	struct umtxq_chain *uc;
	int error;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	if (!(uq->uq_flags & UQF_UMTXQ))
		return (0);
	error = msleep(uq, &uc->uc_lock, PCATCH, wmesg, timo);
	if (error == EWOULDBLOCK)
		error = ETIMEDOUT;
	return (error);
}

/*
 * Convert userspace address into unique logical address.
 */
static int
umtx_key_get(void *addr, int type, int share, struct umtx_key *key)
{
	struct thread *td = curthread;
	vm_map_t map;
	vm_map_entry_t entry;
	vm_pindex_t pindex;
	vm_prot_t prot;
	boolean_t wired;

	key->type = type;
	if (share == THREAD_SHARE) {
		key->shared = 0;
		key->info.private.vs = td->td_proc->p_vmspace;
		key->info.private.addr = (uintptr_t)addr;
	} else if (share == PROCESS_SHARE || share == AUTO_SHARE) {
		map = &td->td_proc->p_vmspace->vm_map;
		if (vm_map_lookup(&map, (vm_offset_t)addr, VM_PROT_WRITE,
		    &entry, &key->info.shared.object, &pindex, &prot,
		    &wired) != KERN_SUCCESS) {
			return EFAULT;
		}

		if ((share == PROCESS_SHARE) ||
		    (share == AUTO_SHARE &&
		     VM_INHERIT_SHARE == entry->inheritance)) {
			key->shared = 1;
			key->info.shared.offset = entry->offset + entry->start -
				(vm_offset_t)addr;
			vm_object_reference(key->info.shared.object);
		} else {
			key->shared = 0;
			key->info.private.vs = td->td_proc->p_vmspace;
			key->info.private.addr = (uintptr_t)addr;
		}
		vm_map_lookup_done(map, entry);
	}

	umtxq_hash(key);
	return (0);
}

/*
 * Release key.
 */
static inline void
umtx_key_release(struct umtx_key *key)
{
	if (key->shared)
		vm_object_deallocate(key->info.shared.object);
}

/*
 * Lock a umtx object.
 */
static int
_do_lock(struct thread *td, struct umtx *umtx, uintptr_t id, int timo)
{
	struct umtx_q *uq;
	intptr_t owner;
	intptr_t old;
	int error = 0;

	uq = td->td_umtxq;

	/*
	 * Care must be exercised when dealing with umtx structure. It
	 * can fault on any access.
	 */
	for (;;) {
		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		owner = casuptr((intptr_t *)&umtx->u_owner, UMTX_UNOWNED, id);

		/* The acquire succeeded. */
		if (owner == UMTX_UNOWNED)
			return (0);

		/* The address was invalid. */
		if (owner == -1)
			return (EFAULT);

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMTX_CONTESTED) {
			owner = casuptr((intptr_t *)&umtx->u_owner,
			    UMTX_CONTESTED, id | UMTX_CONTESTED);

			if (owner == UMTX_CONTESTED)
				return (0);

			/* The address was invalid. */
			if (owner == -1)
				return (EFAULT);

			/* If this failed the lock has changed, restart. */
			continue;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			return (error);

		if ((error = umtx_key_get(umtx, TYPE_SIMPLE_LOCK,
			AUTO_SHARE, &uq->uq_key)) != 0)
			return (error);

		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		old = casuptr((intptr_t *)&umtx->u_owner, owner,
		    owner | UMTX_CONTESTED);

		/* The address was invalid. */
		if (old == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		if (old == owner)
			error = umtxq_sleep(uq, "umtx", timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);
	}

	return (0);
}

/*
 * Lock a umtx object.
 */
static int
do_lock(struct thread *td, struct umtx *umtx, uintptr_t id,
	struct timespec *timeout)
{
	struct timespec ts, ts2, ts3;
	struct timeval tv;
	int error;

	if (timeout == NULL) {
		error = _do_lock(td, umtx, id, 0);
	} else {
		getnanouptime(&ts);
		timespecadd(&ts, timeout);
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		for (;;) {
			error = _do_lock(td, umtx, id, tvtohz(&tv));
			if (error != ETIMEDOUT)
				break;
			getnanouptime(&ts2);
			if (timespeccmp(&ts2, &ts, >=)) {
				error = ETIMEDOUT;
				break;
			}
			ts3 = ts;
			timespecsub(&ts3, &ts2);
			TIMESPEC_TO_TIMEVAL(&tv, &ts3);
		}
	}
	/* Mutex locking is be restarted if it is interrupted. */
	if (error == EINTR)
		error = ERESTART;
	return (error);
}

/*
 * Unlock a umtx object.
 */
static int
do_unlock(struct thread *td, struct umtx *umtx, uintptr_t id)
{
	struct umtx_key key;
	intptr_t owner;
	intptr_t old;
	int error;
	int count;

	/*
	 * Make sure we own this mtx.
	 *
	 * XXX Need a {fu,su}ptr this is not correct on arch where
	 * sizeof(intptr_t) != sizeof(long).
	 */
	owner = fuword(&umtx->u_owner);
	if (owner == -1)
		return (EFAULT);

	if ((owner & ~UMTX_CONTESTED) != id)
		return (EPERM);

	/* This should be done in userland */
	if ((owner & UMTX_CONTESTED) == 0) {
		old = casuptr((intptr_t *)&umtx->u_owner, owner,
			UMTX_UNOWNED);
		if (old == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(umtx, TYPE_SIMPLE_LOCK, AUTO_SHARE,
		&key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	old = casuptr((intptr_t *)&umtx->u_owner, owner,
			count <= 1 ? UMTX_UNOWNED : UMTX_CONTESTED);
	umtxq_lock(&key);
	umtxq_signal(&key,1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (old == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

/*
 * Fetch and compare value, sleep on the address if value is not changed.
 */
static int
do_wait(struct thread *td, struct umtx *umtx, uintptr_t id, struct timespec *timeout)
{
	struct umtx_q *uq;
	struct timespec ts, ts2, ts3;
	struct timeval tv;
	uintptr_t tmp;
	int error = 0;

	uq = td->td_umtxq;
	if ((error = umtx_key_get(umtx, TYPE_SIMPLE_WAIT, AUTO_SHARE,
	    &uq->uq_key)) != 0)
		return (error);

	umtxq_lock(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);
	tmp = fuword(&umtx->u_owner);
	if (tmp != id) {
		umtxq_lock(&uq->uq_key);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
	} else if (timeout == NULL) {
		umtxq_lock(&uq->uq_key);
		error = umtxq_sleep(uq, "ucond", 0);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
	} else {
		getnanouptime(&ts);
		timespecadd(&ts, timeout);
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		umtxq_lock(&uq->uq_key);
		for (;;) {
			error = umtxq_sleep(uq, "ucond", tvtohz(&tv));
			if (!(uq->uq_flags & UQF_UMTXQ))
				break;
			if (error != ETIMEDOUT)
				break;
			umtxq_unlock(&uq->uq_key);
			getnanouptime(&ts2);
			if (timespeccmp(&ts2, &ts, >=)) {
				error = ETIMEDOUT;
				umtxq_lock(&uq->uq_key);
				break;
			}
			ts3 = ts;
			timespecsub(&ts3, &ts2);
			TIMESPEC_TO_TIMEVAL(&tv, &ts3);
			umtxq_lock(&uq->uq_key);
		}
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
	}
	umtx_key_release(&uq->uq_key);
	/* Mutex locking is be restarted if it is interrupted. */
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

/*
 * Wake up threads sleeping on the specified address.
 */
int
kern_umtx_wake(struct thread *td, void *uaddr, int n_wake)
{
	struct umtx_key key;
	int ret;
	
	if ((ret = umtx_key_get(uaddr, TYPE_SIMPLE_WAIT, AUTO_SHARE,
	   &key)) != 0)
		return (ret);
	umtxq_lock(&key);
	ret = umtxq_signal(&key, n_wake);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (0);
}

/*
 * Lock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
_do_lock_normal(struct thread *td, struct umutex *m, uint32_t flags, int timo,
	int try)
{
	struct umtx_q *uq;
	uint32_t owner, old, id;
	int error = 0;

	id = td->td_tid;
	uq = td->td_umtxq;

	/*
	 * Care must be exercised when dealing with umtx structure. It
	 * can fault on any access.
	 */
	for (;;) {
		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		owner = casuword32(&m->m_owner, UMUTEX_UNOWNED, id);

		/* The acquire succeeded. */
		if (owner == UMUTEX_UNOWNED)
			return (0);

		/* The address was invalid. */
		if (owner == -1)
			return (EFAULT);

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMUTEX_CONTESTED) {
			owner = casuword32(&m->m_owner,
			    UMUTEX_CONTESTED, id | UMUTEX_CONTESTED);

			if (owner == UMUTEX_CONTESTED)
				return (0);

			/* The address was invalid. */
			if (owner == -1)
				return (EFAULT);

			/* If this failed the lock has changed, restart. */
			continue;
		}

		if ((flags & UMUTEX_ERROR_CHECK) != 0 &&
		    (owner & ~UMUTEX_CONTESTED) == id)
			return (EDEADLK);

		if (try != 0)
			return (EBUSY);

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			return (error);

		if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX,
		    GET_SHARE(flags), &uq->uq_key)) != 0)
			return (error);

		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		old = casuword32(&m->m_owner, owner, owner | UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (old == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		if (old == owner)
			error = umtxq_sleep(uq, "umtxn", timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);
	}

	return (0);
}

/*
 * Lock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
do_lock_normal(struct thread *td, struct umutex *m, uint32_t flags,
	struct timespec *timeout, int try)
{
	struct timespec ts, ts2, ts3;
	struct timeval tv;
	int error;

	if (timeout == NULL) {
		error = _do_lock_normal(td, m, flags, 0, try);
	} else {
		getnanouptime(&ts);
		timespecadd(&ts, timeout);
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		for (;;) {
			error = _do_lock_normal(td, m, flags, tvtohz(&tv), try);
			if (error != ETIMEDOUT)
				break;
			getnanouptime(&ts2);
			if (timespeccmp(&ts2, &ts, >=)) {
				error = ETIMEDOUT;
				break;
			}
			ts3 = ts;
			timespecsub(&ts3, &ts2);
			TIMESPEC_TO_TIMEVAL(&tv, &ts3);
		}
	}
	/* Mutex locking is be restarted if it is interrupted. */
	if (error == EINTR)
		error = ERESTART;
	return (error);
}

/*
 * Unlock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
do_unlock_normal(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	uint32_t owner, old, id;
	int error;
	int count;

	id = td->td_tid;
	/*
	 * Make sure we own this mtx.
	 */
	owner = fuword32(&m->m_owner);
	if (owner == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	/* This should be done in userland */
	if ((owner & UMUTEX_CONTESTED) == 0) {
		old = casuword32(&m->m_owner, owner, UMUTEX_UNOWNED);
		if (old == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	old = casuword32(&m->m_owner, owner,
		count <= 1 ? UMUTEX_UNOWNED : UMUTEX_CONTESTED);
	umtxq_lock(&key);
	umtxq_signal(&key,1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (old == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

static inline struct umtx_pi *
umtx_pi_alloc(void)
{
	struct umtx_pi *pi;

	pi = uma_zalloc(umtx_pi_zone, M_ZERO | M_WAITOK);
	TAILQ_INIT(&pi->pi_blocked);
	atomic_add_int(&umtx_pi_allocated, 1);
	return (pi);
}

static inline void
umtx_pi_free(struct umtx_pi *pi)
{
	uma_zfree(umtx_pi_zone, pi);
	atomic_add_int(&umtx_pi_allocated, -1);
}

/*
 * Adjust the thread's position on a pi_state after its priority has been
 * changed.
 */
static int
umtx_pi_adjust_thread(struct umtx_pi *pi, struct thread *td)
{
	struct umtx_q *uq, *uq1, *uq2;
	struct thread *td1;

	mtx_assert(&sched_lock, MA_OWNED);
	if (pi == NULL)
		return (0);

	uq = td->td_umtxq;

	/*
	 * Check if the thread needs to be moved on the blocked chain.
	 * It needs to be moved if either its priority is lower than
	 * the previous thread or higher than the next thread.
	 */
	uq1 = TAILQ_PREV(uq, umtxq_head, uq_lockq);
	uq2 = TAILQ_NEXT(uq, uq_lockq);
	if ((uq1 != NULL && UPRI(td) < UPRI(uq1->uq_thread)) ||
	    (uq2 != NULL && UPRI(td) > UPRI(uq2->uq_thread))) {
		/*
		 * Remove thread from blocked chain and determine where
		 * it should be moved to.
		 */
		TAILQ_REMOVE(&pi->pi_blocked, uq, uq_lockq);
		TAILQ_FOREACH(uq1, &pi->pi_blocked, uq_lockq) {
			td1 = uq1->uq_thread;
			MPASS(td1->td_proc->p_magic == P_MAGIC);
			if (UPRI(td1) > UPRI(td))
				break;
		}

		if (uq1 == NULL)
			TAILQ_INSERT_TAIL(&pi->pi_blocked, uq, uq_lockq);
		else
			TAILQ_INSERT_BEFORE(uq1, uq, uq_lockq);
	}
	return (1);
}

/*
 * Propagate priority when a thread is blocked on POSIX
 * PI mutex.
 */ 
static void
umtx_propagate_priority(struct thread *td)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;
	int pri;

	mtx_assert(&sched_lock, MA_OWNED);
	pri = UPRI(td);
	uq = td->td_umtxq;
	pi = uq->uq_pi_blocked;
	if (pi == NULL)
		return;

	for (;;) {
		td = pi->pi_owner;
		if (td == NULL)
			return;

		MPASS(td->td_proc != NULL);
		MPASS(td->td_proc->p_magic == P_MAGIC);

		if (UPRI(td) <= pri)
			return;

		sched_lend_user_prio(td, pri);

		/*
		 * Pick up the lock that td is blocked on.
		 */
		uq = td->td_umtxq;
		pi = uq->uq_pi_blocked;
		/* Resort td on the list if needed. */
		if (!umtx_pi_adjust_thread(pi, td))
			break;
	}
}

/*
 * Unpropagate priority for a PI mutex when a thread blocked on
 * it is interrupted by signal or resumed by others.
 */
static void
umtx_unpropagate_priority(struct umtx_pi *pi)
{
	struct umtx_q *uq, *uq_owner;
	struct umtx_pi *pi2;
	int pri;

	mtx_assert(&sched_lock, MA_OWNED);

	while (pi != NULL && pi->pi_owner != NULL) {
		pri = PRI_MAX;
		uq_owner = pi->pi_owner->td_umtxq;

		TAILQ_FOREACH(pi2, &uq_owner->uq_pi_contested, pi_link) {
			uq = TAILQ_FIRST(&pi2->pi_blocked);
			if (uq != NULL) {
				if (pri > UPRI(uq->uq_thread))
					pri = UPRI(uq->uq_thread);
			}
		}

		if (pri > uq_owner->uq_inherited_pri)
			pri = uq_owner->uq_inherited_pri;
		sched_unlend_user_prio(pi->pi_owner, pri);
		pi = uq_owner->uq_pi_blocked;
	}
}

/*
 * Insert a PI mutex into owned list.
 */
static void
umtx_pi_setowner(struct umtx_pi *pi, struct thread *owner)
{
	struct umtx_q *uq_owner;

	uq_owner = owner->td_umtxq;
	mtx_assert(&sched_lock, MA_OWNED);
	if (pi->pi_owner != NULL)
		panic("pi_ower != NULL");
	pi->pi_owner = owner;
	TAILQ_INSERT_TAIL(&uq_owner->uq_pi_contested, pi, pi_link);
}

/*
 * Claim ownership of a PI mutex.
 */
static int
umtx_pi_claim(struct umtx_pi *pi, struct thread *owner)
{
	struct umtx_q *uq, *uq_owner;

	uq_owner = owner->td_umtxq;
	mtx_lock_spin(&sched_lock);
	if (pi->pi_owner == owner) {
		mtx_unlock_spin(&sched_lock);
		return (0);
	}

	if (pi->pi_owner != NULL) {
		/*
		 * userland may have already messed the mutex, sigh.
		 */
		mtx_unlock_spin(&sched_lock);
		return (EPERM);
	}
	umtx_pi_setowner(pi, owner);
	uq = TAILQ_FIRST(&pi->pi_blocked);
	if (uq != NULL) {
		int pri;

		pri = UPRI(uq->uq_thread);
		if (pri < UPRI(owner))
			sched_lend_user_prio(owner, pri);
	}
	mtx_unlock_spin(&sched_lock);
	return (0);
}

/*
 * Adjust a thread's order position in its blocked PI mutex,
 * this may result new priority propagating process.
 */
void
umtx_pi_adjust(struct thread *td, u_char oldpri)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;

	uq = td->td_umtxq;

	mtx_assert(&sched_lock, MA_OWNED);
	MPASS(TD_ON_UPILOCK(td));

	/*
	 * Pick up the lock that td is blocked on.
	 */
	pi = uq->uq_pi_blocked;
	MPASS(pi != NULL);

	/* Resort the turnstile on the list. */
	if (!umtx_pi_adjust_thread(pi, td))
		return;

	/*
	 * If our priority was lowered and we are at the head of the
	 * turnstile, then propagate our new priority up the chain.
	 */
	if (uq == TAILQ_FIRST(&pi->pi_blocked) && UPRI(td) < oldpri)
		umtx_propagate_priority(td);
}

/*
 * Sleep on a PI mutex.
 */
static int
umtxq_sleep_pi(struct umtx_q *uq, struct umtx_pi *pi,
	uint32_t owner, const char *wmesg, int timo)
{
	struct umtxq_chain *uc;
	struct thread *td, *td1;
	struct umtx_q *uq1;
	int pri;
	int error = 0;

	td = uq->uq_thread;
	KASSERT(td == curthread, ("inconsistent uq_thread"));
	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	umtxq_insert(uq);
	if (pi->pi_owner == NULL) {
		/* XXX
		 * Current, We only support process private PI-mutex,
		 * non-contended PI-mutexes are locked in userland.
		 * Process shared PI-mutex should always be initialized
		 * by kernel and be registered in kernel, locking should
		 * always be done by kernel to avoid security problems.
		 * For process private PI-mutex, we can find owner
		 * thread and boost its priority safely.
		 */
		PROC_LOCK(curproc);
		td1 = thread_find(curproc, owner);
		mtx_lock_spin(&sched_lock);
		if (td1 != NULL && pi->pi_owner == NULL) {
			uq1 = td1->td_umtxq;
			umtx_pi_setowner(pi, td1);
		}
		PROC_UNLOCK(curproc);
	} else {
		mtx_lock_spin(&sched_lock);
	}

	TAILQ_FOREACH(uq1, &pi->pi_blocked, uq_lockq) {
		pri = UPRI(uq1->uq_thread);
		if (pri > UPRI(td))
			break;
	}

	if (uq1 != NULL)
		TAILQ_INSERT_BEFORE(uq1, uq, uq_lockq);
	else
		TAILQ_INSERT_TAIL(&pi->pi_blocked, uq, uq_lockq);

	uq->uq_pi_blocked = pi;
	td->td_flags |= TDF_UPIBLOCKED;
	mtx_unlock_spin(&sched_lock);
	umtxq_unlock(&uq->uq_key);

	mtx_lock_spin(&sched_lock);
	umtx_propagate_priority(td);
	mtx_unlock_spin(&sched_lock);

	umtxq_lock(&uq->uq_key);
	if (uq->uq_flags & UQF_UMTXQ) {
		error = msleep(uq, &uc->uc_lock, PCATCH, wmesg, timo);
		if (error == EWOULDBLOCK)
			error = ETIMEDOUT;
		if (uq->uq_flags & UQF_UMTXQ) {
			umtxq_busy(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unbusy(&uq->uq_key);
		}
	}
	umtxq_unlock(&uq->uq_key);

	mtx_lock_spin(&sched_lock);
	uq->uq_pi_blocked = NULL;
	td->td_flags &= ~TDF_UPIBLOCKED;
	TAILQ_REMOVE(&pi->pi_blocked, uq, uq_lockq);
	umtx_unpropagate_priority(pi);
	mtx_unlock_spin(&sched_lock);

	umtxq_lock(&uq->uq_key);

	return (error);
}

/*
 * Add reference count for a PI mutex.
 */
static void
umtx_pi_ref(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	pi->pi_refcount++;
}

/*
 * Decrease reference count for a PI mutex, if the counter
 * is decreased to zero, its memory space is freed.
 */ 
static void
umtx_pi_unref(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;
	int free = 0;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	KASSERT(pi->pi_refcount > 0, ("invalid reference count"));
	if (--pi->pi_refcount == 0) {
		mtx_lock_spin(&sched_lock);
		if (pi->pi_owner != NULL) {
			TAILQ_REMOVE(&pi->pi_owner->td_umtxq->uq_pi_contested,
				pi, pi_link);
			pi->pi_owner = NULL;
		}
		KASSERT(TAILQ_EMPTY(&pi->pi_blocked),
			("blocked queue not empty"));
		mtx_unlock_spin(&sched_lock);
		TAILQ_REMOVE(&uc->uc_pi_list, pi, pi_hashlink);
		free = 1;
	}
	if (free)
		umtx_pi_free(pi);
}

/*
 * Find a PI mutex in hash table.
 */
static struct umtx_pi *
umtx_pi_lookup(struct umtx_key *key)
{
	struct umtxq_chain *uc;
	struct umtx_pi *pi;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);

	TAILQ_FOREACH(pi, &uc->uc_pi_list, pi_hashlink) {
		if (umtx_key_match(&pi->pi_key, key)) {
			return (pi);
		}
	}
	return (NULL);
}

/*
 * Insert a PI mutex into hash table.
 */
static inline void
umtx_pi_insert(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_INSERT_TAIL(&uc->uc_pi_list, pi, pi_hashlink);
}

/*
 * Lock a PI mutex.
 */
static int
_do_lock_pi(struct thread *td, struct umutex *m, uint32_t flags, int timo,
	int try)
{
	struct umtx_q *uq;
	struct umtx_pi *pi, *new_pi;
	uint32_t id, owner, old;
	int error;

	id = td->td_tid;
	uq = td->td_umtxq;

	if ((error = umtx_key_get(m, TYPE_PI_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);
	for (;;) {
		pi = NULL;
		umtxq_lock(&uq->uq_key);
		pi = umtx_pi_lookup(&uq->uq_key);
		if (pi == NULL) {
			umtxq_unlock(&uq->uq_key);
			new_pi = umtx_pi_alloc();
			new_pi->pi_key = uq->uq_key;
			umtxq_lock(&uq->uq_key);
			pi = umtx_pi_lookup(&uq->uq_key);
			if (pi != NULL)
				umtx_pi_free(new_pi);
			else {
				umtx_pi_insert(new_pi);
				pi = new_pi;
			}
		}

		umtx_pi_ref(pi);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Care must be exercised when dealing with umtx structure.  It
		 * can fault on any access.
		 */

		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		owner = casuword32(&m->m_owner, UMUTEX_UNOWNED, id);

		/* The acquire succeeded. */
		if (owner == UMUTEX_UNOWNED) {
			error = 0;
			break;
		}

		/* The address was invalid. */
		if (owner == -1) {
			error = EFAULT;
			break;
		}

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMUTEX_CONTESTED) {
			owner = casuword32(&m->m_owner,
			    UMUTEX_CONTESTED, id | UMUTEX_CONTESTED);

			if (owner == UMUTEX_CONTESTED) {
				umtxq_lock(&uq->uq_key);
				error = umtx_pi_claim(pi, td);
				umtxq_unlock(&uq->uq_key);
				break;
			}

			/* The address was invalid. */
			if (owner == -1) {
				error = EFAULT;
				break;
			}

			/* If this failed the lock has changed, restart. */
			umtxq_lock(&uq->uq_key);
			umtx_pi_unref(pi);
			umtxq_unlock(&uq->uq_key);
			pi = NULL;
			continue;
		}

		if ((flags & UMUTEX_ERROR_CHECK) != 0 &&
		    (owner & ~UMUTEX_CONTESTED) == id) {
			error = EDEADLK;
			break;
		}

		if (try != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;
			
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		old = casuword32(&m->m_owner, owner, owner | UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (old == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_unbusy(&uq->uq_key);
			umtxq_unlock(&uq->uq_key);
			error = EFAULT;
			break;
		}

		umtxq_lock(&uq->uq_key);
		umtxq_unbusy(&uq->uq_key);
		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		if (old == owner)
			error = umtxq_sleep_pi(uq, pi, owner & ~UMUTEX_CONTESTED,
				 "umtxpi", timo);
		umtx_pi_unref(pi);
		umtxq_unlock(&uq->uq_key);
		pi = NULL;
	}

	if (pi != NULL) {
		umtxq_lock(&uq->uq_key);
		umtx_pi_unref(pi);
		umtxq_unlock(&uq->uq_key);
	}

	umtx_key_release(&uq->uq_key);
	return (error);
}

static int
do_lock_pi(struct thread *td, struct umutex *m, uint32_t flags,
	struct timespec *timeout, int try)
{
	struct timespec ts, ts2, ts3;
	struct timeval tv;
	int error;

	if (timeout == NULL) {
		error = _do_lock_pi(td, m, flags, 0, try);
	} else {
		getnanouptime(&ts);
		timespecadd(&ts, timeout);
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		for (;;) {
			error = _do_lock_pi(td, m, flags, tvtohz(&tv), try);
			if (error != ETIMEDOUT)
				break;
			getnanouptime(&ts2);
			if (timespeccmp(&ts2, &ts, >=)) {
				error = ETIMEDOUT;
				break;
			}
			ts3 = ts;
			timespecsub(&ts3, &ts2);
			TIMESPEC_TO_TIMEVAL(&tv, &ts3);
		}
	}
	/* Mutex locking is be restarted if it is interrupted. */
	if (error == EINTR)
		error = ERESTART;
	return (error);
}

/*
 * Unlock a PI mutex.
 */
static int
do_unlock_pi(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	struct umtx_q *uq_first, *uq_first2, *uq_me;
	struct umtx_pi *pi, *pi2;
	uint32_t owner, old, id;
	int error;
	int count;
	int pri;

	id = td->td_tid;
	/*
	 * Make sure we own this mtx.
	 */
	owner = fuword32(&m->m_owner);
	if (owner == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	/* This should be done in userland */
	if ((owner & UMUTEX_CONTESTED) == 0) {
		old = casuword32(&m->m_owner, owner, UMUTEX_UNOWNED);
		if (old == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_PI_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count_pi(&key, &uq_first);
	if (uq_first != NULL) {
		pi = uq_first->uq_pi_blocked;
		if (pi->pi_owner != curthread) {
			umtxq_unbusy(&key);
			umtxq_unlock(&key);
			/* userland messed the mutex */
			return (EPERM);
		}
		uq_me = curthread->td_umtxq;
		mtx_lock_spin(&sched_lock);
		pi->pi_owner = NULL;
		TAILQ_REMOVE(&uq_me->uq_pi_contested, pi, pi_link);
		uq_first = TAILQ_FIRST(&pi->pi_blocked);
		pri = PRI_MAX;
		TAILQ_FOREACH(pi2, &uq_me->uq_pi_contested, pi_link) {
			uq_first2 = TAILQ_FIRST(&pi2->pi_blocked);
			if (uq_first2 != NULL) {
				if (pri > UPRI(uq_first2->uq_thread))
					pri = UPRI(uq_first2->uq_thread);
			}
		}
		sched_unlend_user_prio(curthread, pri);
		mtx_unlock_spin(&sched_lock);
	}
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	old = casuword32(&m->m_owner, owner,
		count <= 1 ? UMUTEX_UNOWNED : UMUTEX_CONTESTED);

	umtxq_lock(&key);
	if (uq_first != NULL)
		umtxq_signal_thread(uq_first);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (old == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

/*
 * Lock a PP mutex.
 */
static int
_do_lock_pp(struct thread *td, struct umutex *m, uint32_t flags, int timo,
	int try)
{
	struct umtx_q *uq, *uq2;
	struct umtx_pi *pi;
	uint32_t ceiling;
	uint32_t owner, id;
	int error, pri, old_inherited_pri, su;

	id = td->td_tid;
	uq = td->td_umtxq;
	if ((error = umtx_key_get(m, TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);
	su = (suser(td) == 0);
	for (;;) {
		old_inherited_pri = uq->uq_inherited_pri;
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		ceiling = RTP_PRIO_MAX - fuword32(&m->m_ceilings[0]);
		if (ceiling > RTP_PRIO_MAX) {
			error = EINVAL;
			goto out;
		}

		mtx_lock_spin(&sched_lock);
		if (UPRI(td) < PRI_MIN_REALTIME + ceiling) {
			mtx_unlock_spin(&sched_lock);
			error = EINVAL;
			goto out;
		}
		if (su && PRI_MIN_REALTIME + ceiling < uq->uq_inherited_pri) {
			uq->uq_inherited_pri = PRI_MIN_REALTIME + ceiling;
			if (uq->uq_inherited_pri < UPRI(td))
				sched_lend_user_prio(td, uq->uq_inherited_pri);
		}
		mtx_unlock_spin(&sched_lock);

		owner = casuword32(&m->m_owner,
		    UMUTEX_CONTESTED, id | UMUTEX_CONTESTED);

		if (owner == UMUTEX_CONTESTED) {
			error = 0;
			break;
		}

		/* The address was invalid. */
		if (owner == -1) {
			error = EFAULT;
			break;
		}

		if ((flags & UMUTEX_ERROR_CHECK) != 0 &&
		    (owner & ~UMUTEX_CONTESTED) == id) {
			error = EDEADLK;
			break;
		}

		if (try != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		umtxq_lock(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		error = umtxq_sleep(uq, "umtxpp", timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);

		mtx_lock_spin(&sched_lock);
		uq->uq_inherited_pri = old_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		sched_unlend_user_prio(td, pri);
		mtx_unlock_spin(&sched_lock);
	}

	if (error != 0) {
		mtx_lock_spin(&sched_lock);
		uq->uq_inherited_pri = old_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		sched_unlend_user_prio(td, pri);
		mtx_unlock_spin(&sched_lock);
	}

out:
	umtxq_lock(&uq->uq_key);
	umtxq_unbusy(&uq->uq_key);
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Lock a PP mutex.
 */
static int
do_lock_pp(struct thread *td, struct umutex *m, uint32_t flags,
	struct timespec *timeout, int try)
{
	struct timespec ts, ts2, ts3;
	struct timeval tv;
	int error;

	if (timeout == NULL) {
		error = _do_lock_pp(td, m, flags, 0, try);
	} else {
		getnanouptime(&ts);
		timespecadd(&ts, timeout);
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		for (;;) {
			error = _do_lock_pp(td, m, flags, tvtohz(&tv), try);
			if (error != ETIMEDOUT)
				break;
			getnanouptime(&ts2);
			if (timespeccmp(&ts2, &ts, >=)) {
				error = ETIMEDOUT;
				break;
			}
			ts3 = ts;
			timespecsub(&ts3, &ts2);
			TIMESPEC_TO_TIMEVAL(&tv, &ts3);
		}
	}
	/* Mutex locking is be restarted if it is interrupted. */
	if (error == EINTR)
		error = ERESTART;
	return (error);
}

/*
 * Unlock a PP mutex.
 */
static int
do_unlock_pp(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	struct umtx_q *uq, *uq2;
	struct umtx_pi *pi;
	uint32_t owner, id;
	uint32_t rceiling;
	int error, pri, new_inherited_pri;

	id = td->td_tid;
	uq = td->td_umtxq;

	/*
	 * Make sure we own this mtx.
	 */
	owner = fuword32(&m->m_owner);
	if (owner == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	error = copyin(&m->m_ceilings[1], &rceiling, sizeof(uint32_t));
	if (error != 0)
		return (error);

	if (rceiling == -1)
		new_inherited_pri = PRI_MAX;
	else {
		rceiling = RTP_PRIO_MAX - rceiling;
		if (rceiling > RTP_PRIO_MAX)
			return (EINVAL);
		new_inherited_pri = PRI_MIN_REALTIME + rceiling;
	}

	if ((error = umtx_key_get(m, TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);
	umtxq_lock(&key);
	umtxq_busy(&key);
	umtxq_unlock(&key);
	/*
	 * For priority protected mutex, always set unlocked state
	 * to UMUTEX_CONTESTED, so that userland always enters kernel
	 * to lock the mutex, it is necessary because thread priority
	 * has to be adjusted for such mutex.
	 */
	error = suword32(&m->m_owner, UMUTEX_CONTESTED);

	umtxq_lock(&key);
	if (error == 0)
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);

	if (error == -1)
		error = EFAULT;
	else {
		mtx_lock_spin(&sched_lock);
		uq->uq_inherited_pri = new_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		sched_unlend_user_prio(td, pri);
		mtx_unlock_spin(&sched_lock);
	}
	umtx_key_release(&key);
	return (error);
}

static int
do_set_ceiling(struct thread *td, struct umutex *m, uint32_t ceiling,
	uint32_t *old_ceiling)
{
	struct umtx_q *uq;
	uint32_t save_ceiling;
	uint32_t owner, id;
	uint32_t flags;
	int error;

	flags = fuword32(&m->m_flags);
	if ((flags & UMUTEX_PRIO_PROTECT) == 0)
		return (EINVAL);
	if (ceiling > RTP_PRIO_MAX)
		return (EINVAL);
	id = td->td_tid;
	uq = td->td_umtxq;
	if ((error = umtx_key_get(m, TYPE_PP_UMUTEX, GET_SHARE(flags),
	   &uq->uq_key)) != 0)
		return (error);
	for (;;) {
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		save_ceiling = fuword32(&m->m_ceilings[0]);

		owner = casuword32(&m->m_owner,
		    UMUTEX_CONTESTED, id | UMUTEX_CONTESTED);

		if (owner == UMUTEX_CONTESTED) {
			suword32(&m->m_ceilings[0], ceiling);
			suword32(&m->m_owner, UMUTEX_CONTESTED);
			error = 0;
			break;
		}

		/* The address was invalid. */
		if (owner == -1) {
			error = EFAULT;
			break;
		}

		if ((owner & ~UMUTEX_CONTESTED) == id) {
			suword32(&m->m_ceilings[0], ceiling);
			error = 0;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		error = umtxq_sleep(uq, "umtxpp", 0);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
	}
	umtxq_lock(&uq->uq_key);
	if (error == 0)
		umtxq_signal(&uq->uq_key, INT_MAX);
	umtxq_unbusy(&uq->uq_key);
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	if (error == 0 && old_ceiling != NULL)
		suword32(old_ceiling, save_ceiling);
	return (error);
}

/*
 * Lock a userland POSIX mutex.
 */
static int
do_lock_umutex(struct thread *td, struct umutex *m, struct timespec *ts,
	int try)
{
	uint32_t flags;

	flags = fuword32(&m->m_flags);
	if (flags == -1)
		return (EFAULT);

	switch(flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT)) {
	case 0:
		return (do_lock_normal(td, m, flags, ts, try));
	case UMUTEX_PRIO_INHERIT:
		return (do_lock_pi(td, m, flags, ts, try));
	case UMUTEX_PRIO_PROTECT:
		return (do_lock_pp(td, m, flags, ts, try));
	}

	return (EINVAL);
}

/*
 * Unlock a userland POSIX mutex.
 */
static int
do_unlock_umutex(struct thread *td, struct umutex *m)
{
	uint32_t flags;
	int ret;

	flags = fuword32(&m->m_flags);
	if (flags == -1)
		return (EFAULT);

	if ((flags & UMUTEX_PRIO_INHERIT) != 0)
		ret = do_unlock_pi(td, m, flags);
	else if ((flags & UMUTEX_PRIO_PROTECT) != 0)
		ret = do_unlock_pp(td, m, flags);
	else
		ret = do_unlock_normal(td, m, flags);

	return (ret);
}

int
_umtx_lock(struct thread *td, struct _umtx_lock_args *uap)
    /* struct umtx *umtx */
{
	return _do_lock(td, uap->umtx, td->td_tid, 0);
}

int
_umtx_unlock(struct thread *td, struct _umtx_unlock_args *uap)
    /* struct umtx *umtx */
{
	return do_unlock(td, uap->umtx, td->td_tid);
}

int
_umtx_op(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec timeout;
	struct timespec *ts;
	int error;

	switch(uap->op) {
	case UMTX_OP_MUTEX_LOCK:
		/* Allow a null timespec (wait forever). */
		if (uap->uaddr2 == NULL)
			ts = NULL;
		else {
			error = copyin(uap->uaddr2, &timeout, sizeof(timeout));
			if (error != 0)
				break;
			if (timeout.tv_nsec >= 1000000000 ||
			    timeout.tv_nsec < 0) {
				error = EINVAL;
				break;
			}
			ts = &timeout;
		}
		error = do_lock_umutex(td, uap->obj, ts, 0);
		break;
	case UMTX_OP_MUTEX_UNLOCK:
		error = do_unlock_umutex(td, uap->obj);
		break;
	case UMTX_OP_LOCK:
		/* Allow a null timespec (wait forever). */
		if (uap->uaddr2 == NULL)
			ts = NULL;
		else {
			error = copyin(uap->uaddr2, &timeout, sizeof(timeout));
			if (error != 0)
				break;
			if (timeout.tv_nsec >= 1000000000 ||
			    timeout.tv_nsec < 0) {
				error = EINVAL;
				break;
			}
			ts = &timeout;
		}
		error = do_lock(td, uap->obj, uap->val, ts);
		break;
	case UMTX_OP_UNLOCK:
		error = do_unlock(td, uap->obj, uap->val);
		break;
	case UMTX_OP_WAIT:
		/* Allow a null timespec (wait forever). */
		if (uap->uaddr2 == NULL)
			ts = NULL;
		else {
			error = copyin(uap->uaddr2, &timeout, sizeof(timeout));
			if (error != 0)
				break;
			if (timeout.tv_nsec >= 1000000000 ||
			    timeout.tv_nsec < 0) {
				error = EINVAL;
				break;
			}
			ts = &timeout;
		}
		error = do_wait(td, uap->obj, uap->val, ts);
		break;
	case UMTX_OP_WAKE:
		error = kern_umtx_wake(td, uap->obj, uap->val);
		break;
	case UMTX_OP_MUTEX_TRYLOCK:
		error = do_lock_umutex(td, uap->obj, NULL, 1);
		break;
	case UMTX_OP_SET_CEILING:
		error = do_set_ceiling(td, uap->obj, uap->val, uap->uaddr1);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

void
umtx_thread_init(struct thread *td)
{
	td->td_umtxq = umtxq_alloc();
	td->td_umtxq->uq_thread = td;
}

void
umtx_thread_fini(struct thread *td)
{
	umtxq_free(td->td_umtxq);
}

/*
 * It will be called when new thread is created, e.g fork().
 */
void
umtx_thread_alloc(struct thread *td)
{
	struct umtx_q *uq;

	uq = td->td_umtxq;
	uq->uq_inherited_pri = PRI_MAX;

	KASSERT(uq->uq_flags == 0, ("uq_flags != 0"));
	KASSERT(uq->uq_thread == td, ("uq_thread != td"));
	KASSERT(uq->uq_pi_blocked == NULL, ("uq_pi_blocked != NULL"));
	KASSERT(TAILQ_EMPTY(&uq->uq_pi_contested), ("uq_pi_contested is not empty"));
}

/*
 * exec() hook.
 */
static void
umtx_exec_hook(void *arg __unused, struct proc *p __unused,
	struct image_params *imgp __unused)
{
	umtx_thread_cleanup(curthread);
}

/*
 * thread_exit() hook.
 */
void
umtx_thread_exit(struct thread *td)
{
	umtx_thread_cleanup(td);
}

/*
 * clean up umtx data.
 */
static void
umtx_thread_cleanup(struct thread *td)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;

	if ((uq = td->td_umtxq) == NULL)
		return;

	mtx_lock_spin(&sched_lock);
	uq->uq_inherited_pri = PRI_MAX;
	while ((pi = TAILQ_FIRST(&uq->uq_pi_contested)) != NULL) {
		pi->pi_owner = NULL;
		TAILQ_REMOVE(&uq->uq_pi_contested, pi, pi_link);
	}
	td->td_flags &= ~TDF_UBORROWING;
	mtx_unlock_spin(&sched_lock);
}

/*
 * System call switch table.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * $FreeBSD$
 * created from FreeBSD: src/sys/i386/ibcs2/syscalls.xenix,v 1.6 1999/08/28 00:44:02 peter Exp 
 */

#include <sys/param.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <i386/ibcs2/ibcs2_types.h>
#include <i386/ibcs2/ibcs2_signal.h>
#include <i386/ibcs2/ibcs2_xenix.h>

/* The casts are bogus but will do for now. */
struct sysent xenix_sysent[] = {
	{ 0, (sy_call_t *)nosys },			/* 0 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 1 = xenix_xlocking */
	{ 0, (sy_call_t *)nosys },			/* 2 = xenix_creatsem */
	{ 0, (sy_call_t *)nosys },			/* 3 = xenix_opensem */
	{ 0, (sy_call_t *)nosys },			/* 4 = xenix_sigsem */
	{ 0, (sy_call_t *)nosys },			/* 5 = xenix_waitsem */
	{ 0, (sy_call_t *)nosys },			/* 6 = xenix_nbwaitsem */
	{ 1, (sy_call_t *)xenix_rdchk },		/* 7 = xenix_rdchk */
	{ 0, (sy_call_t *)nosys },			/* 8 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 9 = nosys */
	{ 2, (sy_call_t *)xenix_chsize },		/* 10 = xenix_chsize */
	{ 1, (sy_call_t *)xenix_ftime },		/* 11 = xenix_ftime */
	{ 1, (sy_call_t *)xenix_nap },			/* 12 = xenix_nap */
	{ 0, (sy_call_t *)nosys },			/* 13 = xenix_sdget */
	{ 0, (sy_call_t *)nosys },			/* 14 = xenix_sdfree */
	{ 0, (sy_call_t *)nosys },			/* 15 = xenix_sdenter */
	{ 0, (sy_call_t *)nosys },			/* 16 = xenix_sdleave */
	{ 0, (sy_call_t *)nosys },			/* 17 = xenix_sdgetv */
	{ 0, (sy_call_t *)nosys },			/* 18 = xenix_sdwaitv */
	{ 0, (sy_call_t *)nosys },			/* 19 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 20 = nosys */
	{ 0, (sy_call_t *)xenix_scoinfo },		/* 21 = xenix_scoinfo */
	{ 0, (sy_call_t *)nosys },			/* 22 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 23 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 24 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 25 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 26 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 27 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 28 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 29 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 30 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 31 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 32 = xenix_proctl */
	{ 0, (sy_call_t *)nosys },			/* 33 = xenix_execseg */
	{ 0, (sy_call_t *)nosys },			/* 34 = xenix_unexecseg */
	{ 0, (sy_call_t *)nosys },			/* 35 = nosys */
	{ 5, (sy_call_t *)select },			/* 36 = select */
	{ 2, (sy_call_t *)xenix_eaccess },		/* 37 = xenix_eaccess */
	{ 0, (sy_call_t *)nosys },			/* 38 = xenix_paccess */
	{ 3, (sy_call_t *)ibcs2_sigaction },		/* 39 = ibcs2_sigaction */
	{ 3, (sy_call_t *)ibcs2_sigprocmask },		/* 40 = ibcs2_sigprocmask */
	{ 1, (sy_call_t *)ibcs2_sigpending },		/* 41 = ibcs2_sigpending */
	{ 1, (sy_call_t *)ibcs2_sigsuspend },		/* 42 = ibcs2_sigsuspend */
	{ 2, (sy_call_t *)ibcs2_getgroups },		/* 43 = ibcs2_getgroups */
	{ 2, (sy_call_t *)ibcs2_setgroups },		/* 44 = ibcs2_setgroups */
	{ 1, (sy_call_t *)ibcs2_sysconf },		/* 45 = ibcs2_sysconf */
	{ 2, (sy_call_t *)ibcs2_pathconf },		/* 46 = ibcs2_pathconf */
	{ 2, (sy_call_t *)ibcs2_fpathconf },		/* 47 = ibcs2_fpathconf */
	{ 2, (sy_call_t *)ibcs2_rename },		/* 48 = ibcs2_rename */
	{ 0, (sy_call_t *)nosys },			/* 49 = nosys */
	{ 1, (sy_call_t *)xenix_utsname },		/* 50 = xenix_utsname */
	{ 0, (sy_call_t *)nosys },			/* 51 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 52 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 53 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 54 = nosys */
	{ 2, (sy_call_t *)getitimer },			/* 55 = getitimer */
	{ 3, (sy_call_t *)setitimer },			/* 56 = setitimer */
};

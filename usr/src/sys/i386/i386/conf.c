/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * %sccs.include.386.c%
 *
 *	@(#)conf.c	5.6 (Berkeley) %G%
 */

#include "param.h"
#include "systm.h"
#include "buf.h"
#include "ioctl.h"
#include "tty.h"
#include "conf.h"

int	nullop(), enxio(), enodev(), rawread(), rawwrite(), swstrategy();
int	rawread(), rawwrite(), swstrategy();

#include "wd.h"
#if NWD > 0
int	wdopen(),wdclose(),wdstrategy(),wdread(),wdwrite(),wdioctl();
int	wddump(),wdsize();
#else
#define	wdopen		enxio
#define	wdclose		enxio
#define	wdstrategy	enxio
#define	wdread		enxio
#define	wdwrite		enxio
#define	wdioctl		enxio
#define	wddump		enxio
#define	wdsize		NULL
#endif

#include "xd.h"
#if NXD > 0
int	xdopen(),xdclose(),xdstrategy(),xdread(),xdwrite(),xdioctl();
int	xddump(),xdsize();
#else
#define	xdopen		enxio
#define	xdclose		enxio
#define	xdstrategy	enxio
#define	xdread		enxio
#define	xdwrite		enxio
#define	xdioctl		enxio
#define	xddump		enxio
#define	xdsize		NULL
#endif

#include "wt.h"
#if NWT > 0
int	wtopen(),wtclose(),wtstrategy(),wtread(),wtwrite(),wtioctl();
int	wtdump(),wtsize();
#else
#define	wtopen		enxio
#define	wtclose		enxio
#define	wtstrategy	enxio
#define	wtread		enxio
#define	wtwrite		enxio
#define	wtioctl		enxio
#define	wtdump		enxio
#define	wtsize		NULL
#endif

#include "fd.h"
#if NFD > 0
int	Fdopen(),fdclose(),fdstrategy(),fdread(),fdwrite();
#define	fdioctl		enxio
#define	fddump		enxio
#define	fdsize		NULL
#else
#define	Fdopen		enxio
#define	fdclose		enxio
#define	fdstrategy	enxio
#define	fdread		enxio
#define	fdwrite		enxio
#define	fdioctl		enxio
#define	fddump		enxio
#define	fdsize		NULL
#endif

int	swstrategy(),swread(),swwrite();

struct bdevsw	bdevsw[] =
{
	{ wdopen,	wdclose,	wdstrategy,	wdioctl,	/*0*/
	  wddump,	wdsize,		NULL },
	{ enodev,	enodev,		swstrategy,	enodev,		/*1*/
	  enodev,	enodev,		NULL },
	{ Fdopen,	fdclose,	fdstrategy,	fdioctl,	/*2*/
	  fddump,	fdsize,		NULL },
	{ wtopen,	wtclose,	wtstrategy,	wtioctl,	/*3*/
	  wtdump,	wtsize,		B_TAPE },
	{ xdopen,	xdclose,	xdstrategy,	xdioctl,	/*4*/
	  xddump,	xdsize,		NULL }
};
int	nblkdev = sizeof (bdevsw) / sizeof (bdevsw[0]);

int	cnopen(),cnclose(),cnread(),cnwrite(),cnioctl();

int	cttyopen(), cttyread(), cttywrite(), cttyioctl(), cttyselect();

int 	mmrw();
#define	mmselect	seltrue

#include "pty.h"
#if NPTY > 0
int	ptsopen(),ptsclose(),ptsread(),ptswrite(),ptsstop();
int	ptcopen(),ptcclose(),ptcread(),ptcwrite(),ptcselect();
int	ptyioctl();
struct	tty pt_tty[];
#else
#define ptsopen		enxio
#define ptsclose	enxio
#define ptsread		enxio
#define ptswrite	enxio
#define ptcopen		enxio
#define ptcclose	enxio
#define ptcread		enxio
#define ptcwrite	enxio
#define ptyioctl	enxio
#define	pt_tty		NULL
#define	ptcselect	enxio
#define	ptsstop		nullop
#endif

#include "com.h"
#if NCOM > 0
int	comopen(),comclose(),comread(),comwrite(),comioctl();
#define comreset	enxio
extern	struct tty com_tty[];
#else
#define comopen		enxio
#define comclose	enxio
#define comread		enxio
#define comwrite	enxio
#define comioctl	enxio
#define comreset	enxio
#define	com_tty		NULL
#endif

int	logopen(),logclose(),logread(),logioctl(),logselect();

int	ttselect(), seltrue();


struct cdevsw	cdevsw[] =
{
	{ cnopen,	cnclose,	cnread,		cnwrite,	/*0*/
	  cnioctl,	nullop,		nullop,		NULL,
	  ttselect,	enodev,		NULL },
	{ cttyopen,	nullop,		cttyread,	cttywrite,	/*1*/
	  cttyioctl,	nullop,		nullop,		NULL,
	  cttyselect,	enodev,		NULL },
	{ wdopen,	wdclose,	wdread,		wdwrite,	/*3*/
	  wdioctl,	enodev,		nullop,		NULL,
	  seltrue,	enodev,		wdstrategy },
	{ nullop,	nullop,		rawread,	rawwrite,	/*4*/
	  enodev,	enodev,		nullop,		NULL,
	  enodev,	enodev,		swstrategy },
	{ ptsopen,	ptsclose,	ptsread,	ptswrite,	/*5*/
	  ptyioctl,	ptsstop,	nullop,		pt_tty,
	  ttselect,	enodev,		NULL },
	{ ptcopen,	ptcclose,	ptcread,	ptcwrite,	/*6*/
	  ptyioctl,	nullop,		nullop,		pt_tty,
	  ptcselect,	enodev,		NULL },
	{ logopen,	logclose,	logread,	enodev,		/*7*/
	  logioctl,	enodev,		nullop,		NULL,
	  logselect,	enodev,		NULL },
	{ comopen,	comclose,	comread,	comwrite,	/*8*/
	  comioctl,	enodev,		comreset,	com_tty,
	  ttselect,	enodev,		NULL },
	{ Fdopen,	fdclose,	fdread,		fdwrite,	/*9*/
	  fdioctl,	enodev,		nullop,		NULL,
	  seltrue,	enodev,		fdstrategy },
	{ wtopen,	wtclose,	wtread,		wtwrite,	/*A*/
	  wtioctl,	enodev,		nullop,		NULL,
	  seltrue,	enodev,		wtstrategy },
	{ xdopen,	xdclose,	xdread,		xdwrite,	/*B*/
	  xdioctl,	enodev,		nullop,		NULL,
	  seltrue,	enodev,		xdstrategy },
};
int	nchrdev = sizeof (cdevsw) / sizeof (cdevsw[0]);

int	mem_no = 2; 	/* major device number of memory special file */

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines. Instead, /dev/drum is
 * provided as a character (raw) device.
 */
dev_t	swapdev = makedev(1, 0);

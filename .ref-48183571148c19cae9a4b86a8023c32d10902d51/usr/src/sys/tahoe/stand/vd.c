/*	vd.c	7.6	87/02/18	*/

/*
 * Stand alone driver for the VDDC/SMDE controller 
 */
#include "../machine/mtpr.h"

#include "param.h"
#include "inode.h"
#include "fs.h"
#include "disklabel.h"
#include "saio.h"

#include "../tahoevba/vdreg.h"
#include "../tahoevba/vbaparam.h"

#define	COMPAT_42	1

#define NVD		4
#define	NDRIVE		8		/* drives per controller */
#define VDSLAVE(x)	((x) % NDRIVE)
#define VDCTLR(x)	((x) / NDRIVE)

#define	VDADDR(ctlr)	((struct vddevice *)vdaddrs[ctlr])
long	vdaddrs[NVD] = { 0xffff2000, 0xffff2100, 0xffff2200, 0xffff2300 };

u_char	vdinit[NVD];			/* controller initialized */
u_char	vdtype[NVD];			/* controller type */
u_char	dkconfigured[NVD*NDRIVE];	/* unit configured */
struct	disklabel dklabel[NVD*NDRIVE];	/* pack label */

struct	mdcb mdcb;
struct	dcb dcb;
char	lbuf[DEV_BSIZE];

vdopen(io)
	register struct iob *io;
{
	register int ctlr = VDCTLR(io->i_unit);
	register struct dkinfo *dk;
	register struct disklabel *lp;
	int error;

	if (ctlr >= NVD) {
		printf("invalid controller number\n");
		return (ENXIO);
	}
	if (!vdinit[ctlr] && (error = vdreset_ctlr(ctlr, io->i_unit)))
		return (error);
	lp = &dklabel[io->i_unit];
	if (!dkconfigured[io->i_unit]) {
		struct iob tio;

		/*
		 * Read in the pack label.
		 */
		lp->d_secsize = 512;
		lp->d_nsectors = 32;
		lp->d_ntracks = 24;
		lp->d_ncylinders = 711;
		lp->d_secpercyl = 32*24;
		if (!vdreset_drive(io))
			return (ENXIO);
		tio = *io;
		tio.i_bn = LABELSECTOR;
		tio.i_ma = lbuf;
		tio.i_cc = DEV_BSIZE;
		tio.i_flgs |= F_RDDATA;
		if (vdstrategy(&tio, READ) != DEV_BSIZE) {
			printf("can't read disk label");
			return (EIO);
		}
		*lp = *(struct disklabel *)(lbuf + LABELOFFSET);
		if (lp->d_magic != DISKMAGIC || lp->d_magic2 != DISKMAGIC) {
			printf("dk%d: unlabeled\n", io->i_unit);
#ifdef COMPAT_42
			if (error = vdmaptype(io))
				return (error);
#else
			return (ENXIO);
#endif
		}
		if (!vdreset_drive(io))
			return (ENXIO);
		dkconfigured[io->i_unit] = 1;
	}
	if (io->i_boff < 0 || io->i_boff >= lp->d_npartitions ||
	    lp->d_partitions[io->i_boff].p_size == 0) {
		printf("dk bad minor");
		return (EUNIT);
	}
	io->i_boff =
	    (lp->d_partitions[io->i_boff].p_offset * lp->d_secsize) / DEV_BSIZE;
	return (0);
}

/*
 * Reset and initialize the controller.
 */
vdreset_ctlr(ctlr, unit)
	register int ctlr, unit;
{
	register int i;
	register struct vddevice *vdaddr = VDADDR(ctlr);

	if (badaddr(vdaddr, 2)) {
		printf("vd%d: %x: invalid csr\n", ctlr, vdaddr);
		return (ENXIO);
	}
	/* probe further to find what kind of controller it is */
	vdaddr->vdreset = 0xffffffff;
	DELAY(1000000);
	if (vdaddr->vdreset != 0xffffffff) {
		vdtype[ctlr] = VDTYPE_VDDC;
		DELAY(1000000);
	} else {
		vdtype[ctlr] = VDTYPE_SMDE;
		vdaddr->vdrstclr = 0;
		DELAY(3000000);
		vdaddr->vdcsr =  0;
		vdaddr->vdtcf_mdcb = AM_ENPDA;
		vdaddr->vdtcf_dcb = AM_ENPDA;
		vdaddr->vdtcf_trail = AM_ENPDA;
		vdaddr->vdtcf_data = AM_ENPDA;
		vdaddr->vdccf = CCF_SEN | CCF_DER | CCF_STS |
		    XMD_32BIT | BSZ_16WRD |
		    CCF_ENP | CCF_EPE | CCF_EDE | CCF_ECE | CCF_ERR;
	}
	if (!vdcmd(ctlr, 0, VDOP_INIT, 10) ||
	    !vdcmd(ctlr, 0, VDOP_DIAG, 10)) {
		vderror(unit, dcb.opcode == VDOP_INIT ? "init" : "diag", &dcb);
		return (EIO);
	}
	vdinit[ctlr] = 1;
	for (i = unit = ctlr * NDRIVE; i < unit + NDRIVE; i++)
		dkconfigured[i] = 0;
	return (0);
}

/*
 * Reset and configure a drive's parameters.
 */
vdreset_drive(io)
	register struct iob *io;
{
	register int ctlr = VDCTLR(io->i_unit), slave = VDSLAVE(io->i_unit);
	register struct disklabel *lp;
	register struct vddevice *vdaddr = VDADDR(ctlr);
	int pass = 0, type = vdtype[ctlr], error;

	lp = &dklabel[io->i_unit];
again:
	dcb.opcode = VDOP_CONFIG;		/* command */
	dcb.intflg = DCBINT_NONE;
	dcb.nxtdcb = (struct dcb *)0;	/* end of chain */
	dcb.operrsta = 0;
	dcb.devselect = slave;
	dcb.trail.rstrail.ncyl = lp->d_ncylinders;
	dcb.trail.rstrail.nsurfaces = lp->d_ntracks;
	if (type == VDTYPE_SMDE) {
		dcb.trailcnt = sizeof (treset) / sizeof (long);
		dcb.trail.rstrail.nsectors = lp->d_nsectors;
		dcb.trail.rstrail.slip_sec = 0;		/* XXX */
		dcb.trail.rstrail.recovery = 0x18f;	/* ??? */
	} else
		dcb.trailcnt = 2;	/* XXX */
	mdcb.mdcb_head = &dcb;
	mdcb.mdcb_status = 0;
	VDGO(vdaddr, (u_long)&mdcb, type);
	if (!vdpoll(vdaddr, &dcb, 10, type)) {
		if (pass++ != 0) {
			printf(" during drive configuration.\n");
			return (0);
		}
		VDRESET(vdaddr, type);
		if (error = vdreset_ctlr(ctlr, io->i_unit))
			return (error);
		goto again;
	}
	if ((dcb.operrsta & VDERR_HARD) == 0)		/* success */
		return (1);
	if (type == VDTYPE_SMDE && (vdaddr->vdstatus[slave] & STA_US) == 0) {
		printf("dk%d: nonexistent drive\n", io->i_unit);
		return (0);
	}
	if ((dcb.operrsta & (DCBS_OCYL|DCBS_NRDY)) == 0) {
		vderror(io->i_unit, "config", &dcb);
		return (0);
	}
	if (pass++)			/* give up */
		return (0);
	/*
	 * Try to spin up drive with remote command.
	 */
	if (!vdcmd(ctlr, 0, VDOP_START, 62)) {
		vderror(io->i_unit, "start", &dcb);
		return (0);
	}
	DELAY(62000000);
	goto again;
}

vdcmd(ctlr, unit, cmd, time)
	register int ctlr;
	int unit, cmd, time;
{
	register struct vddevice *vdaddr = VDADDR(ctlr);

	dcb.opcode = cmd;
	dcb.intflg = DCBINT_NONE;
	dcb.nxtdcb = (struct dcb *)0;	/* end of chain */
	dcb.operrsta  = 0;
	dcb.devselect = unit;
	dcb.trailcnt = 0;
	mdcb.mdcb_head = &dcb;
	mdcb.mdcb_status = 0;
	VDGO(vdaddr, (u_long)&mdcb, vdtype[ctlr]);
	if (!vdpoll(vdaddr, &dcb, time, vdtype[ctlr]))
		_stop(" during initialization operation.\n");
	return ((dcb.operrsta & VDERR_HARD) == 0);
}

vdstrategy(io, cmd)
	register struct iob *io;
	int cmd;
{
	register struct disklabel *lp;
	int ctlr, cn, tn, sn;
	daddr_t bn;
	struct vddevice *vdaddr;

	if (io->i_cc == 0 || io->i_cc > 65535) {
		printf("dk%d: invalid transfer size %d\n", io->i_unit,
		    io->i_cc);
		io->i_error = EIO;
		return (-1);
	}
	lp = &dklabel[io->i_unit];
	bn = io->i_bn * (DEV_BSIZE / lp->d_secsize);
	cn = bn / lp->d_secpercyl;
	sn = bn % lp->d_secpercyl;
	tn = sn / lp->d_nsectors;
	sn = sn % lp->d_nsectors;

	dcb.opcode = (cmd == READ ? VDOP_RD : VDOP_WD);
	dcb.intflg = DCBINT_NONE;
	dcb.nxtdcb = (struct dcb *)0;	/* end of chain */
	dcb.operrsta  = 0;
	dcb.devselect = VDSLAVE(io->i_unit);
	dcb.trailcnt = sizeof (trrw) / sizeof (int);
	dcb.trail.rwtrail.memadr = io->i_ma; 
	dcb.trail.rwtrail.wcount = (io->i_cc + 1) / sizeof (short);
	dcb.trail.rwtrail.disk.cylinder = cn;
	dcb.trail.rwtrail.disk.track = tn;
	dcb.trail.rwtrail.disk.sector = sn;
	mdcb.mdcb_head = &dcb;
	mdcb.mdcb_status = 0;
	ctlr = VDCTLR(io->i_unit);
	vdaddr = VDADDR(ctlr);
	VDGO(vdaddr, (u_long)&mdcb, vdtype[ctlr]);
	if (!vdpoll(vdaddr, &dcb, 60, vdtype[ctlr]))
		_stop(" during i/o operation.\n");
	if (dcb.operrsta & VDERR_HARD) {
		vderror(io->i_unit, cmd == READ ? "read" : "write", &dcb);
		io->i_error = EIO;
		return (-1);
	}
	mtpr(PADC, 0);
	return (io->i_cc);
}

vderror(unit, cmd, dcb)
	int unit;
	char *cmd;
	struct dcb *dcb;
{

	printf("dk%d: %s error; status %b", unit, cmd,
	    dcb->operrsta, VDERRBITS);
	if (dcb->err_code)
		printf(", code %x", dcb->err_code);
	printf("\n");
}

/*
 * Poll controller until operation
 * completes or timeout expires.
 */
vdpoll(vdaddr, dcb, t, type)
	register struct vddevice *vdaddr;
	register struct dcb *dcb;
	register int t, type;
{

	t *= 1000;
	for (;;) {
		uncache(&dcb->operrsta);
		if (dcb->operrsta & (DCBS_DONE|DCBS_ABORT))
			break;
		if (--t <= 0) {
			printf("vd: controller timeout");
			VDABORT(vdaddr, type);
			DELAY(30000);
			uncache(&dcb->operrsta);
			return (0);
		}
		DELAY(1000);
	}
	if (type == VDTYPE_SMDE) {
		for (;;) {
			uncache(&vdaddr->vdcsr);
			if ((vdaddr->vdcsr & CS_GO) == 0)
				break;
			DELAY(50);
		}
		DELAY(300);
		uncache(&dcb->err_code);
	}
	DELAY(200);
	uncache(&dcb->operrsta);
	return (1);
}

#ifdef COMPAT_42
struct	dkcompat {
	int	nsectors;		/* sectors per track */
	int	ntracks;		/* tracks per cylinder */
	int	ncylinders;		/* cylinders per drive */
#define	NPART	2
	int	poff[NPART];		/* [a+b] for bootstrapping */
} dkcompat[] = {
	{ 48, 24, 711, 0, 61056 },	/* xsd */
	{ 44, 20, 842, 0, 52800 },	/* eagle */
	{ 64, 10, 823, 0, 38400 },	/* fuji 360 */
	{ 32, 24, 711, 0, 40704 },	/* xfd */
	{ 32, 19, 823, 0, 40128 },	/* smd */
	{ 32, 10, 823, 0, 19200 },	/* fsd */
};
#define	NDKCOMPAT	(sizeof (dkcompat) / sizeof (dkcompat[0]))

/*
 * Identify and configure drive from above table
 * by trying to read the last sector until a description
 * is found for which we're successful.
 */
vdmaptype(io)
	struct iob *io;
{
	register struct disklabel *lp = &dklabel[io->i_unit];
	register struct dkcompat *dp;
	int i, ctlr, type;
	struct vddevice *vdaddr;

	ctlr = VDCTLR(io->i_unit);
	vdaddr = VDADDR(ctlr);
	type = vdtype[ctlr];
	for (dp = dkcompat; dp < &dkcompat[NDKCOMPAT]; dp++) {
		if (type == VDTYPE_VDDC && dp->nsectors != 32)
			continue;
		lp->d_nsectors = dp->nsectors;
		lp->d_ntracks = dp->ntracks;
		lp->d_ncylinders = dp->ncylinders;
		if (!vdreset_drive(io))		/* set drive parameters */
			return (EIO);
		dcb.opcode = VDOP_RD;
		dcb.intflg = DCBINT_NONE;
		dcb.nxtdcb = (struct dcb *)0;	/* end of chain */
		dcb.devselect = VDSLAVE(io->i_unit);
		dcb.operrsta = 0;
		dcb.trailcnt = sizeof (trrw) / sizeof (long);
		dcb.trail.rwtrail.memadr = lbuf; 
		dcb.trail.rwtrail.wcount = sizeof (lbuf) / sizeof (short);
		dcb.trail.rwtrail.disk.cylinder = dp->ncylinders - 2;
		dcb.trail.rwtrail.disk.track = dp->ntracks - 1;
		dcb.trail.rwtrail.disk.sector = dp->nsectors - 1;
		mdcb.mdcb_head = &dcb;
		mdcb.mdcb_status = 0;
		VDGO(vdaddr, (u_long)&mdcb, type);
		if (!vdpoll(vdaddr, &dcb, 60, type))
			_stop(" during i/o operation.\n");
		if (dcb.operrsta & VDERR_HARD)
			continue;
		/* simulate necessary parts of disk label */
		lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
		lp->d_secperunit = lp->d_secpercyl * lp->d_ncylinders;
		lp->d_npartitions = NPART;
		for (i = 0; i < NPART; i++) {
			lp->d_partitions[i].p_offset = dp->poff[i];
			lp->d_partitions[i].p_size =
			    lp->d_secperunit - dp->poff[i];
		}
		return (0);
	}
	printf("dk%d: unknown drive type\n", io->i_unit);
	return (ENXIO);
}
#endif

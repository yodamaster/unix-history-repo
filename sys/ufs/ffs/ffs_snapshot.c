/*
 * Copyright 2000 Marshall Kirk McKusick. All Rights Reserved.
 *
 * Further information about snapshots can be obtained from:
 *
 *	Marshall Kirk McKusick		http://www.mckusick.com/softdep/
 *	1614 Oxford Street		mckusick@mckusick.com
 *	Berkeley, CA 94709-1608		+1-510-843-9542
 *	USA
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY MARSHALL KIRK MCKUSICK ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL MARSHALL KIRK MCKUSICK BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_snapshot.c	8.11 (McKusick) 7/23/00
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/stdint.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#define KERNCRED thread0.td_ucred
#define DEBUG 1

static int cgaccount(int, struct vnode *, struct buf *, int);
static int expunge_ufs1(struct vnode *, struct inode *, struct fs *,
    int (*)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *, struct fs *,
    ufs_lbn_t, int), int);
static int indiracct_ufs1(struct vnode *, struct vnode *, int,
    ufs1_daddr_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, struct fs *,
    int (*)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *, struct fs *,
    ufs_lbn_t, int), int);
static int fullacct_ufs1(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int snapacct_ufs1(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int mapacct_ufs1(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int expunge_ufs2(struct vnode *, struct inode *, struct fs *,
    int (*)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *, struct fs *,
    ufs_lbn_t, int), int);
static int indiracct_ufs2(struct vnode *, struct vnode *, int,
    ufs2_daddr_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, struct fs *,
    int (*)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *, struct fs *,
    ufs_lbn_t, int), int);
static int fullacct_ufs2(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int snapacct_ufs2(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int mapacct_ufs2(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int ffs_copyonwrite(struct vnode *, struct buf *);
static int readblock(struct buf *, ufs2_daddr_t);

/*
 * To ensure the consistency of snapshots across crashes, we must
 * synchronously write out copied blocks before allowing the
 * originals to be modified. Because of the rather severe speed
 * penalty that this imposes, the following flag allows this
 * crash persistence to be disabled.
 */
int dopersistence = 0;

#ifdef DEBUG
#include <sys/sysctl.h>
SYSCTL_INT(_debug, OID_AUTO, dopersistence, CTLFLAG_RW, &dopersistence, 0, "");
int snapdebug = 0;
SYSCTL_INT(_debug, OID_AUTO, snapdebug, CTLFLAG_RW, &snapdebug, 0, "");
int collectsnapstats = 0;
SYSCTL_INT(_debug, OID_AUTO, collectsnapstats, CTLFLAG_RW, &collectsnapstats,
	0, "");
#endif /* DEBUG */

/*
 * Create a snapshot file and initialize it for the filesystem.
 */
int
ffs_snapshot(mp, snapfile)
	struct mount *mp;
	char *snapfile;
{
	ufs2_daddr_t numblks, blkno;
	int error, cg, snaploc;
	int i, size, len, loc;
	int flag = mp->mnt_flag;
	struct timespec starttime = {0, 0}, endtime;
	char saved_nice = 0;
	long redo = 0;
	int32_t *lp;
	void *space;
	daddr_t *listhd;
	struct fs *copy_fs = NULL, *fs = VFSTOUFS(mp)->um_fs;
	struct snaphead *snaphead;
	struct thread *td = curthread;
	struct inode *ip, *xp;
	struct buf *bp, *nbp, *ibp, *sbp = NULL;
	struct nameidata nd;
	struct mount *wrtmp;
	struct vattr vat;
	struct vnode *vp, *xvp, *nvp;
	struct uio auio;
	struct iovec aiov;

	/*
	 * Need to serialize access to snapshot code per filesystem.
	 */
	/*
	 * Assign a snapshot slot in the superblock.
	 */
	for (snaploc = 0; snaploc < FSMAXSNAP; snaploc++)
		if (fs->fs_snapinum[snaploc] == 0)
			break;
	if (snaploc == FSMAXSNAP)
		return (ENOSPC);
	/*
	 * Create the snapshot file.
	 */
restart:
	NDINIT(&nd, CREATE, LOCKPARENT | LOCKLEAF, UIO_USERSPACE, snapfile, td);
	if ((error = namei(&nd)) != 0)
		return (error);
	if (nd.ni_vp != NULL) {
		vput(nd.ni_vp);
		error = EEXIST;
	}
	if (nd.ni_dvp->v_mount != mp)
		error = EXDEV;
	if (error) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		return (error);
	}
	VATTR_NULL(&vat);
	vat.va_type = VREG;
	vat.va_mode = S_IRUSR;
	vat.va_vaflags |= VA_EXCLUSIVE;
	if (VOP_GETWRITEMOUNT(nd.ni_dvp, &wrtmp))
		wrtmp = NULL;
	if (wrtmp != mp)
		panic("ffs_snapshot: mount mismatch");
	if (vn_start_write(NULL, &wrtmp, V_NOWAIT) != 0) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		vput(nd.ni_dvp);
		if ((error = vn_start_write(NULL, &wrtmp,
		    V_XSLEEP | PCATCH)) != 0)
			return (error);
		goto restart;
	}
	VOP_LEASE(nd.ni_dvp, td, KERNCRED, LEASE_WRITE);
	error = VOP_CREATE(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vat);
	vput(nd.ni_dvp);
	if (error) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		vn_finished_write(wrtmp);
		return (error);
	}
	vp = nd.ni_vp;
	ip = VTOI(vp);
	/*
	 * Allocate and copy the last block contents so as to be able
	 * to set size to that of the filesystem.
	 */
	numblks = howmany(fs->fs_size, fs->fs_frag);
	error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(numblks - 1)),
	    fs->fs_bsize, KERNCRED, BA_CLRBUF, &bp);
	if (error)
		goto out;
	ip->i_size = lblktosize(fs, (off_t)numblks);
	DIP(ip, i_size) = ip->i_size;
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	if ((error = readblock(bp, numblks - 1)) != 0)
		goto out;
	bawrite(bp);
	/*
	 * Preallocate critical data structures so that we can copy
	 * them in without further allocation after we suspend all
	 * operations on the filesystem. We would like to just release
	 * the allocated buffers without writing them since they will
	 * be filled in below once we are ready to go, but this upsets
	 * the soft update code, so we go ahead and write the new buffers.
	 *
	 * Allocate all indirect blocks and mark all of them as not
	 * needing to be copied.
	 */
	for (blkno = NDADDR; blkno < numblks; blkno += NINDIR(fs)) {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)blkno),
		    fs->fs_bsize, td->td_ucred, BA_METAONLY, &ibp);
		if (error)
			goto out;
		bdwrite(ibp);
	}
	/*
	 * Allocate copies for the superblock and its summary information.
	 */
	error = UFS_BALLOC(vp, lfragtosize(fs, fs->fs_sblockloc),
	    fs->fs_sbsize, KERNCRED, 0, &nbp);
	if (error)
		goto out;
	bawrite(nbp);
	blkno = fragstoblks(fs, fs->fs_csaddr);
	len = howmany(fs->fs_cssize, fs->fs_bsize);
	for (loc = 0; loc < len; loc++) {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(blkno + loc)),
		    fs->fs_bsize, KERNCRED, 0, &nbp);
		if (error)
			goto out;
		bawrite(nbp);
	}
	/*
	 * Allocate all cylinder group blocks.
	 */
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		error = UFS_BALLOC(vp, (off_t)(cgtod(fs, cg)) << fs->fs_fshift,
		    fs->fs_bsize, KERNCRED, 0, &nbp);
		if (error)
			goto out;
		bdwrite(nbp);
	}
	/*
	 * Copy all the cylinder group maps. Although the
	 * filesystem is still active, we hope that only a few
	 * cylinder groups will change between now and when we
	 * suspend operations. Thus, we will be able to quickly
	 * touch up the few cylinder groups that changed during
	 * the suspension period.
	 */
	len = howmany(fs->fs_ncg, NBBY);
	MALLOC(fs->fs_active, int *, len, M_DEVBUF, M_WAITOK);
	bzero(fs->fs_active, len);
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		error = bread(vp, fragstoblks(fs, cgtod(fs, cg)), fs->fs_bsize,
		    KERNCRED, &nbp);
		if (error) {
			brelse(nbp);
			goto out;
		}
		error = cgaccount(cg, vp, nbp, 1);
		bawrite(nbp);
		if (error)
			goto out;
	}
	/*
	 * Change inode to snapshot type file.
	 */
	ip->i_flags |= SF_SNAPSHOT;
	DIP(ip, i_flags) = ip->i_flags;
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	/*
	 * Ensure that the snapshot is completely on disk.
	 */
	if ((error = VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td)) != 0)
		goto out;
	/*
	 * All allocations are done, so we can now snapshot the system.
	 *
	 * Recind nice scheduling while running with the filesystem suspended.
	 */
	if (td->td_ksegrp->kg_nice > 0) {
		saved_nice = td->td_ksegrp->kg_nice;
		td->td_ksegrp->kg_nice = 0;
	}
	/*
	 * Suspend operation on filesystem.
	 */
	for (;;) {
		vn_finished_write(wrtmp);
		vfs_write_suspend(vp->v_mount);
		if (mp->mnt_kern_flag & MNTK_SUSPENDED)
			break;
		vn_start_write(NULL, &wrtmp, V_WAIT);
	}
	if (collectsnapstats)
		nanotime(&starttime);
	/*
	 * First, copy all the cylinder group maps that have changed.
	 */
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		if ((ACTIVECGNUM(fs, cg) & ACTIVECGOFF(cg)) != 0)
			continue;
		redo++;
		error = bread(vp, fragstoblks(fs, cgtod(fs, cg)), fs->fs_bsize,
			KERNCRED, &nbp);
		if (error) {
			brelse(nbp);
			goto out1;
		}
		error = cgaccount(cg, vp, nbp, 2);
		bawrite(nbp);
		if (error)
			goto out1;
	}
	/*
	 * Grab a copy of the superblock and its summary information.
	 * We delay writing it until the suspension is released below.
	 */
	error = bread(vp, fragstoblks(fs, fs->fs_sblockloc), fs->fs_bsize,
	    KERNCRED, &sbp);
	if (error) {
		brelse(sbp);
		sbp = NULL;
		goto out1;
	}
	loc = blkoff(fs, lfragtosize(fs, fs->fs_sblockloc));
	copy_fs = (struct fs *)(sbp->b_data + loc);
	bcopy(fs, copy_fs, fs->fs_sbsize);
	if ((fs->fs_flags & (FS_UNCLEAN | FS_NEEDSFSCK)) == 0)
		copy_fs->fs_clean = 1;
	if (fs->fs_sbsize < SBLOCKSIZE)
		bzero(&sbp->b_data[loc + fs->fs_sbsize],
		    SBLOCKSIZE - fs->fs_sbsize);
	size = blkroundup(fs, fs->fs_cssize);
	if (fs->fs_contigsumsize > 0)
		size += fs->fs_ncg * sizeof(int32_t);
	space = malloc((u_long)size, M_UFSMNT, M_WAITOK);
	copy_fs->fs_csp = space;
	bcopy(fs->fs_csp, copy_fs->fs_csp, fs->fs_cssize);
	(char *)space += fs->fs_cssize;
	loc = howmany(fs->fs_cssize, fs->fs_fsize);
	i = fs->fs_frag - loc % fs->fs_frag;
	len = (i == fs->fs_frag) ? 0 : i * fs->fs_fsize;
	if (len > 0) {
		if ((error = bread(ip->i_devvp,
		    fsbtodb(fs, fs->fs_csaddr + loc),
		    len, KERNCRED, &bp)) != 0) {
			brelse(bp);
			free(copy_fs->fs_csp, M_UFSMNT);
			bawrite(sbp);
			sbp = NULL;
			goto out1;
		}
		bcopy(bp->b_data, space, (u_int)len);
		(char *)space += len;
		bp->b_flags |= B_INVAL | B_NOCACHE;
		brelse(bp);
	}
	if (fs->fs_contigsumsize > 0) {
		copy_fs->fs_maxcluster = lp = space;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
	}
	/*
	 * We must check for active files that have been unlinked
	 * (e.g., with a zero link count). We have to expunge all
	 * trace of these files from the snapshot so that they are
	 * not reclaimed prematurely by fsck or unnecessarily dumped.
	 * We turn off the MNTK_SUSPENDED flag to avoid a panic from
	 * spec_strategy about writing on a suspended filesystem.
	 * Note that we skip unlinked snapshot files as they will
	 * be handled separately below.
	 */
	mp->mnt_kern_flag &= ~MNTK_SUSPENDED;
	mtx_lock(&mntvnode_mtx);
loop:
	for (xvp = TAILQ_FIRST(&mp->mnt_nvnodelist); xvp; xvp = nvp) {
		/*
		 * Make sure this vnode wasn't reclaimed in getnewvnode().
		 * Start over if it has (it won't be on the list anymore).
		 */
		if (xvp->v_mount != mp)
			goto loop;
		nvp = TAILQ_NEXT(xvp, v_nmntvnodes);
		mtx_unlock(&mntvnode_mtx);
		mp_fixme("Unlocked GETATTR.");
		if (vrefcnt(xvp) == 0 || xvp->v_type == VNON ||
		    (VTOI(xvp)->i_flags & SF_SNAPSHOT) ||
		    (VOP_GETATTR(xvp, &vat, td->td_proc->p_ucred, td) == 0 &&
		    vat.va_nlink > 0)) {
			mtx_lock(&mntvnode_mtx);
			continue;
		}
		if (snapdebug)
			vprint("ffs_snapshot: busy vnode", xvp);
		if (vn_lock(xvp, LK_EXCLUSIVE, td) != 0)
			goto loop;
		xp = VTOI(xvp);
		/*
		 * If there is a fragment, clear it here.
		 */
		blkno = 0;
		loc = howmany(xp->i_size, fs->fs_bsize) - 1;
		if (loc < NDADDR) {
			len = fragroundup(fs, blkoff(fs, xp->i_size));
			if (len < fs->fs_bsize) {
				ffs_blkfree(copy_fs, vp, DIP(xp, i_db[loc]),
				    len, xp->i_number);
				blkno = DIP(xp, i_db[loc]);
				DIP(xp, i_db[loc]) = 0;
			}
		}
		if (xp->i_ump->um_fstype == UFS1)
			error = expunge_ufs1(vp, xp, copy_fs, fullacct_ufs1,
			    BLK_NOCOPY);
		else
			error = expunge_ufs2(vp, xp, copy_fs, fullacct_ufs2,
			    BLK_NOCOPY);
		if (blkno)
			DIP(xp, i_db[loc]) = blkno;
		if (!error)
			error = ffs_freefile(copy_fs, vp, xp->i_number,
			    xp->i_mode);
		VOP_UNLOCK(xvp, 0, td);
		if (error) {
			free(copy_fs->fs_csp, M_UFSMNT);
			bawrite(sbp);
			sbp = NULL;
			goto out1;
		}
		mtx_lock(&mntvnode_mtx);
	}
	mtx_unlock(&mntvnode_mtx);
	/*
	 * Record snapshot inode. Since this is the newest snapshot,
	 * it must be placed at the end of the list.
	 */
	fs->fs_snapinum[snaploc] = ip->i_number;
	if (ip->i_nextsnap.tqe_prev != 0)
		panic("ffs_snapshot: %d already on list", ip->i_number);
	snaphead = &ip->i_devvp->v_rdev->si_snapshots;
	TAILQ_INSERT_TAIL(snaphead, ip, i_nextsnap);

	ASSERT_VOP_LOCKED(ip->i_devvp, "ffs_snapshot devvp");
	ip->i_devvp->v_rdev->si_copyonwrite = ffs_copyonwrite;
	ip->i_devvp->v_vflag |= VV_COPYONWRITE;
	ASSERT_VOP_LOCKED(vp, "ffs_snapshot vp");
	vp->v_vflag |= VV_SYSTEM;
out1:
	/*
	 * Resume operation on filesystem.
	 */
	vfs_write_resume(vp->v_mount);
	if (saved_nice > 0)
		td->td_ksegrp->kg_nice = saved_nice;
	vn_start_write(NULL, &wrtmp, V_WAIT);
	if (collectsnapstats && starttime.tv_sec > 0) {
		nanotime(&endtime);
		timespecsub(&endtime, &starttime);
		printf("%s: suspended %d.%03ld sec, redo %ld of %d\n",
		    vp->v_mount->mnt_stat.f_mntonname, endtime.tv_sec,
		    endtime.tv_nsec / 1000000, redo, fs->fs_ncg);
	}
	if (sbp == NULL)
		goto out;
	/*
	 * Copy allocation information from all the snapshots in
	 * this snapshot and then expunge them from its view.
	 */
	snaphead = &ip->i_devvp->v_rdev->si_snapshots;
	TAILQ_FOREACH(xp, snaphead, i_nextsnap) {
		if (xp == ip)
			break;
		if (xp->i_ump->um_fstype == UFS1)
			error = expunge_ufs1(vp, xp, fs, snapacct_ufs1,
			    BLK_SNAP);
		else
			error = expunge_ufs2(vp, xp, fs, snapacct_ufs2,
			    BLK_SNAP);
		if (error) {
			fs->fs_snapinum[snaploc] = 0;
			goto done;
		}
	}
	/*
	 * Allocate the space for the list of preallocated snapshot blocks.
	 */
	ip->i_snaplistsize = fragstoblks(fs, dbtofsb(fs, DIP(ip,i_blocks))) + 1;
	MALLOC(listhd, daddr_t *, ip->i_snaplistsize * sizeof(daddr_t),
	    M_UFSMNT, M_WAITOK);
	ip->i_snapblklist = listhd;
	*ip->i_snapblklist++ = ip->i_snaplistsize;
	/*
	 * Expunge the blocks used by the snapshots from the set of
	 * blocks marked as used in the snapshot bitmaps. Also, collect
	 * the list of allocated blocks in i_snapblklist.
	 */
	if (ip->i_ump->um_fstype == UFS1)
		error = expunge_ufs1(vp, ip, copy_fs, mapacct_ufs1, BLK_SNAP);
	else
		error = expunge_ufs2(vp, ip, copy_fs, mapacct_ufs2, BLK_SNAP);
	if (error) {
		fs->fs_snapinum[snaploc] = 0;
		FREE(listhd, M_UFSMNT);
		goto done;
	}
	/*
	 * Write out the list of allocated blocks to the end of the snapshot.
	 */
	if (ip->i_snapblklist - listhd != ip->i_snaplistsize)
		printf("Snaplist mismatch, got %d should be %jd\n",
		    ip->i_snapblklist - listhd, (intmax_t)ip->i_snaplistsize);
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (void *)listhd;
	aiov.iov_len = ip->i_snaplistsize * sizeof(daddr_t);
	auio.uio_resid = aiov.iov_len;;
	auio.uio_offset = ip->i_size;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;
	if ((error = VOP_WRITE(vp, &auio, IO_UNIT, td->td_ucred)) != 0) {
		fs->fs_snapinum[snaploc] = 0;
		FREE(listhd, M_UFSMNT);
		goto done;
	}
	ip->i_snapblklist = listhd;
	/*
	 * Write the superblock and its summary information
	 * to the snapshot.
	 */
	blkno = fragstoblks(fs, fs->fs_csaddr);
	len = howmany(fs->fs_cssize, fs->fs_bsize);
	space = copy_fs->fs_csp;
	for (loc = 0; loc < len; loc++) {
		error = bread(vp, blkno + loc, fs->fs_bsize, KERNCRED, &nbp);
		if (error) {
			brelse(nbp);
			fs->fs_snapinum[snaploc] = 0;
			FREE(listhd, M_UFSMNT);
			ip->i_snapblklist = NULL;
			goto done;
		}
		bcopy(space, nbp->b_data, fs->fs_bsize);
		space = (char *)space + fs->fs_bsize;
		bawrite(nbp);
	}
done:
	free(copy_fs->fs_csp, M_UFSMNT);
	bawrite(sbp);
out:
	if (fs->fs_active != 0) {
		FREE(fs->fs_active, M_DEVBUF);
		fs->fs_active = 0;
	}
	mp->mnt_flag = flag;
	if (error)
		(void) UFS_TRUNCATE(vp, (off_t)0, 0, NOCRED, td);
	(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
	if (error)
		vput(vp);
	else
		VOP_UNLOCK(vp, 0, td);
	vn_finished_write(wrtmp);
	return (error);
}

/*
 * Copy a cylinder group map. All the unallocated blocks are marked
 * BLK_NOCOPY so that the snapshot knows that it need not copy them
 * if they are later written. If passno is one, then this is a first
 * pass, so only setting needs to be done. If passno is 2, then this
 * is a revision to a previous pass which must be undone as the
 * replacement pass is done.
 */
static int
cgaccount(cg, vp, nbp, passno)
	int cg;
	struct vnode *vp;
	struct buf *nbp;
	int passno;
{
	struct buf *bp, *ibp;
	struct inode *ip;
	struct cg *cgp;
	struct fs *fs;
	ufs2_daddr_t base, numblks;
	int error, len, loc, indiroff;

	ip = VTOI(vp);
	fs = ip->i_fs;
	error = bread(ip->i_devvp, fsbtodb(fs, cgtod(fs, cg)),
		(int)fs->fs_cgsize, KERNCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp)) {
		brelse(bp);
		return (EIO);
	}
	atomic_set_int(&ACTIVECGNUM(fs, cg), ACTIVECGOFF(cg));
	bcopy(bp->b_data, nbp->b_data, fs->fs_cgsize);
	if (fs->fs_cgsize < fs->fs_bsize)
		bzero(&nbp->b_data[fs->fs_cgsize],
		    fs->fs_bsize - fs->fs_cgsize);
	if (passno == 2)
		nbp->b_flags |= B_VALIDSUSPWRT;
	numblks = howmany(fs->fs_size, fs->fs_frag);
	len = howmany(fs->fs_fpg, fs->fs_frag);
	base = cg * fs->fs_fpg / fs->fs_frag;
	if (base + len >= numblks)
		len = numblks - base - 1;
	loc = 0;
	if (base < NDADDR) {
		for ( ; loc < NDADDR; loc++) {
			if (ffs_isblock(fs, cg_blksfree(cgp), loc))
				DIP(ip, i_db[loc]) = BLK_NOCOPY;
			else if (passno == 2 && DIP(ip, i_db[loc])== BLK_NOCOPY)
				DIP(ip, i_db[loc]) = 0;
			else if (passno == 1 && DIP(ip, i_db[loc])== BLK_NOCOPY)
				panic("ffs_snapshot: lost direct block");
		}
	}
	error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(base + loc)),
	    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
	if (error) {
		brelse(bp);
		return (error);
	}
	indiroff = (base + loc - NDADDR) % NINDIR(fs);
	for ( ; loc < len; loc++, indiroff++) {
		if (indiroff >= NINDIR(fs)) {
			if (passno == 2)
				ibp->b_flags |= B_VALIDSUSPWRT;
			bawrite(ibp);
			error = UFS_BALLOC(vp,
			    lblktosize(fs, (off_t)(base + loc)),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			if (error) {
				brelse(bp);
				return (error);
			}
			indiroff = 0;
		}
		if (ip->i_ump->um_fstype == UFS1) {
			if (ffs_isblock(fs, cg_blksfree(cgp), loc))
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] =
				    BLK_NOCOPY;
			else if (passno == 2 && ((ufs1_daddr_t *)(ibp->b_data))
			    [indiroff] == BLK_NOCOPY)
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] = 0;
			else if (passno == 1 && ((ufs1_daddr_t *)(ibp->b_data))
			    [indiroff] == BLK_NOCOPY)
				panic("ffs_snapshot: lost indirect block");
			continue;
		}
		if (ffs_isblock(fs, cg_blksfree(cgp), loc))
			((ufs2_daddr_t *)(ibp->b_data))[indiroff] = BLK_NOCOPY;
		else if (passno == 2 &&
		    ((ufs2_daddr_t *)(ibp->b_data)) [indiroff] == BLK_NOCOPY)
			((ufs2_daddr_t *)(ibp->b_data))[indiroff] = 0;
		else if (passno == 1 &&
		    ((ufs2_daddr_t *)(ibp->b_data)) [indiroff] == BLK_NOCOPY)
			panic("ffs_snapshot: lost indirect block");
	}
	bqrelse(bp);
	if (passno == 2)
		ibp->b_flags |= B_VALIDSUSPWRT;
	bdwrite(ibp);
	return (0);
}

/*
 * Before expunging a snapshot inode, note all the
 * blocks that it claims with BLK_SNAP so that fsck will
 * be able to account for those blocks properly and so
 * that this snapshot knows that it need not copy them
 * if the other snapshot holding them is freed. This code
 * is reproduced once each for UFS1 and UFS2.
 */
static int
expunge_ufs1(snapvp, cancelip, fs, acctfunc, expungetype)
	struct vnode *snapvp;
	struct inode *cancelip;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
{
	int i, error, indiroff;
	ufs_lbn_t lbn, rlbn;
	ufs2_daddr_t len, blkno, numblks, blksperindir;
	struct ufs1_dinode *dip;
	struct thread *td = curthread;
	struct buf *bp;

	numblks = howmany(cancelip->i_size, fs->fs_bsize);
	if ((error = (*acctfunc)(snapvp, &cancelip->i_din1->di_db[0],
	    &cancelip->i_din1->di_ib[NIADDR], fs, 0, expungetype)))
		return (error);
	blksperindir = 1;
	lbn = -NDADDR;
	len = numblks - NDADDR;
	rlbn = NDADDR;
	for (i = 0; len > 0 && i < NIADDR; i++) {
		error = indiracct_ufs1(snapvp, ITOV(cancelip), i,
		    cancelip->i_din1->di_ib[i], lbn, rlbn, len,
		    blksperindir, fs, acctfunc, expungetype);
		if (error)
			return (error);
		blksperindir *= NINDIR(fs);
		lbn -= blksperindir + 1;
		len -= blksperindir;
		rlbn += blksperindir;
	}
	/*
	 * Prepare to expunge the inode. If its inode block has not
	 * yet been copied, then allocate and fill the copy.
	 */
	lbn = fragstoblks(fs, ino_to_fsba(fs, cancelip->i_number));
	blkno = 0;
	if (lbn < NDADDR) {
		blkno = cancelip->i_din1->di_db[lbn];
	} else {
		td->td_proc->p_flag |= P_COWINPROGRESS;
		error = UFS_BALLOC(snapvp, lblktosize(fs, (off_t)lbn),
		   fs->fs_bsize, KERNCRED, BA_METAONLY, &bp);
		td->td_proc->p_flag &= ~P_COWINPROGRESS;
		if (error)
			return (error);
		indiroff = (lbn - NDADDR) % NINDIR(fs);
		blkno = ((ufs1_daddr_t *)(bp->b_data))[indiroff];
		bqrelse(bp);
	}
	error = UFS_BALLOC(snapvp, lblktosize(fs, (off_t)lbn),
	    fs->fs_bsize, KERNCRED, 0, &bp);
	if (error)
		return (error);
	if (blkno == 0 && (error = readblock(bp, lbn)))
		return (error);
	/*
	 * Set a snapshot inode to be a zero length file, regular files
	 * to be completely unallocated.
	 */
	dip = (struct ufs1_dinode *)bp->b_data +
	    ino_to_fsbo(fs, cancelip->i_number);
	if (expungetype == BLK_NOCOPY)
		dip->di_mode = 0;
	dip->di_size = 0;
	dip->di_blocks = 0;
	dip->di_flags &= ~SF_SNAPSHOT;
	bzero(&dip->di_db[0], (NDADDR + NIADDR) * sizeof(ufs1_daddr_t));
	bdwrite(bp);
	return (0);
}

/*
 * Descend an indirect block chain for vnode cancelvp accounting for all
 * its indirect blocks in snapvp.
 */ 
static int
indiracct_ufs1(snapvp, cancelvp, level, blkno, lbn, rlbn, remblks,
	    blksperindir, fs, acctfunc, expungetype)
	struct vnode *snapvp;
	struct vnode *cancelvp;
	int level;
	ufs1_daddr_t blkno;
	ufs_lbn_t lbn;
	ufs_lbn_t rlbn;
	ufs_lbn_t remblks;
	ufs_lbn_t blksperindir;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
{
	int error, num, i;
	ufs_lbn_t subblksperindir;
	struct indir indirs[NIADDR + 2];
	ufs1_daddr_t last, *bap;
	struct buf *bp;

	if ((error = ufs_getlbns(cancelvp, rlbn, indirs, &num)) != 0)
		return (error);
	if (lbn != indirs[num - 1 - level].in_lbn || blkno == 0 || num < 2)
		panic("indiracct: botched params");
	/*
	 * We have to expand bread here since it will deadlock looking
	 * up the block number for any blocks that are not in the cache.
	 */
	bp = getblk(cancelvp, lbn, fs->fs_bsize, 0, 0);
	bp->b_blkno = fsbtodb(fs, blkno);
	if ((bp->b_flags & (B_DONE | B_DELWRI)) == 0 &&
	    (error = readblock(bp, fragstoblks(fs, blkno)))) {
		brelse(bp);
		return (error);
	}
	/*
	 * Account for the block pointers in this indirect block.
	 */
	last = howmany(remblks, blksperindir);
	if (last > NINDIR(fs))
		last = NINDIR(fs);
	MALLOC(bap, ufs1_daddr_t *, fs->fs_bsize, M_DEVBUF, M_WAITOK);
	bcopy(bp->b_data, (caddr_t)bap, fs->fs_bsize);
	bqrelse(bp);
	error = (*acctfunc)(snapvp, &bap[0], &bap[last], fs, rlbn, expungetype);
	if (error || level == 0)
		goto out;
	/*
	 * Account for the block pointers in each of the indirect blocks
	 * in the levels below us.
	 */
	subblksperindir = blksperindir / NINDIR(fs);
	for (lbn++, level--, i = 0; i < last; i++) {
		error = indiracct_ufs1(snapvp, cancelvp, level, bap[i], lbn,
		    rlbn, remblks, subblksperindir, fs, acctfunc, expungetype);
		if (error)
			goto out;
		rlbn += blksperindir;
		lbn -= blksperindir;
		remblks -= blksperindir;
	}
out:
	FREE(bap, M_DEVBUF);
	return (error);
}

/*
 * Do both snap accounting and map accounting.
 */
static int
fullacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, exptype)
	struct vnode *vp;
	ufs1_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int exptype;	/* BLK_SNAP or BLK_NOCOPY */
{
	int error;

	if ((error = snapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, exptype)))
		return (error);
	return (mapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, exptype));
}

/*
 * Identify a set of blocks allocated in a snapshot inode.
 */
static int
snapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs1_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;	/* BLK_SNAP or BLK_NOCOPY */
{
	struct inode *ip = VTOI(vp);
	ufs1_daddr_t blkno, *blkp;
	ufs_lbn_t lbn;
	struct buf *ibp;
	int error;

	for ( ; oldblkp < lastblkp; oldblkp++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY || blkno == BLK_SNAP)
			continue;
		lbn = fragstoblks(fs, blkno);
		if (lbn < NDADDR) {
			blkp = &ip->i_din1->di_db[lbn];
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		} else {
			error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			if (error)
				return (error);
			blkp = &((ufs1_daddr_t *)(ibp->b_data))
			    [(lbn - NDADDR) % NINDIR(fs)];
		}
		/*
		 * If we are expunging a snapshot vnode and we
		 * find a block marked BLK_NOCOPY, then it is
		 * one that has been allocated to this snapshot after
		 * we took our current snapshot and can be ignored.
		 */
		if (expungetype == BLK_SNAP && *blkp == BLK_NOCOPY) {
			if (lbn >= NDADDR)
				brelse(ibp);
		} else {
			if (*blkp != 0)
				panic("snapacct: bad block");
			*blkp = expungetype;
			if (lbn >= NDADDR)
				bdwrite(ibp);
		}
	}
	return (0);
}

/*
 * Account for a set of blocks allocated in a snapshot inode.
 */
static int
mapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs1_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;
{
	ufs1_daddr_t blkno;
	struct inode *ip;
	ino_t inum;

	ip = VTOI(vp);
	inum = ip->i_number;
	for ( ; oldblkp < lastblkp; oldblkp++, lblkno++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY)
			continue;
		if (expungetype == BLK_SNAP && blkno != BLK_SNAP)
			*ip->i_snapblklist++ = lblkno;
		if (blkno == BLK_SNAP)
			blkno = blkstofrags(fs, lblkno);
		ffs_blkfree(fs, vp, blkno, fs->fs_bsize, inum);
	}
	return (0);
}

/*
 * Before expunging a snapshot inode, note all the
 * blocks that it claims with BLK_SNAP so that fsck will
 * be able to account for those blocks properly and so
 * that this snapshot knows that it need not copy them
 * if the other snapshot holding them is freed. This code
 * is reproduced once each for UFS1 and UFS2.
 */
static int
expunge_ufs2(snapvp, cancelip, fs, acctfunc, expungetype)
	struct vnode *snapvp;
	struct inode *cancelip;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
{
	int i, error, indiroff;
	ufs_lbn_t lbn, rlbn;
	ufs2_daddr_t len, blkno, numblks, blksperindir;
	struct ufs2_dinode *dip;
	struct thread *td = curthread;
	struct buf *bp;

	numblks = howmany(cancelip->i_size, fs->fs_bsize);
	if ((error = (*acctfunc)(snapvp, &cancelip->i_din2->di_db[0],
	    &cancelip->i_din2->di_ib[NIADDR], fs, 0, expungetype)))
		return (error);
	blksperindir = 1;
	lbn = -NDADDR;
	len = numblks - NDADDR;
	rlbn = NDADDR;
	for (i = 0; len > 0 && i < NIADDR; i++) {
		error = indiracct_ufs2(snapvp, ITOV(cancelip), i,
		    cancelip->i_din2->di_ib[i], lbn, rlbn, len,
		    blksperindir, fs, acctfunc, expungetype);
		if (error)
			return (error);
		blksperindir *= NINDIR(fs);
		lbn -= blksperindir + 1;
		len -= blksperindir;
		rlbn += blksperindir;
	}
	/*
	 * Prepare to expunge the inode. If its inode block has not
	 * yet been copied, then allocate and fill the copy.
	 */
	lbn = fragstoblks(fs, ino_to_fsba(fs, cancelip->i_number));
	blkno = 0;
	if (lbn < NDADDR) {
		blkno = cancelip->i_din2->di_db[lbn];
	} else {
		td->td_proc->p_flag |= P_COWINPROGRESS;
		error = UFS_BALLOC(snapvp, lblktosize(fs, (off_t)lbn),
		   fs->fs_bsize, KERNCRED, BA_METAONLY, &bp);
		td->td_proc->p_flag &= ~P_COWINPROGRESS;
		if (error)
			return (error);
		indiroff = (lbn - NDADDR) % NINDIR(fs);
		blkno = ((ufs2_daddr_t *)(bp->b_data))[indiroff];
		bqrelse(bp);
	}
	error = UFS_BALLOC(snapvp, lblktosize(fs, (off_t)lbn),
	    fs->fs_bsize, KERNCRED, 0, &bp);
	if (error)
		return (error);
	if (blkno == 0 && (error = readblock(bp, lbn)))
		return (error);
	/*
	 * Set a snapshot inode to be a zero length file, regular files
	 * to be completely unallocated.
	 */
	dip = (struct ufs2_dinode *)bp->b_data +
	    ino_to_fsbo(fs, cancelip->i_number);
	if (expungetype == BLK_NOCOPY)
		dip->di_mode = 0;
	dip->di_size = 0;
	dip->di_blocks = 0;
	dip->di_flags &= ~SF_SNAPSHOT;
	bzero(&dip->di_db[0], (NDADDR + NIADDR) * sizeof(ufs2_daddr_t));
	bdwrite(bp);
	return (0);
}

/*
 * Descend an indirect block chain for vnode cancelvp accounting for all
 * its indirect blocks in snapvp.
 */ 
static int
indiracct_ufs2(snapvp, cancelvp, level, blkno, lbn, rlbn, remblks,
	    blksperindir, fs, acctfunc, expungetype)
	struct vnode *snapvp;
	struct vnode *cancelvp;
	int level;
	ufs2_daddr_t blkno;
	ufs_lbn_t lbn;
	ufs_lbn_t rlbn;
	ufs_lbn_t remblks;
	ufs_lbn_t blksperindir;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
{
	int error, num, i;
	ufs_lbn_t subblksperindir;
	struct indir indirs[NIADDR + 2];
	ufs2_daddr_t last, *bap;
	struct buf *bp;

	if ((error = ufs_getlbns(cancelvp, rlbn, indirs, &num)) != 0)
		return (error);
	if (lbn != indirs[num - 1 - level].in_lbn || blkno == 0 || num < 2)
		panic("indiracct: botched params");
	/*
	 * We have to expand bread here since it will deadlock looking
	 * up the block number for any blocks that are not in the cache.
	 */
	bp = getblk(cancelvp, lbn, fs->fs_bsize, 0, 0);
	bp->b_blkno = fsbtodb(fs, blkno);
	if ((bp->b_flags & (B_DONE | B_DELWRI)) == 0 &&
	    (error = readblock(bp, fragstoblks(fs, blkno)))) {
		brelse(bp);
		return (error);
	}
	/*
	 * Account for the block pointers in this indirect block.
	 */
	last = howmany(remblks, blksperindir);
	if (last > NINDIR(fs))
		last = NINDIR(fs);
	MALLOC(bap, ufs2_daddr_t *, fs->fs_bsize, M_DEVBUF, M_WAITOK);
	bcopy(bp->b_data, (caddr_t)bap, fs->fs_bsize);
	bqrelse(bp);
	error = (*acctfunc)(snapvp, &bap[0], &bap[last], fs, rlbn, expungetype);
	if (error || level == 0)
		goto out;
	/*
	 * Account for the block pointers in each of the indirect blocks
	 * in the levels below us.
	 */
	subblksperindir = blksperindir / NINDIR(fs);
	for (lbn++, level--, i = 0; i < last; i++) {
		error = indiracct_ufs2(snapvp, cancelvp, level, bap[i], lbn,
		    rlbn, remblks, subblksperindir, fs, acctfunc, expungetype);
		if (error)
			goto out;
		rlbn += blksperindir;
		lbn -= blksperindir;
		remblks -= blksperindir;
	}
out:
	FREE(bap, M_DEVBUF);
	return (error);
}

/*
 * Do both snap accounting and map accounting.
 */
static int
fullacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, exptype)
	struct vnode *vp;
	ufs2_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int exptype;	/* BLK_SNAP or BLK_NOCOPY */
{
	int error;

	if ((error = snapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, exptype)))
		return (error);
	return (mapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, exptype));
}

/*
 * Identify a set of blocks allocated in a snapshot inode.
 */
static int
snapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs2_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;	/* BLK_SNAP or BLK_NOCOPY */
{
	struct inode *ip = VTOI(vp);
	ufs2_daddr_t blkno, *blkp;
	ufs_lbn_t lbn;
	struct buf *ibp;
	int error;

	for ( ; oldblkp < lastblkp; oldblkp++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY || blkno == BLK_SNAP)
			continue;
		lbn = fragstoblks(fs, blkno);
		if (lbn < NDADDR) {
			blkp = &ip->i_din2->di_db[lbn];
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		} else {
			error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			if (error)
				return (error);
			blkp = &((ufs2_daddr_t *)(ibp->b_data))
			    [(lbn - NDADDR) % NINDIR(fs)];
		}
		/*
		 * If we are expunging a snapshot vnode and we
		 * find a block marked BLK_NOCOPY, then it is
		 * one that has been allocated to this snapshot after
		 * we took our current snapshot and can be ignored.
		 */
		if (expungetype == BLK_SNAP && *blkp == BLK_NOCOPY) {
			if (lbn >= NDADDR)
				brelse(ibp);
		} else {
			if (*blkp != 0)
				panic("snapacct: bad block");
			*blkp = expungetype;
			if (lbn >= NDADDR)
				bdwrite(ibp);
		}
	}
	return (0);
}

/*
 * Account for a set of blocks allocated in a snapshot inode.
 */
static int
mapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs2_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;
{
	ufs2_daddr_t blkno;
	struct inode *ip;
	ino_t inum;

	ip = VTOI(vp);
	inum = ip->i_number;
	for ( ; oldblkp < lastblkp; oldblkp++, lblkno++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY)
			continue;
		if (expungetype == BLK_SNAP && blkno != BLK_SNAP)
			*ip->i_snapblklist++ = lblkno;
		if (blkno == BLK_SNAP)
			blkno = blkstofrags(fs, lblkno);
		ffs_blkfree(fs, vp, blkno, fs->fs_bsize, inum);
	}
	return (0);
}

/*
 * Decrement extra reference on snapshot when last name is removed.
 * It will not be freed until the last open reference goes away.
 */
void
ffs_snapgone(ip)
	struct inode *ip;
{
	struct inode *xp;
	struct fs *fs;
	int snaploc;

	/*
	 * Find snapshot in incore list.
	 */
	TAILQ_FOREACH(xp, &ip->i_devvp->v_rdev->si_snapshots, i_nextsnap)
		if (xp == ip)
			break;
	if (xp == 0)
		printf("ffs_snapgone: lost snapshot vnode %d\n",
		    ip->i_number);
	else
		vrele(ITOV(ip));
	/*
	 * Delete snapshot inode from superblock. Keep list dense.
	 */
	fs = ip->i_fs;
	for (snaploc = 0; snaploc < FSMAXSNAP; snaploc++)
		if (fs->fs_snapinum[snaploc] == ip->i_number)
			break;
	if (snaploc < FSMAXSNAP) {
		for (snaploc++; snaploc < FSMAXSNAP; snaploc++) {
			if (fs->fs_snapinum[snaploc] == 0)
				break;
			fs->fs_snapinum[snaploc - 1] = fs->fs_snapinum[snaploc];
		}
		fs->fs_snapinum[snaploc - 1] = 0;
	}
}

/*
 * Prepare a snapshot file for being removed.
 */
void
ffs_snapremove(vp)
	struct vnode *vp;
{
	struct inode *ip;
	struct vnode *devvp;
	struct buf *ibp;
	struct fs *fs;
	ufs2_daddr_t numblks, blkno, dblk;
	int error, loc, last;

	ip = VTOI(vp);
	fs = ip->i_fs;
	/*
	 * If active, delete from incore list (this snapshot may
	 * already have been in the process of being deleted, so
	 * would not have been active).
	 *
	 * Clear copy-on-write flag if last snapshot.
	 */
	if (ip->i_nextsnap.tqe_prev != 0) {
		devvp = ip->i_devvp;
		TAILQ_REMOVE(&devvp->v_rdev->si_snapshots, ip, i_nextsnap);
		ip->i_nextsnap.tqe_prev = 0;
		ASSERT_VOP_LOCKED(devvp, "ffs_snapremove devvp");
		if (TAILQ_FIRST(&devvp->v_rdev->si_snapshots) == 0) {
			devvp->v_rdev->si_copyonwrite = 0;
			devvp->v_vflag &= ~VV_COPYONWRITE;
		}
	}
	/*
	 * Get rid of its hints list.
	 */
	if (ip->i_snapblklist != NULL) {
		FREE(ip->i_snapblklist, M_UFSMNT);
		ip->i_snapblklist = NULL;
	}
	/*
	 * Clear all BLK_NOCOPY fields. Pass any block claims to other
	 * snapshots that want them (see ffs_snapblkfree below).
	 */
	for (blkno = 1; blkno < NDADDR; blkno++) {
		dblk = DIP(ip, i_db[blkno]);
		if (dblk == BLK_NOCOPY || dblk == BLK_SNAP)
			DIP(ip, i_db[blkno]) = 0;
		else if ((dblk == blkstofrags(fs, blkno) &&
		     ffs_snapblkfree(fs, ip->i_devvp, dblk, fs->fs_bsize,
		     ip->i_number))) {
			DIP(ip, i_blocks) -= btodb(fs->fs_bsize);
			DIP(ip, i_db[blkno]) = 0;
		}
	}
	numblks = howmany(ip->i_size, fs->fs_bsize);
	for (blkno = NDADDR; blkno < numblks; blkno += NINDIR(fs)) {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)blkno),
		    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
		if (error)
			continue;
		if (fs->fs_size - blkno > NINDIR(fs))
			last = NINDIR(fs);
		else
			last = fs->fs_size - blkno;
		for (loc = 0; loc < last; loc++) {
			if (ip->i_ump->um_fstype == UFS1) {
				dblk = ((ufs1_daddr_t *)(ibp->b_data))[loc];
				if (dblk == BLK_NOCOPY || dblk == BLK_SNAP)
					((ufs1_daddr_t *)(ibp->b_data))[loc]= 0;
				else if ((dblk == blkstofrags(fs, blkno) &&
				     ffs_snapblkfree(fs, ip->i_devvp, dblk,
				     fs->fs_bsize, ip->i_number))) {
					ip->i_din1->di_blocks -=
					    btodb(fs->fs_bsize);
					((ufs1_daddr_t *)(ibp->b_data))[loc]= 0;
				}
				continue;
			}
			dblk = ((ufs2_daddr_t *)(ibp->b_data))[loc];
			if (dblk == BLK_NOCOPY || dblk == BLK_SNAP)
				((ufs2_daddr_t *)(ibp->b_data))[loc] = 0;
			else if ((dblk == blkstofrags(fs, blkno) &&
			     ffs_snapblkfree(fs, ip->i_devvp, dblk,
			     fs->fs_bsize, ip->i_number))) {
				ip->i_din2->di_blocks -= btodb(fs->fs_bsize);
				((ufs2_daddr_t *)(ibp->b_data))[loc] = 0;
			}
		}
		bawrite(ibp);
	}
	/*
	 * Clear snapshot flag and drop reference.
	 */
	ip->i_flags &= ~SF_SNAPSHOT;
	DIP(ip, i_flags) = ip->i_flags;
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
}

/*
 * Notification that a block is being freed. Return zero if the free
 * should be allowed to proceed. Return non-zero if the snapshot file
 * wants to claim the block. The block will be claimed if it is an
 * uncopied part of one of the snapshots. It will be freed if it is
 * either a BLK_NOCOPY or has already been copied in all of the snapshots.
 * If a fragment is being freed, then all snapshots that care about
 * it must make a copy since a snapshot file can only claim full sized
 * blocks. Note that if more than one snapshot file maps the block,
 * we can pick one at random to claim it. Since none of the snapshots
 * can change, we are assurred that they will all see the same unmodified
 * image. When deleting a snapshot file (see ffs_snapremove above), we
 * must push any of these claimed blocks to one of the other snapshots
 * that maps it. These claimed blocks are easily identified as they will
 * have a block number equal to their logical block number within the
 * snapshot. A copied block can never have this property because they
 * must always have been allocated from a BLK_NOCOPY location.
 */
int
ffs_snapblkfree(fs, devvp, bno, size, inum)
	struct fs *fs;
	struct vnode *devvp;
	ufs2_daddr_t bno;
	long size;
	ino_t inum;
{
	struct buf *ibp, *cbp, *savedcbp = 0;
	struct thread *td = curthread;
	struct inode *ip;
	struct vnode *vp;
	ufs_lbn_t lbn;
	ufs2_daddr_t blkno;
	int indiroff = 0, error = 0, claimedblk = 0;
	struct snaphead *snaphead;

	lbn = fragstoblks(fs, bno);
	snaphead = &devvp->v_rdev->si_snapshots;
	TAILQ_FOREACH(ip, snaphead, i_nextsnap) {
		vp = ITOV(ip);
		/*
		 * Lookup block being written.
		 */
		if (lbn < NDADDR) {
			blkno = DIP(ip, i_db[lbn]);
		} else {
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
			td->td_proc->p_flag |= P_COWINPROGRESS;
			error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			td->td_proc->p_flag &= ~P_COWINPROGRESS;
			VOP_UNLOCK(vp, 0, td);
			if (error)
				break;
			indiroff = (lbn - NDADDR) % NINDIR(fs);
			if (ip->i_ump->um_fstype == UFS1)
				blkno=((ufs1_daddr_t *)(ibp->b_data))[indiroff];
			else
				blkno=((ufs2_daddr_t *)(ibp->b_data))[indiroff];
		}
		/*
		 * Check to see if block needs to be copied.
		 */
		if (blkno == 0) {
			/*
			 * A block that we map is being freed. If it has not
			 * been claimed yet, we will claim or copy it (below).
			 */
			claimedblk = 1;
		} else if (blkno == BLK_SNAP) {
			/*
			 * No previous snapshot claimed the block,
			 * so it will be * freed and become a BLK_NOCOPY
			 * (don't care) for us.
			 */
			if (claimedblk)
				panic("snapblkfree: inconsistent block type");
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
			if (lbn < NDADDR) {
				DIP(ip, i_db[lbn]) = BLK_NOCOPY;
				ip->i_flag |= IN_CHANGE | IN_UPDATE;
			} else if (ip->i_ump->um_fstype == UFS1) {
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] =
				    BLK_NOCOPY;
				bdwrite(ibp);
			} else {
				((ufs2_daddr_t *)(ibp->b_data))[indiroff] =
				    BLK_NOCOPY;
				bdwrite(ibp);
			}
			VOP_UNLOCK(vp, 0, td);
			continue;
		} else /* BLK_NOCOPY or default */ {
			/*
			 * If the snapshot has already copied the block
			 * (default), or does not care about the block,
			 * it is not needed.
			 */
			if (lbn >= NDADDR)
				bqrelse(ibp);
			continue;
		}
		/*
		 * If this is a full size block, we will just grab it
		 * and assign it to the snapshot inode. Otherwise we
		 * will proceed to copy it. See explanation for this
		 * routine as to why only a single snapshot needs to
		 * claim this block.
		 */
		if (size == fs->fs_bsize) {
#ifdef DEBUG
			if (snapdebug)
				printf("%s %d lbn %jd from inum %d\n",
				    "Grabonremove: snapino", ip->i_number,
				    (intmax_t)lbn, inum);
#endif
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
			if (lbn < NDADDR) {
				DIP(ip, i_db[lbn]) = bno;
			} else if (ip->i_ump->um_fstype == UFS1) {
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] = bno;
				bdwrite(ibp);
			} else {
				((ufs2_daddr_t *)(ibp->b_data))[indiroff] = bno;
				bdwrite(ibp);
			}
			DIP(ip, i_blocks) += btodb(size);
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			VOP_UNLOCK(vp, 0, td);
			return (1);
		}
		if (lbn >= NDADDR)
			bqrelse(ibp);
		/*
		 * Allocate the block into which to do the copy. Note that this
		 * allocation will never require any additional allocations for
		 * the snapshot inode.
		 */
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
		td->td_proc->p_flag |= P_COWINPROGRESS;
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
		    fs->fs_bsize, KERNCRED, 0, &cbp);
		td->td_proc->p_flag &= ~P_COWINPROGRESS;
		if (error) {
			VOP_UNLOCK(vp, 0, td);
			break;
		}
#ifdef DEBUG
		if (snapdebug)
			printf("%s%d lbn %jd %s %d size %ld to blkno %jd\n",
			    "Copyonremove: snapino ", ip->i_number,
			    (intmax_t)lbn, "for inum", inum, size,
			    (intmax_t)cbp->b_blkno);
#endif
		/*
		 * If we have already read the old block contents, then
		 * simply copy them to the new block. Note that we need
		 * to synchronously write snapshots that have not been
		 * unlinked, and hence will be visible after a crash,
		 * to ensure their integrity.
		 */
		if (savedcbp != 0) {
			bcopy(savedcbp->b_data, cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if (dopersistence && ip->i_effnlink > 0)
				(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			VOP_UNLOCK(vp, 0, td);
			continue;
		}
		/*
		 * Otherwise, read the old block contents into the buffer.
		 */
		if ((error = readblock(cbp, lbn)) != 0) {
			bzero(cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if (dopersistence && ip->i_effnlink > 0)
				(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			VOP_UNLOCK(vp, 0, td);
			break;
		}
		VOP_UNLOCK(vp, 0, td);
		savedcbp = cbp;
	}
	/*
	 * Note that we need to synchronously write snapshots that
	 * have not been unlinked, and hence will be visible after
	 * a crash, to ensure their integrity.
	 */
	if (savedcbp) {
		vp = savedcbp->b_vp;
		bawrite(savedcbp);
		if (dopersistence && VTOI(vp)->i_effnlink > 0) {
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
			(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			VOP_UNLOCK(vp, 0, td);
		}
	}
	/*
	 * If we have been unable to allocate a block in which to do
	 * the copy, then return non-zero so that the fragment will
	 * not be freed. Although space will be lost, the snapshot
	 * will stay consistent.
	 */
	return (error);
}

/*
 * Associate snapshot files when mounting.
 */
void
ffs_snapshot_mount(mp)
	struct mount *mp;
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct fs *fs = ump->um_fs;
	struct thread *td = curthread;
	struct snaphead *snaphead;
	struct vnode *vp;
	struct inode *ip;
	struct uio auio;
	struct iovec aiov;
	void *listhd;
	char *reason;
	int error, snaploc, loc;

	/*
	 * XXX The following needs to be set before UFS_TRUNCATE or
	 * VOP_READ can be called.
	 */
	mp->mnt_stat.f_iosize = fs->fs_bsize;
	/*
	 * Process each snapshot listed in the superblock.
	 */
	snaphead = &ump->um_devvp->v_rdev->si_snapshots;
	for (snaploc = 0; snaploc < FSMAXSNAP; snaploc++) {
		if (fs->fs_snapinum[snaploc] == 0)
			return;
		if ((error = VFS_VGET(mp, fs->fs_snapinum[snaploc],
		    LK_EXCLUSIVE, &vp)) != 0){
			printf("ffs_snapshot_mount: vget failed %d\n", error);
			continue;
		}
		ip = VTOI(vp);
		if ((ip->i_flags & SF_SNAPSHOT) == 0 || ip->i_size ==
		    lblktosize(fs, howmany(fs->fs_size, fs->fs_frag))) {
			if ((ip->i_flags & SF_SNAPSHOT) == 0) {
				reason = "non-snapshot";
			} else {
				reason = "old format snapshot";
				(void)UFS_TRUNCATE(vp, (off_t)0, 0, NOCRED, td);
				(void)VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			}
			printf("ffs_snapshot_mount: %s inode %d\n",
			    reason, fs->fs_snapinum[snaploc]);
			vput(vp);
			for (loc = snaploc + 1; loc < FSMAXSNAP; loc++) {
				if (fs->fs_snapinum[loc] == 0)
					break;
				fs->fs_snapinum[loc - 1] = fs->fs_snapinum[loc];
			}
			fs->fs_snapinum[loc - 1] = 0;
			snaploc--;
			continue;
		}
		/*
		 * Allocate the space for the block hints list.
		 */
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		aiov.iov_base = (void *)&ip->i_snaplistsize;
		aiov.iov_len = sizeof(ip->i_snaplistsize);
		auio.uio_resid = aiov.iov_len;
		auio.uio_offset =
		    lblktosize(fs, howmany(fs->fs_size, fs->fs_frag));
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_rw = UIO_READ;
		auio.uio_td = td;
		if ((error = VOP_READ(vp, &auio, IO_UNIT, td->td_ucred)) != 0) {
			printf("ffs_snapshot_mount: read_1 failed %d\n", error);
			continue;
		}
		MALLOC(listhd, void *, ip->i_snaplistsize * sizeof(daddr_t),
		    M_UFSMNT, M_WAITOK);
		auio.uio_iovcnt = 1;
		aiov.iov_base = listhd;
		aiov.iov_len = ip->i_snaplistsize * sizeof (daddr_t);
		auio.uio_resid = aiov.iov_len;
		auio.uio_offset -= sizeof(ip->i_snaplistsize);
		if ((error = VOP_READ(vp, &auio, IO_UNIT, td->td_ucred)) != 0) {
			printf("ffs_snapshot_mount: read_2 failed %d\n", error);
			FREE(listhd, M_UFSMNT);
			continue;
		}
		ip->i_snapblklist = (daddr_t *)listhd;
		/*
		 * Link it onto the active snapshot list.
		 */
		if (ip->i_nextsnap.tqe_prev != 0)
			panic("ffs_snapshot_mount: %d already on list",
			    ip->i_number);
		else
			TAILQ_INSERT_TAIL(snaphead, ip, i_nextsnap);
		vp->v_vflag |= VV_SYSTEM;
		ump->um_devvp->v_rdev->si_copyonwrite = ffs_copyonwrite;
		ASSERT_VOP_LOCKED(ump->um_devvp, "ffs_snapshot_mount");
		ump->um_devvp->v_vflag |= VV_COPYONWRITE;
		VOP_UNLOCK(vp, 0, td);
	}
}

/*
 * Disassociate snapshot files when unmounting.
 */
void
ffs_snapshot_unmount(mp)
	struct mount *mp;
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct snaphead *snaphead = &ump->um_devvp->v_rdev->si_snapshots;
	struct inode *xp;

	while ((xp = TAILQ_FIRST(snaphead)) != 0) {
		TAILQ_REMOVE(snaphead, xp, i_nextsnap);
		if (xp->i_snapblklist != NULL) {
			FREE(xp->i_snapblklist, M_UFSMNT);
			xp->i_snapblklist = NULL;
		}
		xp->i_nextsnap.tqe_prev = 0;
		if (xp->i_effnlink > 0)
			vrele(ITOV(xp));
	}
	ASSERT_VOP_LOCKED(ump->um_devvp, "ffs_snapshot_unmount");
	ump->um_devvp->v_rdev->si_copyonwrite = 0;
	ump->um_devvp->v_vflag &= ~VV_COPYONWRITE;
}

/*
 * Check for need to copy block that is about to be written,
 * copying the block if necessary.
 */
static int
ffs_copyonwrite(devvp, bp)
	struct vnode *devvp;
	struct buf *bp;
{
	struct buf *ibp, *cbp, *savedcbp = 0;
	struct thread *td = curthread;
	struct fs *fs;
	struct inode *ip;
	struct vnode *vp;
	ufs2_daddr_t lbn, blkno;
	int lower, upper, mid, indiroff, error = 0;

	fs = TAILQ_FIRST(&devvp->v_rdev->si_snapshots)->i_fs;
	lbn = fragstoblks(fs, dbtofsb(fs, bp->b_blkno));
	if (td->td_proc->p_flag & P_COWINPROGRESS)
		panic("ffs_copyonwrite: recursive call");
	TAILQ_FOREACH(ip, &devvp->v_rdev->si_snapshots, i_nextsnap) {
		vp = ITOV(ip);
		/*
		 * We ensure that everything of our own that needs to be
		 * copied will be done at the time that ffs_snapshot is
		 * called. Thus we can skip the check here which can
		 * deadlock in doing the lookup in UFS_BALLOC.
		 */
		if (bp->b_vp == vp)
			continue;
		/*
		 * First check to see if it is in the preallocated list.
		 * By doing this check we avoid several potential deadlocks.
		 */
		lower = 1;
		upper = ip->i_snaplistsize - 1;
		while (lower <= upper) {
			mid = (lower + upper) / 2;
			if (ip->i_snapblklist[mid] == lbn)
				break;
			if (ip->i_snapblklist[mid] < lbn)
				lower = mid + 1;
			else
				upper = mid - 1;
		}
		if (lower <= upper)
			continue;
		/*
		 * Check to see if block needs to be copied. We have to
		 * be able to do the UFS_BALLOC without blocking, otherwise
		 * we may get in a deadlock with another process also
		 * trying to allocate. If we find outselves unable to
		 * get the buffer lock, we unlock the snapshot vnode,
		 * sleep briefly, and try again.
		 */
retry:
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
		if (lbn < NDADDR) {
			blkno = DIP(ip, i_db[lbn]);
		} else {
			td->td_proc->p_flag |= P_COWINPROGRESS;
			error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
			   fs->fs_bsize, KERNCRED, BA_METAONLY | BA_NOWAIT, &ibp);
			td->td_proc->p_flag &= ~P_COWINPROGRESS;
			if (error) {
				VOP_UNLOCK(vp, 0, td);
				if (error != EWOULDBLOCK)
					break;
				tsleep(vp, td->td_ksegrp->kg_user_pri, "nap", 1);
				goto retry;
			}
			indiroff = (lbn - NDADDR) % NINDIR(fs);
			if (ip->i_ump->um_fstype == UFS1)
				blkno=((ufs1_daddr_t *)(ibp->b_data))[indiroff];
			else
				blkno=((ufs2_daddr_t *)(ibp->b_data))[indiroff];
			bqrelse(ibp);
		}
#ifdef DIAGNOSTIC
		if (blkno == BLK_SNAP && bp->b_lblkno >= 0)
			panic("ffs_copyonwrite: bad copy block");
#endif
		if (blkno != 0) {
			VOP_UNLOCK(vp, 0, td);
			continue;
		}
		/*
		 * Allocate the block into which to do the copy. Note that this
		 * allocation will never require any additional allocations for
		 * the snapshot inode.
		 */
		td->td_proc->p_flag |= P_COWINPROGRESS;
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
		    fs->fs_bsize, KERNCRED, BA_NOWAIT, &cbp);
		td->td_proc->p_flag &= ~P_COWINPROGRESS;
		if (error) {
			VOP_UNLOCK(vp, 0, td);
			if (error != EWOULDBLOCK)
				break;
			tsleep(vp, td->td_ksegrp->kg_user_pri, "nap", 1);
			goto retry;
		}
#ifdef DEBUG
		if (snapdebug) {
			printf("Copyonwrite: snapino %d lbn %jd for ",
			    ip->i_number, (intmax_t)lbn);
			if (bp->b_vp == devvp)
				printf("fs metadata");
			else
				printf("inum %d", VTOI(bp->b_vp)->i_number);
			printf(" lblkno %jd to blkno %jd\n",
			    (intmax_t)bp->b_lblkno, (intmax_t)cbp->b_blkno);
		}
#endif
		/*
		 * If we have already read the old block contents, then
		 * simply copy them to the new block. Note that we need
		 * to synchronously write snapshots that have not been
		 * unlinked, and hence will be visible after a crash,
		 * to ensure their integrity.
		 */
		if (savedcbp != 0) {
			bcopy(savedcbp->b_data, cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if (dopersistence && ip->i_effnlink > 0)
				(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			VOP_UNLOCK(vp, 0, td);
			continue;
		}
		/*
		 * Otherwise, read the old block contents into the buffer.
		 */
		if ((error = readblock(cbp, lbn)) != 0) {
			bzero(cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if (dopersistence && ip->i_effnlink > 0)
				(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			VOP_UNLOCK(vp, 0, td);
			break;
		}
		savedcbp = cbp;
		VOP_UNLOCK(vp, 0, td);
	}
	/*
	 * Note that we need to synchronously write snapshots that
	 * have not been unlinked, and hence will be visible after
	 * a crash, to ensure their integrity.
	 */
	if (savedcbp) {
		vp = savedcbp->b_vp;
		bawrite(savedcbp);
		if (dopersistence && VTOI(vp)->i_effnlink > 0) {
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
			(void) VOP_FSYNC(vp, KERNCRED, MNT_WAIT, td);
			VOP_UNLOCK(vp, 0, td);
		}
	}
	return (error);
}

/*
 * Read the specified block into the given buffer.
 * Much of this boiler-plate comes from bwrite().
 */
static int
readblock(bp, lbn)
	struct buf *bp;
	ufs2_daddr_t lbn;
{
	struct uio auio;
	struct iovec aiov;
	struct thread *td = curthread;
	struct inode *ip = VTOI(bp->b_vp);

	aiov.iov_base = bp->b_data;
	aiov.iov_len = bp->b_bcount;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = dbtob(fsbtodb(ip->i_fs, blkstofrags(ip->i_fs, lbn)));
	auio.uio_resid = bp->b_bcount;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	return (physio(ip->i_devvp->v_rdev, &auio, 0));
}

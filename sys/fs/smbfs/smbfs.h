/*
 * Copyright (c) 2000-2001, Boris Popov
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
#ifndef _SMBFS_SMBFS_H_
#define _SMBFS_SMBFS_H_

#define VT_SMBFS	24

#define SMBFS_VERMAJ	1
#define SMBFS_VERMIN	1012
#define SMBFS_VERSION	(SMBFS_VERMAJ*100000 + SMBFS_VERMIN)
#define	SMBFS_VFSNAME	"smbfs"

/* Values for flags */
#define SMBFS_MOUNT_SOFT	0x0001
#define SMBFS_MOUNT_INTR	0x0002
#define SMBFS_MOUNT_STRONG	0x0004
#define	SMBFS_MOUNT_HAVE_NLS	0x0008
#define	SMBFS_MOUNT_NO_LONG	0x0010

#define	SMBFS_MAXPATHCOMP	256	/* maximum number of path components */


/* Layout of the mount control block for an smb file system. */
struct smbfs_args {
	int		version;
	int		dev;
	u_int		flags;
	char		mount_point[MAXPATHLEN];
	u_char		root_path[512+1];
	uid_t		uid;
	gid_t 		gid;
	mode_t 		file_mode;
	mode_t 		dir_mode;
	int		caseopt;
};

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_SMBFSMNT);
#endif

struct smbnode;
struct smb_share;
struct u_cred;
struct vop_ioctl_args;
struct buf;

struct smbmount {
	struct smbfs_args	sm_args;
	struct mount * 		sm_mp;
	struct smbnode *	sm_root;
	struct ucred *		sm_owner;
	int			sm_flags;
	long			sm_nextino;
	struct smb_share * 	sm_share;
/*	struct simplelock	sm_npslock;*/
	struct smbnode *	sm_npstack[SMBFS_MAXPATHCOMP];
	int			sm_caseopt;
	struct lock		sm_hashlock;
	LIST_HEAD(smbnode_hashhead, smbnode) *sm_hash;
	u_long			sm_hashlen;
	int			sm_didrele;
};

#define VFSTOSMBFS(mp)		((struct smbmount *)((mp)->mnt_data))
#define SMBFSTOVFS(smp)		((struct mount *)((smp)->sm_mp))
#define VTOVFS(vp)		((vp)->v_mount)
#define	VTOSMBFS(vp)		(VFSTOSMBFS(VTOVFS(vp)))

int smbfs_ioctl(struct vop_ioctl_args *ap);
int smbfs_doio(struct buf *bp, struct ucred *cr, struct proc *p);
int smbfs_vinvalbuf(struct vnode *vp, int flags, struct ucred *cred, 
	struct proc *p, int intrflg);
#endif	/* KERNEL */

#endif /* _SMBFS_SMBFS_H_ */

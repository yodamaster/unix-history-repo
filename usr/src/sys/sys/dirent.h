/*-
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)dirent.h	7.2 (Berkeley) %G%
 */

/*
 * The dirent structure defines the format of directory entries returned by 
 * the getdirentries(2) system call.
 *
 * A directory entry has a struct dirent at the front of it, containing its
 * inode number, the length of the entry, and the length of the name
 * contained in the entry.  These are followed by the name padded to a 4
 * byte boundary with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is MAXNAMLEN.
 */

struct dirent {
	u_long	d_fileno;		/* file number of entry */
	u_short	d_reclen;		/* length of this record */
	u_char	d_type; 		/* file type, see below */
	u_char	d_namlen;		/* length of string in d_name */
#ifdef _POSIX_SOURCE
	char	d_name[255 + 1];	/* name must be no longer than this */
#else
#define	MAXNAMLEN	255
	char	d_name[MAXNAMLEN + 1];	/* name must be no longer than this */
#endif
};

/*
 * File types
 */
#define		D_UNKNOWN	0
#define		D_REG		1
#define		D_DIR		2
#define		D_BLK		3
#define		D_CHR		4
#define		D_LNK		5
#define		D_SOCK		6
#define		D_FIFO		7

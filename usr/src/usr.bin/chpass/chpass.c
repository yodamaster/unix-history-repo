/*
 * Copyright (c) 1988 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1988 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)chpass.c	5.3 (Berkeley) %G%";
#endif /* not lint */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include </usr/src/include/pwd.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <chpass.h>
#include <strings.h>

char e1[] = ": ";
char e2[] = ":,";

int p_login(), p_uid(), p_gid(), p_hdir(), p_shell(), p_change(), p_class();
int p_expire(), p_save();

struct entry list[] = {
	{ "Login",		p_login,  1,   5, e1,   },
	{ "Uid",		p_uid,    1,   3, e1,   },
	{ "Gid",		p_gid,    1,   3, e1,   },
	{ "Class",		p_class,  1,   5, e1,   },
	{ "Change",		p_change, 1,   6, NULL, },
	{ "Expire",		p_expire, 1,   6, NULL, },
#define	E_NAME		6
	{ "Full Name",		p_save,   0,   9, e2,   },
#define	E_BPHONE	7
	{ "Office Phone",	p_save,   0,  12, e2,   },
#define	E_HPHONE	8
	{ "Home Phone",		p_save,   0,  10, e2,   },
#define	E_LOCATE	9
	{ "Location",		p_save,   0,   8, e2,   },
	{ "Home directory",	p_hdir,   1,  14, e1,   },
	{ "Shell",		p_shell,  0,   5, e1,   },
	{ NULL, 0, },
};

uid_t euid, uid;

main(argc, argv)
	int argc;
	char **argv;
{
	extern int errno;
	register char *p;
	struct passwd *pw;
	struct rlimit rlim;
	FILE *temp_fp;
	int fd;
	char *fend, *passwd, *temp, *tend, buf[1024];
	char from[MAXPATHLEN], to[MAXPATHLEN];
	char *getusershell();

	euid = geteuid();
	uid = getuid();
	switch(--argc) {
	case 0:
		if (!(pw = getpwuid(uid))) {
			fprintf(stderr, "chpass: unknown user: uid %u\n", uid);
			exit(1);
		}
		break;
	case 1:
		if (!(pw = getpwnam(argv[1]))) {
			fprintf(stderr, "chpass: unknown user %s.\n", argv[1]);
			exit(1);
		}
		if (uid && uid != pw->pw_uid) {
			fprintf(stderr, "chpass: %s\n", strerror(EACCES));
			exit(1);
		}
		break;
	default:
		fprintf(stderr, "usage: chpass [user]\n");
		exit(1);
	}

	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGTSTP, SIG_IGN);

	rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
	(void)setrlimit(RLIMIT_CPU, &rlim);
	(void)setrlimit(RLIMIT_FSIZE, &rlim);

	(void)umask(0);

	temp = _PATH_PTMP;
	if ((fd = open(temp, O_WRONLY|O_CREAT|O_EXCL, 0600)) < 0) {
		if (errno == EEXIST) {
			fprintf(stderr,
			    "chpass: password file busy -- try again later.\n");
			exit(1);
		}
		fprintf(stderr, "chpass: %s: %s", temp, strerror(errno));
		goto bad;
	}
	if (!(temp_fp = fdopen(fd, "w"))) {
		fprintf(stderr, "chpass: can't write %s", temp);
		goto bad;
	}

	if (!info(pw))
		goto bad;

	/*
	 * special checks...
	 *
	 * there has to be a limit on the size of the gecos fields,
	 * otherwise getpwent(3) can choke.
	 * ``if I swallow anything evil, put your fingers down my throat...''
	 *	-- The Who
	 */
	if (strlen(list[E_NAME].save) + strlen(list[E_BPHONE].save) +
	    strlen(list[E_HPHONE].save) + strlen(list[E_LOCATE].save)
	    > 512) {
		fprintf(stderr, "chpass: gecos field too large.\n");
		exit(1);
	}
	(void)sprintf(pw->pw_gecos = buf, "%s,%s,%s,%s",
	    list[E_NAME].save, list[E_LOCATE].save, list[E_BPHONE].save,
	    list[E_HPHONE].save);

	/* root should have a 0 uid and a reasonable shell */
	if (!strcmp(pw->pw_name, "root")) {
		if (pw->pw_uid) {
			fprintf(stderr, "chpass: root uid should be 0.");
			exit(1);
		}
		setusershell();
		for (;;)
			if (!(p = getusershell())) {
				fprintf(stderr,
				    "chpass: warning, unknown root shell.");
				break;
			}
			else if (!strcmp(pw->pw_shell, p))
				break;
	}

	passwd = _PATH_MASTERPASSWD;
	if (!freopen(passwd, "r", stdin)) {
		fprintf(stderr, "chpass: can't read %s", passwd);
		goto bad;
	}
	if (!copy(pw, temp_fp))
		goto bad;

	(void)fclose(temp_fp);
	(void)fclose(stdin);

	switch(fork()) {
	case 0:
		break;
	case -1:
		fprintf(stderr, "chpass: can't fork");
		goto bad;
		/* NOTREACHED */
	default:
		exit(0);
		/* NOTREACHED */
	}

	if (makedb(temp)) {
		fprintf(stderr, "chpass: mkpasswd failed");
bad:		fprintf(stderr, "; information unchanged.\n");
		(void)unlink(temp);
		exit(1);
	}

	/*
	 * possible race; have to rename four files, and someone could slip
	 * in between them.  LOCK_EX and rename the ``passwd.dir'' file first
	 * so that getpwent(3) can't slip in; the lock should never fail and
	 * it's unclear what to do if it does.  Rename ``ptmp'' last so that
	 * passwd/vipw/chpass can't slip in.
	 */
	(void)setpriority(PRIO_PROCESS, 0, -20);
	fend = strcpy(from, temp) + strlen(temp);
	tend = strcpy(to, passwd) + strlen(passwd);
	bcopy(".dir", fend, 5);
	bcopy(".dir", tend, 5);
	if ((fd = open(from, O_RDONLY, 0)) >= 0)
		(void)flock(fd, LOCK_EX);
	/* here we go... */
	(void)rename(from, to);
	bcopy(".pag", fend, 5);
	bcopy(".pag", tend, 5);
	(void)rename(from, to);
	bcopy(".orig", fend, 6);
	(void)rename(from, _PATH_PASSWD);
	(void)rename(temp, passwd);
	/* done! */
	exit(0);
}

info(pw)
	struct passwd *pw;
{
	register struct entry *ep;
	register char *p;
	struct stat begin, end;
	FILE *fp;
	int fd, rval;
	char *tfile, buf[1024], *getenv();

	tfile = "/tmp/passwd.XXXXXX";
	if ((fd = mkstemp(tfile)) == -1 || !(fp = fdopen(fd, "w+"))) {
		fprintf(stderr, "chpass: no temporary file");
		return(0);
	}

	print(fp, pw);
	(void)fflush(fp);

	/*
	 * give the file to the real user; setuid permissions
	 * are discarded in edit()
	 */
	(void)fchown(fd, getuid(), getgid());
	(void)fstat(fd, &begin);
	rval = edit(tfile);
	(void)unlink(tfile);

	if (rval) {
		fprintf(stderr, "chpass: edit failed");
		return(0);
	}
	(void)fstat(fd, &end);
	if (begin.st_mtime == end.st_mtime) {
		fprintf(stderr, "chpass: no changes made");
		return(0);
	}
	(void)rewind(fp);
	while (fgets(buf, sizeof(buf), fp)) {
		if (!buf[0])
			continue;
		if (!(p = index(buf, '\n'))) {
			fprintf(stderr, "chpass: line too long");
			return(0);
		}
		*p = '\0';
		for (ep = list; ep->prompt; ++ep)
			if (!strncasecmp(buf, ep->prompt, ep->len)) {
				if (ep->restricted && euid)
					continue;
				if (!(p = index(buf, ':'))) {
					fprintf(stderr,
					    "chpass: line corrupted");
					return(0);
				}
				while (isspace(*++p));
				if (ep->except && strpbrk(p, ep->except)) {
					fprintf(stderr,
					   "chpass: illegal character in the \"%s\" field",
					    ep->prompt);
					return(0);
				}
				if ((ep->func)(p, pw, ep))
					return(0);
				break;
			}
	}
	(void)fclose(fp);
	return(1);
}

copy(pw, fp)
	struct passwd *pw;
	FILE *fp;
{
	register int done;
	register char *p;
	char buf[1024];

	for (done = 0; fgets(buf, sizeof(buf), stdin);) {
		/* skip lines that are too big */
		if (!index(buf, '\n')) {
			fprintf(stderr, "chpass: line too long");
			return(0);
		}
		if (done) {
			fprintf(fp, "%s", buf);
			continue;
		}
		if (!(p = index(buf, ':'))) {
			fprintf(stderr, "chpass: corrupted entry");
			return(0);
		}
		*p = '\0';
		if (strcmp(buf, pw->pw_name)) {
			*p = ':';
			fprintf(fp, "%s", buf);
			continue;
		}
		fprintf(fp, "%s:%s:%d:%d:%s:%ld:%ld:%s:%s:%s\n",
		    pw->pw_name, pw->pw_passwd, pw->pw_uid, pw->pw_gid,
		    pw->pw_class, pw->pw_change, pw->pw_expire, pw->pw_gecos,
		    pw->pw_dir, pw->pw_shell);
		done = 1;
	}
	if (!done)
		fprintf(fp, "%s:%s:%d:%d:%s:%ld:%ld:%s:%s:%s\n",
		    pw->pw_name, "NOLOGIN", pw->pw_uid, pw->pw_gid,
		    pw->pw_class, pw->pw_change, pw->pw_expire, pw->pw_gecos,
		    pw->pw_dir, pw->pw_shell);
	return(1);
}

makedb(file)
	char *file;
{
	int status, pid, w;

	if (!(pid = vfork())) {
		execl(_PATH_MKPASSWD, "mkpasswd", "-p", file, NULL);
		_exit(127);
	}
	while ((w = wait(&status)) != pid && w != -1);
	return(w == -1 || status);
}

edit(file)
	char *file;
{
	int status, pid, w;
	char *p, *editor, *getenv();

	if (editor = getenv("EDITOR")) {
		if (p = rindex(editor, '/'))
			++p;
		else
			p = editor;
	}
	else
		p = editor = "vi";
	if (!(pid = vfork())) {
		(void)setuid(getuid());
		(void)setgid(getgid());
		execlp(editor, p, file, NULL);
		_exit(127);
	}
	while ((w = wait(&status)) != pid && w != -1);
	return(w == -1 || status);
}

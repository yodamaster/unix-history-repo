/*
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if !defined(lint) && !defined(sgi) && !defined(__NetBSD__)
static char sccsid[] = "@(#)trace.c	8.1 (Berkeley) 6/5/93";
#elif defined(__NetBSD__)
static char rcsid[] = "$NetBSD$";
#endif
#ident "$Revision: 1.17 $"

#define	RIPCMDS
#include "defs.h"
#include "pathnames.h"
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>


#ifdef sgi
/* use *stat64 for files on large filesystems */
#define stat	stat64
#endif

#define	NRECORDS	50		/* size of circular trace buffer */

int	tracelevel, new_tracelevel;
FILE	*ftrace = stdout;		/* output trace file */
static char *sigtrace_pat = "%s\n";
static char savetracename[MAXPATHLEN+1];
char	inittracename[MAXPATHLEN+1];
int	file_trace;			/* 1=tracing to file, not stdout */

static void trace_dump(void);


/* convert string to printable characters
 */
static char *
qstring(u_char *s, int len)
{
	static char buf[8*20+1];
	char *p;
	u_char *s2, c;


	for (p = buf; len != 0 && p < &buf[sizeof(buf)-1]; len--) {
		c = *s++;
		if (c == '\0') {
			for (s2 = s+1; s2 < &s[len]; s2++) {
				if (*s2 != '\0')
					break;
			}
			if (s2 >= &s[len])
			    goto exit;
		}

		if (c >= ' ' && c < 0x7f && c != '\\') {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch (c) {
		case '\\':
			*p++ = '\\';
			break;
		case '\n':
			*p++= 'n';
			break;
		case '\r':
			*p++= 'r';
			break;
		case '\t':
			*p++ = 't';
			break;
		case '\b':
			*p++ = 'b';
			break;
		default:
			p += sprintf(p,"%o",c);
			break;
		}
	}
exit:
	*p = '\0';
	return buf;
}


/* convert IP address to a string, but not into a single buffer
 */
char *
naddr_ntoa(naddr a)
{
#define NUM_BUFS 4
	static int bufno;
	static struct {
	    char    str[16];		/* xxx.xxx.xxx.xxx\0 */
	} bufs[NUM_BUFS];
	char *s;
	struct in_addr addr;

	addr.s_addr = a;
	s = strcpy(bufs[bufno].str, inet_ntoa(addr));
	bufno = (bufno+1) % NUM_BUFS;
	return s;
#undef NUM_BUFS
}


char *
saddr_ntoa(struct sockaddr *sa)
{
	return (sa == 0) ? "?" : naddr_ntoa(S_ADDR(sa));
}


static char *
ts(time_t secs) {
	static char s[20];

	secs += epoch.tv_sec;
#ifdef sgi
	(void)cftime(s, "%T", &secs);
#else
	bcopy(ctime(&secs)+11, s, 8);
	s[8] = '\0';
#endif
	return s;
}


/* On each event, display a time stamp.
 * This assumes that 'now' is update once for each event, and
 * that at least now.tv_usec changes.
 */
static struct timeval lastlog_time;

void
lastlog(void)
{
	if (lastlog_time.tv_sec != now.tv_sec
	    || lastlog_time.tv_usec != now.tv_usec) {
		(void)fprintf(ftrace, "-- %s --\n", ts(now.tv_sec));
		lastlog_time = now;
	}
}


static void
tmsg(char *p, ...)
{
	va_list args;

	if (ftrace != 0) {
		lastlog();
		va_start(args, p);
		vfprintf(ftrace, p, args);
		fflush(ftrace);
	}
}


static void
trace_close(void)
{
	int fd;


	fflush(stdout);
	fflush(stderr);

	if (ftrace != 0 && file_trace) {
		if (ftrace != stdout)
			fclose(ftrace);
		ftrace = 0;
		fd = open(_PATH_DEVNULL, O_RDWR);
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		(void)close(fd);
	}
	lastlog_time.tv_sec = 0;
}


void
trace_flush(void)
{
	if (ftrace != 0) {
		fflush(ftrace);
		if (ferror(ftrace))
			trace_off("tracing off: ", strerror(ferror(ftrace)));
	}
}


void
trace_off(char *p, ...)
{
	va_list args;


	if (ftrace != 0) {
		lastlog();
		va_start(args, p);
		vfprintf(ftrace, p, args);
	}
	trace_close();

	new_tracelevel = tracelevel = 0;
}


/* log a change in tracing
 */
void
tracelevel_msg(char *pat,
	       int dump)		/* -1=no dump, 0=default, 1=force */
{
	static char *off_msgs[MAX_TRACELEVEL] = {
		"Tracing actions stopped",
		"Tracing packets stopped",
		"Tracing packet contents stopped",
		"Tracing kernel changes stopped",
	};
	static char *on_msgs[MAX_TRACELEVEL] = {
		"Tracing actions started",
		"Tracing packets started",
		"Tracing packet contents started",
		"Tracing kernel changes started",
	};
	u_int old_tracelevel = tracelevel;


	if (new_tracelevel < 0)
		new_tracelevel = 0;
	else if (new_tracelevel > MAX_TRACELEVEL)
		new_tracelevel = MAX_TRACELEVEL;

	if (new_tracelevel < tracelevel) {
		if (new_tracelevel <= 0) {
			trace_off(pat, off_msgs[0]);
		} else do {
			tmsg(pat, off_msgs[tracelevel]);
		}
		while (--tracelevel != new_tracelevel);

	} else if (new_tracelevel > tracelevel) {
		do {
			tmsg(pat, on_msgs[tracelevel++]);
		} while (tracelevel != new_tracelevel);
	}

	if (dump > 0
	    || (dump == 0 && old_tracelevel == 0 && tracelevel != 0))
		trace_dump();
}


void
set_tracefile(char *filename,
	      char *pat,
	      int dump)			/* -1=no dump, 0=default, 1=force */
{
	struct stat stbuf;
	FILE *n_ftrace;
	char *fn;


	/* Allow a null filename to increase the level if the trace file
	 * is already open or if coming from a trusted source, such as
	 * a signal or the command line.
	 */
	if (filename == 0 || filename[0] == '\0') {
		filename = 0;
		if (ftrace == 0) {
			if (inittracename[0] == '\0') {
				msglog("missing trace file name");
				return;
			}
			fn = inittracename;
		} else {
			fn = 0;
		}

	} else if (!strcmp(filename,"dump/../table")) {
		trace_dump();
		return;

	} else {
		/* Allow the file specified with "-T file" to be reopened,
		 * but require all other names specified over the net to
		 * match the official path.  The path can specify a directory
		 * in which the file is to be created.
		 */
		if (strcmp(filename, inittracename)
#ifdef _PATH_TRACE
		    && (strncmp(filename, _PATH_TRACE, sizeof(_PATH_TRACE)-1)
			|| strstr(filename,"../")
			|| 0 > stat(_PATH_TRACE, &stbuf))
#endif
		    ) {
			msglog("wrong trace file \"%s\"", filename);
			return;
		}

		/* If the new tracefile exists, it must be a regular file.
		 */
		if (stat(filename, &stbuf) >= 0
		    && (stbuf.st_mode & S_IFMT) != S_IFREG) {
			msglog("wrong type (%#x) of trace file \"%s\"",
			       stbuf.st_mode, filename);
			return;
		}

		fn = filename;
	}

	if (fn != 0) {
		n_ftrace = fopen(fn, "a");
		if (n_ftrace == 0) {
			msglog("failed to open trace file \"%s\" %s",
			       fn, strerror(errno));
			if (fn == inittracename)
				inittracename[0] = '\0';
			return;
		}

		tmsg("switch to trace file %s\n", fn);

		file_trace = 1;
		trace_close();

		if (fn != savetracename)
			strncpy(savetracename, fn, sizeof(savetracename)-1);
		ftrace = n_ftrace;

		fflush(stdout);
		fflush(stderr);
		dup2(fileno(ftrace), STDOUT_FILENO);
		dup2(fileno(ftrace), STDERR_FILENO);
	}

	if (new_tracelevel == 0 || filename == 0)
		new_tracelevel++;
	tracelevel_msg(pat, dump != 0 ? dump : (filename != 0));
}


/* ARGSUSED */
void
sigtrace_on(int s)
{
	new_tracelevel++;
	sigtrace_pat = "SIGUSR1: %s\n";
}


/* ARGSUSED */
void
sigtrace_off(int s)
{
	new_tracelevel--;
	sigtrace_pat = "SIGUSR2: %s\n";
}


/* Set tracing after a signal.
 */
void
set_tracelevel(void)
{
	if (new_tracelevel == tracelevel)
		return;

	/* If tracing entirely off, and there was no tracefile specified
	 * on the command line, then leave it off.
	 */
	if (new_tracelevel > tracelevel && ftrace == 0) {
		if (savetracename[0] != '\0') {
			set_tracefile(savetracename,sigtrace_pat,0);
		} else if (inittracename[0] != '\0') {
				set_tracefile(inittracename,sigtrace_pat,0);
		} else {
			new_tracelevel = 0;
			return;
		}
	} else {
		tracelevel_msg(sigtrace_pat, 0);
	}
}


/* display an address
 */
char *
addrname(naddr	addr,			/* in network byte order */
	 naddr	mask,
	 int	force)			/* 0=show mask if nonstandard, */
{					/*	1=always show mask, 2=never */
#define NUM_BUFS 4
	static int bufno;
	static struct {
	    char    str[15+20];
	} bufs[NUM_BUFS];
	char *s, *sp;
	naddr dmask;
	int i;

	s = strcpy(bufs[bufno].str, naddr_ntoa(addr));
	bufno = (bufno+1) % NUM_BUFS;

	if (force == 1 || (force == 0 && mask != std_mask(addr))) {
		sp = &s[strlen(s)];

		dmask = mask & -mask;
		if (mask + dmask == 0) {
			for (i = 0; i != 32 && ((1<<i) & mask) == 0; i++)
				continue;
			(void)sprintf(sp, "/%d", 32-i);

		} else {
			(void)sprintf(sp, " (mask %#x)", (u_int)mask);
		}
	}

	return s;
#undef NUM_BUFS
}


/* display a bit-field
 */
struct bits {
	int	bits_mask;
	int	bits_clear;
	char	*bits_name;
};

static struct bits if_bits[] = {
	{ IFF_LOOPBACK,		0,		"LOOPBACK" },
	{ IFF_POINTOPOINT,	0,		"PT-TO-PT" },
	{ 0,			0,		0}
};

static struct bits is_bits[] = {
	{ IS_ALIAS,		0,		"ALIAS" },
	{ IS_SUBNET,		0,		"" },
	{ IS_REMOTE,		(IS_NO_RDISC
				 | IS_BCAST_RDISC), "REMOTE" },
	{ IS_PASSIVE,		(IS_NO_RDISC
				 | IS_NO_RIP
				 | IS_NO_SUPER_AG
				 | IS_PM_RDISC
				 | IS_NO_AG),	"PASSIVE" },
	{ IS_EXTERNAL,		0,		"EXTERNAL" },
	{ IS_CHECKED,		0,		"" },
	{ IS_ALL_HOSTS,		0,		"" },
	{ IS_ALL_ROUTERS,	0,		"" },
	{ IS_DISTRUST,		0,		"DISTRUST" },
	{ IS_BROKE,		IS_SICK,	"BROKEN" },
	{ IS_SICK,		0,		"SICK" },
	{ IS_DUP,		0,		"DUPLICATE" },
	{ IS_REDIRECT_OK,	0,		"REDIRECT_OK" },
	{ IS_NEED_NET_SYN,	0,		"" },
	{ IS_NO_AG,		IS_NO_SUPER_AG,	"NO_AG" },
	{ IS_NO_SUPER_AG,	0,		"NO_SUPER_AG" },
	{ (IS_NO_RIPV1_IN
	   | IS_NO_RIPV2_IN
	   | IS_NO_RIPV1_OUT
	   | IS_NO_RIPV2_OUT),	0,		"NO_RIP" },
	{ (IS_NO_RIPV1_IN
	   | IS_NO_RIPV1_OUT),	0,		"RIPV2" },
	{ IS_NO_RIPV1_IN,	0,		"NO_RIPV1_IN" },
	{ IS_NO_RIPV2_IN,	0,		"NO_RIPV2_IN" },
	{ IS_NO_RIPV1_OUT,	0,		"NO_RIPV1_OUT" },
	{ IS_NO_RIPV2_OUT,	0,		"NO_RIPV2_OUT" },
	{ (IS_NO_ADV_IN
	   | IS_NO_SOL_OUT
	   | IS_NO_ADV_OUT),	IS_BCAST_RDISC,	"NO_RDISC" },
	{ IS_NO_SOL_OUT,	0,		"NO_SOLICIT" },
	{ IS_SOL_OUT,		0,		"SEND_SOLICIT" },
	{ IS_NO_ADV_OUT,	IS_BCAST_RDISC,	"NO_RDISC_ADV" },
	{ IS_ADV_OUT,		0,		"RDISC_ADV" },
	{ IS_BCAST_RDISC,	0,		"BCAST_RDISC" },
	{ IS_PM_RDISC,		0,		"" },
	{ 0,			0,		"%#x"}
};

static struct bits rs_bits[] = {
	{ RS_IF,		0,		"IF" },
	{ RS_NET_INT,		RS_NET_SYN,	"NET_INT" },
	{ RS_NET_SYN,		0,		"NET_SYN" },
	{ RS_SUBNET,		0,		"" },
	{ RS_LOCAL,		0,		"LOCAL" },
	{ RS_MHOME,		0,		"MHOME" },
	{ RS_STATIC,		0,		"STATIC" },
	{ RS_RDISC,		0,		"RDISC" },
	{ 0,			0,		"%#x"}
};


static void
trace_bits(struct bits *tbl,
	   u_int field,
	   int force)
{
	int b;
	char c;

	if (force) {
		(void)putc('<', ftrace);
		c = 0;
	} else {
		c = '<';
	}

	while (field != 0
	       && (b = tbl->bits_mask) != 0) {
		if ((b & field) == b) {
			if (tbl->bits_name[0] != '\0') {
				if (c)
					(void)putc(c, ftrace);
				(void)fprintf(ftrace, "%s", tbl->bits_name);
				c = '|';
			}
			if (0 == (field &= ~(b | tbl->bits_clear)))
				break;
		}
		tbl++;
	}
	if (field != 0 && tbl->bits_name != 0) {
		if (c)
			(void)putc(c, ftrace);
		(void)fprintf(ftrace, tbl->bits_name, field);
		c = '|';
	}

	if (c != '<' || force)
		(void)fputs("> ", ftrace);
}


static char *
trace_pair(naddr dst,
	   naddr mask,
	   char *gate)
{
	static char buf[3*4+3+1+2+3	/* "xxx.xxx.xxx.xxx/xx-->" */
			+3*4+3+1];	/* "xxx.xxx.xxx.xxx" */
	int i;

	i = sprintf(buf, "%-16s-->", addrname(dst, mask, 0));
	(void)sprintf(&buf[i], "%-*s", 15+20-MAX(20,i), gate);
	return buf;
}


static void
print_rts(struct rt_spare *rts,
	  int force_metric,		/* -1=suppress, 0=default */
	  int force_ifp,		/* -1=suppress, 0=default */
	  int force_router,		/* -1=suppress, 0=default, 1=display */
	  int force_tag,		/* -1=suppress, 0=default, 1=display */
	  int force_time)		/* 0=suppress, 1=display */
{
	if (force_metric >= 0)
		(void)fprintf(ftrace, "metric=%-2d ", rts->rts_metric);
	if (force_ifp >= 0)
		(void)fprintf(ftrace, "%s ", (rts->rts_ifp == 0 ?
					      "if?" : rts->rts_ifp->int_name));
	if (force_router > 0
	    || (force_router == 0 && rts->rts_router != rts->rts_gate))
		(void)fprintf(ftrace, "router=%s ",
			      naddr_ntoa(rts->rts_router));
	if (force_time > 0)
		(void)fprintf(ftrace, "%s ", ts(rts->rts_time));
	if (force_tag > 0
	    || (force_tag == 0 && rts->rts_tag != 0))
		(void)fprintf(ftrace, "tag=%#x ",
			      ntohs(rts->rts_tag));
}


void
trace_if(char *act,
	  struct interface *ifp)
{
	if (!TRACEACTIONS || ftrace == 0)
		return;

	lastlog();
	(void)fprintf(ftrace, "%-3s interface %-4s ", act, ifp->int_name);
	(void)fprintf(ftrace, "%-15s-->%-15s ",
		      naddr_ntoa(ifp->int_addr),
		      addrname(((ifp->int_if_flags & IFF_POINTOPOINT)
				? ifp->int_dstaddr
				: htonl(ifp->int_net)),
			       ifp->int_mask, 1));
	if (ifp->int_metric != 0)
		(void)fprintf(ftrace, "metric=%d ", ifp->int_metric);
	if (!IS_RIP_OUT_OFF(ifp->int_state)
	    && ifp->int_d_metric != 0)
		(void)fprintf(ftrace, "fake_default=%d ", ifp->int_d_metric);
	trace_bits(if_bits, ifp->int_if_flags, 0);
	trace_bits(is_bits, ifp->int_state, 0);
	(void)fputc('\n',ftrace);
}


void
trace_upslot(struct rt_entry *rt,
	     struct rt_spare *rts,
	     naddr	gate,
	     naddr	router,
	     struct interface *ifp,
	     int	metric,
	     u_short	tag,
	     time_t	new_time)
{
	struct rt_spare new;

	if (!TRACEACTIONS || ftrace == 0)
		return;

	if (rts->rts_gate == gate
	    && rts->rts_router == router
	    && rts->rts_metric == metric
	    && rts->rts_tag == tag)
		return;
	new.rts_ifp = ifp;
	new.rts_gate = gate;
	new.rts_router = router;
	new.rts_metric = metric;
	new.rts_time = new_time;
	new.rts_tag = tag;

	lastlog();
	if (rts->rts_gate != RIP_DEFAULT) {
		(void)fprintf(ftrace, "Chg #%d %-35s ",
			      rts - rt->rt_spares,
			      trace_pair(rt->rt_dst, rt->rt_mask,
					 naddr_ntoa(rts->rts_gate)));
		print_rts(rts, 0,0,
			  rts->rts_gate != gate,
			  rts->rts_tag != tag,
			  rts != rt->rt_spares || AGE_RT(rt->rt_state,
							rt->rt_ifp));

		(void)fprintf(ftrace, "\n       %19s%-16s ", "",
			      gate != rts->rts_gate ? naddr_ntoa(gate) : "");
		print_rts(&new,
			  -(metric == rts->rts_metric),
			  -(ifp == rts->rts_ifp),
			  0,
			  rts->rts_tag != tag,
			  new_time != rts->rts_time && (rts != rt->rt_spares
							|| AGE_RT(rt->rt_state,
							    ifp)));

	} else {
		(void)fprintf(ftrace, "Add #%d %-35s ",
			      rts - rt->rt_spares,
			      trace_pair(rt->rt_dst, rt->rt_mask,
					 naddr_ntoa(gate)));
		print_rts(&new, 0,0,0,0,
			  rts != rt->rt_spares || AGE_RT(rt->rt_state,ifp));
	}
	(void)fputc('\n',ftrace);
}


/* talk about a change made to the kernel table
 */
void
trace_kernel(char *p, ...)
{
	va_list args;

	if (!TRACEKERNEL || ftrace == 0)
		return;

	lastlog();
	va_start(args, p);
	vfprintf(ftrace, p, args);
}


/* display a message if tracing actions
 */
void
trace_act(char *p, ...)
{
	va_list args;

	if (!TRACEACTIONS || ftrace == 0)
		return;

	lastlog();
	va_start(args, p);
	vfprintf(ftrace, p, args);
	(void)fputc('\n',ftrace);
}


/* display a message if tracing packets
 */
void
trace_pkt(char *p, ...)
{
	va_list args;

	if (!TRACEPACKETS || ftrace == 0)
		return;

	lastlog();
	va_start(args, p);
	vfprintf(ftrace, p, args);
	(void)fputc('\n',ftrace);
}


void
trace_change(struct rt_entry *rt,
	     u_int	state,
	     naddr	gate,		/* forward packets here */
	     naddr	router,		/* on the authority of this router */
	     int	metric,
	     u_short	tag,
	     struct interface *ifp,
	     time_t	new_time,
	     char	*label)
{
	struct rt_spare new;

	if (ftrace == 0)
		return;

	if (rt->rt_metric == metric
	    && rt->rt_gate == gate
	    && rt->rt_router == router
	    && rt->rt_state == state
	    && rt->rt_tag == tag)
		return;
	new.rts_ifp = ifp;
	new.rts_gate = gate;
	new.rts_router = router;
	new.rts_metric = metric;
	new.rts_time = new_time;
	new.rts_tag = tag;

	lastlog();
	(void)fprintf(ftrace, "%s %-35s ",
		      label,
		      trace_pair(rt->rt_dst, rt->rt_mask,
				 naddr_ntoa(rt->rt_gate)));
	print_rts(rt->rt_spares,
		  0,0,0,0, AGE_RT(rt->rt_state, rt->rt_ifp));
	trace_bits(rs_bits, rt->rt_state, rt->rt_state != state);

	(void)fprintf(ftrace, "\n%*s %19s%-16s ",
		      strlen(label), "", "",
		      rt->rt_gate != gate ? naddr_ntoa(gate) : "");
	print_rts(&new,
		  -(metric == rt->rt_metric),
		  -(ifp == rt->rt_ifp),
		  0,
		  rt->rt_tag != tag,
		  rt->rt_time != new_time && AGE_RT(rt->rt_state,ifp));
	if (rt->rt_state != state)
		trace_bits(rs_bits, state, 1);
	(void)fputc('\n',ftrace);
}


void
trace_add_del(char * action, struct rt_entry *rt)
{
	if (ftrace == 0)
		return;

	lastlog();
	(void)fprintf(ftrace, "%s    %-35s ",
		      action,
		      trace_pair(rt->rt_dst, rt->rt_mask,
				 naddr_ntoa(rt->rt_gate)));
	print_rts(rt->rt_spares, 0,0,0,0,AGE_RT(rt->rt_state,rt->rt_ifp));
	trace_bits(rs_bits, rt->rt_state, 0);
	(void)fputc('\n',ftrace);
}


/* ARGSUSED */
static int
walk_trace(struct radix_node *rn,
	   struct walkarg *w)
{
#define RT ((struct rt_entry *)rn)
	struct rt_spare *rts;
	int i, age = AGE_RT(RT->rt_state, RT->rt_ifp);

	(void)fprintf(ftrace, "  %-35s ", trace_pair(RT->rt_dst, RT->rt_mask,
						     naddr_ntoa(RT->rt_gate)));
	print_rts(&RT->rt_spares[0], 0,0,0,0,age);
	trace_bits(rs_bits, RT->rt_state, 0);
	if (RT->rt_poison_time >= now_garbage
	    && RT->rt_poison_metric < RT->rt_metric)
		(void)fprintf(ftrace, "pm=%d@%s",
			      RT->rt_poison_metric,
			      ts(RT->rt_poison_time));

	rts = &RT->rt_spares[1];
	for (i = 1; i < NUM_SPARES; i++, rts++) {
		if (rts->rts_gate != RIP_DEFAULT) {
			(void)fprintf(ftrace,"\n    #%d%15s%-16s ",
				      i, "", naddr_ntoa(rts->rts_gate));
			print_rts(rts, 0,0,0,0,1);
		}
	}
	(void)fputc('\n',ftrace);

	return 0;
}


static void
trace_dump(void)
{
	struct interface *ifp;

	if (ftrace == 0)
		return;
	lastlog();

	(void)fputs("current daemon state:\n", ftrace);
	for (ifp = ifnet; ifp != 0; ifp = ifp->int_next)
		trace_if("", ifp);
	(void)rn_walktree(rhead, walk_trace, 0);
}


void
trace_rip(char *dir1, char *dir2,
	  struct sockaddr_in *who,
	  struct interface *ifp,
	  struct rip *msg,
	  int size)			/* total size of message */
{
	struct netinfo *n, *lim;
#	define NA (msg->rip_auths)
	int i, seen_route;

	if (!TRACEPACKETS || ftrace == 0)
		return;

	lastlog();
	if (msg->rip_cmd >= RIPCMD_MAX
	    || msg->rip_vers == 0) {
		(void)fprintf(ftrace, "%s bad RIPv%d cmd=%d %s"
			      " %s.%d size=%d\n",
			      dir1, msg->rip_vers, msg->rip_cmd, dir2,
			      naddr_ntoa(who->sin_addr.s_addr),
			      ntohs(who->sin_port),
			      size);
		return;
	}

	(void)fprintf(ftrace, "%s RIPv%d %s %s %s.%d%s%s\n",
		      dir1, msg->rip_vers, ripcmds[msg->rip_cmd], dir2,
		      naddr_ntoa(who->sin_addr.s_addr), ntohs(who->sin_port),
		      ifp ? " via " : "", ifp ? ifp->int_name : "");
	if (!TRACECONTENTS)
		return;

	seen_route = 0;
	switch (msg->rip_cmd) {
	case RIPCMD_REQUEST:
	case RIPCMD_RESPONSE:
		n = msg->rip_nets;
		lim = (struct netinfo *)((char*)msg + size);
		for (; n < lim; n++) {
			if (!seen_route
			    && n->n_family == RIP_AF_UNSPEC
			    && ntohl(n->n_metric) == HOPCNT_INFINITY
			    && msg->rip_cmd == RIPCMD_REQUEST
			    && (n+1 == lim
				|| (n+2 == lim
				    && (n+1)->n_family == RIP_AF_AUTH))) {
				(void)fputs("\tQUERY ", ftrace);
				if (n->n_dst != 0)
					(void)fprintf(ftrace, "%s ",
						      naddr_ntoa(n->n_dst));
				if (n->n_mask != 0)
					(void)fprintf(ftrace, "mask=%#x ",
						      (u_int)ntohl(n->n_mask));
				if (n->n_nhop != 0)
					(void)fprintf(ftrace, "nhop=%s ",
						      naddr_ntoa(n->n_nhop));
				if (n->n_tag != 0)
					(void)fprintf(ftrace, "tag=%#x ",
						      ntohs(n->n_tag));
				(void)fputc('\n',ftrace);
				continue;
			}

			if (n->n_family == RIP_AF_AUTH) {
				if (NA->a_type == RIP_AUTH_PW
				    && n == msg->rip_nets) {
					(void)fprintf(ftrace, "\tPassword"
						      " Authentication:"
						      " \"%s\"\n",
						      qstring(NA->au.au_pw,
							  RIP_AUTH_PW_LEN));
					continue;
				}

				if (NA->a_type == RIP_AUTH_MD5
				    && n == msg->rip_nets) {
					(void)fprintf(ftrace,
						      "\tMD5 Authentication"
						      " len=%d KeyID=%u"
						      " seqno=%u"
						      " rsvd=%#x,%#x\n",
						      NA->au.a_md5.md5_pkt_len,
						      NA->au.a_md5.md5_keyid,
						      NA->au.a_md5.md5_seqno,
						      NA->au.a_md5.rsvd[0],
						      NA->au.a_md5.rsvd[1]);
					continue;
				}
				(void)fprintf(ftrace,
					      "\tAuthentication"
					      " type %d: ",
					      ntohs(NA->a_type));
				for (i = 0;
				     i < sizeof(NA->au.au_pw);
				     i++)
					(void)fprintf(ftrace, "%02x ",
						      NA->au.au_pw[i]);
				(void)fputc('\n',ftrace);
				continue;
			}

			seen_route = 1;
			if (n->n_family != RIP_AF_INET) {
				(void)fprintf(ftrace,
					      "\t(af %d) %-18s mask=%#x ",
					      ntohs(n->n_family),
					      naddr_ntoa(n->n_dst),
					      (u_int)ntohl(n->n_mask));
			} else if (msg->rip_vers == RIPv1) {
				(void)fprintf(ftrace, "\t%-18s ",
					      addrname(n->n_dst,
						       ntohl(n->n_mask),
						       n->n_mask==0 ? 2 : 1));
			} else {
				(void)fprintf(ftrace, "\t%-18s ",
					      addrname(n->n_dst,
						       ntohl(n->n_mask),
						       n->n_mask==0 ? 2 : 0));
			}
			(void)fprintf(ftrace, "metric=%-2d ",
				      (u_int)ntohl(n->n_metric));
			if (n->n_nhop != 0)
				(void)fprintf(ftrace, " nhop=%s ",
					      naddr_ntoa(n->n_nhop));
			if (n->n_tag != 0)
				(void)fprintf(ftrace, "tag=%#x",
					      ntohs(n->n_tag));
			(void)fputc('\n',ftrace);
		}
		if (size != (char *)n - (char *)msg)
			(void)fprintf(ftrace, "truncated record, len %d\n",
				size);
		break;

	case RIPCMD_TRACEON:
		fprintf(ftrace, "\tfile=\"%.*s\"\n", size-4,
			msg->rip_tracefile);
		break;

	case RIPCMD_TRACEOFF:
		break;
	}
}

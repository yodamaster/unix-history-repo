/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/sdt_impl.h>

static dtrace_pattr_t vtrace_attr = {
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t info_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t fpu_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_CPU },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t fsinfo_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t stab_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t sdt_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t xpv_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_PLATFORM },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_PLATFORM },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_PLATFORM },
};

sdt_provider_t sdt_providers[] = {
	{ "vtrace", "__vtrace_", &vtrace_attr, 0 },
	{ "sysinfo", "__cpu_sysinfo_", &info_attr, 0 },
	{ "vminfo", "__cpu_vminfo_", &info_attr, 0 },
	{ "fpuinfo", "__fpuinfo_", &fpu_attr, 0 },
	{ "sched", "__sched_", &stab_attr, 0 },
	{ "proc", "__proc_", &stab_attr, 0 },
	{ "io", "__io_", &stab_attr, 0 },
	{ "mib", "__mib_", &stab_attr, 0 },
	{ "fsinfo", "__fsinfo_", &fsinfo_attr, 0 },
	{ "nfsv3", "__nfsv3_", &stab_attr, 0 },
	{ "nfsv4", "__nfsv4_", &stab_attr, 0 },
	{ "xpv", "__xpv_", &xpv_attr, 0 },
	{ "sysevent", "__sysevent_", &stab_attr, 0 },
	{ "sdt", NULL, &sdt_attr, 0 },
	{ NULL }
};

sdt_argdesc_t sdt_args[] = {
	{ "sched", "wakeup", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "wakeup", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "dequeue", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "dequeue", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "dequeue", 2, 1, "disp_t *", "cpuinfo_t *" },
	{ "sched", "enqueue", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "enqueue", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "enqueue", 2, 1, "disp_t *", "cpuinfo_t *" },
	{ "sched", "enqueue", 3, 2, "int" },
	{ "sched", "off-cpu", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "off-cpu", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "tick", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "tick", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "change-pri", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "change-pri", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "change-pri", 2, 1, "pri_t" },
	{ "sched", "schedctl-nopreempt", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "schedctl-nopreempt", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "schedctl-nopreempt", 2, 1, "int" },
	{ "sched", "schedctl-preempt", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "schedctl-preempt", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "schedctl-yield", 0, 0, "int" },
	{ "sched", "surrender", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "surrender", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "cpucaps-sleep", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "cpucaps-sleep", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "cpucaps-wakeup", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "cpucaps-wakeup", 1, 0, "kthread_t *", "psinfo_t *" },

	{ "proc", "create", 0, 0, "proc_t *", "psinfo_t *" },
	{ "proc", "exec", 0, 0, "string" },
	{ "proc", "exec-failure", 0, 0, "int" },
	{ "proc", "exit", 0, 0, "int" },
	{ "proc", "fault", 0, 0, "int" },
	{ "proc", "fault", 1, 1, "siginfo_t *" },
	{ "proc", "lwp-create", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "proc", "lwp-create", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "proc", "signal-clear", 0, 0, "int" },
	{ "proc", "signal-clear", 1, 1, "siginfo_t *" },
	{ "proc", "signal-discard", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "proc", "signal-discard", 1, 1, "proc_t *", "psinfo_t *" },
	{ "proc", "signal-discard", 2, 2, "int" },
	{ "proc", "signal-handle", 0, 0, "int" },
	{ "proc", "signal-handle", 1, 1, "siginfo_t *" },
	{ "proc", "signal-handle", 2, 2, "void (*)(void)" },
	{ "proc", "signal-send", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "proc", "signal-send", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "proc", "signal-send", 2, 1, "int" },

	{ "io", "start", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "start", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "start", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "io", "done", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "done", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "done", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "io", "wait-start", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "wait-start", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "wait-start", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "io", "wait-done", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "wait-done", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "wait-done", 2, 0, "buf_t *", "fileinfo_t *" },

	{ "mib", NULL, 0, 0, "int" },

	{ "fsinfo", NULL, 0, 0, "vnode_t *", "fileinfo_t *" },
	{ "fsinfo", NULL, 1, 1, "int", "int" },

	{ "nfsv3", "op-getattr-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-getattr-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-getattr-start", 2, 3, "GETATTR3args *" },
	{ "nfsv3", "op-getattr-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-getattr-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-getattr-done", 2, 3, "GETATTR3res *" },
	{ "nfsv3", "op-setattr-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-setattr-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-setattr-start", 2, 3, "SETATTR3args *" },
	{ "nfsv3", "op-setattr-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-setattr-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-setattr-done", 2, 3, "SETATTR3res *" },
	{ "nfsv3", "op-lookup-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-lookup-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-lookup-start", 2, 3, "LOOKUP3args *" },
	{ "nfsv3", "op-lookup-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-lookup-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-lookup-done", 2, 3, "LOOKUP3res *" },
	{ "nfsv3", "op-access-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-access-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-access-start", 2, 3, "ACCESS3args *" },
	{ "nfsv3", "op-access-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-access-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-access-done", 2, 3, "ACCESS3res *" },
	{ "nfsv3", "op-commit-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-commit-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-commit-start", 2, 3, "COMMIT3args *" },
	{ "nfsv3", "op-commit-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-commit-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-commit-done", 2, 3, "COMMIT3res *" },
	{ "nfsv3", "op-create-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-create-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-create-start", 2, 3, "CREATE3args *" },
	{ "nfsv3", "op-create-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-create-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-create-done", 2, 3, "CREATE3res *" },
	{ "nfsv3", "op-fsinfo-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-fsinfo-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-fsinfo-start", 2, 3, "FSINFO3args *" },
	{ "nfsv3", "op-fsinfo-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-fsinfo-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-fsinfo-done", 2, 3, "FSINFO3res *" },
	{ "nfsv3", "op-fsstat-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-fsstat-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-fsstat-start", 2, 3, "FSSTAT3args *" },
	{ "nfsv3", "op-fsstat-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-fsstat-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-fsstat-done", 2, 3, "FSSTAT3res *" },
	{ "nfsv3", "op-link-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-link-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-link-start", 2, 3, "LINK3args *" },
	{ "nfsv3", "op-link-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-link-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-link-done", 2, 3, "LINK3res *" },
	{ "nfsv3", "op-mkdir-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-mkdir-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-mkdir-start", 2, 3, "MKDIR3args *" },
	{ "nfsv3", "op-mkdir-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-mkdir-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-mkdir-done", 2, 3, "MKDIR3res *" },
	{ "nfsv3", "op-mknod-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-mknod-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-mknod-start", 2, 3, "MKNOD3args *" },
	{ "nfsv3", "op-mknod-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-mknod-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-mknod-done", 2, 3, "MKNOD3res *" },
	{ "nfsv3", "op-null-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-null-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-null-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-null-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-pathconf-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-pathconf-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-pathconf-start", 2, 3, "PATHCONF3args *" },
	{ "nfsv3", "op-pathconf-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-pathconf-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-pathconf-done", 2, 3, "PATHCONF3res *" },
	{ "nfsv3", "op-read-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-read-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-read-start", 2, 3, "READ3args *" },
	{ "nfsv3", "op-read-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-read-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-read-done", 2, 3, "READ3res *" },
	{ "nfsv3", "op-readdir-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-readdir-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-readdir-start", 2, 3, "READDIR3args *" },
	{ "nfsv3", "op-readdir-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-readdir-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-readdir-done", 2, 3, "READDIR3res *" },
	{ "nfsv3", "op-readdirplus-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-readdirplus-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-readdirplus-start", 2, 3, "READDIRPLUS3args *" },
	{ "nfsv3", "op-readdirplus-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-readdirplus-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-readdirplus-done", 2, 3, "READDIRPLUS3res *" },
	{ "nfsv3", "op-readlink-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-readlink-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-readlink-start", 2, 3, "READLINK3args *" },
	{ "nfsv3", "op-readlink-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-readlink-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-readlink-done", 2, 3, "READLINK3res *" },
	{ "nfsv3", "op-remove-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-remove-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-remove-start", 2, 3, "REMOVE3args *" },
	{ "nfsv3", "op-remove-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-remove-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-remove-done", 2, 3, "REMOVE3res *" },
	{ "nfsv3", "op-rename-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-rename-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-rename-start", 2, 3, "RENAME3args *" },
	{ "nfsv3", "op-rename-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-rename-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-rename-done", 2, 3, "RENAME3res *" },
	{ "nfsv3", "op-rmdir-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-rmdir-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-rmdir-start", 2, 3, "RMDIR3args *" },
	{ "nfsv3", "op-rmdir-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-rmdir-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-rmdir-done", 2, 3, "RMDIR3res *" },
	{ "nfsv3", "op-setattr-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-setattr-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-setattr-start", 2, 3, "SETATTR3args *" },
	{ "nfsv3", "op-setattr-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-setattr-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-setattr-done", 2, 3, "SETATTR3res *" },
	{ "nfsv3", "op-symlink-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-symlink-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-symlink-start", 2, 3, "SYMLINK3args *" },
	{ "nfsv3", "op-symlink-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-symlink-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-symlink-done", 2, 3, "SYMLINK3res *" },
	{ "nfsv3", "op-write-start", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-write-start", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-write-start", 2, 3, "WRITE3args *" },
	{ "nfsv3", "op-write-done", 0, 0, "struct svc_req *",
	    "conninfo_t *" },
	{ "nfsv3", "op-write-done", 1, 1, "nfsv3oparg_t *",
	    "nfsv3opinfo_t *" },
	{ "nfsv3", "op-write-done", 2, 3, "WRITE3res *" },

	{ "nfsv4", "null-start", 0, 0, "struct svc_req *", "conninfo_t *" },
	{ "nfsv4", "null-done", 0, 0, "struct svc_req *", "conninfo_t *" },
	{ "nfsv4", "compound-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "compound-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "compound-start", 2, 1, "COMPOUND4args *" },
	{ "nfsv4", "compound-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "compound-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "compound-done", 2, 1, "COMPOUND4res *" },
	{ "nfsv4", "op-access-start", 0, 0, "struct compound_state *",
	    "conninfo_t *"},
	{ "nfsv4", "op-access-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-access-start", 2, 1, "ACCESS4args *" },
	{ "nfsv4", "op-access-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-access-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-access-done", 2, 1, "ACCESS4res *" },
	{ "nfsv4", "op-close-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-close-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-close-start", 2, 1, "CLOSE4args *" },
	{ "nfsv4", "op-close-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-close-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-close-done", 2, 1, "CLOSE4res *" },
	{ "nfsv4", "op-commit-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-commit-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-commit-start", 2, 1, "COMMIT4args *" },
	{ "nfsv4", "op-commit-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-commit-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-commit-done", 2, 1, "COMMIT4res *" },
	{ "nfsv4", "op-create-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-create-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-create-start", 2, 1, "CREATE4args *" },
	{ "nfsv4", "op-create-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-create-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-create-done", 2, 1, "CREATE4res *" },
	{ "nfsv4", "op-delegpurge-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-delegpurge-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-delegpurge-start", 2, 1, "DELEGPURGE4args *" },
	{ "nfsv4", "op-delegpurge-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-delegpurge-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-delegpurge-done", 2, 1, "DELEGPURGE4res *" },
	{ "nfsv4", "op-delegreturn-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-delegreturn-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-delegreturn-start", 2, 1, "DELEGRETURN4args *" },
	{ "nfsv4", "op-delegreturn-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-delegreturn-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-delegreturn-done", 2, 1, "DELEGRETURN4res *" },
	{ "nfsv4", "op-getattr-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-getattr-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-getattr-start", 2, 1, "GETATTR4args *" },
	{ "nfsv4", "op-getattr-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-getattr-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-getattr-done", 2, 1, "GETATTR4res *" },
	{ "nfsv4", "op-getfh-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-getfh-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-getfh-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-getfh-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-getfh-done", 2, 1, "GETFH4res *" },
	{ "nfsv4", "op-link-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-link-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-link-start", 2, 1, "LINK4args *" },
	{ "nfsv4", "op-link-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-link-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-link-done", 2, 1, "LINK4res *" },
	{ "nfsv4", "op-lock-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lock-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lock-start", 2, 1, "LOCK4args *" },
	{ "nfsv4", "op-lock-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lock-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lock-done", 2, 1, "LOCK4res *" },
	{ "nfsv4", "op-lockt-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lockt-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lockt-start", 2, 1, "LOCKT4args *" },
	{ "nfsv4", "op-lockt-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lockt-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lockt-done", 2, 1, "LOCKT4res *" },
	{ "nfsv4", "op-locku-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-locku-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-locku-start", 2, 1, "LOCKU4args *" },
	{ "nfsv4", "op-locku-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-locku-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-locku-done", 2, 1, "LOCKU4res *" },
	{ "nfsv4", "op-lookup-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lookup-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lookup-start", 2, 1, "LOOKUP4args *" },
	{ "nfsv4", "op-lookup-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lookup-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lookup-done", 2, 1, "LOOKUP4res *" },
	{ "nfsv4", "op-lookupp-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lookupp-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lookupp-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-lookupp-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-lookupp-done", 2, 1, "LOOKUPP4res *" },
	{ "nfsv4", "op-nverify-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-nverify-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-nverify-start", 2, 1, "NVERIFY4args *" },
	{ "nfsv4", "op-nverify-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-nverify-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-nverify-done", 2, 1, "NVERIFY4res *" },
	{ "nfsv4", "op-open-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-open-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-open-start", 2, 1, "OPEN4args *" },
	{ "nfsv4", "op-open-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-open-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-open-done", 2, 1, "OPEN4res *" },
	{ "nfsv4", "op-open-confirm-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-open-confirm-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-open-confirm-start", 2, 1, "OPEN_CONFIRM4args *" },
	{ "nfsv4", "op-open-confirm-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-open-confirm-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-open-confirm-done", 2, 1, "OPEN_CONFIRM4res *" },
	{ "nfsv4", "op-open-downgrade-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-open-downgrade-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-open-downgrade-start", 2, 1, "OPEN_DOWNGRADE4args *" },
	{ "nfsv4", "op-open-downgrade-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-open-downgrade-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-open-downgrade-done", 2, 1, "OPEN_DOWNGRADE4res *" },
	{ "nfsv4", "op-openattr-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-openattr-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-openattr-start", 2, 1, "OPENATTR4args *" },
	{ "nfsv4", "op-openattr-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-openattr-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-openattr-done", 2, 1, "OPENATTR4res *" },
	{ "nfsv4", "op-putfh-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-putfh-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-putfh-start", 2, 1, "PUTFH4args *" },
	{ "nfsv4", "op-putfh-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-putfh-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-putfh-done", 2, 1, "PUTFH4res *" },
	{ "nfsv4", "op-putpubfh-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-putpubfh-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-putpubfh-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-putpubfh-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-putpubfh-done", 2, 1, "PUTPUBFH4res *" },
	{ "nfsv4", "op-putrootfh-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-putrootfh-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-putrootfh-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-putrootfh-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-putrootfh-done", 2, 1, "PUTROOTFH4res *" },
	{ "nfsv4", "op-read-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-read-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-read-start", 2, 1, "READ4args *" },
	{ "nfsv4", "op-read-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-read-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-read-done", 2, 1, "READ4res *" },
	{ "nfsv4", "op-readdir-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-readdir-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-readdir-start", 2, 1, "READDIR4args *" },
	{ "nfsv4", "op-readdir-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-readdir-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-readdir-done", 2, 1, "READDIR4res *" },
	{ "nfsv4", "op-readlink-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-readlink-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-readlink-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-readlink-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-readlink-done", 2, 1, "READLINK4res *" },
	{ "nfsv4", "op-release-lockowner-start", 0, 0,
	    "struct compound_state *", "conninfo_t *" },
	{ "nfsv4", "op-release-lockowner-start", 1, 0,
	    "struct compound_state *", "nfsv4opinfo_t *" },
	{ "nfsv4", "op-release-lockowner-start", 2, 1,
	    "RELEASE_LOCKOWNER4args *" },
	{ "nfsv4", "op-release-lockowner-done", 0, 0,
	    "struct compound_state *", "conninfo_t *" },
	{ "nfsv4", "op-release-lockowner-done", 1, 0,
	    "struct compound_state *", "nfsv4opinfo_t *" },
	{ "nfsv4", "op-release-lockowner-done", 2, 1,
	    "RELEASE_LOCKOWNER4res *" },
	{ "nfsv4", "op-remove-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-remove-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-remove-start", 2, 1, "REMOVE4args *" },
	{ "nfsv4", "op-remove-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-remove-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-remove-done", 2, 1, "REMOVE4res *" },
	{ "nfsv4", "op-rename-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-rename-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-rename-start", 2, 1, "RENAME4args *" },
	{ "nfsv4", "op-rename-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-rename-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-rename-done", 2, 1, "RENAME4res *" },
	{ "nfsv4", "op-renew-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-renew-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-renew-start", 2, 1, "RENEW4args *" },
	{ "nfsv4", "op-renew-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-renew-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-renew-done", 2, 1, "RENEW4res *" },
	{ "nfsv4", "op-restorefh-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-restorefh-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-restorefh-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-restorefh-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-restorefh-done", 2, 1, "RESTOREFH4res *" },
	{ "nfsv4", "op-savefh-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-savefh-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-savefh-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-savefh-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-savefh-done", 2, 1, "SAVEFH4res *" },
	{ "nfsv4", "op-secinfo-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-secinfo-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-secinfo-start", 2, 1, "SECINFO4args *" },
	{ "nfsv4", "op-secinfo-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-secinfo-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-secinfo-done", 2, 1, "SECINFO4res *" },
	{ "nfsv4", "op-setattr-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-setattr-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-setattr-start", 2, 1, "SETATTR4args *" },
	{ "nfsv4", "op-setattr-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-setattr-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-setattr-done", 2, 1, "SETATTR4res *" },
	{ "nfsv4", "op-setclientid-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-setclientid-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-setclientid-start", 2, 1, "SETCLIENTID4args *" },
	{ "nfsv4", "op-setclientid-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-setclientid-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-setclientid-done", 2, 1, "SETCLIENTID4res *" },
	{ "nfsv4", "op-setclientid-confirm-start", 0, 0,
	    "struct compound_state *", "conninfo_t *" },
	{ "nfsv4", "op-setclientid-confirm-start", 1, 0,
	    "struct compound_state *", "nfsv4opinfo_t *" },
	{ "nfsv4", "op-setclientid-confirm-start", 2, 1,
	    "SETCLIENTID_CONFIRM4args *" },
	{ "nfsv4", "op-setclientid-confirm-done", 0, 0,
	    "struct compound_state *", "conninfo_t *" },
	{ "nfsv4", "op-setclientid-confirm-done", 1, 0,
	    "struct compound_state *", "nfsv4opinfo_t *" },
	{ "nfsv4", "op-setclientid-confirm-done", 2, 1,
	    "SETCLIENTID_CONFIRM4res *" },
	{ "nfsv4", "op-verify-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-verify-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-verify-start", 2, 1, "VERIFY4args *" },
	{ "nfsv4", "op-verify-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-verify-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-verify-done", 2, 1, "VERIFY4res *" },
	{ "nfsv4", "op-write-start", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-write-start", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-write-start", 2, 1, "WRITE4args *" },
	{ "nfsv4", "op-write-done", 0, 0, "struct compound_state *",
	    "conninfo_t *" },
	{ "nfsv4", "op-write-done", 1, 0, "struct compound_state *",
	    "nfsv4opinfo_t *" },
	{ "nfsv4", "op-write-done", 2, 1, "WRITE4res *" },
	{ "nfsv4", "cb-recall-start", 0, 0, "rfs4_client_t *",
	    "conninfo_t *" },
	{ "nfsv4", "cb-recall-start", 1, 1, "rfs4_deleg_state_t *",
	    "nfsv4cbinfo_t *" },
	{ "nfsv4", "cb-recall-start", 2, 2, "CB_RECALL4args *" },
	{ "nfsv4", "cb-recall-done", 0, 0, "rfs4_client_t *",
	    "conninfo_t *" },
	{ "nfsv4", "cb-recall-done", 1, 1, "rfs4_deleg_state_t *",
	    "nfsv4cbinfo_t *" },
	{ "nfsv4", "cb-recall-done", 2, 2, "CB_RECALL4res *" },

	{ "sysevent", "post", 0, 0, "evch_bind_t *", "syseventchaninfo_t *" },
	{ "sysevent", "post", 1, 1, "sysevent_impl_t *", "syseventinfo_t *" },

	{ "xpv", "add-to-physmap-end", 0, 0, "int" },
	{ "xpv", "add-to-physmap-start", 0, 0, "domid_t" },
	{ "xpv", "add-to-physmap-start", 1, 1, "uint_t" },
	{ "xpv", "add-to-physmap-start", 2, 2, "ulong_t" },
	{ "xpv", "add-to-physmap-start", 3, 3, "ulong_t" },
	{ "xpv", "decrease-reservation-end", 0, 0, "int" },
	{ "xpv", "decrease-reservation-start", 0, 0, "domid_t" },
	{ "xpv", "decrease-reservation-start", 1, 1, "ulong_t" },
	{ "xpv", "decrease-reservation-start", 2, 2, "uint_t" },
	{ "xpv", "decrease-reservation-start", 3, 3, "ulong_t *" },
	{ "xpv", "dom-create-start", 0, 0, "xen_domctl_t *" },
	{ "xpv", "dom-destroy-start", 0, 0, "domid_t" },
	{ "xpv", "dom-pause-start", 0, 0, "domid_t" },
	{ "xpv", "dom-unpause-start", 0, 0, "domid_t" },
	{ "xpv", "dom-create-end", 0, 0, "int" },
	{ "xpv", "dom-destroy-end", 0, 0, "int" },
	{ "xpv", "dom-pause-end", 0, 0, "int" },
	{ "xpv", "dom-unpause-end", 0, 0, "int" },
	{ "xpv", "evtchn-op-end", 0, 0, "int" },
	{ "xpv", "evtchn-op-start", 0, 0, "int" },
	{ "xpv", "evtchn-op-start", 1, 1, "void *" },
	{ "xpv", "increase-reservation-end", 0, 0, "int" },
	{ "xpv", "increase-reservation-start", 0, 0, "domid_t" },
	{ "xpv", "increase-reservation-start", 1, 1, "ulong_t" },
	{ "xpv", "increase-reservation-start", 2, 2, "uint_t" },
	{ "xpv", "increase-reservation-start", 3, 3, "ulong_t *" },
	{ "xpv", "mmap-end", 0, 0, "int" },
	{ "xpv", "mmap-entry", 0, 0, "ulong_t" },
	{ "xpv", "mmap-entry", 1, 1, "ulong_t" },
	{ "xpv", "mmap-entry", 2, 2, "ulong_t" },
	{ "xpv", "mmap-start", 0, 0, "domid_t" },
	{ "xpv", "mmap-start", 1, 1, "int" },
	{ "xpv", "mmap-start", 2, 2, "privcmd_mmap_entry_t *" },
	{ "xpv", "mmapbatch-end", 0, 0, "int" },
	{ "xpv", "mmapbatch-end", 1, 1, "struct seg *" },
	{ "xpv", "mmapbatch-end", 2, 2, "caddr_t" },
	{ "xpv", "mmapbatch-start", 0, 0, "domid_t" },
	{ "xpv", "mmapbatch-start", 1, 1, "int" },
	{ "xpv", "mmapbatch-start", 2, 2, "caddr_t" },
	{ "xpv", "mmu-ext-op-end", 0, 0, "int" },
	{ "xpv", "mmu-ext-op-start", 0, 0, "int" },
	{ "xpv", "mmu-ext-op-start", 1, 1, "struct mmuext_op *" },
	{ "xpv", "mmu-update-start", 0, 0, "int" },
	{ "xpv", "mmu-update-start", 1, 1, "int" },
	{ "xpv", "mmu-update-start", 2, 2, "mmu_update_t *" },
	{ "xpv", "mmu-update-end", 0, 0, "int" },
	{ "xpv", "populate-physmap-end", 0, 0, "int" },
	{ "xpv", "populate-physmap-start", 0, 0, "domid_t" },
	{ "xpv", "populate-physmap-start", 1, 1, "ulong_t" },
	{ "xpv", "populate-physmap-start", 2, 2, "ulong_t *" },
	{ "xpv", "set-memory-map-end", 0, 0, "int" },
	{ "xpv", "set-memory-map-start", 0, 0, "domid_t" },
	{ "xpv", "set-memory-map-start", 1, 1, "int" },
	{ "xpv", "set-memory-map-start", 2, 2, "struct xen_memory_map *" },
	{ "xpv", "setvcpucontext-end", 0, 0, "int" },
	{ "xpv", "setvcpucontext-start", 0, 0, "domid_t" },
	{ "xpv", "setvcpucontext-start", 1, 1, "vcpu_guest_context_t *" },
	{ NULL }
};

/*ARGSUSED*/
void
sdt_getargdesc(void *arg, dtrace_id_t id, void *parg, dtrace_argdesc_t *desc)
{
	sdt_probe_t *sdp = parg;
	int i;

	desc->dtargd_native[0] = '\0';
	desc->dtargd_xlate[0] = '\0';

	for (i = 0; sdt_args[i].sda_provider != NULL; i++) {
		sdt_argdesc_t *a = &sdt_args[i];

		if (strcmp(sdp->sdp_provider->sdtp_name, a->sda_provider) != 0)
			continue;

		if (a->sda_name != NULL &&
		    strcmp(sdp->sdp_name, a->sda_name) != 0)
			continue;

		if (desc->dtargd_ndx != a->sda_ndx)
			continue;

		if (a->sda_native != NULL)
			(void) strcpy(desc->dtargd_native, a->sda_native);

		if (a->sda_xlate != NULL)
			(void) strcpy(desc->dtargd_xlate, a->sda_xlate);

		desc->dtargd_mapping = a->sda_mapping;
		return;
	}

	desc->dtargd_ndx = DTRACE_ARGNONE;
}

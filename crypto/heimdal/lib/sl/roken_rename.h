/*
 * Copyright (c) 1998 - 2001 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

/* $Id: roken_rename.h,v 1.5 2001/05/06 21:47:54 assar Exp $ */

#ifndef __roken_rename_h__
#define __roken_rename_h__

#ifndef HAVE_STRTOK_R
#define strtok_r _sl_strtok_r
#endif
#ifndef HAVE_SNPRINTF
#define snprintf _sl_snprintf
#endif
#ifndef HAVE_ASPRINTF
#define asprintf _sl_asprintf
#endif
#ifndef HAVE_ASNPRINTF
#define asnprintf _sl_asnprintf
#endif
#ifndef HAVE_VASPRINTF
#define vasprintf _sl_vasprintf
#endif
#ifndef HAVE_VASNPRINTF
#define vasnprintf _sl_vasnprintf
#endif
#ifndef HAVE_VSNPRINTF
#define vsnprintf _sl_vsnprintf
#endif
#ifndef HAVE_STRUPR
#define strupr _sl_strupr
#endif
#ifndef HAVE_STRDUP
#define strdup _sl_strdup
#endif

#endif /* __roken_rename_h__ */

/* Multiple object format emulation.
   Copyright (C) 1995, 96, 97, 99, 2000
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 1, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef _OBJ_MULTI_H
#define _OBJ_MULTI_H

#ifdef OBJ_HEADER
#include OBJ_HEADER
#else

#include "emul.h"
#include "targ-cpu.h"

#define OUTPUT_FLAVOR					\
	(this_format->flavor)

#define obj_frob_symbol(S,P)				\
	(*this_format->frob_symbol) (S, &(P))

#define obj_frob_file()					\
	(this_format->frob_file				\
	 ? (*this_format->frob_file) ()			\
	 : (void) 0)

#define obj_frob_file_after_relocs()			\
	(this_format->frob_file_after_relocs		\
	 ? (*this_format->frob_file_after_relocs) ()	\
	 : (void) 0)

#define obj_ecoff_set_ext				\
	(*this_format->ecoff_set_ext)

#define obj_pop_insert					\
	(*this_format->pop_insert)

#define obj_read_begin_hook()				\
	(this_format->read_begin_hook			\
	 ? (*this_format->read_begin_hook) ()		\
	 : (void) 0)

#define obj_symbol_new_hook(S)				\
	(this_format->symbol_new_hook			\
	 ? (*this_format->symbol_new_hook) (S)		\
	 : (void) 0)

#define obj_sec_sym_ok_for_reloc(A)			\
	(this_format->sec_sym_ok_for_reloc		\
	 ? (*this_format->sec_sym_ok_for_reloc) (A)	\
	 : 0)

#define S_GET_SIZE					\
	(*this_format->s_get_size)

#define S_SET_SIZE					\
	(*this_format->s_set_size)

#define S_GET_ALIGN					\
	(*this_format->s_get_align)

#define S_SET_ALIGN					\
	(*this_format->s_set_align)

#define S_GET_OTHER					\
	(*this_format->s_get_other)

#define S_GET_DESC					\
	(*this_format->s_get_desc)

#define OBJ_COPY_SYMBOL_ATTRIBUTES(d,s)			\
	(this_format->copy_symbol_attributes		\
	 ? (*this_format->copy_symbol_attributes) (d, s) \
	 : (void) 0)

#define OBJ_PROCESS_STAB(SEG,W,S,T,O,D)			\
	(this_format->process_stab			\
	 ? (*this_format->process_stab) (SEG,W,S,T,O,D)	\
	 : (void) 0)

#ifdef OBJ_MAYBE_ELF
/* We need OBJ_SYMFIELD_TYPE so that symbol_get_obj is defined in symbol.c
   We also need various STAB defines for stab.c  */
#include "obj-elf.h"
#endif

#endif /* !OBJ_HEADER */
#endif /* _OBJ_MULTI_H */

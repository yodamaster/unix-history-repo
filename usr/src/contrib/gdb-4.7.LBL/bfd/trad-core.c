/* BFD back end for traditional Unix core files (U-area and raw sections)
   Copyright (C) 1988, 1989, 1991 Free Software Foundation, Inc.
   Written by John Gilmore of Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* To use this file on a particular host, configure the host with these
   parameters in the config/h-HOST file:

	HDEFINES=-DTRAD_CORE
	HDEPFILES=trad-core.o

 */

#include "bfd.h"
#include "sysdep.h"
#include "libbfd.h"
#include "libaout.h"           /* BFD a.out internal data structures */

#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/dir.h>
#include <signal.h>

#include <sys/user.h>		/* After a.out.h  */
#include <sys/file.h>

#include <errno.h>

  struct trad_core_struct 
    {
      asection *data_section;
      asection *stack_section;
      asection *reg_section;

      struct user		u;
    } *rawptr;


#define core_upage(bfd) (&((bfd)->tdata.trad_core_data->u))
#define core_datasec(bfd) ((bfd)->tdata.trad_core_data->data_section)
#define core_stacksec(bfd) ((bfd)->tdata.trad_core_data->stack_section)
#define core_regsec(bfd) ((bfd)->tdata.trad_core_data->reg_section)

/* Handle 4.2-style (and perhaps also sysV-style) core dump file.  */

/* ARGSUSED */
bfd_target *
trad_unix_core_file_p (abfd)
     bfd *abfd;

{
  int val;
  struct user u;
  /* This struct is just for allocating two things with one zalloc, so
     they will be freed together, without violating alignment constraints. */


  val = bfd_read ((void *)&u, 1, sizeof u, abfd);
  if (val != sizeof u)
    return 0;			/* Too small to be a core file */

  /* Sanity check perhaps??? */
  if (u.u_dsize > 0x1000000)	/* Remember, it's in pages... */
    return 0;
  if (u.u_ssize > 0x1000000)
    return 0;
  /* Check that the size claimed is no greater than the file size. FIXME. */

  /* OK, we believe you.  You're a core file (sure, sure).  */

  /* Allocate both the upage and the struct core_data at once, so
     a single free() will free them both.  */
  rawptr = (struct trad_core_struct *)bfd_zalloc (abfd, sizeof (struct trad_core_struct));
  if (rawptr == NULL) {
    bfd_error = no_memory;
    return 0;
  }
  
  abfd->tdata.trad_core_data = rawptr;

  rawptr->u = u; /*Copy the uarea into the tdata part of the bfd */

  /* Create the sections.  This is raunchy, but bfd_close wants to free
     them separately.  */

  core_stacksec(abfd) = (asection *) zalloc (sizeof (asection));
  if (core_stacksec (abfd) == NULL) {
  loser:
    bfd_error = no_memory;
    free ((void *)rawptr);
    return 0;
  }
  core_datasec (abfd) = (asection *) zalloc (sizeof (asection));
  if (core_datasec (abfd) == NULL) {
  loser1:
    free ((void *)core_stacksec (abfd));
    goto loser;
  }
  core_regsec (abfd) = (asection *) zalloc (sizeof (asection));
  if (core_regsec (abfd) == NULL) {
    free ((void *)core_datasec (abfd));
    goto loser1;
  }

  core_stacksec (abfd)->name = ".stack";
  core_datasec (abfd)->name = ".data";
  core_regsec (abfd)->name = ".reg";

  core_stacksec (abfd)->flags = SEC_ALLOC + SEC_LOAD + SEC_HAS_CONTENTS;
  core_datasec (abfd)->flags = SEC_ALLOC + SEC_LOAD + SEC_HAS_CONTENTS;
  core_regsec (abfd)->flags = SEC_ALLOC + SEC_HAS_CONTENTS;

  core_datasec (abfd)->_raw_size =  NBPG * u.u_dsize;
  core_stacksec (abfd)->_raw_size = NBPG * u.u_ssize;
  core_regsec (abfd)->_raw_size = NBPG * UPAGES; /* Larger than sizeof struct u */

  /* What a hack... we'd like to steal it from the exec file,
     since the upage does not seem to provide it.  FIXME.  */
#ifdef HOST_DATA_START_ADDR
  core_datasec (abfd)->vma = HOST_DATA_START_ADDR;
#else
  core_datasec (abfd)->vma = HOST_TEXT_START_ADDR + (NBPG * u.u_tsize);
#endif
  core_stacksec (abfd)->vma = HOST_STACK_END_ADDR - (NBPG * u.u_ssize);
  /* This is tricky.  As the "register section", we give them the entire
     upage and stack.  u.u_ar0 points to where "register 0" is stored.
     There are two tricks with this, though.  One is that the rest of the
     registers might be at positive or negative (or both) displacements
     from *u_ar0.  The other is that u_ar0 is sometimes an absolute address
     in kernel memory, and on other systems it is an offset from the beginning
     of the `struct user'.
     
     As a practical matter, we don't know where the registers actually are,
     so we have to pass the whole area to GDB.  We encode the value of u_ar0
     by setting the .regs section up so that its virtual memory address
     0 is at the place pointed to by u_ar0 (by setting the vma of the start
     of the section to -u_ar0).  GDB uses this info to locate the regs,
     using minor trickery to get around the offset-or-absolute-addr problem. */
  core_regsec (abfd)->vma = 0 - (int) u.u_ar0;

  core_datasec (abfd)->filepos = NBPG * UPAGES;
  core_stacksec (abfd)->filepos = (NBPG * UPAGES) + NBPG * u.u_dsize;
  core_regsec (abfd)->filepos = 0; /* Register segment is the upage */

  /* Align to word at least */
  core_stacksec (abfd)->alignment_power = 2;
  core_datasec (abfd)->alignment_power = 2;
  core_regsec (abfd)->alignment_power = 2;

  abfd->sections = core_stacksec (abfd);
  core_stacksec (abfd)->next = core_datasec (abfd);
  core_datasec (abfd)->next = core_regsec (abfd);
  abfd->section_count = 3;

  return abfd->xvec;
}

char *
trad_unix_core_file_failing_command (abfd)
     bfd *abfd;
{
#if !defined(NO_CORE_COMMAND) && BSD < 199207
  char *com = abfd->tdata.trad_core_data->u.u_comm;
  if (*com)
    return com;
  else
#endif
    return 0;
}

/* ARGSUSED */
int
trad_unix_core_file_failing_signal (ignore_abfd)
     bfd *ignore_abfd;
{
  return -1;		/* FIXME, where is it? */
}

/* ARGSUSED */
boolean
trad_unix_core_file_matches_executable_p  (core_bfd, exec_bfd)
     bfd *core_bfd, *exec_bfd;
{
  return true;		/* FIXME, We have no way of telling at this point */
}

/* No archive file support via this BFD */
#define	trad_unix_openr_next_archived_file	bfd_generic_openr_next_archived_file
#define	trad_unix_generic_stat_arch_elt		bfd_generic_stat_arch_elt
#define	trad_unix_slurp_armap			bfd_false
#define	trad_unix_slurp_extended_name_table	bfd_true
#define	trad_unix_write_armap			(PROTO (boolean, (*),	\
     (bfd *arch, unsigned int elength, struct orl *map, \
      unsigned int orl_count, int stridx))) bfd_false
#define	trad_unix_truncate_arname		bfd_dont_truncate_arname
#define	aout_32_openr_next_archived_file	bfd_generic_openr_next_archived_file

#define	trad_unix_close_and_cleanup		bfd_generic_close_and_cleanup
#define	trad_unix_set_section_contents		(PROTO(boolean, (*),	\
         (bfd *abfd, asection *section, PTR data, file_ptr offset,	\
         bfd_size_type count))) bfd_false
#define	trad_unix_get_section_contents		bfd_generic_get_section_contents
#define	trad_unix_new_section_hook		(PROTO (boolean, (*),	\
	(bfd *, sec_ptr))) bfd_true
#define	trad_unix_get_symtab_upper_bound	bfd_0u
#define	trad_unix_get_symtab			(PROTO (unsigned int, (*), \
        (bfd *, struct symbol_cache_entry **))) bfd_0u
#define	trad_unix_get_reloc_upper_bound		(PROTO (unsigned int, (*), \
	(bfd *, sec_ptr))) bfd_0u
#define	trad_unix_canonicalize_reloc		(PROTO (unsigned int, (*), \
	(bfd *, sec_ptr, arelent **, struct symbol_cache_entry**))) bfd_0u
#define	trad_unix_make_empty_symbol		(PROTO (		\
	struct symbol_cache_entry *, (*), (bfd *))) bfd_false
#define	trad_unix_print_symbol			(PROTO (void, (*),	\
	(bfd *, PTR, struct symbol_cache_entry  *,			\
	 bfd_print_symbol_type))) bfd_false
#define	trad_unix_get_lineno			(PROTO (alent *, (*),	\
	(bfd *, struct symbol_cache_entry *))) bfd_nullvoidptr
#define	trad_unix_set_arch_mach			(PROTO (boolean, (*),	\
	(bfd *, enum bfd_architecture, unsigned long))) bfd_false
#define	trad_unix_find_nearest_line		(PROTO (boolean, (*),	\
        (bfd *abfd, struct sec  *section,				\
         struct symbol_cache_entry  **symbols,bfd_vma offset,		\
         CONST char **file, CONST char **func, unsigned int *line))) bfd_false
#define	trad_unix_sizeof_headers		(PROTO (int, (*),	\
	(bfd *, boolean))) bfd_0

#define trad_unix_bfd_debug_info_start		bfd_void
#define trad_unix_bfd_debug_info_end		bfd_void
#define trad_unix_bfd_debug_info_accumulate	(PROTO (void, (*),	\
	(bfd *, struct sec *))) bfd_void
#define trad_unix_bfd_get_relocated_section_contents bfd_generic_get_relocated_section_contents
#define trad_unix_bfd_relax_section bfd_generic_relax_section
/* If somebody calls any byte-swapping routines, shoot them.  */
void
swap_abort()
{
  abort(); /* This way doesn't require any declaration for ANSI to fuck up */
}
#define	NO_GET	((PROTO(bfd_vma, (*), (         bfd_byte *))) swap_abort )
#define	NO_PUT	((PROTO(void,    (*), (bfd_vma, bfd_byte *))) swap_abort )

bfd_target trad_core_vec =
  {
    "trad-core",
    bfd_target_unknown_flavour,
    true,			/* target byte order */
    true,			/* target headers byte order */
    (HAS_RELOC | EXEC_P |	/* object flags */
     HAS_LINENO | HAS_DEBUG |
     HAS_SYMS | HAS_LOCALS | DYNAMIC | WP_TEXT | D_PAGED),
    (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
    '_',		                                   /* symbol prefix */
    ' ',						   /* ar_pad_char */
    16,							   /* ar_max_namelen */
    3,							   /* minimum alignment power */
    NO_GET, NO_PUT, NO_GET, NO_PUT, NO_GET, NO_PUT, /* data */
    NO_GET, NO_PUT, NO_GET, NO_PUT, NO_GET, NO_PUT, /* hdrs */

    {_bfd_dummy_target, _bfd_dummy_target,
     _bfd_dummy_target, trad_unix_core_file_p},
    {bfd_false, bfd_false,	/* bfd_create_object */
     bfd_false, bfd_false},
    {bfd_false, bfd_false,	/* bfd_write_contents */
     bfd_false, bfd_false},
    
    JUMP_TABLE(trad_unix)
};

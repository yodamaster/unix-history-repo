/* callback.c -- functions to use readline as an X `callback' mechanism. */

/* Copyright (C) 1987, 1989, 1992 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 1, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   675 Mass Ave, Cambridge, MA 02139, USA. */
#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include "rlconf.h"

#if defined (READLINE_CALLBACKS)

#include <sys/types.h>
#include <stdio.h>

/* System-specific feature definitions and include files. */
#include "rldefs.h"
#include "readline.h"

extern void readline_internal_startup ();
extern char *readline_internal_teardown ();
extern int readline_internal_char ();

extern int _rl_meta_flag;
extern char *rl_prompt;
extern int rl_visible_prompt_length;

/* **************************************************************** */
/*								    */
/*			Callback Readline Functions                 */
/*								    */
/* **************************************************************** */

/* Allow using readline in situations where a program may have multiple
   things to handle at once, and dispatches them via select().  Call
   rl_callback_handler_install() with the prompt and a function to call
   whenever a complete line of input is ready.  The user must then
   call readline_char() every time some input is available, and 
   readline_char() will call the user's function with the complete text
   read in at each end of line.  The terminal is kept prepped and signals
   handled all the time, except during calls to the user's function. */

VFunction *rl_linefunc;		/* user callback function */
static int in_handler;		/* terminal_prepped and signals set? */

/* Make sure the terminal is set up, initialize readline, and prompt. */
static void
_rl_callback_newline ()
{
  rl_initialize ();

  if (in_handler == 0)
    {
      in_handler = 1;

      (*rl_prep_term_function) (_rl_meta_flag);

#if defined (HANDLE_SIGNALS)
      rl_set_signals ();
#endif
    }

  readline_internal_setup ();
}

/* Install a readline handler, set up the terminal, and issue the prompt. */
void
rl_callback_handler_install (prompt, linefunc)
     char *prompt;
     VFunction *linefunc;
{
  rl_prompt = prompt;
  rl_visible_prompt_length = rl_prompt ? rl_expand_prompt (rl_prompt) : 0;
  rl_linefunc = linefunc;
  _rl_callback_newline ();
}

/* Read one character, and dispatch to the handler if it ends the line. */
void
rl_callback_read_char ()
{
  char *line;
  int eof;

  if (rl_linefunc == NULL)
    {
      fprintf (stderr, "readline: readline_callback_read_char() called with no handler!\r\n");
      abort ();
    }

  eof = readline_internal_char ();

  if (rl_done)
    {
      line = readline_internal_teardown (eof);

      (*rl_deprep_term_function) ();
#if defined (HANDLE_SIGNALS)
      rl_clear_signals ();
#endif
      in_handler = 0;
      (*rl_linefunc) (line);

    /* Redisplay the prompt if readline_handler_{install,remove} not called. */
      if (in_handler == 0 && rl_linefunc)
	_rl_callback_newline ();
    }
}

/* Remove the handler, and make sure the terminal is in its normal state. */
void
rl_callback_handler_remove ()
{
  rl_linefunc = NULL;
  if (in_handler)
    {
      in_handler = 0;
      (*rl_deprep_term_function) ();
#if defined (HANDLE_SIGNALS)
      rl_clear_signals ();
#endif
    }
}

#endif

/*
 * strtol : convert a string to long.
 *
 * Andy Wilson, 2-Oct-89.
 */

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include "ansidecl.h"

#ifndef ULONG_MAX
#define	ULONG_MAX	((unsigned long)(~0L))		/* 0xFFFFFFFF */
#endif

extern int errno;

unsigned long
strtoul(s, ptr, base)
     CONST char *s; char **ptr; int base;
{
  unsigned long total = 0, tmp = 0;
  unsigned digit;
  CONST char *start=s;
  int did_conversion=0;
  int negate = 0;

  if (s==NULL)
    {
      errno = ERANGE;
      if (!ptr)
	*ptr = (char *)start;
      return 0L;
    }

  while (isspace(*s))
    s++;
  if (*s == '+')
    s++;
  else if (*s == '-')
    s++, negate = 1;
  if (base==0 || base==16) /*  the 'base==16' is for handling 0x */
    {
      /*
       * try to infer base from the string
       */
      if (*s != '0')
        tmp = 10;	/* doesn't start with 0 - assume decimal */
      else if (s[1] == 'X' || s[1] == 'x')
	tmp = 16, s += 2; /* starts with 0x or 0X - hence hex */
      else
	tmp = 8;	/* starts with 0 - hence octal */
      if (base==0)
	base = (int)tmp;
    }

  while ( digit = *s )
    {
      if (digit >= '0' && digit < ('0'+base))
	digit -= '0';
      else
	if (base > 10)
	  {
	    if (digit >= 'a' && digit < ('a'+(base-10)))
	      digit = digit - 'a' + 10;
	    else if (digit >= 'A' && digit < ('A'+(base-10)))
	      digit = digit - 'A' + 10;
	    else
	      break;
	  }
	else
	  break;
      did_conversion = 1;
      tmp = (total * base) + digit;
      if (tmp < total)	/* check overflow */
	{
	  errno = ERANGE;
	  if (ptr != NULL)
	    *ptr = (char *)s;
	  return (ULONG_MAX);
	}
      total = tmp;
      s++;
    }
  if (ptr != NULL)
    *ptr = (char *) ((did_conversion) ? (char *)s : (char *)start);
  return negate ? -total : total;
}

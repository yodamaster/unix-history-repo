#include "../h/rt.h"

/*
 * right(s1,n,s2) - pad s1 on left with s2 to length n.
 */

Xright(nargs, arg3, arg2, arg1, arg0)
int nargs;
struct descrip arg3, arg2, arg1, arg0;
   {
   register char *s, *st;
   int cnt, slen;
   char *sbuf, *s3, sbuf1[MAXSTRING], sbuf2[MAXSTRING];
   extern char *alcstr();

   /*
    * s1 must be a string.  n must be a non-negative integer and defaults
    *  to 1.  s2 must be a string and defaults to a blank.
    */
   if (cvstr(&arg1, sbuf1) == NULL)
      runerr(103, &arg1);
   defshort(&arg2, 1);
   if ((cnt = INTVAL(arg2)) < 0)
      runerr(205, &arg2);
   defstr(&arg3, sbuf2, &blank);

   sneed(cnt);

   if (STRLEN(arg3) == 0) {
      /*
       * The padding string is null, make it a blank.
       */
      slen = 1;
      s3 = " ";
      }
   else {
      slen = STRLEN(arg3);
      s3 = STRLOC(arg3);
      }

   /*
    * Get n bytes of string space.  Start at the left end of the new
    *  string and copy s2 into the new string as many times as it fits.
    */
   sbuf = alcstr(NULL, cnt);
   s = sbuf;
   while (s < sbuf + cnt) {
      st = s3;
      while (st < s3 + slen && s < sbuf + cnt)
         *s++ = *st++;
      }

   /*
    * Copy s1 into the new string, starting at the right end and copying
    *  s2 from right to left.  If *s1 > n, only copy n bytes.
    */
   s = sbuf + cnt;
   slen = STRLEN(arg1);
   st = STRLOC(arg1) + slen;
   if (slen > cnt)
      slen = cnt;
   while (slen-- > 0)
      *--s = *--st;

   /*
    * Return the new string.
    */
   STRLEN(arg0) = cnt;
   STRLOC(arg0) = sbuf;
   }

Procblock(right,3)

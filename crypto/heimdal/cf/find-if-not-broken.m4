dnl $Id: find-if-not-broken.m4 13338 2004-02-12 14:21:14Z lha $
dnl
dnl
dnl Mix between AC_FIND_FUNC and AC_BROKEN
dnl

AC_DEFUN([AC_FIND_IF_NOT_BROKEN],
[AC_FIND_FUNC([$1], [$2], [$3], [$4])
if eval "test \"$ac_cv_func_$1\" != yes"; then 
	rk_LIBOBJ([$1])
fi
])

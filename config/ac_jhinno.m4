##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Custom for jhinno job scheduler integration
#
#  SYNOPSIS:
#    AC_JHINNO
#
#  DESCRIPTION:
#    Checks for whether to include jhinno module
#
##*****************************************************************************

AC_DEFUN([AC_JHINNO],
[
  #
  # Check for whether to build jhinno module
  # 
  AC_MSG_CHECKING([for whether to build jhinno module])
  AC_ARG_WITH([jhinno],
    AS_HELP_STRING([--with-jhinno],[support jhinno job scheduler via jhosts/jjobs]),
      [ case "$withval" in
        yes) ac_with_jhinno=yes ;;
        no)  ac_with_jhinno=no ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$withval" for --with-jhinno]) ;;
      esac
    ]
  )
  AC_MSG_RESULT([${ac_with_jhinno=no}]) 

  if test "$ac_with_jhinno" = "yes"; then
     AC_CHECK_PROG(JOBS_CMD, jjobs, jjobs, [/bin/false])
     AC_CHECK_PROG(JHOSTS_CMD, jhosts, jhosts, [/bin/false])
     
     if test "$JOBS_CMD" = "/bin/false" -o "$JHOSTS_CMD" = "/bin/false"; then
        AC_MSG_NOTICE([Cannot support jhinno without jjobs and jhosts commands.])
     else
        ac_have_jhinno=yes
        AC_ADD_STATIC_MODULE("jhinno")
        AC_DEFINE([HAVE_JHINNO], [1], [Define if you have jhinno.])
     fi
  fi

  AC_SUBST(HAVE_JHINNO)
])

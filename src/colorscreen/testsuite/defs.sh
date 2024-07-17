#! /bin/sh

# Make sure srcdir is an absolute path.  Supply the variable
# if it does not exist.  We want to be able to run the tests
# stand-alone!!
#
srcdir=${srcdir-.}
if test ! -d $srcdir ; then
    echo "defs: installation error" 1>&2
    exit 1
fi

#  IF the source directory is a Unix or a DOS root directory, ...
#
case "$srcdir" in
    /* | [A-Za-z]:\\*) ;;
    *) srcdir=`\cd $srcdir && pwd` ;;
esac

case "$top_builddir" in
    /* | [A-Za-z]:\\*) ;;
    *) top_builddir=`\cd ${top_builddir-..} && pwd` ;;
esac

TESTDATA=${top_srcdir}/src/colorscreen/testsuite
RUNCOLORSCREEN=${top_builddir}/src/colorscreen/colorscreen

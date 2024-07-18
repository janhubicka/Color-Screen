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

WRAP=""
if test -n "$TEST_VALGRIND" ; then
  WRAP="valgrind "
fi

# iterate across all rendering modes. invoke as
# test_all_render_modes <basename of scan> <basename of par in output dir>
test_all_render_modes() {
  NNAME=$1
  NPARNAME=$2
  MODES=`$RUNCOLORSCREEN --help 2>&1 | sed -n '/select one/,/--/p' | sed '1d;$d'`
  for mode in $MODES
  do
    echo rendering $NNAME with $NPARNAME.par to $NPARNAME-$mode.tif
    $RUNCOLORSCREEN render --mode $mode $NNAME $NPARNAME.par $NPARNAME-$mode.tif
  done
}
# autodetect
# test_autodetect <basename of scan> <basename of output par>
test_autodetect()
{
  NNAME=$1
  NPARNAME=$2
  shift
  shift
  echo "autodetect $NNAME to $NPARNAME with flags $*"
  $RUNCOLORSCREEN autodetect $TESTDATA/$NNAME.tif $NPARNAME.par --report=$NPARNAME.txt $* || exit 1
  grep "^Analyzed 99" "$NPARNAME".txt || exit 1
}
test_autodetect90()
{
  NNAME=$1
  NPARNAME=$2
  shift
  shift
  echo "autodetect $NNAME to $NPARNAME with flags $*"
  $RUNCOLORSCREEN autodetect $TESTDATA/$NNAME.tif $NPARNAME.par --report=$NPARNAME.txt $* || exit 1
  grep "^Analyzed 9[0-9]" "$NPARNAME".txt || exit 1
}
test_autodetect80()
{
  NNAME=$1
  NPARNAME=$2
  shift
  shift
  echo "autodetect $NNAME to $NPARNAME with flags $*"
  $RUNCOLORSCREEN autodetect $TESTDATA/$NNAME.tif $NPARNAME.par --report=$NPARNAME.txt $* || exit 1
  grep "^Analyzed [8-9][0-9]" "$NPARNAME".txt || exit 1
}
# autodetect and iterate across all rendering modes. invoke as
# test_all_render_modes <basename of scan> <basename of output par>
test_autodetect_and_render() {
  test_autodetect $*
  test_all_render_modes $TESTDATA/"$1".tif "$2"
}

TESTDATA=${top_srcdir}/testsuite
RUNCOLORSCREEN=$WRAP${top_builddir}/src/colorscreen/colorscreen

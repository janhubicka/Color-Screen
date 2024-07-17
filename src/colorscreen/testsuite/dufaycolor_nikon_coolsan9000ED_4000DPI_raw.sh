## -*- sh -*-
## incomplete.test -- Test incomplete command handling

# Common definitions
if test -z "$srcdir"; then
    srcdir=echo "$0" | sed 's,[^/]*$,,'
    test "$srcdir" = "$0" && srcdir=.
    test -z "$srcdir" && srcdir=.
    test "${VERBOSE+set}" != set && VERBOSE=1
fi
. $srcdir/defs.sh
NAME=dufaycolor_nikon_coolsan9000ED_4000DPI_raw
MODES=`$RUNCOLORSCREEN --help 2>&1 | sed -n '/select one/,/--/p' | sed '1d;$d'`
echo modes: $MODES
$RUNCOLORSCREEN autodetect --gamma 1 $TESTDATA/$NAME.tif $name.par --report=$NAME.txt || exit 1
grep "^Analyzed 99" $NAME.txt || echo bad
for mode in $MODES
do
  $RUNCOLORSCREEN render --mode $mode $TESTDATA/$NAME.tif $name.par $NAME-$mode.tif
done

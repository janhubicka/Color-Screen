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
NAME=dufaycolor_dt_captureone_export
$RUNCOLORSCREEN stitch --ncols 2 "$srcdir/${NAME}_tile1.jpg" "$srcdir/${NAME}_tile2.jpg"  --out "out-$name.csprj"  --report "out-$name.txt" --no-cpfind

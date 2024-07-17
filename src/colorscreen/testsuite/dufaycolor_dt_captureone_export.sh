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
$RUNCOLORSCREEN stitch --ncols 2 "$srcdir/${NAME}_tile1.jpg" "$srcdir/${NAME}_tile2.jpg"  --out "out-${NAME}.csprj"  --report "out-${NAME}.txt" --no-cpfind || exit 1

#test_all_render_modes out-${NAME}.csprj out-${NAME}

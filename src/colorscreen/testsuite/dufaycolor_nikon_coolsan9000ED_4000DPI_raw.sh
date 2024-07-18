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
test_autodetect_and_render $NAME out-$NAME-mesh --gamma 1 --mesh
test_autodetect_and_render $NAME out-$NAME-nomesh --gamma 1 --no-mesh
test_autodetect90 $NAME out-$NAME-nomesh-fast --gamma 1 --no-mesh --no-slow-floodfill --fast-floodfill
test_autodetect80 $NAME out-$NAME-nomesh-slow --gamma 1 --no-mesh --no-fast-floodfill --slow-floodfill

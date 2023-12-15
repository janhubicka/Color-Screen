for name in $*
do
echo $name
#min = 380
#max = 780
min=400
max=720
out=`basename $name .dat`.pdf
out2=`basename $name .dat`.emf
out3=`basename $name .dat`.svg
out4=`basename $name .dat`.png

gnuplot <<END
set terminal pdf
set output "$out"

FILE = "$name"

set palette defined (380 "black", 400 "dark-violet", 440 "blue",  490 '#00b0c0', \
                     530 "green", 560 "yellow", 620 "red", 780 "black")
unset cblabel
unset cbtics
set colorbox horizontal user origin graph 0, 0 size graph 1, 1 back
unset key
set xtics 400, 40
set ytics out
set grid x,y front
set xrange [$min:$max]

plot FILE u 1:2 w filledcurves x2 lc rgb "white", \
       (NaN) w p palette   # just to get the colorbox
END
gnuplot <<END
set terminal emf
set output "$out2"

FILE = "$name"

set palette defined (380 "black", 400 "dark-violet", 440 "blue",  490 '#00b0c0', \
                     530 "green", 560 "yellow", 620 "red", 780 "black")
unset cblabel
unset cbtics
set colorbox horizontal user origin graph 0, 0 size graph 1, 1 back
unset key
set xtics out
set ytics out
set grid x,y front
set xrange [$min:$max]

plot FILE u 1:2 w filledcurves x2 lc rgb "white", \
       (NaN) w p palette   # just to get the colorbox
END
gnuplot <<END
set terminal svg
set output "$out3"

FILE = "$name"

set palette defined (380 "black", 400 "dark-violet", 440 "blue",  490 '#00b0c0', \
                     530 "green", 560 "yellow", 620 "red", 780 "black")
unset cblabel
unset cbtics
set colorbox horizontal user origin graph 0, 0 size graph 1, 1 back
unset key
set xtics out
set ytics out
set grid x,y front
set xrange [$min:$max]

plot FILE u 1:2 w filledcurves x2 lc rgb "white", \
       (NaN) w p palette   # just to get the colorbox
END
gnuplot <<END
set terminal pngcairo size 2048,682
set output "$out4"

FILE = "$name"

set palette defined (380 "black", 400 "dark-violet", 440 "blue",  490 '#00b0c0', \
                     530 "green", 560 "yellow", 620 "red", 780 "black")
unset cblabel
unset cbtics
set colorbox horizontal user origin graph 0, 0 size graph 1, 1 back
unset key
set xtics out
set ytics out
set grid x,y front
set xrange [$min:$max]

plot FILE u 1:2 w filledcurves x2 lc rgb "white", \
       (NaN) w p palette   # just to get the colorbox
END

done

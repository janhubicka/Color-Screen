plot()
{
#min=400
#max=700
min=380
max=780
gnuplot <<END
set terminal $1
set output "$3"

FILE = "$2"

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
}
plot2()
{
min=400
max=720
gnuplot <<END
set terminal $1
set output "$3"

FILE = "$2"

### visible spectrum ("quick and dirty")

set palette defined (380 "black", 400 "dark-violet", 440 "blue",  490 '#00b0c0', 530 "green", 560 "yellow", 620 "red", 780 "black")

set samples 1000
unset colorbox

set multiplot 
plot [380:780] '+' u 1:(1):1 w impulse lc palette notitle

plot FILE w l lw 2 lc rgb "white" notitle
unset multiplot
 
END
}

for name in $*
do
echo $name
#min = 380
#max = 780
out=`basename $name .dat`.pdf
#out2=`basename $name .dat`.emf
out3=`basename $name .dat`.svg
out4=`basename $name .dat`.png

plot "pngcairo size 2048,682" $name $out4
plot svg $name $out3
plot pdf $name $out
done

set terminal pdf
set output "spectras.pdf"

set style line 1 \
    linecolor rgb '#ff0000' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2
set style line 2 \
    linecolor rgb '#00ff00' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2
set style line 3 \
    linecolor rgb '#0000ff' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2


set style line 4 \
    linecolor rgb '#afaf00' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2
set style line 5 \
    linecolor rgb '#af1f00' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2
set style line 6 \
    linecolor rgb '#ff00ff' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2
set style line 7 \
    linecolor rgb '#ff004f' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.2

set title "spectras of dyes"
set xlabel "Wavelength"
set ylabel "absorbance"
plot '/tmp/red.dat' with linespoints linestyle 1, \
     '/tmp/green.dat' with linespoints linestyle 2, \
     '/tmp/blue.dat' with linespoints linestyle 3

set ylabel "transmitance"
plot '/tmp/red-trans.dat' with linespoints linestyle 1, \
     '/tmp/green-trans.dat' with linespoints linestyle 2, \
     '/tmp/blue-trans.dat' with linespoints linestyle 3

set title "backlight"
plot '/tmp/backlight.dat' with linespoints linestyle 1

set title "XYZ"
plot '/tmp/x.dat' with linespoints linestyle 1, \
     '/tmp/y.dat' with linespoints linestyle 2, \
     '/tmp/z.dat' with linespoints linestyle 3
set title "autochrome dyes"
plot '/tmp/tartrazine-trans.dat' with linespoints linestyle 4, \
     '/tmp/patent-trans.dat' with linespoints linestyle 5, \
     '/tmp/erythrosine-trans.dat' with linespoints linestyle 1, \
     '/tmp/rose-trans.dat' with linespoints linestyle 6, \
     '/tmp/flexo-trans.dat' with linespoints linestyle 3, \
     '/tmp/crystal-trans.dat' with linespoints linestyle 7, 
set title "o2 autochrome dyes"
plot '/tmp/tartrazine-trans.dat' with linespoints linestyle 4, \
     '/tmp/o2_patent-trans.dat' with linespoints linestyle 5, \
     '/tmp/o2_erythrosine-trans.dat' with linespoints linestyle 1, \
     '/tmp/o2_rose-trans.dat' with linespoints linestyle 6, \
     '/tmp/o2_flexo-trans.dat' with linespoints linestyle 3, \
     '/tmp/o2_crystal-trans.dat' with linespoints linestyle 7, 

set title "o2 autochrome dyes"
plot '/tmp/patent-trans.dat' with linespoints linestyle 1, \
     '/tmp/o2_patent-trans.dat' with linespoints linestyle 2, 
plot '/tmp/erythrosine-trans.dat' with linespoints linestyle 1, \
     '/tmp/o2_erythrosine-trans.dat' with linespoints linestyle 2, 
plot '/tmp/rose-trans.dat' with linespoints linestyle 1, \
     '/tmp/o2_rose-trans.dat' with linespoints linestyle 2, 
plot '/tmp/flexo-trans.dat' with linespoints linestyle 1, \
     '/tmp/o2_flexo-trans.dat' with linespoints linestyle 2, 
plot '/tmp/crystal-trans.dat' with linespoints linestyle 1, \
     '/tmp/o2_crystal-trans.dat' with linespoints linestyle 2, 

set terminal pdf
set output "spectras.pdf"

set style line 1 \
    linecolor rgb '#ff0000' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.5
set style line 2 \
    linecolor rgb '#00ff00' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.5
set style line 3 \
    linecolor rgb '#0000ff' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.5

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

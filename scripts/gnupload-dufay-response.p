set terminal pdf
set output "spectra.pdf"

set style line 1 \
    linecolor rgb '#ff0000' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.0
set style line 2 \
    linecolor rgb '#00ff00' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.0
set style line 3 \
    linecolor rgb '#0000ff' \
    linetype 1 linewidth 2 \
    pointtype 7 pointsize 0.0

set style line 4 lt 2 lc rgb "green" lw 2

set style line 4 \
    linecolor rgb '#ff0000' \
    linetype 2 dt 2 linewidth 1 \
    pointtype 7 pointsize 0.0
set style line 5 \
    linecolor rgb '#00ff00' \
    linetype 2 dt 2 linewidth 1 \
    pointtype 7 pointsize 0.0
set style line 6 \
    linecolor rgb '#0000ff' \
    linetype 2 dt 2 linewidth 1 \
    pointtype 7 pointsize 0.0


set title "estimated (full) and optimal (dashed) spectral response of Dufaycolor"
set xlabel "Wavelength"
set ylabel "relative response"
unset key
plot 'absolute-spectral-response-red.dat' with linespoints linestyle 1, \
     'absolute-spectral-response-green.dat' with linespoints linestyle 2, \
     'absolute-spectral-response-blue.dat' with linespoints linestyle 3, \
     'optimal-response-red.dat' with linespoints linestyle 4, \
     'optimal-response-green.dat' with linespoints linestyle 5, \
     'optimal-response-blue.dat' with linespoints linestyle 6


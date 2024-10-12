set terminal pdf
set output "spectra.pdf"

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

set title "HD curve"
set xlabel "x"
set ylabel "y"
plot '/tmp/shd.dat' with linespoints linestyle 1, 

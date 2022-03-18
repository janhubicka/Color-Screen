.PHONY: html pdf

EXTRA_DIST = \
    gtkimageviewer_tutorial.txt \
    images/agg-circles.png \
    images/cairo-circles.png \
    images/lena-circle.png \
    images/mandelbrot.png 


html:   
	rm -f $(srcdir)/gtkimageviewer_tutorial.html
	asciidoc $(srcdir)/gtkimageviewer_tutorial.txt > $(srcdir)/gtkimageviewer_tutorial.html


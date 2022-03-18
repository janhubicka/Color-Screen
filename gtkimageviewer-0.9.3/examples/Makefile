CFLAGS = -g -Wall `pkg-config --cflags gtkimageviewer-2.0` -lm
LDFLAGS = -g `pkg-config --libs gtkimageviewer-2.0` -lm

examples = test_file test_scroll circle mandelbrot cairo-circles test-track lena-circle agg-circles agg-lion

all : $(examples)

agg-circles : agg-circles.cc
	g++ $(CFLAGS) -o $@ $< -I/usr/include/agg2 $(LDFLAGS) -lagg

agg-lion : agg-lion.cc
	g++ $(CFLAGS) -o $@ $< -I/usr/include/agg2 $(LDFLAGS) -lagg

clean: 
	rm -f *.o $(examples)

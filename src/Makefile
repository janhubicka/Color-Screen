CFLAGS= -g -ffast-math -funroll-all-loops -Wno-deprecated-declarations -Ofast `pkg-config gtk+-x11-2.0 --cflags` `pkg-config gtk+-x11-2.0 --libs` -march=native

all: barveni-bin matrix

check:
	./matrix

barveni.o: barveni.C matrix.h scr-to-img.h render.h gtk-image-viewer.h screen.h render-superposeimg.h

scr-to-img.o: scr-to-img.h

screen.o: screen.h

render.o: render.h scr-to-img.h

render-fast.o: render.h scr-to-img.h render-fast.h

matrix.o: matrix.h

barveni-bin: barveni.o gtk-image-viewer.o scr-to-img.o render.o render-fast.o screen.o
	$(CC) -o $@ $^ $(CFLAGS)  `pkg-config gtk+-x11-2.0 --cflags` `pkg-config gtk+-x11-2.0 --libs` -lnetpbm -export-dynamic -lm

matrix: matrix.o

gtk-image-viewer.c: gtk-image-viewer.gob
	gob2 gtk-image-viewer.gob
	patch -p0 <viewer-patch

%.o: %.C $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
